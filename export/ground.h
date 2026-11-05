#pragma once
// The per-column ground rules shared by the exporter and the chunk writer:
// the climate zone, biome names and groundBlockAt (moved out of mcworldexporter.cpp).
#include "export/blocks.h"
#include <QtGlobal>

//! The climate zone of the current export (Climate::Zone) and the chunk biome names for the cell values 0..9 (see biomeCells); set in Export before the chunk assembly.
extern int g_zone;
extern const char* g_biomeName[10];
extern quint16 g_zoneSurface;
extern quint16 g_sandId, g_clayId;   // the sand and clay ids (palette ones; set in Export before the chunk assembly)


//! The ground block in a column (top/surf/wd — metadata) at height y; IdAir above the surface.
//! It repeats the former rules of the fill loop: water up to top, surface exactly at top, 2 ground blocks under it,
//! deeper — stone (with wd>3 the water also reaches below top-3 — hence the stone threshold max(3, wd)).

inline quint16 groundBlockAt(int top, quint16 surf, int wd, int y, int wx = 0, int wz = 0, bool artificial = false)
{
    if (y > top) return IdAir;
    if (wd > 0 && y > top - wd) return IdWater;
    if (wd > 0 && g_sandId && !artificial && y >= top - wd - 2)
    {   // the bottom of a reservoir: a 3-block layer of sand, in places (in patches) clay instead of the top block
        if (y == top - wd)
        {
            quint32 h = quint32(wx >> 1) * 374761393u ^ quint32(wz >> 1) * 668265263u ^ 0xC1A7u;
            h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
            return (g_clayId && h % 100u < 24u) ? g_clayId : g_sandId;
        }
        return g_sandId;
    }
    if (y == top) return surf;
    if (y >= top - 2) return IdDirt;
    return IdStone;
}

