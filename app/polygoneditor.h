#pragma once
// Editing a polygon with the mouse over a map: a click adds a vertex, a click on the first one closes it, vertices can be dragged,
// a click on an edge of a closed polygon inserts a vertex, the right button deletes. The vertices are stored in "map"
// coordinates (the data projection or longitude/latitude); the owner supplies the conversion to widget pixels.
// With setRestartDistance() a click far away from a closed polygon clears it and starts a new one.
#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <functional>

namespace PolygonGeom {
//! Minimum distance from a point to the polygon border, meters (0 — the point is inside). sx/sy: meters per coordinate unit
//! along X and Y (for lon/lat take them at the point's latitude). A click restart rule uses it: farther than this many meters
//! from a finished boundary — the old one is cleared and a new one starts.
const double kNewBoundaryMeters = 500.0;            // (not inline constexpr: the project is built with C++14)
inline bool containsPoint(const QVector<QPointF>& pts, const QPointF& p)
{
    bool in = false;                                                                                // ray casting
    for (int i = 0, j = pts.size() - 1; i < pts.size(); j = i++)
    {
        const QPointF& a = pts[i];
        const QPointF& b = pts[j];
        if ((a.y() > p.y()) != (b.y() > p.y()) &&
            p.x() < (b.x() - a.x()) * (p.y() - a.y()) / (b.y() - a.y()) + a.x()) in = !in;
    }
    return in;
}
inline double distToPolygonMeters(const QVector<QPointF>& pts, const QPointF& p, double sx, double sy)
{
    if (pts.size() < 3) return 0.0;
    if (containsPoint(pts, p)) return 0.0;
    double best = 1e30;
    for (int i = 0; i < pts.size(); ++i)
    {
        const QPointF& a = pts[i];
        const QPointF& b = pts[(i + 1) % pts.size()];
        const QPointF ab(b.x() - a.x(), b.y() - a.y()), ap(p.x() - a.x(), p.y() - a.y());
        const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
        const double t = len2 > 0 ? std::min(1.0, std::max(0.0, (ap.x() * ab.x() + ap.y() * ab.y()) / len2)) : 0.0;
        best = std::min(best, std::hypot((ap.x() - ab.x() * t) * sx, (ap.y() - ab.y() * t) * sy));
    }
    return best;
}
//! A point guaranteed to lie INSIDE the polygon, like ST_PointOnSurface in PostGIS: the area centroid when it falls
//! inside, otherwise the middle of the longest horizontal span across the polygon taken at the MIDDLE between two
//! neighbouring vertex heights (a row strictly inside, not on an edge). xWeight scales X when measuring span lengths
//! only (lon/lat maps pass cos(latitude)); the result is inside for any simple polygon. False — fewer than 3 vertices
//! or a degenerate (zero-area) one.
inline bool pointOnSurface(const QVector<QPointF>& pts, QPointF& out, double xWeight = 1.0)
{
    if (pts.size() < 3) return false;
    double a2 = 0.0, cx = 0.0, cy = 0.0;                                                            // the area centroid
    for (int i = 0, j = pts.size() - 1; i < pts.size(); j = i++)
    {
        const double cr = pts[j].x() * pts[i].y() - pts[i].x() * pts[j].y();
        a2 += cr;
        cx += (pts[j].x() + pts[i].x()) * cr;
        cy += (pts[j].y() + pts[i].y()) * cr;
    }
    if (std::fabs(a2) < 1e-12) return false;
    cx /= 3.0 * a2;
    cy /= 3.0 * a2;
    if (containsPoint(pts, QPointF(cx, cy))) { out = QPointF(cx, cy); return true; }
    QVector<double> ys;
    for (const QPointF& p : pts) ys << p.y();
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    double bestLen = -1.0;
    QPointF best;
    for (int k = 0; k + 1 < ys.size(); ++k)                              // rows strictly between neighbouring vertex heights
    {
        const double y = 0.5 * (ys[k] + ys[k + 1]);
        QVector<double> xs;
        for (int i = 0, j = pts.size() - 1; i < pts.size(); j = i++)
        {
            const QPointF& a = pts[i];
            const QPointF& b = pts[j];
            if ((a.y() > y) != (b.y() > y)) xs << a.x() + (b.x() - a.x()) * (y - a.y()) / (b.y() - a.y());
        }
        std::sort(xs.begin(), xs.end());
        for (int i = 0; i + 1 < xs.size(); i += 2)                        // even-odd: pairs bound the inside spans
        {
            const double len = (xs[i + 1] - xs[i]) * xWeight;
            if (len > bestLen) { bestLen = len; best = QPointF(0.5 * (xs[i] + xs[i + 1]), y); }
        }
    }
    if (bestLen <= 0.0) return false;
    out = best;
    return true;                                                          // the middle of an inside span — inside by construction
}
}

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
    //! A closed polygon: a click farther than setRestartDistance()'s distance — clear the old one and start a new one from this click.
    bool click(const QPoint& pos)
    {
        QPointF q;
        const int e = edgeAt(pos, &q);
        if (e >= 0) { mPts.insert(e + 1, mFrom(q)); return true; }
        if (mClosed)
        {
            if (!mRestart) return false;
            const QPointF p = mFrom(QPointF(pos));
            if (mRestart(p) <= mRestartMeters) return false;
            mPts.clear();
            mClosed = false;
            mPts.push_back(p);
            return true;
        }
        mPts.push_back(mFrom(QPointF(pos)));
        return true;
    }
    //! Enable "a far click starts a new boundary": restart — distance (meters) from a map point to the closed polygon; beyond it the polygon is cleared
    void setRestartDistance(double meters, std::function<double(const QPointF&)> restart)
    { mRestartMeters = meters; mRestart = std::move(restart); }
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
    double mRestartMeters = 0.0;                          //!< 0 — "a far click starts a new boundary" is off
    std::function<double(const QPointF&)> mRestart;       //!< distance in meters from the point to the closed polygon
    Convert mTo, mFrom;
};
