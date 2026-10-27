#pragma once
// The height of structures (the REL field of the bldpol layer): a port of the floor-based calculation and a compact table of typical heights heights.csv by code.
#include <QString>
#include <QHash>

//! The compact table of typical heights heights.csv (CODE;HIGH;OBJNAME50) — an extract of the classifier, only the codes of the layers in use. Needed when an object has neither height nor floors.
class TmcHeightTable
{
public:
    //! Load the CSV. false — no file / unreadable.
    bool load(const QString& dbfPath);
    //! Find heights.csv: next to the exe, then in the current folder. An empty string — not found.
    static QString findDefault();
    //! Typical height by code; -1 — the code is not in the table (0 — the code exists but has no height: lawns, meadows)
    double high(int code) const { return mHigh.value(code, -1.0); }
    int size() const { return mHigh.size(); }
private:
    QHash<int, double> mHigh;
};

//! Height by floors, code and outline shape — the formulas converting floors to height (REL from FLOOR).
//! floor <= 0 counts as 1; areaM2/perimM — outline area and perimeter in meters. Returns <= 0 for code 44350000 (an inner courtyard).
double tmcRelFromFloors(int code, int floor, double areaM2, double perimM);

//! The final building height: height (OSM) -> floors (building:levels) by the formulas -> enhancer data (db:rel, db:floor) -> heights.csv by code -> defaultH.
//! tags: height, building:levels, db:rel, db:floor, _area (m²), _perim (m).
QString tmcAutoRel(const QHash<QString, QString>& tags, int code, const TmcHeightTable& clutters, double defaultH = 6.0);
