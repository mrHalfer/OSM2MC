#pragma once
// The "enhancer": fallback data about buildings (type CODE, height REL, floors FLOOR) from a ready .dat file. The developer produces the file
// with a private tool of the author (points of buildings with the heights and floor counts fuzzed on purpose); tMC uses only the .dat itself and no database.
// The format is described in tests/tools/tmcb.py.
#include <QString>
#include <QVector>
#include <QHash>
#include <QSet>
#include <QFile>

class OGRGeometry;

class TmcEnhancer
{
public:
    struct Rec
    {
        qint64 id = -1;          //!< record number in the file (for marking a point as "taken")
        double lat = 0, lon = 0; //!< WGS84 degrees (already shifted)
        int code = 0;            //!< the building CODE
        double rel = 0;          //!< height, m (0 — unknown)
        int floor = 0;           //!< floors (0 — unknown)
    };

    TmcEnhancer() = default;
    TmcEnhancer(const TmcEnhancer&) = delete;
    TmcEnhancer& operator=(const TmcEnhancer&) = delete;

    //! Open a .dat (the header and the cell index are read; the records on demand). false + err — no file / wrong format.
    bool load(const QString& path, QString* err);
    //! tmc_buildings.dat next to the exe, then in the current folder; an empty string — none
    static QString findDefault();
    bool isLoaded() const { return mLoaded; }
    qint64 recordCount() const { return mRecords; }
    QString path() const { return mPath; }

    //! Records that fall into a WGS84 rectangle
    QVector<Rec> query(double lon0, double lat0, double lon1, double lat1) const;
    //! Pick a record for the building outline g (WGS84): inside the outline or not farther than maxDistM meters from it; the points from claimed are skipped,
    //! the chosen one is added to claimed (one point — one building). false — nothing suitable.
    bool match(const OGRGeometry* g, QSet<qint64>& claimed, Rec* out, double maxDistM = 8.0) const;
    //! The database has several codes for the private sector and for multi-storey buildings, we do not distinguish them: 44400100/44400110 -> 44400002, 44400001 -> 44400000
    static int normalizeCode(int code);

private:
    struct Cell { qint64 first = 0; quint32 count = 0; };
    bool mLoaded = false;
    QString mPath;
    qint64 mRecords = 0;
    qint64 mDataOffset = 0;
    int mCellMicro = 10000;
    QHash<qint64, Cell> mCells;
    mutable QFile mFile;
    mutable QHash<qint64, QVector<Rec>> mCache;
};
