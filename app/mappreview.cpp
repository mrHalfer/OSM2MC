#include "app/mappreview.h"
#include "app/polygoneditor.h"
#include "app/theme.h"
#include "input/tmcproject.h"
#include "input/t3dsettings.h"
#include "core/maplayer.h"
#include "scene/mcscene.h"
#include "ogrsf_frmts.h"
#include <QtConcurrent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QPainterPath>
#include <QMap>
#include <QRgb>
#include <cmath>

namespace
{

QString suffixOf(const QString& n) { return n.section(QLatin1Char('.'), -1).toLower(); }

struct Style { QColor fill; QColor stroke; double width; bool dash; };

//! Color and pen by layer and object code (conventional colors so that the district can be read by eye)
Style styleFor(const QString& sfx, int code)
{
    if (sfx == QLatin1String("bldpol"))
    {
        if (code == 44400002 || code == 44400100 || code == 44400110) return { QColor(0xe0, 0xb8, 0x6a), QColor(0x8a, 0x6a, 0x30), 0.5, false };   // private sector
        if (code == 44400000 || code == 44400001) return { QColor(0xd2, 0x6a, 0x4c), QColor(0x7a, 0x32, 0x20), 0.5, false };                       // multi-storey
        if (code == 44340000) return { QColor(0x8c, 0x91, 0x9b), QColor(0x4a, 0x4f, 0x58), 0.5, false };                                           // industrial
        if (code == 44000000) return { QColor(0xc0, 0xb8, 0xa8), QColor(0x80, 0x78, 0x68), 0.4, false };                                           // sheds
        if (code / 1000 >= 44311 && code / 1000 <= 44319) return { QColor(0x4a, 0x7f, 0xd4), QColor(0x20, 0x40, 0x80), 0.5, false };            // public
        return { QColor(0xa8, 0xa8, 0xa8), QColor(0x60, 0x60, 0x60), 0.5, false };                                                                // general purpose
    }
    if (sfx == QLatin1String("vegpol") || sfx == QLatin1String("vgbpol"))
    {
        if (code == 71126000) return { QColor(0x4f, 0xb0, 0x5c), QColor(), 0, false };                                                              // parks
        if (code / 1000 == 71111 || code == 71111111) return { QColor(0x35, 0x86, 0x44), QColor(), 0, false };                                      // forests, taiga
        if (code == 71126190) return { QColor(0x86, 0xc7, 0x7c), QColor(), 0, false };                                                              // yards
        return { QColor(0xa9, 0xd6, 0x95), QColor(), 0, false };                                                                                    // lawns, meadows, fields
    }
    if (sfx == QLatin1String("hdrpol")) return { QColor(0x6b, 0xb0, 0xe8), QColor(0x3a, 0x7c, 0xb8), 0.5, false };
    if (sfx == QLatin1String("hdrlin")) return { QColor(), QColor(0x4a, 0x9c, 0xe0), 1.5, false };
    if (sfx == QLatin1String("csrpol")) return { QColor(0xd6, 0xd6, 0xd0), QColor(0x9a, 0x9a, 0x94), 0.5, false };
    if (sfx == QLatin1String("rdcpol")) return { QColor(0xb8, 0xb8, 0xc0), QColor(), 0, false };
    if (sfx == QLatin1String("blkpol")) return { QColor(0xe6, 0xdc, 0xc4), QColor(), 0, false };
    if (sfx == QLatin1String("rrslin")) return { QColor(), QColor(0x35, 0x35, 0x35), 1.6, true };
    if (sfx == QLatin1String("rdslin"))
    {
        if (code == 45210000 || code == 45220000 || code == 61210000 || code == 61220000 || code == 61230000 || code == 61231000) return { QColor(), QColor(0xf0, 0xb2, 0x3c), 3.0, false };
        if (code == 45230000 || code == 45240000 || code == 45270000) return { QColor(), QColor(0xff, 0xff, 0xff), 2.0, false };
        if (code == 45262000) return { QColor(), QColor(), 0, false };                                                                              // crosswalk — not drawn
        return { QColor(), QColor(0xc4, 0xa4, 0x7c), 1.0, false };                                                                                 // alleys, paths, dirt roads
    }
    return { QColor(0xcc, 0xcc, 0xcc), QColor(0x88, 0x88, 0x88), 0.5, false };
}

//! Object geometry -> a path in pixels (polygons with holes, lines)
void addToPath(QPainterPath& path, const OGRGeometry* g, double x0, double y1, double sc)
{
    if (!g) return;
    auto ptf = [&](double x, double y) { return QPointF((x - x0) * sc, (y1 - y) * sc); };
    auto ring = [&](const OGRLinearRing* r)
    {
        if (!r || r->getNumPoints() < 3) return;
        QPolygonF poly;
        poly.reserve(r->getNumPoints());
        for (int i = 0; i < r->getNumPoints(); ++i) poly << ptf(r->getX(i), r->getY(i));
        path.addPolygon(poly);
        path.closeSubpath();
    };
    auto polygon = [&](const OGRPolygon* p)
    {
        ring(p->getExteriorRing());
        for (int i = 0; i < p->getNumInteriorRings(); ++i) ring(p->getInteriorRing(i));
    };
    auto line = [&](const OGRLineString* l)
    {
        if (l->getNumPoints() < 2) return;
        path.moveTo(ptf(l->getX(0), l->getY(0)));
        for (int i = 1; i < l->getNumPoints(); ++i) path.lineTo(ptf(l->getX(i), l->getY(i)));
    };
    switch (wkbFlatten(g->getGeometryType()))
    {
    case wkbPolygon: polygon(static_cast<const OGRPolygon*>(g)); break;
    case wkbMultiPolygon:
    {
        const OGRMultiPolygon* mp = static_cast<const OGRMultiPolygon*>(g);
        for (int i = 0; i < mp->getNumGeometries(); ++i) polygon(static_cast<const OGRPolygon*>(mp->getGeometryRef(i)));
        break;
    }
    case wkbLineString: line(static_cast<const OGRLineString*>(g)); break;
    case wkbMultiLineString:
    {
        const OGRMultiLineString* ml = static_cast<const OGRMultiLineString*>(g);
        for (int i = 0; i < ml->getNumGeometries(); ++i) line(static_cast<const OGRLineString*>(ml->getGeometryRef(i)));
        break;
    }
    default: break;
    }
}

}   // namespace

TmcPreview tmcBuildPreview(const TmcRunOptions& optIn, const std::function<void(const QString&)>& logFn)
{
    TmcPreview P;
    TmcRunOptions opt = optIn;
    opt.noEnhancer = true;                // for the picture the OSM building types are accurate enough, and the enhancer and the vegetation top-up are just extra seconds
    opt.noGreenery = true;
    TmcProject project;
    T3dSettings settings;
    QString err;
    if (!tmcLoadProject(opt, logFn, project, settings, &err)) { P.error = err; return P; }
    double bx0, by0, bx1, by1;
    if (!project.bounds(bx0, by0, bx1, by1) || bx1 <= bx0 || by1 <= by0) { P.error = QObject::tr("There are no objects in the data."); return P; }
    const double mx = (bx1 - bx0) * 0.02, my = (by1 - by0) * 0.02;
    P.x0 = bx0 - mx; P.x1 = bx1 + mx; P.y0 = by0 - my; P.y1 = by1 + my;
    P.centerX = 0.5 * (bx0 + bx1); P.centerY = 0.5 * (by0 + by1);
    P.wkt = project.wkt();
    {
        double ux = 1, uy = 1;
        if (metersPerMapUnit(P.wkt, P.centerX, P.centerY, ux, uy) && ux > 0 && uy > 0) { P.unitsToMetersX = ux; P.unitsToMetersY = uy; }
    }
    const double wU = P.x1 - P.x0, hU = P.y1 - P.y0;

    // Paths are built once in projection units (from the top-left corner), and the pictures of different resolutions are obtained by scaling the painter
    struct Group { Style st; QPainterPath path; };
    QVector<Group> draw;                                            // in drawing order, bottom to top
    static const char* order[] = { "vgbpol", "vegpol", "hdrpol", "blkpol", "csrpol", "rdcpol", "hdrlin", "rdslin", "rrslin", "bldpol" };
    for (const char* sfx : order)
    {
        // group the layer objects by style: one QPainterPath per style is an order of magnitude faster than drawing every object separately
        QMap<QRgb, QPair<Style, QPainterPath>> groups;
        for (const MapLayer* l : project.layers())
        {
            if (suffixOf(l->name) != QLatin1String(sfx)) continue;
            const int ci = l->fieldIndex(QStringLiteral("CODE"));
            for (const MapFeature* f : l->features)
            {
                if (!f->g) continue;
                const int code = ci >= 0 ? f->fields.value(ci).toInt() : 0;
                const Style st = styleFor(QString::fromLatin1(sfx), code);
                if (!st.fill.isValid() && !st.stroke.isValid()) continue;
                const QRgb key = qRgba(st.fill.isValid() ? st.fill.red() : 0, st.fill.isValid() ? st.fill.green() : 0, st.stroke.isValid() ? st.stroke.red() : 0, int(st.width * 10) + (st.stroke.isValid() ? 100 : 0) + code % 7);
                auto it = groups.find(key);
                if (it == groups.end()) it = groups.insert(key, qMakePair(st, QPainterPath()));
                it.value().second.setFillRule(Qt::OddEvenFill);
                addToPath(it.value().second, f->g, P.x0, P.y1, 1.0);
                ++P.objects;
            }
        }
        for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) draw.push_back({ it.value().first, it.value().second });
    }

    // Levels: the long side is 1400, 2800, 5600 px (the last one only if the picture is not too heavy in memory). The line width grows with the level.
    const double baseLong = 1400.0;
    auto render = [&draw, wU, hU, baseLong](double longSide) -> QImage
    {
        const double sc = longSide / std::max(wU, hU);              // pixels per projection unit
        const double k = longSide / baseLong;
        QImage img(std::max(64, int(std::ceil(wU * sc))), std::max(64, int(std::ceil(hU * sc))), QImage::Format_RGB32);
        img.fill(QColor(0xee, 0xf0, 0xe4));
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.scale(sc, sc);
        for (const Group& g : draw)
        {
            p.setBrush(g.st.fill.isValid() ? QBrush(g.st.fill) : QBrush(Qt::NoBrush));
            if (g.st.stroke.isValid())
            {
                QPen pen(g.st.stroke, g.st.width * k / sc);
                pen.setCapStyle(Qt::RoundCap); pen.setJoinStyle(Qt::RoundJoin);
                if (g.st.dash) pen.setStyle(Qt::DashLine);
                p.setPen(pen);
            }
            else p.setPen(Qt::NoPen);
            p.drawPath(g.path);
        }
        p.end();
        return img;
    };
    QVector<double> sizes = { baseLong, baseLong * 2, baseLong * 4 };
    const double aspect = std::min(wU, hU) / std::max(wU, hU);
    if (sizes.last() * sizes.last() * aspect > 36e6) sizes.removeLast();          // we do not build more than ~36 Mpixels (144 MB)
    QVector<QFuture<QImage>> jobs;
    for (int i = 1; i < sizes.size(); ++i) jobs.push_back(QtConcurrent::run(render, sizes[i]));
    P.levels.push_back(render(sizes[0]));
    for (QFuture<QImage>& j : jobs) P.levels.push_back(j.result());
    P.image = P.levels.first();
    P.ok = true;
    return P;
}

MapPreview::MapPreview(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(320, 280);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::CrossCursor);
}

void MapPreview::setPreview(const TmcPreview& p) { mPrev = p; mHave = p.ok && !p.image.isNull(); mHasSpawn = false; mClip.clear(); mClipClosed = false; mMarkSet[0] = mMarkSet[1] = mMarkSet[2] = false; resetView(); emit clipChanged(); emit marksChanged(); }
void MapPreview::clearPreview() { mPrev = TmcPreview(); mHave = false; mHasSpawn = false; mClip.clear(); mClipClosed = false; mMarkSet[0] = mMarkSet[1] = mMarkSet[2] = false; resetView(); emit clipChanged(); emit marksChanged(); }
void MapPreview::resetView() { mZoom = 1.0; mPan = QPointF(); update(); }

void MapPreview::setTool(Tool t)
{
    mTool = t;
    mDragVertex = -1;
    setCursor(t == Tool::Spawn ? Qt::CrossCursor : Qt::PointingHandCursor);
    update();
}

void MapPreview::clearMarks()
{
    for (int i = 0; i < 3; ++i) mMarkSet[i] = false;
    update();
    emit marksChanged();
}

void MapPreview::clearClip()
{
    mClip.clear(); mClipClosed = false; mDragVertex = -1;
    update();
    emit clipChanged();
}

int MapPreview::vertexAt(const QPoint& pos) const
{
    int best = -1;
    double bd = 10.0;                                       // vertex grab radius, pixels
    for (int i = 0; i < mClip.size(); ++i)
    {
        const QPointF w = toWidget(mClip[i].x(), mClip[i].y());
        const double d = std::hypot(w.x() - pos.x(), w.y() - pos.y());
        if (d <= bd) { bd = d; best = i; }
    }
    return best;
}

int MapPreview::edgeAt(const QPoint& pos, QPointF* onEdge) const
{
    if (!mClipClosed) return -1;
    int best = -1;
    double bd = 7.0;
    for (int i = 0; i < mClip.size(); ++i)
    {
        const QPointF a = toWidget(mClip[i].x(), mClip[i].y());
        const QPointF b = toWidget(mClip[(i + 1) % mClip.size()].x(), mClip[(i + 1) % mClip.size()].y());
        const QPointF ab = b - a, ap = QPointF(pos) - a;
        const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
        const double t = len2 > 0 ? std::min(1.0, std::max(0.0, (ap.x() * ab.x() + ap.y() * ab.y()) / len2)) : 0.0;
        const QPointF q = a + ab * t;
        const double d = std::hypot(q.x() - pos.x(), q.y() - pos.y());
        if (d <= bd) { bd = d; best = i; if (onEdge) *onEdge = q; }
    }
    return best;
}
QPointF MapPreview::defaultSpawn() const
{
    if (!mClipClosed || mClip.isEmpty()) return QPointF(mPrev.centerX, mPrev.centerY);
    QPointF sp;                                       // the same rule as the exporter: a point inside the boundary, not its bbox center
    if (PolygonGeom::pointOnSurface(mClip, sp)) return sp;
    return QPointF(mPrev.centerX, mPrev.centerY);
}

double MapPreview::distToClipMeters(const QPointF& pt) const
{
    return PolygonGeom::distToPolygonMeters(mClip, pt, mPrev.unitsToMetersX, mPrev.unitsToMetersY);
}

void MapPreview::setSpawn(double x, double y) { mHasSpawn = true; mSpawnX = x; mSpawnY = y; update(); }
void MapPreview::clearSpawn() { mHasSpawn = false; update(); }

QRect MapPreview::imageRect() const
{
    if (!mHave) return QRect();
    const QSize fit = mPrev.image.size().scaled(size() - QSize(24, 24), Qt::KeepAspectRatio);
    const QSize s(std::max(1, int(std::lround(fit.width() * mZoom))), std::max(1, int(std::lround(fit.height() * mZoom))));
    return QRect(QPoint(int(std::lround((width() - s.width()) / 2.0 + mPan.x())), int(std::lround((height() - s.height()) / 2.0 + mPan.y()))), s);
}

void MapPreview::wheelEvent(QWheelEvent* e)
{
    if (!mHave) return;
    const double f = std::pow(1.0015, e->angleDelta().y());
    const double z = std::min(16.0, std::max(1.0, mZoom * f));
    if (z == mZoom) return;
    // the point under the cursor stays in place: the picture center moves proportionally
    const QPointF cur = e->position();
    const QPointF c0 = QPointF(width() / 2.0, height() / 2.0) + mPan;
    const double r = z / mZoom;
    mPan = cur + (c0 - cur) * r - QPointF(width() / 2.0, height() / 2.0);
    mZoom = z;
    if (mZoom <= 1.0) mPan = QPointF();
    update();
    e->accept();
}

void MapPreview::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton) resetView();
}

QPointF MapPreview::toProject(const QPoint& pos) const
{
    const QRect r = imageRect();
    const double fx = (pos.x() - r.x()) / double(std::max(1, r.width())), fy = (pos.y() - r.y()) / double(std::max(1, r.height()));
    return QPointF(mPrev.x0 + fx * (mPrev.x1 - mPrev.x0), mPrev.y1 - fy * (mPrev.y1 - mPrev.y0));
}

QPointF MapPreview::toWidget(double x, double y) const
{
    const QRect r = imageRect();
    return QPointF(r.x() + (x - mPrev.x0) / (mPrev.x1 - mPrev.x0) * r.width(), r.y() + (mPrev.y1 - y) / (mPrev.y1 - mPrev.y0) * r.height());
}

void MapPreview::mousePressEvent(QMouseEvent* e)
{
    if (!mHave) return;
    if (e->button() == Qt::MiddleButton) { resetView(); return; }
    if (mTool >= Tool::Stronghold && e->button() == Qt::RightButton)
    {   // the right button removes the mark of the selected entrance
        const int mi = int(mTool) - int(Tool::Stronghold);
        if (mMarkSet[mi]) { mMarkSet[mi] = false; update(); emit marksChanged(); }
        return;
    }
    if (mTool == Tool::Clip && e->button() == Qt::RightButton)
    {   // delete the vertex under the cursor (none — the last one of an unclosed boundary)
        int v = vertexAt(e->pos());
        if (v < 0 && !mClipClosed && !mClip.isEmpty()) v = mClip.size() - 1;
        if (v >= 0)
        {
            mClip.removeAt(v);
            if (mClip.size() < 3) mClipClosed = false;
            update();
            emit clipChanged();
        }
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    mDragging = true; mMoved = false; mPressPos = mLastPos = e->pos();
    mDragVertex = mTool == Tool::Clip ? vertexAt(e->pos()) : -1;
}

void MapPreview::mouseMoveEvent(QMouseEvent* e)
{
    if (!mDragging) return;
    if (mDragVertex >= 0)
    {   // move a vertex (not beyond the picture bounds)
        mMoved = true;
        const QRect r = imageRect();
        const QPoint p(std::min(r.right(), std::max(r.left(), e->pos().x())), std::min(r.bottom(), std::max(r.top(), e->pos().y())));
        const QPointF pt = toProject(p);
        mClip[mDragVertex] = pt;
        update();
        return;
    }
    if (!mMoved && (e->pos() - mPressPos).manhattanLength() < 5) return;     // below the threshold it is still a click
    mMoved = true;
    setCursor(Qt::ClosedHandCursor);
    if (mZoom > 1.0)
    {
        mPan += e->pos() - mLastPos;
        const QRect r = imageRect();                                  // the picture must not leave the window completely
        const int m = 60;
        if (r.right() < m) mPan.rx() += m - r.right();
        if (r.left() > width() - m) mPan.rx() -= r.left() - (width() - m);
        if (r.bottom() < m) mPan.ry() += m - r.bottom();
        if (r.top() > height() - m) mPan.ry() -= r.top() - (height() - m);
        update();
    }
    mLastPos = e->pos();
}

void MapPreview::mouseReleaseEvent(QMouseEvent* e)
{
    if (!mDragging || e->button() != Qt::LeftButton) return;
    mDragging = false;
    setCursor(mTool == Tool::Spawn ? Qt::CrossCursor : Qt::PointingHandCursor);
    if (mDragVertex >= 0)
    {
        const bool moved = mMoved;
        mDragVertex = -1;
        if (moved) { emit clipChanged(); return; }
        // a click (without movement) on the first vertex of an unclosed boundary — close it
        if (!mClipClosed && mClip.size() >= 3 && vertexAt(e->pos()) == 0) { mClipClosed = true; update(); emit clipChanged(); }
        return;
    }
    if (mMoved || !imageRect().contains(e->pos())) return;
    if (mTool >= Tool::Stronghold)
    {
        const int mi = int(mTool) - int(Tool::Stronghold);
        mMark[mi] = toProject(e->pos()); mMarkSet[mi] = true;
        update();
        emit marksChanged();
        return;
    }
    if (mTool == Tool::Clip)
    {
        const QPointF pt = toProject(e->pos());
        QPointF q;
        const int edge = edgeAt(e->pos(), &q);
        if (edge >= 0) mClip.insert(edge + 1, toProject(q.toPoint()));
        else if (!mClipClosed) mClip.push_back(pt);
        else if (distToClipMeters(pt) <= PolygonGeom::kNewBoundaryMeters) return;    // near the finished boundary — keep it, the click is not ours
        else { mClip.clear(); mClipClosed = false; mClip.push_back(pt); }            // far from it — start a new boundary from this click
        update();
        emit clipChanged();
        return;
    }
    const QPointF pt = toProject(e->pos());
    setSpawn(pt.x(), pt.y());
    emit spawnPicked(pt.x(), pt.y());
}

void MapPreview::paintEvent(QPaintEvent*)
{
    const TmcTheme::Colors& col = TmcTheme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    // card: a rounded background with a thin frame
    const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(col.border, 1));
    p.setBrush(col.card);
    p.drawRoundedRect(card, 12, 12);
    if (!mHave)
    {
        p.setPen(col.muted);
        p.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, tr("The preview is not loaded.\nPress \"Load map\", then click on the map\nto choose the spawn point."));
        return;
    }
    const QRect r = imageRect();
    // the map with rounded corners
    QPainterPath clip;
    clip.addRoundedRect(QRectF(r), 8, 8);
    p.save();
    p.setClipPath(clip);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    // the smallest level with enough pixels on screen (taking devicePixelRatio into account); otherwise the largest one
    const double dpr = devicePixelRatioF();
    const QImage* img = &mPrev.image;
    if (!mPrev.levels.isEmpty())
    {
        img = &mPrev.levels.last();
        for (const QImage& lv : mPrev.levels)
            if (lv.width() >= r.width() * dpr) { img = &lv; break; }
    }
    p.drawImage(r, *img);
    p.restore();
    p.setPen(QPen(col.border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    // export boundary: outside the closed polygon the map is dimmed
    if (!mClip.isEmpty())
    {
        QPolygonF poly;
        for (const QPointF& v : mClip) poly << toWidget(v.x(), v.y());
        QColor fillC = col.accent; fillC.setAlpha(45);
        if (mClipClosed)
        {
            QPainterPath shade;
            shade.addRect(QRectF(r));
            QPainterPath inner;
            inner.addPolygon(poly);
            inner.closeSubpath();
            p.save();
            p.setClipPath(clip);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 100));
            p.drawPath(shade.subtracted(inner));
            p.restore();
            p.setPen(QPen(col.accent, 2));
            p.setBrush(fillC);
            p.drawPolygon(poly);
        }
        else
        {
            p.setPen(QPen(col.accent, 2));
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(poly);
        }
        if (mTool == Tool::Clip || !mClipClosed)
            for (int i = 0; i < poly.size(); ++i)
            {
                const bool closer = !mClipClosed && i == 0 && mClip.size() >= 3;     // the boundary is closed by it
                p.setPen(QPen(col.accent, 2));
                p.setBrush(closer ? col.accent : QColor(Qt::white));
                p.drawEllipse(poly[i], closer ? 6.5 : 4.5, closer ? 6.5 : 4.5);
            }
    }
    // entrance marks: a colored dot with a label
    {
        static const struct { const char* text; QRgb color; } kMark[3] = { { QT_TRANSLATE_NOOP("MapPreview", "Stronghold"), 0x8e44ad }, { QT_TRANSLATE_NOOP("MapPreview", "Ancient City"), 0x16a085 }, { QT_TRANSLATE_NOOP("MapPreview", "Pale Garden"), 0xc0392b } };
        QFont f = p.font(); f.setPointSizeF(f.pointSizeF() * 0.9); f.setBold(true);
        p.setFont(f);
        for (int i = 0; i < 3; ++i)
        {
            if (!mMarkSet[i]) continue;
            const QPointF c = toWidget(mMark[i].x(), mMark[i].y());
            const QColor mc(kMark[i].color);
            p.setPen(QPen(Qt::white, 3));
            p.setBrush(mc);
            p.drawEllipse(c, 6.0, 6.0);
            const QString t = tr(kMark[i].text);
            const QRectF tr = QFontMetricsF(f).boundingRect(t).adjusted(-5, -2, 5, 2).translated(c.x() + 11, c.y() + 4);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(mc.red(), mc.green(), mc.blue(), 225));
            p.drawRoundedRect(tr, 4, 4);
            p.setPen(Qt::white);
            p.drawText(tr, Qt::AlignCenter, t);
        }
    }
    // spawn marker: the chosen one is a pin in the theme color with a white rim; the default (boundary center, else data center) is a pale ring
    const bool chosen = mHasSpawn;
    const QPointF def = defaultSpawn();
    const QPointF c = chosen ? toWidget(mSpawnX, mSpawnY) : toWidget(def.x(), def.y());
    if (chosen)
    {
        p.setPen(QPen(Qt::white, 3.5));
        p.setBrush(col.accent);
        p.drawEllipse(c, 7.0, 7.0);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawEllipse(c, 2.2, 2.2);
        p.setPen(QPen(col.accent, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, 12.0, 12.0);
    }
    else
    {
        p.setPen(QPen(QColor(0x30, 0x30, 0x30, 150), 1.5, Qt::DashLine));
        p.setBrush(QColor(255, 255, 255, 90));
        p.drawEllipse(c, 8.0, 8.0);
    }
}
