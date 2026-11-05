#pragma once
// caveset (donor-world templates): moved out of mcworldexporter.cpp.
#include "export/blocks.h"
#include "export/colkey.h"
#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>
#include <QtGlobal>

//! A block entity of a donor-world structure (a chest/barrel with loot, a spawner with a mob) in chunk coordinates.
struct CaveBe
{
    int lx = 0, lz = 0, y = 0;
    int kind = 0;          //!< 1 chest, 2 trapped_chest, 3 barrel, 4 mob_spawner, 5 — any other BE (trial_spawner, vault, dispenser, a pot…) as raw NBT
    QString str;           //!< LootTable or the mob id
    QByteArray raw;        //!< kind 5: the block entity fields (id and data) without x/y/z — written into the chunk as is
};

//! The underground of ONE chunk (16×16×128, y=-64..63), assembled from a continuous block of the donor world.
struct CaveTemplate
{
    QVector<quint16> blocks;   //!< the index (y+64)*256 + lz*16 + lx, y ∈ [-64..63] — internal block ids
    quint16 uniId[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };  //!< section k (y=-64+16k..-49+16k) entirely
         //!< of ONE solid block — written as a unary palette without data (like the stone
         //!< "apron" depth); 0 — the section is motley (caves/different rock), a merge is needed
    QVector<CaveBe> bes;
};

//! The cave world (cave_templates.bin, the MCW2 format from extract_cave_world.py): a W×H RECTANGLE of neighboring
//! chunks of the donor world with their mutual positions preserved — caves, mines, dungeons and ancient cities
//! stay whole. The export world is covered with it by MIRROR repetition (a reflection across the boundary:
//! at the tile joint the content matches, caves do not break off); the tile offset depends on the seed.
//! The blocks are stored together with properties; on reflection the properties are mirrored (facing, east/west/north/south,
//! the shape of rails/stairs, hinge, the chest type, rotation).
struct CaveSet
{
    int W = 0, H = 0, offX = 0, offZ = 0;
    QVector<CaveState> states;           //!< the file palette + mirrored variants (g_caveStates looks here)
    QVector<quint16> idOf;               //!< the palette index -> the internal block id
    QVector<quint32> flipOf[3];          //!< the mirror by X / by Z / by XZ: the palette index -> the palette index
    int baseN = 0;                       //!< the size of the file palette (before the mirrored variants)
    QSet<quint16> oceanIds;              //!< the ids of water and underwater plants: the top solid body of them (the donor ocean) is filled with stone
    QByteArray file;
    QVector<quint32> off, len;           //!< the chunk blobs in the file (index j*W + i)

    CaveSet() = default;
    CaveSet(const CaveSet&) = delete;
    CaveSet& operator=(const CaveSet&) = delete;
    ~CaveSet() { if (g_caveStates == &states) g_caveStates = nullptr; }

    bool empty() const { return W == 0; }
    struct TplPiece { QString id, loc; int bb[6] = { 0, 0, 0, 0, 0, 0 }; };         //!< a structure piece: a box in the block coordinates of the template window
    struct TplStart { QString id; int sx = 0, sz = 0; QVector<TplPiece> pieces; };   //!< a start of a template structure: the start chunk (indices in the window)
    struct WorldStart { QString id; int startCx = 0, startCz = 0; bool here = false; QVector<TplPiece> pieces; };   //!< a start for ONE world chunk
    QVector<TplStart> tplStarts;                                   //!< mines, trial chambers, donor cities — get starts in the world
    QHash<ColKey, QVector<int>> tplStartsByChunk;                  //!< the template chunk (u,v) -> the numbers of starts whose pieces touch it
    //! The starts touching a world chunk (taking the mirrored tiling into account): a reference to a start — in every touched chunk, the start itself (here) — in the start chunk.
    void startsAt(int cx, int cz, QVector<WorldStart>& out) const;
    bool load(const QString& path, quint64 seed, QString* err);   //!< false — the file is corrupt/does not fit
    CaveTemplate get(int cx, int cz) const;                       //!< the underground of the chunk (cx,cz) of the export world
};

QString caveStateKey(const CaveState& s);
quint16 caveStateToId(const CaveState& st, int paletteIdx);

