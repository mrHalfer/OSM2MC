#pragma once
// playgroundset (the playground structures, PGP1 from extract_playgrounds.py).
#include "export/caveset.h"
#include <QString>
#include <QVector>

//! The playground structures copied from the owner's .schem files (playground_templates.bin): the exact block grid
//! of each structure, kept in four clockwise rotations with the state properties turned accordingly. The palette
//! strings are mapped through the cave-state registry (like the cars and the Pale Garden donor). The grid's y=0
//! layer is the layer ABOVE the ground: the exporter puts it on the pad's surface.
struct PlaygroundSet
{
    struct Cell { quint8 x, y, z; quint16 id; };                  //!< a non-air cell of the rotated structure
    struct Rot { QVector<Cell> cells; };                          //!< one quarter turn
    struct Item { QString name; int sx = 0, sy = 0, sz = 0; Rot rots[4]; };   //!< the native box, r%2 swaps sx and sz

    QVector<Item> items;

    bool empty() const { return items.isEmpty(); }
    bool load(const QString& path, CaveSet& cs);                  //!< false — the file is corrupt/absent
};
