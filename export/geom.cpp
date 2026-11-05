#include "export/geom.h"
#include <QHash>
#include <cstring>


//! Clip a 3D polygon by a half-plane (y >= plane for minY, otherwise y <= plane) — the polygon is convex,
//! the result is convex too. The intersection point is a lerp between the "outer" and the "inner" one.
void clipHalf3(const Poly3& in, Poly3& out, bool minY, double plane)
{
    out.clear();
    const int n = in.size();
    for (int i = 0; i < n; ++i)
    {
        const QVector3D& a = in[i];
        const QVector3D& b = in[(i + 1) % n];
        const double da = a.y() - plane;
        const double db = b.y() - plane;
        const bool ain = minY ? da >= 0 : da <= 0;
        const bool bin = minY ? db >= 0 : db <= 0;
        if (ain) out.append(a);
        if (ain != bin)
        {
            const double t = da / (da - db);
            out.append(a + (b - a) * float(t));
        }
    }
}

//! Clip a 2D polygon by the strip X∈[x0,x1] (successively x>=x0 and x<=x1).
void clipStripX(const Poly2& in, Poly2& out, double x0, double x1)
{
    Poly2 tmp;
    out = in;
    // x >= x0
    tmp.clear();
    for (int i = 0; i < out.size(); ++i)
    {
        const QPointF& a = out[i];
        const QPointF& b = out[(i + 1) % out.size()];
        const double da = a.x() - x0, db = b.x() - x0;
        const bool ain = da >= 0, bin = db >= 0;
        if (ain) tmp.append(a);
        if (ain != bin)
        {
            const double t = da / (da - db);
            tmp.append(QPointF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t));
        }
    }
    // x <= x1
    out.clear();
    for (int i = 0; i < tmp.size(); ++i)
    {
        const QPointF& a = tmp[i];
        const QPointF& b = tmp[(i + 1) % tmp.size()];
        const double da = a.x() - x1, db = b.x() - x1;
        const bool ain = da <= 0, bin = db <= 0;
        if (ain) out.append(a);
        if (ain != bin)
        {
            const double t = da / (da - db);
            out.append(QPointF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t));
        }
    }
}

// ---------------------------------------------------------------------------
// Voxelization of one object
// ---------------------------------------------------------------------------

//! An artificial reservoir (a pond with a fountain, an embankment): ALL outline corners are right angles (with a tolerance of tolDeg degrees: 87.5° counts as "right" too). The outline is the boundary edges of the top horizontal
//! lid of the mesh (edges belonging to exactly one triangle); nearly straight vertices (a turn < 3°) are ignored; >= 4 corners are needed. The axis scales sx/sy — map units -> meters.
bool isRectilinearOutline(const McWorldExporter::McObject& o, double sx, double sy, double tolDeg)
{
    const auto& V = o.vertices;
    const auto& I = o.indices;
    if (I.size() < 3) return false;
    double zmax = -1e300;
    for (int t = 0; t + 2 < I.size(); t += 3)
    {
        const double z0 = V[int(I[t])].z(), z1 = V[int(I[t + 1])].z(), z2 = V[int(I[t + 2])].z();
        if (std::fabs(z0 - z1) < 1e-4 && std::fabs(z1 - z2) < 1e-4) zmax = std::max(zmax, z0);
    }
    if (zmax < -1e299) return false;
    QHash<QPair<qint64, qint64>, int> vid;                       // the quantized (x,y) -> the vertex number
    QVector<QPointF> pts;
    auto vertexId = [&](const QVector3D& v) -> int
    {
        const QPair<qint64, qint64> k(qint64(std::llround(double(v.x()) * 1000.0)), qint64(std::llround(double(v.y()) * 1000.0)));
        auto it = vid.find(k);
        if (it == vid.end()) { it = vid.insert(k, pts.size()); pts.append(QPointF(double(v.x()) * sx, double(v.y()) * sy)); }
        return it.value();
    };
    QHash<QPair<int, int>, int> edgeCnt;
    for (int t = 0; t + 2 < I.size(); t += 3)
    {
        const QVector3D& a = V[int(I[t])]; const QVector3D& b = V[int(I[t + 1])]; const QVector3D& c = V[int(I[t + 2])];
        if (std::fabs(double(a.z()) - zmax) > 1e-4 || std::fabs(double(b.z()) - zmax) > 1e-4 || std::fabs(double(c.z()) - zmax) > 1e-4) continue;
        const int ia = vertexId(a), ib = vertexId(b), ic = vertexId(c);
        const int e[3][2] = { { ia, ib }, { ib, ic }, { ic, ia } };
        for (auto& ed : e)
        {
            if (ed[0] == ed[1]) continue;
            ++edgeCnt[qMakePair(std::min(ed[0], ed[1]), std::max(ed[0], ed[1]))];
        }
    }
    QHash<int, QVector<int>> adj;
    for (auto it = edgeCnt.constBegin(); it != edgeCnt.constEnd(); ++it)
        if (it.value() == 1) { adj[it.key().first].append(it.key().second); adj[it.key().second].append(it.key().first); }
    if (adj.isEmpty()) return false;
    for (auto it = adj.constBegin(); it != adj.constEnd(); ++it)
        if (it.value().size() != 2) return false;               // a complex/broken outline — we consider it natural
    QSet<int> seen;
    int corners = 0;
    for (auto it = adj.constBegin(); it != adj.constEnd(); ++it)
    {
        if (seen.contains(it.key())) continue;
        QVector<int> loop;
        int prev = -1, cur = it.key();
        while (!seen.contains(cur))
        {
            seen.insert(cur); loop.append(cur);
            const QVector<int>& nb = adj[cur];
            const int nxt = (nb[0] != prev) ? nb[0] : nb[1];
            prev = cur; cur = nxt;
        }
        const int n = loop.size();
        if (n < 3) return false;
        for (int i = 0; i < n; ++i)
        {
            const QPointF p0 = pts[loop[(i + n - 1) % n]], p1 = pts[loop[i]], p2 = pts[loop[(i + 1) % n]];
            const double ax = p1.x() - p0.x(), ay = p1.y() - p0.y(), bx = p2.x() - p1.x(), by = p2.y() - p1.y();
            if ((ax * ax + ay * ay) < 1e-12 || (bx * bx + by * by) < 1e-12) continue;
            const double turn = std::fabs(std::atan2(ax * by - ay * bx, ax * bx + ay * by)) * 180.0 / 3.14159265358979;
            if (turn < 3.0) continue;                           // an almost straight section
            if (std::fabs(turn - 90.0) > tolDeg) return false;  // the angle is not right — a natural reservoir
            ++corners;
        }
    }
    return corners >= 4;
}

//! The cells "touched" by a triangle, in the local grid of the object extent:
//! the cell (lx,ly,lz) <=> the block (minx+lx, miny+ly, minz+lz); grid[cell]=1 — a surface voxel.
void rasterizeTriangle(const QVector3D& t0, const QVector3D& t1, const QVector3D& t2,
                              int minx, int miny, int minz, int sx, int sy, int sz,
                              QByteArray& grid, QVector<int>& surfaceCells)
{
    const double fminy = std::min({ t0.y(), t1.y(), t2.y() });
    const double fmaxy = std::max({ t0.y(), t1.y(), t2.y() });
    const double fminx = std::min({ t0.x(), t1.x(), t2.x() });
    const double fmaxx = std::max({ t0.x(), t1.x(), t2.x() });
    const double fminz = std::min({ t0.z(), t1.z(), t2.z() });
    const double fmaxz = std::max({ t0.z(), t1.z(), t2.z() });

    // A quick rejection by the XZ extent of the grid
    if (int(std::floor(fmaxx)) < minx || int(std::floor(fminx)) >= minx + sx ||
        int(std::floor(fmaxz)) < minz || int(std::floor(fminz)) >= minz + sz)
        return;

    const int yLo = int(std::floor(fminy));
    const int yHi = int(std::ceil(fmaxy));   // the slabs [Y,Y+1] at Y=yHi give an empty section — we skip it below

    Poly3 tri;
    tri << t0 << t1 << t2;

    Poly3 slab, upper;
    Poly2 flat, strip;
    for (int Y = yLo; Y <= yHi; ++Y)
    {
        const double slabY0 = double(Y);
        const double slabY1 = slabY0 + 1.0;
        // the section of the triangle by the slab [Y, Y+1] — a convex 3D polygon (up to 6 vertices)
        clipHalf3(tri, slab, true, slabY0);
        if (slab.size() < 3) continue;
        clipHalf3(slab, upper, false, slabY1);
        if (upper.size() < 3) continue;

        flat.clear();
        double pminx = std::numeric_limits<double>::max(), pmaxx = -pminx;
        for (const QVector3D& p : upper)
        {
            flat << QPointF(p.x(), p.z());
            pminx = std::min(pminx, double(p.x()));
            pmaxx = std::max(pmaxx, double(p.x()));
        }

        int xLo = int(std::floor(pminx));
        // INCLUSIVE up to floor(max): there used to be an epsilon offset (max - 1e-9), which lost
        // the corner columns of walls when a mesh face stands exactly on an integer boundary (a wall
        // of a mall shell at x/z = an integer): the corner was written by neither the east nor the south quarks, while the roof
        // capital covered the corner — above the empty corner column colMin from the roof raised
        // the ground fill up to the eaves, and the cave template bit a hole through it (a bad column in verify).
        int xHi = int(std::floor(pmaxx));
        if (xHi < xLo) xHi = xLo;
        for (int X = xLo; X <= xHi; ++X)
        {
            clipStripX(flat, strip, double(X), double(X + 1));
            if (strip.size() < 3) continue;
            double zmin = std::numeric_limits<double>::max(), zmax = -zmin;
            for (const QPointF& p : strip)
            {
                zmin = std::min(zmin, double(p.y()));
                zmax = std::max(zmax, double(p.y()));
            }
            int zLo = int(std::floor(zmin));
            int zHi = int(std::floor(zmax));         // clearly: see xHi — an exactly integer face must
            if (zHi < zLo) zHi = zLo;                // cover its outermost column

            const int lx = X - minx;
            const int ly = Y - miny;
            if (lx < 0 || lx >= sx || ly < 0 || ly >= sy) continue;
            for (int Z = zLo; Z <= zHi; ++Z)
            {
                const int lz = Z - minz;
                if (lz < 0 || lz >= sz) continue;
                const int cell = (lz * sy + ly) * sx + lx;
                if (!grid[cell])
                {
                    grid[cell] = 1;
                    surfaceCells.append(cell);
                }
            }
        }
    }
}


