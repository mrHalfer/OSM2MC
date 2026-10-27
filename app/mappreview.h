#pragma once
// Basic map preview: a picture of the area (no 3D) at several resolutions, wheel zoom, drag panning, a click sets the spawn point.
#include <QEvent>
#include <QWidget>
#include <QImage>
#include "app/tmcrun.h"

class MapPreview : public QWidget
{
    Q_OBJECT
public:
    explicit MapPreview(QWidget* parent = nullptr);

    void setPreview(const TmcPreview& p);
    void clearPreview();
    bool hasPreview() const { return mHave; }
    const TmcPreview& preview() const { return mPrev; }

    //! Spawn point in project coordinates (a marker on the map); clearSpawn returns to the "default"
    void setSpawn(double x, double y);
    void clearSpawn();
    bool hasSpawn() const { return mHasSpawn; }
    double spawnX() const { return mSpawnX; }
    double spawnY() const { return mSpawnY; }

    //! Picture pixel <-> project coordinates
    QPointF toProject(const QPoint& widgetPos) const;
    QPointF toWidget(double x, double y) const;
    QRect imageRect() const;      //!< where the picture (with zoom and pan) lies in the widget; at zoom 1 it is fitted keeping the aspect ratio
    void resetView();             //!< zoom 1, no pan

    //! Export boundary — a polygon. In edit mode: a click adds a vertex (a click on the first one closes it), dragging a vertex moves it,
    //! a click on an edge of a closed polygon inserts a vertex, the right button on a vertex deletes it. Coordinates are the data project coordinates.
    //! What a click on the map does: sets the spawn, edits the boundary or marks an entrance place (stronghold, Ancient City, Pale Garden)
    enum class Tool { Spawn, Clip, Stronghold, City, Pale };
    void setTool(Tool t);
    Tool tool() const { return mTool; }
    bool editMode() const { return mTool == Tool::Clip; }
    //! Entrance marks (Tool::Stronghold … Pale), project coordinates; index = Tool - Stronghold
    bool hasMark(Tool t) const { return mMarkSet[int(t) - int(Tool::Stronghold)]; }
    QPointF mark(Tool t) const { return mMark[int(t) - int(Tool::Stronghold)]; }
    bool anyMark() const { return mMarkSet[0] || mMarkSet[1] || mMarkSet[2]; }
    void setMark(Tool t, const QPointF& pt) { const int i = int(t) - int(Tool::Stronghold); if (i < 0 || i > 2) return; mMark[i] = pt; mMarkSet[i] = true; update(); emit marksChanged(); }
    void clearMarks();
    bool hasClip() const { return mClipClosed; }
    QVector<QPointF> clipPolygon() const { return mClipClosed ? mClip : QVector<QPointF>(); }
    void clearClip();
    //! Set the boundary programmatically (data project coordinates), for example moved over from the online map
    void setClipPolygon(const QVector<QPointF>& pts, bool closed) { mClip = pts; mClipClosed = closed && pts.size() >= 3; update(); emit clipChanged(); }

signals:
    //! The user clicked on the map: project coordinates
    void spawnPicked(double x, double y);
    //! The boundary polygon changed (created, being edited, cleared)
    void clipChanged();
    //! An entrance mark was placed or removed
    void marksChanged();

protected:
    void paintEvent(QPaintEvent*) override;
    void changeEvent(QEvent* e) override { if (e->type() == QEvent::LanguageChange) update(); QWidget::changeEvent(e); }   // the texts are painted: repaint in the new language
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    QSize sizeHint() const override { return QSize(640, 560); }

private:
    TmcPreview mPrev;
    bool mHave = false;
    bool mHasSpawn = false;
    double mSpawnX = 0, mSpawnY = 0;
    double mZoom = 1.0;           //!< 1 — the whole map in the window; the mouse wheel zooms in up to 16
    QPointF mPan;                 //!< offset of the picture center from the widget center, pixels
    bool mDragging = false, mMoved = false;
    QPoint mPressPos, mLastPos;
    Tool mTool = Tool::Spawn;
    bool mMarkSet[3] = { false, false, false };
    QPointF mMark[3];
    QVector<QPointF> mClip;       //!< boundary vertices, project coordinates
    bool mClipClosed = false;
    int mDragVertex = -1;
    int vertexAt(const QPoint& pos) const;      //!< index of the vertex under the cursor or -1
    int edgeAt(const QPoint& pos, QPointF* onEdge = nullptr) const;   //!< index of the edge (from vertex i to i+1) under the cursor or -1
};
