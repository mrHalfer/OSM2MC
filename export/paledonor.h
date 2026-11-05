#pragma once
// paledonor (donor-world templates): moved out of mcworldexporter.cpp.
#include "export/caveset.h"
#include <QSet>
#include <QString>
#include <QVector>
#include <QtGlobal>


//! The real "Pale Garden" (pale_garden.bin, PGD1 from extract_pale_garden.py): a rectangle from a game-generated world — the terrain height
//! hd of each column, the top block of the terrain and everything standing above the terrain (trees, moss, grass, creaking hearts), with heights counted from hd.
//! It is laid onto the largest forest as a mirrored tile: the donor terrain (relative to its mean) is added to the level of our ground,
//! and fades out smoothly at the forest boundary (blending), trees are transferred whole together with the terrain.
struct PaleDonor
{
    int W = 0, H = 0;
    QVector<qint16> hd;
    QVector<quint16> surf;                 //!< the internal id of the top terrain block
    QVector<quint32> off;                  //!< the column offset into ady/aid (W*H+1)
    QVector<quint32> uoff;                 //!< PGD3: the offset of the column underground cells into udy/uid (W*H+1)
    QVector<quint8> udy;                   //!< PGD3: the depth of a cell UNDER the terrain (1..28)
    QVector<quint16> uid;                  //!< PGD3: the internal id (air / water) — pits, sinkholes and voids right at the surface
    QVector<qint16> ady;                   //!< the block offset from the terrain (PGD2 — signed: building blocks buried in the ground)
    QVector<quint16> aid;                  //!< the internal id of a block above the terrain
    QVector<quint16> aidDry;               //!< the same blocks without waterlogged (for ships standing NOT in water, otherwise water spreads over the deck)
    struct PatchBe { int x = 0, z = 0, dy = 0, kind = 0; QString str; };
    QVector<PatchBe> bes;                  //!< the chests/barrels/spawners of the patch (PGD2)
    int bbx0 = 0, bbz0 = 0, bbx1 = -1, bbz1 = -1;   //!< the box of the building inside the patch (PGD2)
    QString name;
    QSet<quint16> heartIds;                //!< the ids of creaking_heart states
    QSet<quint16> mansionIds;              //!< the ids of mansion blocks (dark oak, cobblestone, glass panes...)
    quint16 logId = 0, heartId = 0;        //!< the ids of pale_oak_log (axis=y) and creaking_heart — hearts are "re-seeded" into trunks from them
    QVector<quint8> mansion;               //!< the columns of the woodland mansion in the template, extended by 4 blocks
    int mx0 = 0, mx1 = -1, mz0 = 0, mz1 = -1;   //!< the mansion box in the template (mx1 < mx0 — no mansion)
    //! The column that replaces a mansion column: a reflection across the box boundary onto the neighboring free part of the template; -1 — there is none.
    int reflectFree(int ux, int uz) const
    {
        auto free = [&](int x, int z) { return x >= 0 && x < W && z >= 0 && z < H && !mansion[z * W + x]; };
        int c[4][2] = { { 2 * mx0 - 1 - ux, uz }, { 2 * (mx1 + 1) - 1 - ux, uz }, { ux, 2 * mz0 - 1 - uz }, { ux, 2 * (mz1 + 1) - 1 - uz } };
        for (int k = 0; k < 4; ++k) if (free(c[k][0], c[k][1])) return c[k][1] * W + c[k][0];
        return -1;
    }
    bool empty() const { return W == 0; }
    bool load(const QString& path, CaveSet& cs);
};

int findPlacementSalt(qint64 seed, int cx, int cz, int spacing);

