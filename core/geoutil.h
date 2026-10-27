#pragma once
// Small geographic calculations on WGS84 longitude/latitude (QPointF points: x — longitude, y — latitude, degrees).
#include <QPointF>
#include <QVector>
#include <cmath>

//! Maximum boundary area for online OSM download, km² (larger is too slow for Overpass and gives a world that is too heavy)
constexpr double kTmcMaxOnlineAreaKm2 = 25.0;
//! Additional limit for the boundary bounding box, km²: protection against a long "thread" across a whole city
constexpr double kTmcMaxOnlineBBoxKm2 = 100.0;

constexpr double kTmcEarthRadiusM = 6371008.8;

inline double tmcDegToRad(double d) { return d * 3.14159265358979323846 / 180.0; }

//! Area of a polygon on the sphere, km² (Chamberlain–Duquette formula); the vertices need not repeat the first one to close the ring
inline double tmcPolygonAreaKm2(const QVector<QPointF>& ll)
{
    const int n = ll.size();
    if (n < 3) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const QPointF& a = ll[i];
        const QPointF& b = ll[(i + 1) % n];
        sum += tmcDegToRad(b.x() - a.x()) * (2.0 + std::sin(tmcDegToRad(a.y())) + std::sin(tmcDegToRad(b.y())));
    }
    return std::abs(sum) * 0.5 * kTmcEarthRadiusM * kTmcEarthRadiusM / 1e6;
}

//! Area of the enclosing rectangle (along parallels and meridians), km²
inline double tmcBBoxAreaKm2(const QVector<QPointF>& ll)
{
    if (ll.isEmpty()) return 0.0;
    double x0 = ll[0].x(), x1 = x0, y0 = ll[0].y(), y1 = y0;
    for (const QPointF& p : ll) { x0 = std::min(x0, p.x()); x1 = std::max(x1, p.x()); y0 = std::min(y0, p.y()); y1 = std::max(y1, p.y()); }
    const double w = tmcDegToRad(x1 - x0) * std::cos(tmcDegToRad(0.5 * (y0 + y1))) * kTmcEarthRadiusM;
    const double h = tmcDegToRad(y1 - y0) * kTmcEarthRadiusM;
    return w * h / 1e6;
}

//! The boundary is suitable for online download: area and extent are within the limits. reason — what is wrong (if anything).
inline bool tmcOnlineClipAllowed(const QVector<QPointF>& ll, QString* reason = nullptr)
{
    if (ll.size() < 3) { if (reason) *reason = QStringLiteral("the boundary is not closed"); return false; }
    if (tmcPolygonAreaKm2(ll) > kTmcMaxOnlineAreaKm2) { if (reason) *reason = QStringLiteral("the area is larger than allowed"); return false; }
    if (tmcBBoxAreaKm2(ll) > kTmcMaxOnlineBBoxKm2) { if (reason) *reason = QStringLiteral("the boundary is too elongated"); return false; }
    return true;
}
