#pragma once
// An OpenStreetMap map for the online mode: raster tiles (Web Mercator) with a disk cache, the wheel zooms, dragging pans,
// on top — the boundary polygon (longitude/latitude) with a check of the allowed area.
#include <QEvent>
#include <QCache>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QSet>
#include <QThreadPool>
#include <QWidget>
#include <atomic>
#include <memory>
#include "app/polygoneditor.h"

class OsmTileMap : public QWidget
{
    Q_OBJECT
public:
    explicit OsmTileMap(QWidget* parent = nullptr);
    ~OsmTileMap() override;

    //! Map view: center (longitude, latitude) and zoom (2…19, fractional)
    void setView(double lon, double lat, double zoom);
    double centerLon() const { return mLon; }
    double centerLat() const { return mLat; }
    double zoom() const { return mZoom; }
    //! Show a rectangle (degrees) entirely
    void fitBounds(double lon0, double lat0, double lon1, double lat1);

    //! What a click does: edits the boundary or places a mark (spawn, the entrance to the stronghold, to the Ancient City, the Pale Garden)
    enum class Tool { Clip, Spawn, Stronghold, City, Pale };
    void setTool(Tool t) { mTool = t; setCursor(t == Tool::Clip ? Qt::CrossCursor : Qt::PointingHandCursor); update(); }
    Tool tool() const { return mTool; }
    bool hasMark(Tool t) const { return t != Tool::Clip && mMarkSet[int(t) - 1]; }
    QPointF mark(Tool t) const { return mMark[int(t) - 1]; }       //!< longitude/latitude
    void setMark(Tool t, const QPointF& lonLat) { if (t == Tool::Clip) return; mMark[int(t) - 1] = lonLat; mMarkSet[int(t) - 1] = true; update(); emit marksChanged(); }
    bool anyMark() const { return mMarkSet[0] || mMarkSet[1] || mMarkSet[2] || mMarkSet[3]; }
    void clearMarks() { for (bool& b : mMarkSet) b = false; update(); emit marksChanged(); }

    bool hasClip() const { return mEditor.closed(); }
    QVector<QPointF> clipLonLat() const { return mEditor.closed() ? mEditor.points() : QVector<QPointF>(); }
    void setClipLonLat(const QVector<QPointF>& pts, bool closed);
    void clearClip();
    //! The boundary is closed and within the allowed area; reason — what is wrong (if it is not suitable)
    bool clipAllowed(QString* reason = nullptr) const;
    double clipAreaKm2() const;

signals:
    void clipChanged();
    void marksChanged();
    void viewChanged();

protected:
    void paintEvent(QPaintEvent*) override;
    void changeEvent(QEvent* e) override { if (e->type() == QEvent::LanguageChange) update(); QWidget::changeEvent(e); }   // the texts are painted: repaint in the new language
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    QSize sizeHint() const override { return QSize(640, 560); }

private:
    struct TileKey { int z, x, y; bool operator==(const TileKey& o) const { return z == o.z && x == o.x && y == o.y; } };
    static quint64 keyOf(int z, int x, int y) { return (quint64(z) << 56) | (quint64(x) << 28) | quint64(y); }
    QPointF lonLatToWorld(double lon, double lat, double z) const;      //!< world pixels at zoom z
    QPointF worldToLonLat(const QPointF& w, double z) const;
    QPointF toWidget(const QPointF& lonLat) const;
    QPointF fromWidget(const QPointF& px) const;
    const QImage* tile(int z, int x, int y, bool request);
    void requestTile(int z, int x, int y);
    void onTileLoaded(quint64 key, const QImage& img, bool failed);

    double mLon = 37.62, mLat = 55.75, mZoom = 10.0;
    PolygonEditor mEditor;
    Tool mTool = Tool::Clip;
    bool mMarkSet[4] = { false, false, false, false };
    QPointF mMark[4];
    QCache<quint64, QImage> mTiles { 400 };
    QSet<quint64> mPending;
    QHash<quint64, qint64> mFailed;                 //!< when a tile failed to load (a retry not earlier than in 15 s)
    QThreadPool mPool;                              //!< no more than 2 concurrent requests — a requirement of the OSM tile server policy
    std::shared_ptr<std::atomic_bool> mAlive;
    QString mCacheDir;
    bool mDragging = false, mMoved = false;
    QPoint mPressPos, mLastPos;
};
