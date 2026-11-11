#pragma once
// Geometry helpers of the exporter: convex polygon clipping (Sutherland-Hodgman),
// the mesh->block transform and the triangle rasterizer
// (moved out of mcworldexporter.cpp).
#include "export/geom.h"
#include "export/mcworldexporter.h"
#include <QByteArray>
#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <QVector3D>
#include <algorithm>
#include <atomic>
#include <limits>
#include <QList>
#include <cmath>

// Convex clipping of polygons (Sutherland-Hodgman)


using Poly3 = QVector<QVector3D>;
using Poly2 = QVector<QPointF>;

struct VxTransform
{
    double cx0 = 0;      // the world X of the scene center -> block X 0
    double cy0 = 0;      // the world Y of the scene center -> block Z 0 (the north of the map = -Z in MC)
    double yBase = 0;    // the world height corresponding to block Y 1
    double zScale = 1;   // the height compression if the range is larger than 240
    double sx = 1;       // blocks per map unit along X (= meters per unit: 1 block = 1 m)
    double sz = 1;       // the same along the map Y (block Z)
};

inline double toBlockX(const VxTransform& tf, double wx) { return (wx - tf.cx0) * tf.sx; }
inline double toBlockY(const VxTransform& tf, double wz) { return (wz - tf.yBase) * tf.zScale + 1.0; }
inline double toBlockZ(const VxTransform& tf, double wy) { return (tf.cy0 - wy) * tf.sz; }

void clipHalf3(const Poly3& in, Poly3& out, bool minY, double plane);
void clipStripX(const Poly2& in, Poly2& out, double x0, double x1);
bool isRectilinearOutline(const McWorldExporter::McObject& o, double sx, double sy, double tolDeg);
//! A column segment: x is fixed, z in [z0, z1]. A forest is stored by segments, not by column: on large data
//! these are tens of millions of columns (gigabytes and tens of seconds of hash insertions) versus tens of thousands of segments.
struct ColSpan { int x, z0, z1; };

QVector<ColSpan> spansFromCols(const QList<qint64>& sorted);
QVector<ColSpan> mergeSpans(QVector<ColSpan> v);
bool spansContain(const QVector<ColSpan>& v, int x, int z);
void footprintColumns(const McWorldExporter::McObject& obj, const VxTransform& tf,
                             QList<qint64>& out, const std::atomic_bool* cancel, const QRectF& clip = QRectF());
void rasterizeTriangle(const QVector3D& t0, const QVector3D& t1, const QVector3D& t2,
                       int minx, int miny, int minz, int sx, int sy, int sz,
                       QByteArray& grid, QVector<int>& surfaceCells);

