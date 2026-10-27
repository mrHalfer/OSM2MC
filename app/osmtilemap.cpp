#include "app/osmtilemap.h"
#include "app/httpclient.h"
#include "app/theme.h"
#include "core/geoutil.h"
#include <QApplication>
#include <QDateTime>
#include <QPainterPath>
#include <QPointer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMouseEvent>
#include <QPainter>
#include <QStandardPaths>
#include <QWheelEvent>
#include <QtConcurrent>
#include <cmath>

namespace
{
constexpr double kMinZoom = 2.0, kMaxZoom = 19.0;
constexpr qint64 kCacheDays = 7;
constexpr double kPi = 3.14159265358979323846;
}

OsmTileMap::OsmTileMap(QWidget* parent) : QWidget(parent), mAlive(std::make_shared<std::atomic_bool>(true))
{
    setMinimumSize(320, 280);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::CrossCursor);
    mPool.setMaxThreadCount(2);
    mCacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/tiles");
    mEditor.setConverters([this](const QPointF& p) { return toWidget(p); }, [this](const QPointF& p) { return fromWidget(p); });
}

OsmTileMap::~OsmTileMap()
{
    mAlive->store(false);                  // deferred tile deliveries check this flag
    mPool.clear();
    mPool.waitForDone();
}

QPointF OsmTileMap::lonLatToWorld(double lon, double lat, double z) const
{
    const double n = 256.0 * std::pow(2.0, z);
    const double la = std::max(-85.0511, std::min(85.0511, lat)) * kPi / 180.0;
    return QPointF((lon + 180.0) / 360.0 * n, (1.0 - std::log(std::tan(la) + 1.0 / std::cos(la)) / kPi) / 2.0 * n);
}

QPointF OsmTileMap::worldToLonLat(const QPointF& w, double z) const
{
    const double n = 256.0 * std::pow(2.0, z);
    const double lon = w.x() / n * 360.0 - 180.0;
    const double lat = std::atan(std::sinh(kPi * (1.0 - 2.0 * w.y() / n))) * 180.0 / kPi;
    return QPointF(lon, lat);
}

QPointF OsmTileMap::toWidget(const QPointF& ll) const
{
    const QPointF c = lonLatToWorld(mLon, mLat, mZoom), p = lonLatToWorld(ll.x(), ll.y(), mZoom);
    return QPointF(width() / 2.0, height() / 2.0) + (p - c);
}

QPointF OsmTileMap::fromWidget(const QPointF& px) const
{
    const QPointF c = lonLatToWorld(mLon, mLat, mZoom);
    return worldToLonLat(c + (px - QPointF(width() / 2.0, height() / 2.0)), mZoom);
}

void OsmTileMap::setView(double lon, double lat, double zoom)
{
    mLon = std::max(-180.0, std::min(180.0, lon));
    mLat = std::max(-84.0, std::min(84.0, lat));
    mZoom = std::max(kMinZoom, std::min(kMaxZoom, zoom));
    update();
    emit viewChanged();
}

void OsmTileMap::fitBounds(double lon0, double lat0, double lon1, double lat1)
{
    double z = kMaxZoom;
    for (; z > kMinZoom; z -= 0.25)
    {
        const QPointF a = lonLatToWorld(lon0, lat1, z), b = lonLatToWorld(lon1, lat0, z);
        if (std::abs(b.x() - a.x()) <= width() * 0.8 && std::abs(b.y() - a.y()) <= height() * 0.8) break;
    }
    setView(0.5 * (lon0 + lon1), 0.5 * (lat0 + lat1), std::min(z, 17.0));
}

void OsmTileMap::setClipLonLat(const QVector<QPointF>& pts, bool closed) { mEditor.set(pts, closed); update(); emit clipChanged(); }
void OsmTileMap::clearClip() { mEditor.clear(); update(); emit clipChanged(); }
double OsmTileMap::clipAreaKm2() const { return tmcPolygonAreaKm2(mEditor.points()); }

bool OsmTileMap::clipAllowed(QString* reason) const
{
    if (!mEditor.closed()) { if (reason) *reason = tr("the boundary is not closed"); return false; }
    QString r;
    const bool ok = tmcOnlineClipAllowed(mEditor.points(), &r);
    if (!ok && reason) *reason = r == QLatin1String("the boundary is too elongated") ? tr("the boundary is too elongated") : tr("the area is larger than allowed");
    return ok;
}

const QImage* OsmTileMap::tile(int z, int x, int y, bool request)
{
    const int n = 1 << z;
    if (y < 0 || y >= n) return nullptr;
    x = ((x % n) + n) % n;                                   // the map wraps around in longitude
    const quint64 key = keyOf(z, x, y);
    if (const QImage* t = mTiles.object(key)) return t;
    if (request) requestTile(z, x, y);
    return nullptr;
}

void OsmTileMap::requestTile(int z, int x, int y)
{
    const quint64 key = keyOf(z, x, y);
    if (mPending.contains(key)) return;
    const auto f = mFailed.constFind(key);
    if (f != mFailed.constEnd() && QDateTime::currentMSecsSinceEpoch() - f.value() < 15000) return;
    mPending.insert(key);
    const QString path = QStringLiteral("%1/%2/%3/%4.png").arg(mCacheDir).arg(z).arg(x).arg(y);
    const std::shared_ptr<std::atomic_bool> alive = mAlive;
    QPointer<OsmTileMap> self(this);
    QtConcurrent::run(&mPool, [=]()
    {
        if (!alive->load()) return;
        QImage img;
        bool fresh = false;
        const QFileInfo fi(path);
        if (fi.exists())
        {
            img.load(path);
            fresh = !img.isNull() && fi.lastModified().daysTo(QDateTime::currentDateTime()) < kCacheDays;
        }
        if (!fresh)
        {   // not in the cache or outdated — download (an outdated one remains a fallback if there is no network)
            const HttpResult r = httpRequest(QStringLiteral("https://tile.openstreetmap.org/%1/%2/%3.png").arg(z).arg(x).arg(y), QByteArray(), QByteArray(), 15000);
            if (r.ok())
            {
                QImage got;
                if (got.loadFromData(r.body))
                {
                    img = got;
                    QDir().mkpath(fi.absolutePath());
                    QFile out(path);
                    if (out.open(QIODevice::WriteOnly)) out.write(r.body);
                }
            }
        }
        if (!alive->load()) return;
        QMetaObject::invokeMethod(qApp, [=]()
        {
            if (alive->load() && self) self->onTileLoaded(key, img, img.isNull());
        }, Qt::QueuedConnection);
    });
}

void OsmTileMap::onTileLoaded(quint64 key, const QImage& img, bool failed)
{
    mPending.remove(key);
    if (failed) mFailed.insert(key, QDateTime::currentMSecsSinceEpoch());
    else mTiles.insert(key, new QImage(img));
    update();
}

void OsmTileMap::paintEvent(QPaintEvent*)
{
    const TmcTheme::Colors& col = TmcTheme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath clip;
    clip.addRoundedRect(card, 12, 12);
    p.setClipPath(clip);
    p.fillRect(rect(), QColor(0xe6, 0xe3, 0xdc));

    // tile level — the integer nearest to the zoom; the tiles are slightly stretched or shrunk
    const int z = std::max(0, std::min(19, int(std::lround(mZoom))));
    const double s = std::pow(2.0, mZoom - z);
    const QPointF c = lonLatToWorld(mLon, mLat, z);                             // the center in world pixels of level z
    const QPointF origin = QPointF(width() / 2.0, height() / 2.0) - c * s;      // where on the screen the world pixel (0, 0) is
    const int tx0 = int(std::floor((0 - origin.x()) / (256.0 * s))), tx1 = int(std::floor((width() - origin.x()) / (256.0 * s)));
    const int ty0 = int(std::floor((0 - origin.y()) / (256.0 * s))), ty1 = int(std::floor((height() - origin.y()) / (256.0 * s)));
    for (int ty = ty0; ty <= ty1; ++ty)
        for (int tx = tx0; tx <= tx1; ++tx)
        {
            const QRectF dst(origin.x() + tx * 256.0 * s, origin.y() + ty * 256.0 * s, 256.0 * s, 256.0 * s);
            if (const QImage* t = tile(z, tx, ty, true)) { p.drawImage(dst, *t); continue; }
            // while the tile is loading — show a piece of a tile of a coarser level (up to 3 levels up)
            bool drawn = false;
            for (int up = 1; up <= 3 && z - up >= 0 && !drawn; ++up)
            {
                const int px = (((tx % (1 << z)) + (1 << z)) % (1 << z)) >> up, py = ty >> up;
                if (const QImage* t = (ty >= 0 && ty < (1 << z)) ? tile(z - up, px, py, false) : nullptr)
                {
                    const int sub = 256 >> up;
                    const int sx = ((((tx % (1 << z)) + (1 << z)) % (1 << z)) & ((1 << up) - 1)) * sub, sy = (ty & ((1 << up) - 1)) * sub;
                    p.drawImage(dst, *t, QRectF(sx, sy, sub, sub));
                    drawn = true;
                }
            }
            if (!drawn && z > 2) tile(z - 1, (((tx % (1 << z)) + (1 << z)) % (1 << z)) >> 1, ty >> 1, true);   // also request the "parent" for the next frame
        }

    mEditor.paint(p, QRectF(rect()), col.accent, !mEditor.points().isEmpty() && mEditor.closed() && !clipAllowed(), true);

    // marks: the spawn is a pin in the theme color, the entrances are colored dots with a label (as in the data preview)
    {
        static const struct { const char* text; QRgb color; } kMark[4] = { { QT_TRANSLATE_NOOP("OsmTileMap", "Spawn"), 0 }, { QT_TRANSLATE_NOOP("OsmTileMap", "Stronghold"), 0x8e44ad }, { QT_TRANSLATE_NOOP("OsmTileMap", "Ancient City"), 0x16a085 }, { QT_TRANSLATE_NOOP("OsmTileMap", "Pale Garden"), 0xc0392b } };
        QFont mf = p.font(); mf.setPointSizeF(mf.pointSizeF() * 0.9); mf.setBold(true);
        p.setFont(mf);
        for (int i = 0; i < 4; ++i)
        {
            if (!mMarkSet[i]) continue;
            const QPointF c = toWidget(mMark[i]);
            const QColor mc = i == 0 ? col.accent : QColor(kMark[i].color);
            p.setPen(QPen(Qt::white, 3));
            p.setBrush(mc);
            p.drawEllipse(c, i == 0 ? 7.0 : 6.0, i == 0 ? 7.0 : 6.0);
            const QString t = tr(kMark[i].text);
            const QRectF tr = QFontMetricsF(mf).boundingRect(t).adjusted(-5, -2, 5, 2).translated(c.x() + 11, c.y() + 4);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(mc.red(), mc.green(), mc.blue(), 225));
            p.drawRoundedRect(tr, 4, 4);
            p.setPen(Qt::white);
            p.drawText(tr, Qt::AlignCenter, t);
        }
    }

    // area label and the mandatory map source attribution
    p.setClipping(false);
    QFont f = p.font();
    f.setPointSizeF(f.pointSizeF() * 0.9);
    p.setFont(f);
    const QFontMetricsF fm(f);
    if (mEditor.closed())
    {
        const bool ok = clipAllowed();
        const QString t = ok ? tr("Area: %1 of %2 km²").arg(clipAreaKm2(), 0, 'f', 2).arg(kTmcMaxOnlineAreaKm2, 0, 'f', 0)
                             : tr("The boundary is too large: %1 km² (up to %2 is allowed)").arg(clipAreaKm2(), 0, 'f', 1).arg(kTmcMaxOnlineAreaKm2, 0, 'f', 0);
        const QRectF r = fm.boundingRect(t).adjusted(-8, -4, 8, 4).translated(14, 14);
        p.setPen(Qt::NoPen);
        p.setBrush(ok ? QColor(col.card.red(), col.card.green(), col.card.blue(), 235) : QColor(0xe5, 0x48, 0x4d, 235));
        p.drawRoundedRect(r, 6, 6);
        p.setPen(ok ? col.text : QColor(Qt::white));
        p.drawText(r, Qt::AlignCenter, t);
    }
    const QString attr = QStringLiteral("© OpenStreetMap contributors");
    const QRectF ar = fm.boundingRect(attr).adjusted(-6, -3, 6, 3);
    const QRectF ad(width() - ar.width() - 8, height() - ar.height() - 8, ar.width(), ar.height());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 200));
    p.drawRoundedRect(ad, 4, 4);
    p.setPen(QColor(0x33, 0x33, 0x33));
    p.drawText(ad, Qt::AlignCenter, attr);

    p.setClipping(true);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(col.border, 1));
    p.drawRoundedRect(card, 12, 12);
}

void OsmTileMap::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::RightButton)
    {
        if (mTool != Tool::Clip) { const int i = int(mTool) - 1; if (mMarkSet[i]) { mMarkSet[i] = false; update(); emit marksChanged(); } }
        else if (mEditor.removeAt(e->pos())) { update(); emit clipChanged(); }
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    mDragging = true; mMoved = false; mPressPos = mLastPos = e->pos();
    if (mTool == Tool::Clip) mEditor.beginDrag(e->pos());
}

void OsmTileMap::mouseMoveEvent(QMouseEvent* e)
{
    if (!mDragging) return;
    if (mEditor.dragging())
    {
        mMoved = true;
        mEditor.dragTo(QPoint(std::max(0, std::min(width() - 1, e->pos().x())), std::max(0, std::min(height() - 1, e->pos().y()))));
        update();
        emit clipChanged();
        return;
    }
    if (!mMoved && (e->pos() - mPressPos).manhattanLength() < 5) return;     // below the threshold it is still a click
    mMoved = true;
    setCursor(Qt::ClosedHandCursor);
    const QPoint d = e->pos() - mLastPos;
    const QPointF c = lonLatToWorld(mLon, mLat, mZoom) - QPointF(d);
    const QPointF ll = worldToLonLat(c, mZoom);
    mLon = ll.x(); mLat = std::max(-84.0, std::min(84.0, ll.y()));
    mLastPos = e->pos();
    update();
    emit viewChanged();
}

void OsmTileMap::mouseReleaseEvent(QMouseEvent* e)
{
    if (!mDragging || e->button() != Qt::LeftButton) return;
    mDragging = false;
    setCursor(Qt::CrossCursor);
    if (mEditor.dragging())
    {
        if (mEditor.endDrag(e->pos())) { update(); emit clipChanged(); }
        return;
    }
    if (mMoved) return;
    if (mTool != Tool::Clip)
    {
        const int i = int(mTool) - 1;
        mMark[i] = fromWidget(QPointF(e->pos())); mMarkSet[i] = true;
        update();
        emit marksChanged();
        return;
    }
    if (mEditor.click(e->pos())) { update(); emit clipChanged(); }
}

void OsmTileMap::wheelEvent(QWheelEvent* e)
{
    const double z = std::max(kMinZoom, std::min(kMaxZoom, mZoom + e->angleDelta().y() / 240.0));
    if (z == mZoom) return;
    // the point under the cursor stays in place
    const QPointF cur = e->position();
    const QPointF anchor = fromWidget(cur);
    mZoom = z;
    const QPointF aw = lonLatToWorld(anchor.x(), anchor.y(), mZoom);
    const QPointF cw = aw - (cur - QPointF(width() / 2.0, height() / 2.0));
    const QPointF ll = worldToLonLat(cw, mZoom);
    mLon = ll.x(); mLat = std::max(-84.0, std::min(84.0, ll.y()));
    update();
    emit viewChanged();
    e->accept();
}
