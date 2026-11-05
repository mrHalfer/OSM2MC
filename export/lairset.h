#pragma once
// lairset (donor-world templates): moved out of mcworldexporter.cpp.
#include "export/caveset.h"
#include <QRect>
#include <QString>
#include <QVector>
#include <QtGlobal>

//! Donor-world "lair" structures (lair_structures.bin, the LST1 format from extract_lair_structs.py): the stronghold portal
//! room and the Ancient City (Deep Dark). Each is a rectangle of chunks with blocks y=-64..63 (like MCW2), placed on the world
//! at an ARBITRARY X/Z shift (so that the entrance column falls under a church shaft), without rotations or mirroring. The Y shift is zero:
//! the stronghold lies at y≈22..36, the city at y≈-52..-22, so they do not overlap in height. The block states are added
//! to the CaveSet palette (g_caveStates is one per export).
struct LairBe
{
    int x = 0, y = 0, z = 0, kind = 0;      //!< x,z — coordinates in the template system (before the shift)
    QString str;
};

struct LairStruct
{
    QString id, biome;
    int cx0 = 0, cz0 = 0, W = 0, H = 0;     //!< the chunk rectangle of the template
    int ex = 0, ez = 0, floorY = 0, topY = 0;   //!< the entrance column: the floor floorY, air up to topY
    int yLo = -64, yHi = 63;                    //!< the heights that the structure overlaps in the underground (for the stronghold and the city they do not overlap)
    int room[6] = { 0, 0, 0, 0, 0, 0 };         //!< the main hall (x0,y0,z0,x1,y1,z1 in the template system): the "structure start" box for /locate
    QVector<quint16> cells;                 //!< internal ids, index ((y+64)*(H*16) + z)*(W*16) + x (x,z — from the rectangle corner)
    QVector<LairBe> bes;
};

struct LairPlace
{
    int s = 0;                              //!< the index in LairSet::st
    int tx = 0, tz = 0;                     //!< the template -> world shift
    int sx = 0, sz = 0;                     //!< the shaft column (world x,z)
};

struct LairSet
{
    QVector<LairStruct> st;
    QVector<LairPlace> placed;

    bool empty() const { return st.isEmpty(); }
    int find(const QString& id) const { for (int i = 0; i < st.size(); ++i) if (st[i].id == id) return i; return -1; }
    bool load(const QString& path, CaveSet& cs);
    //! The world placement rectangle (blocks, inclusive).
    QRect rectOf(const LairPlace& p) const
    {
        const LairStruct& s = st[p.s];
        return QRect(s.cx0 * 16 + p.tx, s.cz0 * 16 + p.tz, s.W * 16, s.H * 16);
    }
    //! The "structure start" (minecraft:stronghold / ancient_city) for /locate, eye of ender search and advancements: the box is the main hall.
    struct Start { QString sid; int scx = 0, scz = 0; int bb[6] = { 0, 0, 0, 0, 0, 0 }; };
    QVector<Start> startsTouching(int cx, int cz) const;   //!< structures whose hall touches the chunk (a reference), the start lies in the hall center chunk
    //! Overlays the placed structures onto the chunk underground; true — the template changed. deepDark — the chunk touches a structure with the Deep Dark biome.
    bool apply(int cx, int cz, CaveTemplate& ct, bool* deepDark) const;
};

