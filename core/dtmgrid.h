#pragma once
// Preparing a height matrix from open elevation tiles: the terrarium pixel decoding and the grid work
// (filling the holes, dropping the small relief, normalization "only raise above the current terrain").
// No GDAL and no network here: see core/dtmbuild.cpp for the GeoTIFF writing and app/terrainfetch.cpp for the download.
#include <QVector>
#include <cmath>

//! Terrarium pixel -> meters: R*256 + G + B/256 - 32768 (the encoding of the open AWS terrain tiles).
inline double tmcTerrariumHeight(int r, int g, int b)
{
    return double(r) * 256.0 + double(g) + double(b) / 256.0 - 32768.0;
}

//! A regular square-pixel grid of heights in the Web Mercator plane (EPSG:3857), meters; NaN — no data.
struct DtmGrid
{
    int w = 0, h = 0;                  //!< pixels
    double x0 = 0, yTop = 0;           //!< the top-left corner, EPSG:3857 meters
    double px = 0;                     //!< the pixel size, meters
    QVector<float> z;                  //!< w*h heights, meters (row-major, row 0 is the top)

    float& at(int i, int j) { return z[qsizetype(j) * w + i]; }
    float at(int i, int j) const { return z[qsizetype(j) * w + i]; }
};

//! Replace the no-data cells by the mean of the valid neighbours (a few passes); the rest stay NaN.
void tmcDtmFillHoles(DtmGrid& g, int passes = 16);

//! Two box-blur passes with the radius meters/px: the small relief goes away, mountains and canyons stay.
void tmcDtmSmooth(DtmGrid& g, double meters);

//! Base = the lowest valid height; every cell becomes its rise above the base (0..maxRaise). Returns the real
//! relief in meters and, through scale, how much it was compressed to fit maxRaise.
double tmcDtmNormalize(DtmGrid& g, double maxRaise, double* scale = nullptr);
