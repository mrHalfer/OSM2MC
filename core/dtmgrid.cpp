#include "core/dtmgrid.h"
#include <algorithm>
#include <limits>

namespace
{
//! The mean of the valid cells of the window [i-r..i+r] x [j-r..j+r] (clamped to the grid) in src; false — all are NaN.
bool windowMean(const DtmGrid& g, const QVector<float>& src, int i, int j, int r, double& out)
{
    double sum = 0;
    int n = 0;
    const int i0 = std::max(0, i - r), i1 = std::min(g.w - 1, i + r);
    const int j0 = std::max(0, j - r), j1 = std::min(g.h - 1, j + r);
    for (int jj = j0; jj <= j1; ++jj)
        for (int ii = i0; ii <= i1; ++ii)
        {
            const float v = src[qsizetype(jj) * g.w + ii];
            if (std::isnan(v)) continue;
            sum += v;
            ++n;
        }
    if (!n) return false;
    out = sum / n;
    return true;
}
}

void tmcDtmFillHoles(DtmGrid& g, int passes)
{
    for (int p = 0; p < passes; ++p)
    {
        bool changed = false;
        for (int j = 0; j < g.h; ++j)
            for (int i = 0; i < g.w; ++i)
            {
                if (!std::isnan(g.at(i, j))) continue;
                double v;
                if (windowMean(g, g.z, i, j, 1, v)) { g.at(i, j) = float(v); changed = true; }
            }
        if (!changed) break;
    }
}

void tmcDtmSmooth(DtmGrid& g, double meters)
{
    if (g.w <= 0 || g.h <= 0 || g.px <= 0) return;
    const int r = std::max(1, int(std::lround(meters / g.px)));
    for (int pass = 0; pass < 2; ++pass)
    {
        const QVector<float> src = g.z;                 // the pass reads only its own input
        for (int j = 0; j < g.h; ++j)
            for (int i = 0; i < g.w; ++i)
            {
                double v;
                g.at(i, j) = windowMean(g, src, i, j, r, v) ? float(v) : std::numeric_limits<float>::quiet_NaN();
            }
    }
}

double tmcDtmNormalize(DtmGrid& g, double maxRaise, double* scale)
{
    if (scale) *scale = 1.0;
    double lo = std::numeric_limits<double>::max(), hi = -lo;
    for (const float v : g.z)
    {
        if (std::isnan(v)) continue;
        lo = std::min(lo, double(v));
        hi = std::max(hi, double(v));
    }
    if (hi < lo) return 0;                              // nothing valid
    const double relief = hi - lo;
    double k = 1.0;
    if (relief > maxRaise && relief > 0) k = maxRaise / relief;
    if (scale) *scale = k;
    for (float& v : g.z)
    {
        if (std::isnan(v)) continue;
        v = float(std::max(0.0, (double(v) - lo) * k));
    }
    return relief;
}
