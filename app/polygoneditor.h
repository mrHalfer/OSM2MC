#pragma once
// Editing a polygon with the mouse over a map: a click adds a vertex, a click on the first one closes it, vertices can be dragged,
// a click on an edge of a closed polygon inserts a vertex, the right button deletes. The vertices are stored in "map"
// coordinates (the data projection or longitude/latitude); the owner supplies the conversion to widget pixels.
#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <functional>

class PolygonEditor
{
public:
    using Convert = std::function<QPointF(const QPointF&)>;
    //! toWidget: map coordinates -> pixels; fromWidget: the reverse
    void setConverters(Convert toWidget, Convert fromWidget) { mTo = std::move(toWidget); mFrom = std::move(fromWidget); }

    const QVector<QPointF>& points() const { return mPts; }
    bool closed() const { return mClosed; }
    void set(const QVector<QPointF>& pts, bool closed) { mPts = pts; mClosed = closed && pts.size() >= 3; mDrag = -1; }
    void clear() { mPts.clear(); mClosed = false; mDrag = -1; }

    //! The left button is pressed: true — a vertex is grabbed (then dragTo/endDrag), false — a click away from vertices (the owner decides: pan the map or click())
    bool beginDrag(const QPoint& pos) { mDrag = vertexAt(pos); mMoved = false; return mDrag >= 0; }
    bool dragging() const { return mDrag >= 0; }
    //! Move the grabbed vertex; point is the new position in pixels (the owner may restrict it to the map area)
    void dragTo(const QPoint& pos) { if (mDrag < 0) return; mPts[mDrag] = mFrom(QPointF(pos)); mMoved = true; }
    //! Release: true — the polygon changed (a vertex was moved or the polygon was closed by a click on the first vertex)
    bool endDrag(const QPoint& pos)
    {
        const int v = mDrag;
        mDrag = -1;
        if (v < 0) return false;
        if (mMoved) return true;
        if (!mClosed && mPts.size() >= 3 && v == 0 && vertexAt(pos) == 0) { mClosed = true; return true; }
        return false;
    }
    //! A click away from vertices: an edge of a closed polygon — insert a vertex, an unclosed one — a new vertex at the end. true — changed.
    bool click(const QPoint& pos)
    {
        QPointF q;
        const int e = edgeAt(pos, &q);
        if (e >= 0) { mPts.insert(e + 1, mFrom(q)); return true; }
        if (mClosed) return false;
        mPts.push_back(mFrom(QPointF(pos)));
        return true;
    }
    //! Right button: delete the vertex under the cursor (none — the last one of an unclosed polygon). true — changed.
    bool removeAt(const QPoint& pos)
    {
        int v = vertexAt(pos);
        if (v < 0 && !mClosed && !mPts.isEmpty()) v = mPts.size() - 1;
        if (v < 0) return false;
        mPts.removeAt(v);
        if (mPts.size() < 3) mClosed = false;
        return true;
    }

    //! Drawing: a closed area is filled with a color, outside it inside area the map is dimmed; bad — red color (the boundary is not suitable)
    void paint(QPainter& p, const QRectF& area, const QColor& accent, bool bad, bool showVertices) const
    {
        if (mPts.isEmpty()) return;
        QPolygonF poly;
        for (const QPointF& v : mPts) poly << mTo(v);
        const QColor c = bad ? QColor(0xe5, 0x48, 0x4d) : accent;
        p.save();
        if (mClosed)
        {
            QPainterPath shade, inner;
            shade.addRect(area);
            inner.addPolygon(poly);
            inner.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 90));
            p.drawPath(shade.subtracted(inner));
            QColor fill = c; fill.setAlpha(45);
            p.setPen(QPen(c, 2));
            p.setBrush(fill);
            p.drawPolygon(poly);
        }
        else
        {
            p.setPen(QPen(c, 2));
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(poly);
        }
        if (showVertices || !mClosed)
            for (int i = 0; i < poly.size(); ++i)
            {
                const bool closer = !mClosed && i == 0 && mPts.size() >= 3;        // the boundary is closed by it
                p.setPen(QPen(c, 2));
                p.setBrush(closer ? c : QColor(Qt::white));
                p.drawEllipse(poly[i], closer ? 6.5 : 4.5, closer ? 6.5 : 4.5);
            }
        p.restore();
    }

private:
    int vertexAt(const QPoint& pos) const
    {
        int best = -1;
        double bd = 10.0;                                                          // vertex grab radius, pixels
        for (int i = 0; i < mPts.size(); ++i)
        {
            const QPointF w = mTo(mPts[i]);
            const double d = std::hypot(w.x() - pos.x(), w.y() - pos.y());
            if (d <= bd) { bd = d; best = i; }
        }
        return best;
    }
    int edgeAt(const QPoint& pos, QPointF* onEdge) const
    {
        if (!mClosed) return -1;
        int best = -1;
        double bd = 7.0;
        for (int i = 0; i < mPts.size(); ++i)
        {
            const QPointF a = mTo(mPts[i]), b = mTo(mPts[(i + 1) % mPts.size()]);
            const QPointF ab = b - a, ap = QPointF(pos) - a;
            const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
            const double t = len2 > 0 ? std::min(1.0, std::max(0.0, (ap.x() * ab.x() + ap.y() * ab.y()) / len2)) : 0.0;
            const QPointF q = a + ab * t;
            const double d = std::hypot(q.x() - pos.x(), q.y() - pos.y());
            if (d <= bd) { bd = d; best = i; if (onEdge) *onEdge = q; }
        }
        return best;
    }

    QVector<QPointF> mPts;
    bool mClosed = false;
    int mDrag = -1;
    bool mMoved = false;
    Convert mTo, mFrom;
};
