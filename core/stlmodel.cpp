#include "core/stlmodel.h"
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QSet>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {

float readF32(const char* p)
{
    float f = 0.0f;
    std::memcpy(&f, p, sizeof(f));                 // the STL floats are little-endian; x86/ARM64 hosts are too
    return f;
}

quint32 readU32(const char* p)
{
    quint32 v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

bool parseBinary(const QByteArray& d, QVector<StlTri>& out)
{
    if (d.size() < 84) return false;
    const quint32 n = readU32(d.constData() + 80);
    const qint64 expected = 84 + qint64(n) * 50;
    if (expected > d.size() || (expected < d.size() && expected + 4 < d.size())) return false;
    if (n == 0 || n > 5000000u) return false;
    out.reserve(int(n));
    const char* p = d.constData() + 84;
    for (quint32 i = 0; i < n; ++i, p += 50)
    {
        StlTri t;
        const char* v = p + 12;                    // the normal is skipped: the orientation comes from the winding
        for (int k = 0; k < 3; ++k, v += 12)
        {
            t.v[k].x = readF32(v);
            t.v[k].y = readF32(v + 4);
            t.v[k].z = readF32(v + 8);
        }
        out.append(t);
    }
    return true;
}

bool parseAscii(const QByteArray& d, QVector<StlTri>& out)
{
    QVector<StlVec> vs;
    const QList<QByteArray> lines = d.split('\n');
    for (const QByteArray& raw : lines)
    {
        const QByteArray line = raw.trimmed();
        if (!line.startsWith("vertex ")) continue;
        const QList<QByteArray> parts = line.mid(7).simplified().split(' ');
        if (parts.size() < 3) continue;
        bool ok1 = false, ok2 = false, ok3 = false;
        const float x = parts[0].toFloat(&ok1);
        const float y = parts[1].toFloat(&ok2);
        const float z = parts[2].toFloat(&ok3);
        if (ok1 && ok2 && ok3) vs.append(StlVec{ x, y, z });
    }
    if (vs.size() < 3) return false;
    out.reserve(vs.size() / 3);
    for (int i = 0; i + 2 < vs.size(); i += 3)
        out.append(StlTri{ { vs[i], vs[i + 1], vs[i + 2] } });
    return true;
}

inline bool finite3(const StlVec& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

bool stlLoad(const QString& path, QVector<StlTri>& tris, QString* err)
{
    tris.clear();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        if (err) *err = QStringLiteral("cannot open %1").arg(path);
        return false;
    }
    const QByteArray d = f.readAll();
    if (d.size() < 15)
    {
        if (err) *err = QStringLiteral("the STL is empty: %1").arg(path);
        return false;
    }
    // Binary first: a binary STL may also start with "solid", so the size/count check decides.
    bool ok = parseBinary(d, tris);
    if (!ok) ok = parseAscii(d, tris);
    if (!ok || tris.isEmpty())
    {
        if (err) *err = QStringLiteral("not a binary or ASCII STL: %1").arg(path);
        return false;
    }
    return true;
}

bool stlVoxelize(const QVector<StlTri>& tris, double heightBlocks, double yawDeg,
                 QVector<StlCell>& cells, QString* err)
{
    cells.clear();
    if (tris.isEmpty()) { if (err) *err = QStringLiteral("no triangles"); return false; }
    if (!(heightBlocks > 0.5) || heightBlocks > 250.0)
    {
        if (err) *err = QStringLiteral("the model height %1 blocks is out of range").arg(heightBlocks);
        return false;
    }
    // The model box: z is the up axis (the landmark STL files are z-up), the footprint is centred on (0,0).
    float mnx = std::numeric_limits<float>::max(), mny = mnx, mnz = mnx;
    float mxx = -mnx, mxy = -mnx, mxz = -mnx;
    for (const StlTri& t : tris)
        for (const StlVec& v : t.v)
        {
            if (!finite3(v)) continue;
            mnx = std::min(mnx, v.x); mxx = std::max(mxx, v.x);
            mny = std::min(mny, v.y); mxy = std::max(mxy, v.y);
            mnz = std::min(mnz, v.z); mxz = std::max(mxz, v.z);
        }
    if (!(mxz > mnz) || !(mxx > mnx) || !(mxy > mny))
    {
        if (err) *err = QStringLiteral("the model is degenerate (flat or empty)");
        return false;
    }
    const double s = heightBlocks / double(mxz - mnz);
    const double cx = 0.5 * (double(mnx) + double(mxx));
    const double cy = 0.5 * (double(mny) + double(mxy));
    const double yaw = yawDeg * 3.14159265358979 / 180.0;
    const double co = std::cos(yaw), si = std::sin(yaw);

    struct WorldTri { float v[3][3]; float bx0, bx1, bz0, bz1; };
    QVector<WorldTri> wt;
    wt.reserve(tris.size());
    double wx0 = 1e300, wx1 = -1e300, wz0 = 1e300, wz1 = -1e300;
    for (const StlTri& t : tris)
    {
        WorldTri w;
        bool ok = true;
        for (int k = 0; k < 3; ++k)
        {
            if (!finite3(t.v[k])) { ok = false; break; }
            const double x = (double(t.v[k].x) - cx) * s;
            const double y = (double(t.v[k].y) - cy) * s;
            const double z = (double(t.v[k].z) - double(mnz)) * s;
            w.v[k][0] = float(x * co - y * si);     // the model's x -> world x, y -> world z, z -> world y (up)
            w.v[k][1] = float(z);
            w.v[k][2] = float(x * si + y * co);
        }
        if (!ok) continue;
        w.bx0 = std::min({ w.v[0][0], w.v[1][0], w.v[2][0] });
        w.bx1 = std::max({ w.v[0][0], w.v[1][0], w.v[2][0] });
        w.bz0 = std::min({ w.v[0][2], w.v[1][2], w.v[2][2] });
        w.bz1 = std::max({ w.v[0][2], w.v[1][2], w.v[2][2] });
        wx0 = std::min(wx0, double(w.bx0)); wx1 = std::max(wx1, double(w.bx1));
        wz0 = std::min(wz0, double(w.bz0)); wz1 = std::max(wz1, double(w.bz1));
        wt.append(w);
    }
    if (wt.isEmpty())
    {
        if (err) *err = QStringLiteral("no finite triangles");
        return false;
    }

    QSet<quint64> voxels;
    auto key = [](int x, int y, int z) -> quint64
    {
        return (quint64(quint16(x)) << 32) | (quint64(quint16(y)) << 16) | quint64(quint16(z));
    };

    // The shell: sample every triangle's surface (the barycentric grid is finer than a block, so thin parts survive).
    for (const WorldTri& w : wt)
    {
        const auto len = [&](int a, int b) -> double
        {
            const double dx = double(w.v[a][0]) - double(w.v[b][0]);
            const double dy = double(w.v[a][1]) - double(w.v[b][1]);
            const double dz = double(w.v[a][2]) - double(w.v[b][2]);
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        };
        const double longest = std::max({ len(0, 1), len(1, 2), len(2, 0) });
        const int n = std::max(1, int(std::ceil(longest / 0.5)));
        for (int i = 0; i <= n; ++i)
            for (int j = 0; i + j <= n; ++j)
            {
                const double a = double(i) / double(n), b = double(j) / double(n), c = 1.0 - a - b;
                const double px = a * w.v[0][0] + b * w.v[1][0] + c * w.v[2][0];
                const double py = a * w.v[0][1] + b * w.v[1][1] + c * w.v[2][1];
                const double pz = a * w.v[0][2] + b * w.v[1][2] + c * w.v[2][2];
                voxels.insert(key(int(std::floor(px)), int(std::floor(py)), int(std::floor(pz))));
            }
    }

    // The interior: a vertical ray through every column of the footprint; the mesh crossings are sorted and the
    // space between each entering/leaving pair is filled (an odd count means a leaking mesh — that column stays a shell).
    const int ix0 = int(std::floor(wx0)), ix1 = int(std::ceil(wx1));
    const int iz0 = int(std::floor(wz0)), iz1 = int(std::ceil(wz1));
    for (int ix = ix0; ix <= ix1; ++ix)
        for (int iz = iz0; iz <= iz1; ++iz)
        {
            const double px = double(ix) + 0.5, pz = double(iz) + 0.5;
            QVector<double> ys;
            for (const WorldTri& w : wt)
            {
                if (px < w.bx0 || px > w.bx1 || pz < w.bz0 || pz > w.bz1) continue;
                const double ax = w.v[0][0], az = w.v[0][2];
                const double bx = w.v[1][0], bz = w.v[1][2];
                const double cx2 = w.v[2][0], cz2 = w.v[2][2];
                const double den = (bz - cz2) * (ax - cx2) + (cx2 - bx) * (az - cz2);
                if (std::abs(den) < 1e-9) continue;
                const double a = ((bz - cz2) * (px - cx2) + (cx2 - bx) * (pz - cz2)) / den;
                const double b = ((cz2 - az) * (px - cx2) + (ax - cx2) * (pz - cz2)) / den;
                const double c = 1.0 - a - b;
                if (a < -1e-6 || b < -1e-6 || c < -1e-6) continue;
                ys.append(a * w.v[0][1] + b * w.v[1][1] + c * w.v[2][1]);
            }
            if (ys.size() < 2) continue;
            std::sort(ys.begin(), ys.end());
            for (int k = 0; k + 1 < int(ys.size()); k += 2)
            {
                const int ya = int(std::ceil(ys[k])), yb = int(std::floor(ys[k + 1]));
                for (int y = ya; y <= yb; ++y) voxels.insert(key(ix, y, iz));
            }
        }

    cells.reserve(voxels.size());
    for (quint64 k : voxels)
    {
        const int x = int(qint16(quint16(k >> 32)));
        const int y = int(qint16(quint16(k >> 16)));
        const int z = int(qint16(quint16(k)));
        if (x < -2000 || x > 2000 || y < 0 || y > 255 || z < -2000 || z > 2000) continue;
        cells.append(StlCell{ qint16(x), qint16(y), qint16(z) });
    }
    if (cells.isEmpty())
    {
        if (err) *err = QStringLiteral("the model produced no voxels");
        return false;
    }
    return true;
}
