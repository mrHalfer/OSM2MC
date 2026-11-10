#pragma once
// carset (donor city-map cars, CAR1 from extract_cars.py).
#include "export/caveset.h"
#include <QString>
#include <QVector>

//! Parking-lot cars copied from the donor city-map schematics (car_templates.bin): the exact block grid of each
//! car, kept in four clockwise rotations with the state properties turned accordingly, and with the TNT core
//! of the punch trap baked into the middle (its rotated coordinates are stored next to the grid so the trap
//! can anchor on them without knowing the shape). The palette strings are mapped through the cave-state
//! registry of the loaded CaveSet (like the Pale Garden donor), so cars need the cave templates present.
struct CarSet
{
    struct Cell { quint8 x, y, z; quint16 id; };                  //!< a non-air cell of the rotated car
    struct Rot { QVector<Cell> cells; int cx = 0, cy = 0, cz = 0; };   //!< one quarter turn + its TNT core
    struct Car { QString name; int sx = 0, sy = 0, sz = 0; Rot rots[4]; };   //!< the native box, r%2 swaps sx and sz

    QVector<Car> cars;

    bool empty() const { return cars.isEmpty(); }
    bool load(const QString& path, CaveSet& cs);                  //!< false — the file is corrupt/absent
};
