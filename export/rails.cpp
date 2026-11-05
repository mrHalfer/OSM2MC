#include "export/rails.h"
#include "export/blocks.h"
#include <algorithm>
#include <cmath>

//! A track axis line (points in block coordinates) -> a chain of cells where neighboring cells are connected BY A SIDE
//! (Minecraft rails connect only orthogonally): a diagonal step is completed with a corner cell,
//! a 180° reversal ("whiskers") is cut off.
QVector<QPoint> railCellChain(const QVector<QPointF>& pts)
{
    QVector<QPoint> cells;
    auto push = [&cells](int x, int z)
    {
        const QPoint c(x, z);
        if (!cells.isEmpty() && cells.last() == c) return;
        if (cells.size() >= 2 && cells[cells.size() - 2] == c) { cells.removeLast(); return; }
        if (!cells.isEmpty())
        {
            const QPoint l = cells.last();
            if (l.x() != x && l.y() != z)
                cells.append(QPoint(x, l.y()));          // a diagonal -> through a corner cell
        }
        cells.append(c);
    };
    for (int i = 0; i + 1 < pts.size(); ++i)
    {
        const QPointF a = pts[i], b = pts[i + 1];
        const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
        const int n = std::max(1, int(std::ceil(len * 4.0)));
        for (int t = (i == 0 ? 0 : 1); t <= n; ++t)
        {
            const double u = double(t) / n;
            push(int(std::floor(a.x() + (b.x() - a.x()) * u)), int(std::floor(a.y() + (b.y() - a.y()) * u)));
        }
    }
    return cells;
}

//! The rail shape (the kRailShapes index) by the neighbors of the chain: the directions to the prev/next cells (dx,dz by one of 4).
//! The end of a track (one link) — straight along this link.
int railShapeFor(const QPoint& cur, const QPoint* prev, const QPoint* next)
{
    bool N = false, E = false, S = false, W = false;
    auto mark = [&](const QPoint& o)
    {
        const int dx = o.x() - cur.x(), dz = o.y() - cur.y();
        if (dx > 0) E = true; else if (dx < 0) W = true; else if (dz > 0) S = true; else if (dz < 0) N = true;
    };
    if (prev) mark(*prev);
    if (next) mark(*next);
    if (N && S) return 0;
    if (E && W) return 1;
    if (S && E) return 6;
    if (S && W) return 7;
    if (N && W) return 8;
    if (N && E) return 9;
    if (N || S) return 0;
    return 1;
}

//! Electrifies a 1-block-wide rail line: every 10th rail becomes a powered rail. A powered rail cannot be
//! curved, so the hit must land on a STRAIGHT cell: if the count falls on a turn, the hit carries over to the
//! next straight cell and several powered rails are laid in a row there (2-5 in a row). Only rails that are
//! really in the store are electrified (a cell blocked by a bridge is skipped and carries its hit further).
//! A redstone block is put under every powered rail (a user request: "so that the electrified rails are
//! always active") - a rail on redstone never switches to its braking state.
//! Returns the number of powered rails laid.
int electrifyRails(VoxelStore& store, const QVector<QPoint>& chain, const QVector<int>& ys)
{
    const int n = chain.size();
    if (n < 3)
        return 0;
    const quint16 rsBlock = quint16(IdPaletteStart + paletteIndexOf("minecraft:redstone_block"));
    QVector<char> straight(n, 0);       // both neighbours on one line — a straight cell, not a turn
    for (int ci = 1; ci + 1 < n; ++ci)
        straight[ci] = (chain[ci] - chain[ci - 1]) == (chain[ci + 1] - chain[ci]);
    int pending = 0, done = 0;
    for (int ci = 0; ci < n; ++ci)
    {
        if ((ci + 1) % 10 != 0)
            continue;                   // the count itself stays "every 10th", the extras are only the carry-overs
        if (!straight[ci]) { ++pending; continue; }
        int place = pending + 1;
        pending = 0;
        for (int k = ci; k < n && place > 0; ++k)
        {
            if (!straight[k])
                break;                  // a turn ahead — the rest of the carry-over moves further along the line
            const quint16 b = store.get(chain[k].x(), ys[k], chain[k].y());
            if (b < IdRailShapeBase || b >= IdRailShapeMax)
                continue;               // occupied / already powered
            store.set(chain[k].x(), ys[k], chain[k].y(),
                      quint16(int(IdPoweredRailShapeBase) + (int(b) - int(IdRailShapeBase))));
            store.set(chain[k].x(), ys[k] - 1, chain[k].y(), rsBlock);    // the rail's power source
            ++done; --place;
        }
        pending += place;
    }
    return done;
}
