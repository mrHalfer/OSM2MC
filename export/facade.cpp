#include "export/facade.h"
#include "export/blocks.h"
#include "export/colkey.h"
#include <QSet>
#include <limits>

//! The facade marking of kindergartens (44313000), schools (44312000) and research institutes (44315000): the blocks of the wall material of ONE building are repainted by position,
//! only the WALLS (not the roof and not the inner slabs). A kindergarten — colored bands every 4 rows and dotted columns, the colors change along the wall (red, blue, green, pink, purple);
//! a school — calm: only white vertical lines at the corners of the building; a research institute — the same white corner lines and red bands every 6 rows. No yellow and no orange.
void decorateWalls(VoxelStore& st, int code, quint16 wallId)
{
    int minY = std::numeric_limits<int>::max();
    for (auto cit = st.chunks.constBegin(); cit != st.chunks.constEnd(); ++cit)
        for (auto sit = cit->constBegin(); sit != cit->constEnd(); ++sit)
        {
            const QVector<quint16>& sec = sit.value();
            for (int i = 0; i < sec.size(); ++i)
                if (sec[i] == wallId) minY = std::min(minY, (sit.key() << 4) + ((i >> 8) & 15));
        }
    if (minY == std::numeric_limits<int>::max()) return;
    static const char* const kKid[] = { "minecraft:red_concrete", "minecraft:blue_concrete", "minecraft:lime_concrete",
                                        "minecraft:pink_concrete", "minecraft:purple_concrete", "minecraft:green_concrete" };
    quint16 kid[6];
    for (int k = 0; k < 6; ++k) kid[k] = quint16(IdPaletteStart + paletteIndexOf(kKid[k]));
    const quint16 white = quint16(IdPaletteStart + paletteIndexOf("minecraft:white_concrete")), red = kid[0];
    const bool kinder = (code == 44313000), school = (code == 44312000);

    // The corner columns of the outline: the columns that have a block in the wall rows (2..7 above the foundation) form the footprint; the air that is connected to the outside
    // (a flood fill from beyond the bounding box) is "outside"; a footprint column with outside neighbors on two perpendicular sides is a corner.
    QSet<qint64> corners;
    if (!kinder)
    {
        QSet<qint64> foot;
        int x0 = std::numeric_limits<int>::max(), x1 = std::numeric_limits<int>::min(), z0 = x0, z1 = x1;
        for (auto cit = st.chunks.constBegin(); cit != st.chunks.constEnd(); ++cit)
        {
            const int bx0 = keyFirst(cit.key()) << 4, bz0 = keySecond(cit.key()) << 4;
            for (auto sit = cit->constBegin(); sit != cit->constEnd(); ++sit)
            {
                const QVector<quint16>& sec = sit.value();
                const int baseY = sit.key() << 4;
                for (int i = 0; i < sec.size(); ++i)
                {
                    if (!sec[i]) continue;
                    const int rel = baseY + ((i >> 8) & 15) - minY;
                    if (rel < 2 || rel > 7) continue;
                    const int x = bx0 + (i & 15), z = bz0 + ((i >> 4) & 15);
                    foot.insert(pairKey(x, z));
                    x0 = std::min(x0, x); x1 = std::max(x1, x); z0 = std::min(z0, z); z1 = std::max(z1, z);
                }
            }
        }
        if (!foot.isEmpty() && qint64(x1 - x0 + 3) * (z1 - z0 + 3) <= 4000000)
        {
            const int W = x1 - x0 + 3, H = z1 - z0 + 3;               // the box with a margin of 1 cell
            QVector<char> outside(W * H, 0);
            QVector<int> stack;
            outside[0] = 1; stack.append(0);
            while (!stack.isEmpty())
            {
                const int cur = stack.takeLast();
                const int cx = cur % W, cz = cur / W;
                const int nb[4][2] = { { cx + 1, cz }, { cx - 1, cz }, { cx, cz + 1 }, { cx, cz - 1 } };
                for (const auto& n : nb)
                {
                    if (n[0] < 0 || n[0] >= W || n[1] < 0 || n[1] >= H) continue;
                    const int idx = n[1] * W + n[0];
                    if (outside[idx] || foot.contains(pairKey(n[0] - 1 + x0, n[1] - 1 + z0))) continue;
                    outside[idx] = 1; stack.append(idx);
                }
            }
            auto isOut = [&](int x, int z) { const int gx = x - x0 + 1, gz = z - z0 + 1; return gx >= 0 && gx < W && gz >= 0 && gz < H && outside[gz * W + gx]; };
            for (const qint64 ck : foot)
            {
                const int x = keyFirst(ck), z = keySecond(ck);
                const bool ox = isOut(x - 1, z) || isOut(x + 1, z), oz = isOut(x, z - 1) || isOut(x, z + 1);
                if (ox && oz) corners.insert(ck);
            }
        }
    }

    for (auto cit = st.chunks.begin(); cit != st.chunks.end(); ++cit)
    {
        const int bx0 = keyFirst(cit.key()) << 4, bz0 = keySecond(cit.key()) << 4;
        for (auto sit = cit->begin(); sit != cit->end(); ++sit)
        {
            QVector<quint16>& sec = sit.value();
            const int baseY = sit.key() << 4;
            for (int i = 0; i < sec.size(); ++i)
            {
                if (sec[i] != wallId) continue;
                const int x = bx0 + (i & 15), z = bz0 + ((i >> 4) & 15), y = baseY + ((i >> 8) & 15), rel = y - minY;
                if (rel < 2) continue;                                              // the foundation stays as it is
                if (st.get(x, y, z) == 0 || st.get(x, y + 1, z) == 0 || st.get(x, y - 1, z) == 0) continue;     // only the WALL: not the roof (air above it) and not an inner slab (air below)
                if (kinder)
                {
                    if (rel % 4 == 2 || (x + z + (rel / 4) * 2) % 6 == 0)
                        sec[i] = kid[((x + z + 4096) / 3 + rel / 4) % 6];
                }
                else if (corners.contains(pairKey(x, z))) sec[i] = white;
                else if (!school && rel % 6 == 3) sec[i] = red;                     // a research institute: red bands
            }
        }
    }
}
