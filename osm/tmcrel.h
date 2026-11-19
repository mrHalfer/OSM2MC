#pragma once
// The height of structures (the REL field of the extruding layers): a port of the floor-based calculation.
// The fallback typical height by code comes from the rules catalog (the Rel/Default of the object descriptor).
#include <QString>
#include <QHash>

//! Height by floors, code and outline shape — the formulas converting floors to height (REL from FLOOR).
//! floor <= 0 counts as 1; areaM2/perimM — outline area and perimeter in meters. Returns <= 0 for code 44350000 (an inner courtyard).
double tmcRelFromFloors(int code, int floor, double areaM2, double perimM);

//! The final building height: height (OSM) -> floors (building:levels) by the formulas -> enhancer data (db:rel, db:floor) -> defaultH.
//! tags: height, building:levels, db:rel, db:floor, _area (m²), _perim (m).
//! defaultH: the catalog typical height for the code (rules/<layer>.json Rel/Default; 0 is a height too — lawns, meadows).
QString tmcAutoRel(const QHash<QString, QString>& tags, int code, double defaultH = 6.0);
