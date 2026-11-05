#pragma once
// The voxel storage: chunk -> section (16^3) -> block id, and the per-column ground metadata.
#include "export/blocks.h"
#include "export/colkey.h"
#include <QHash>
#include <QVector>
#include <limits>

class VoxelStore
{
public:
    //! chunk key = pairKey(chunkX, chunkZ); value: section index (y>>4, -4..19 — a 26.x world: -64..319) -> 4096 ids.
    QHash<ColKey, QHash<int, QVector<quint16>>> chunks;

    quint16 get(int x, int y, int z) const
    {
        if (y < -64 || y > 319) return 0;
        auto it = chunks.constFind(pairKey(x >> 4, z >> 4));
        if (it == chunks.constEnd()) return 0;
        auto sit = it->constFind(y >> 4);
        if (sit == it->constEnd()) return 0;
        return (*sit)[sectionIndex(x, y, z)];
    }

    void set(int x, int y, int z, quint16 id)
    {
        if (y < -64 || y > 319 || id == IdAir) return;
        QVector<quint16>& sec = chunks[pairKey(x >> 4, z >> 4)][y >> 4];
        if (sec.isEmpty())
            sec.fill(0, 4096);
        sec[sectionIndex(x, y, z)] = id;
    }

    //! Erase a block (set() ignores air): needed for cutting down trees under a village plot.
    void erase(int x, int y, int z)
    {
        if (y < -64 || y > 319) return;
        auto it = chunks.find(pairKey(x >> 4, z >> 4));
        if (it == chunks.end()) return;
        auto sit = it->find(y >> 4);
        if (sit == it->end() || sit->isEmpty()) return;
        (*sit)[sectionIndex(x, y, z)] = 0;
    }

    //! Raise/lower everything written by dy blocks (for the "normal world": the city is placed above the
    //! average Minecraft height). The sections (16-block slices) move at an arbitrary dy —
    //! so we simply move every non-empty block through set(), which sorts it out itself.
    void translateY(int dy)
    {
        if (dy == 0) return;
        QHash<ColKey, QHash<int, QVector<quint16>>> old;
        old.swap(chunks);
        for (auto cit = old.constBegin(); cit != old.constEnd(); ++cit)
        {
            const int bx0 = keyFirst(cit.key()) << 4, bz0 = keySecond(cit.key()) << 4;
            for (auto sit = cit->constBegin(); sit != cit->constEnd(); ++sit)
            {
                const int y0 = sit.key() << 4;
                const QVector<quint16>& sec = sit.value();
                for (int i = 0; i < sec.size(); ++i)
                {
                    if (!sec[i]) continue;
                    set(bx0 + (i & 15), y0 + ((i >> 8) & 15) + dy, bz0 + ((i >> 4) & 15), sec[i]);
                }
            }
        }
    }
};

//! The chunk ground is COLUMN METADATA, not blocks (an optimization of the "expensive" ground): previously the fill
//! wrote every block from y=-64 to the surface into VoxelStore (~130 store.set per column, ~170 million
//! calls per noise export). Now the depth (stone) in NBT is a unary section without data anyway, and
//! only the 3 top blocks of a column and the water column are significant — buildChunkNbt completes them analytically.
//! The column index is lz*16+lx (as in the arrays below), the values are as of after the yShift lift.
struct GroundChunk
{
    QVector<qint16> top;    //!< the column top (-1..255; signed — the junction chamfer lowers the edge below 0, see 3.59b)
    QVector<quint16> surf;  //!< the top block: turf or the wool of a csrpol plate
    QVector<quint16> wd;    //!< the depth of the water pit (0 — land); the water occupies (top-wd, top]
    QVector<quint8> art;    //!< 1 — the column of an artificial reservoir (without a sandy bottom); empty — all are natural
    int stoneFloor = std::numeric_limits<int>::min();  //!< below (inclusive) the whole section is solid stone
    int caveTpl = -1;       //!< the index of the cave template (CaveSet) for the underground, -1 — solid rock
    bool isEmpty() const { return top.isEmpty(); }
};
