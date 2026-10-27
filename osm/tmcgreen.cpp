// "Topping up" vegetation for OSM data. OSM usually has noticeably less courtyard vegetation and fewer forests than our data, while a world for the public
// should look similarly full. So free places are searched for over the already loaded objects and ordinary vegetation outlines are made from them —
// the exporter then treats them normally (trees, relief, islands over parks, the Pale Garden, portals, spruces/taiga):
//   a yard between houses  -> vgbpol, internal code 71126190 (trees denser than ordinary courtyard vegetation, relief of empty lands);
//   private sector        -> vegpol, internal code 71111111 (forced taiga: spruces at any area);
//   forests, if there are few -> vegpol 71111110 on large free places far from buildings;
//   parks, if there are few  -> large lawns become parks, then parks are cut out of free places near buildings (vegpol 71126000).
// Anything occupied (buildings, roads, rails, water, plates, parking lots, existing vegetation) is not touched: everything is computed on a raster grid with margins.
#include "input/tmcproject.h"
#include "osm/tmcosmrules.h"
#include "scene/mcscene.h"
#include "ogrsf_frmts.h"
#include "gdal_priv.h"
#include "gdal_alg.h"
#include <QMap>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace
{

QString suffixOf(const QString& layerName) { return layerName.section(QLatin1Char('.'), -1).toLower(); }

using Mask = QVector<quint8>;
using Dist = QVector<quint16>;

//! Distance (in 1/3 of a cell) to the nearest cell of the mask (mask != 0), "chamfer 3-4". The mask cells are 0.
Dist chamfer(const Mask& m, int W, int H)
{
    const quint16 INF = 60000;
    Dist d(m.size(), INF);
    for (int i = 0; i < m.size(); ++i) if (m[i]) d[i] = 0;
    auto at = [&](int x, int y) -> quint16 { return (x < 0 || y < 0 || x >= W || y >= H) ? INF : d[y * W + x]; };
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            quint16& v = d[y * W + x];
            if (!v) continue;
            const int c = std::min({ at(x - 1, y) + 3, at(x, y - 1) + 3, at(x - 1, y - 1) + 4, at(x + 1, y - 1) + 4 });
            if (c < v) v = quint16(std::min(c, int(INF)));
        }
    for (int y = H - 1; y >= 0; --y)
        for (int x = W - 1; x >= 0; --x)
        {
            quint16& v = d[y * W + x];
            if (!v) continue;
            const int c = std::min({ at(x + 1, y) + 3, at(x, y + 1) + 3, at(x + 1, y + 1) + 4, at(x - 1, y + 1) + 4 });
            if (c < v) v = quint16(std::min(c, int(INF)));
        }
    return d;
}

//! Opening of the mask by 1 cell (removes narrow bridges and "whiskers" thinner than 2 cells), the result does not go beyond the original mask
Mask opening(const Mask& m, int W, int H)
{
    Mask er(m.size(), 0);
    for (int y = 1; y + 1 < H; ++y)
        for (int x = 1; x + 1 < W; ++x)
        {
            const int i = y * W + x;
            if (m[i] && m[i - 1] && m[i + 1] && m[i - W] && m[i + W]) er[i] = 1;
        }
    Mask out(m.size(), 0);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const int i = y * W + x;
            if (!m[i]) continue;
            if (er[i] || (x > 0 && er[i - 1]) || (x + 1 < W && er[i + 1]) || (y > 0 && er[i - W]) || (y + 1 < H && er[i + W])) out[i] = 1;
        }
    return out;
}

struct Component { QVector<int> cells; };

//! Connected components (4-neighborhood) of the mask; sorted by descending area, then by the first cell (deterministic)
std::vector<Component> components(const Mask& m, int W, int H, int minCells)
{
    std::vector<Component> res;
    QVector<char> seen(m.size(), 0);
    std::vector<int> stack;
    for (int s = 0; s < m.size(); ++s)
    {
        if (!m[s] || seen[s]) continue;
        Component c;
        stack.assign(1, s);
        seen[s] = 1;
        while (!stack.empty())
        {
            const int i = stack.back(); stack.pop_back();
            c.cells << i;
            const int x = i % W, y = i / W;
            const int nb[4] = { x > 0 ? i - 1 : -1, x + 1 < W ? i + 1 : -1, y > 0 ? i - W : -1, y + 1 < H ? i + W : -1 };
            for (int n : nb)
                if (n >= 0 && m[n] && !seen[n]) { seen[n] = 1; stack.push_back(n); }
        }
        if (c.cells.size() >= minCells) res.push_back(std::move(c));
    }
    std::stable_sort(res.begin(), res.end(), [](const Component& a, const Component& b)
    { return a.cells.size() != b.cells.size() ? a.cells.size() > b.cells.size() : a.cells.first() < b.cells.first(); });
    return res;
}

double polygonArea(const OGRGeometry* g)
{
    if (!g) return 0;
    const OGRwkbGeometryType t = wkbFlatten(g->getGeometryType());
    if (t == wkbPolygon) return static_cast<const OGRPolygon*>(g)->get_Area();
    if (t == wkbMultiPolygon) return static_cast<const OGRMultiPolygon*>(g)->get_Area();
    return 0;
}

}   // namespace

void TmcProject::synthesizeGreenery(const GreeneryOptions& opt)
{
    if (!opt.enabled) return;
    auto suffixLayers = [&](const char* sfx)
    {
        QVector<MapLayer*> r;
        for (MapLayer* l : qAsConst(mLayers)) if (suffixOf(l->name) == QLatin1String(sfx)) r << l;
        return r;
    };
    // data bounds
    double bx0 = 1e300, by0 = 1e300, bx1 = -1e300, by1 = -1e300;
    for (MapLayer* l : qAsConst(mLayers))
        for (const MapFeature* f : qAsConst(l->features))
            if (f->g) { bx0 = std::min(bx0, f->xmin); by0 = std::min(by0, f->ymin); bx1 = std::max(bx1, f->xmax); by1 = std::max(by1, f->ymax); }
    if (bx1 <= bx0 || by1 <= by0) return;
    double ux = 1.0, uy = 1.0;                                      // meters per projection unit
    if (!metersPerMapUnit(mWkt, 0.5 * (bx0 + bx1), 0.5 * (by0 + by1), ux, uy) || ux <= 0 || uy <= 0) { ux = uy = 1.0; }
    const double m2 = ux * uy;                                      // m² per square projection unit
    const double areaKm2 = (bx1 - bx0) * (by1 - by0) * m2 / 1e6;

    // grid: step opt.cellM m, but not more than 8 million cells
    double cellM = opt.cellM;
    while (((bx1 - bx0) * ux / cellM + 2) * ((by1 - by0) * uy / cellM + 2) > 8e6) cellM *= 1.25;
    const double cux = cellM / ux, cuy = cellM / uy;                // grid step in projection units
    const int W = int(std::ceil((bx1 - bx0) / cux)) + 2, H = int(std::ceil((by1 - by0) / cuy)) + 2;
    const double gx0 = bx0 - cux, gy1 = by1 + cuy;                  // top-left corner of the grid

    // --- rasterizing the occupied area: 9 bands ---
    enum Band { BAny = 1, BPriv, BBlock, RWide, RStreet, RPath, Rail, Hard, Veg, NB = Veg };
    GDALAllRegister();
    GDALDriver* memDrv = GetGDALDriverManager()->GetDriverByName("MEM");
    if (!memDrv) return;
    std::unique_ptr<GDALDataset> ras(memDrv->Create("", W, H, NB, GDT_Byte, nullptr));
    if (!ras) return;
    double gt[6] = { gx0, cux, 0, gy1, 0, -cuy };
    ras->SetGeoTransform(gt);
    auto burn = [&](int band, const std::vector<OGRGeometry*>& geoms)
    {
        if (geoms.empty()) return;
        std::vector<double> val(geoms.size(), 255.0);
        int bands[1] = { band };
        char* opts[] = { const_cast<char*>("ALL_TOUCHED=TRUE"), nullptr };
        GDALRasterizeGeometries(ras.get(), 1, bands, int(geoms.size()), reinterpret_cast<OGRGeometryH*>(const_cast<OGRGeometry**>(geoms.data())),
                                nullptr, nullptr, val.data(), opts, nullptr, nullptr);
    };
    static const QSet<int> privCodes = { 44400002, 44400100, 44400110 };
    static const QSet<int> wideRoad = { 45210000, 45220000, 61210000, 61220000, 61230000, 61231000, 61232000 };
    static const QSet<int> streetRoad = { 45230000, 45240000, 45270000 };
    std::vector<OGRGeometry*> gAny, gPriv, gBlock, gWide, gStreet, gPath, gRail, gHard, gVeg;
    auto codeOf = [](const MapLayer* l, const MapFeature* f) { const int ci = l->fieldIndex(QStringLiteral("CODE")); return ci >= 0 ? f->fields.value(ci).toInt() : 0; };
    for (MapLayer* l : suffixLayers("bldpol"))
        for (const MapFeature* f : qAsConst(l->features))
        {
            if (!f->g) continue;
            const int c = codeOf(l, f);
            gAny.push_back(f->g);
            if (privCodes.contains(c)) gPriv.push_back(f->g);
            else if (c != 44000000) gBlock.push_back(f->g);          // sheds define neither the private sector nor a "block"
        }
    for (MapLayer* l : suffixLayers("rdslin"))
        for (const MapFeature* f : qAsConst(l->features))
        {
            if (!f->g) continue;
            const int c = codeOf(l, f);
            if (c == 45262000) continue;                              // crosswalk — a marker line
            (wideRoad.contains(c) ? gWide : (streetRoad.contains(c) ? gStreet : gPath)).push_back(f->g);
        }
    for (MapLayer* l : suffixLayers("rrslin")) for (const MapFeature* f : qAsConst(l->features)) if (f->g) gRail.push_back(f->g);
    for (MapLayer* l : suffixLayers("hdrlin")) for (const MapFeature* f : qAsConst(l->features)) if (f->g) gPath.push_back(f->g);
    for (const char* s : { "rdcpol", "csrpol", "blkpol", "hdrpol" })
        for (MapLayer* l : suffixLayers(s)) for (const MapFeature* f : qAsConst(l->features)) if (f->g) gHard.push_back(f->g);
    for (const char* s : { "vegpol", "vgbpol" })
        for (MapLayer* l : suffixLayers(s)) for (const MapFeature* f : qAsConst(l->features)) if (f->g) gVeg.push_back(f->g);
    burn(BAny, gAny); burn(BPriv, gPriv); burn(BBlock, gBlock); burn(RWide, gWide); burn(RStreet, gStreet);
    burn(RPath, gPath); burn(Rail, gRail); burn(Hard, gHard); burn(Veg, gVeg);

    auto readBand = [&](int band)
    {
        Mask m(qsizetype(W) * H, 0);
        ras->GetRasterBand(band)->RasterIO(GF_Read, 0, 0, W, H, m.data(), W, H, GDT_Byte, 0, 0);
        return m;
    };
    // distances in meters (by grid cells): chamfer gives 1/3 of a cell
    const double dm = cellM / 3.0;
    auto distM = [&](int band) { return chamfer(readBand(band), W, H); };
    const Dist dAny = distM(BAny), dPriv = distM(BPriv), dBlock = distM(BBlock);
    const Dist dWide = distM(RWide), dStreet = distM(RStreet), dPath = distM(RPath), dRail = distM(Rail), dHard = distM(Hard);
    const Mask vegM = readBand(Veg);
    const qsizetype N = qsizetype(W) * H;

    Mask freeM(N, 0), yardM(N, 0), taigaM(N, 0), forestM(N, 0), parkM(N, 0);
    for (qsizetype i = 0; i < N; ++i)
    {
        if (vegM[i]) continue;
        if (dAny[i] * dm < 3.0 || dWide[i] * dm < 9.5 || dStreet[i] * dm < 6.5 || dPath[i] * dm < 3.0 || dRail[i] * dm < 7.0 || dHard[i] * dm < 1.5) continue;
        freeM[i] = 1;
        const bool priv = dPriv[i] * dm <= 22.0 && dBlock[i] * dm > 18.0;                          // private sector: private houses nearby, no block buildings
        const bool block = !priv && dBlock[i] * dm <= 45.0;                                         // block: houses of a non-private kind nearby
        if (priv) { if (dAny[i] * dm >= 4.5) taigaM[i] = 1; }
        else if (block) { yardM[i] = 1; if (dBlock[i] * dm <= 90.0 && dAny[i] * dm >= 5.0) parkM[i] = 1; }
        else
        {
            if (dAny[i] * dm >= 14.0 && dWide[i] * dm >= 12.0) forestM[i] = 1;                     // an open place far from buildings
            if (dBlock[i] * dm <= 90.0 && dAny[i] * dm >= 5.0) parkM[i] = 1;
        }
        if (block && dBlock[i] * dm <= 90.0 && dAny[i] * dm >= 5.0) parkM[i] = 1;
    }

    // --- what already exists: forests and parks (in m²) ---
    double forestArea = 0;
    int parkCount = 0;
    for (MapLayer* l : suffixLayers("vegpol"))
        for (const MapFeature* f : qAsConst(l->features))
        {
            const int c = codeOf(l, f);
            const double a = polygonArea(f->g) * m2;
            if (c / 1000 == 71111 || c == 71113000) forestArea += a;                                // forests, undergrowth
            if (c == 71126000 && a >= 6000.0) ++parkCount;
        }
    const double totalM2 = areaKm2 * 1e6;

    QVector<char> label(N, 0);                    // 1 yard, 2 taiga, 3 forest, 4 park
    auto take = [&](const std::vector<Component>& comps, char lab, int count, double maxArea)
    {
        double got = 0;
        int n = 0;
        for (const Component& c : comps)
        {
            if (count >= 0 && n >= count) break;
            const double a = c.cells.size() * cellM * cellM;
            if (maxArea > 0 && got + a > maxArea * 1.3 && n > 0) continue;
            for (int i : c.cells) label[i] = lab;
            got += a; ++n;
            if (maxArea > 0 && got >= maxArea) break;
        }
        return qMakePair(n, got);
    };
    auto minCells = [&](double areaM) { return std::max(4, int(std::ceil(areaM / (cellM * cellM)))); };
    auto clip = [&](Mask m) { for (qsizetype i = 0; i < N; ++i) if (label[i]) m[i] = 0; return m; };

    int nPark = 0, nPromoted = 0, nYard = 0, nTaiga = 0, nForest = 0;
    double aYard = 0, aTaiga = 0, aForest = 0;

    // 1) parks: first large lawns, then cut out of free places near buildings
    // the "built-up" area — cells not farther than 100 m from a building (the data frame includes long roads and empty edges, which would give too many parks)
    qint64 devCells = 0;
    for (qsizetype i = 0; i < N; ++i) if (dAny[i] * dm <= 100.0) ++devCells;
    const double devKm2 = double(devCells) * cellM * cellM / 1e6;
    const int parksNeeded = std::max(2, int(std::ceil(devKm2 * opt.parksPerKm2)));
    int parksHave = parkCount;
    if (parksHave < parksNeeded)
    {
        QVector<QPair<double, MapFeature*>> lawns;
        QVector<MapLayer*> vegLayers = suffixLayers("vegpol");
        for (MapLayer* l : qAsConst(vegLayers))
            for (MapFeature* f : qAsConst(l->features))
                if (codeOf(l, f) == 71329000)
                {
                    const double a = polygonArea(f->g) * m2;
                    if (a >= 5000.0) lawns << qMakePair(a, f);
                }
        std::stable_sort(lawns.begin(), lawns.end(), [](const QPair<double, MapFeature*>& a, const QPair<double, MapFeature*>& b) { return a.first > b.first; });
        for (const auto& lw : qAsConst(lawns))
        {
            if (parksHave >= parksNeeded) break;
            MapLayer* owner = nullptr;
            for (MapLayer* l : qAsConst(vegLayers)) if (l->features.contains(lw.second)) { owner = l; break; }
            if (!owner) continue;
            const int ci = owner->fieldIndex(QStringLiteral("CODE")), ri = owner->fieldIndex(QStringLiteral("REL"));
            if (ci < 0) continue;
            lw.second->fields[ci] = QStringLiteral("71126000");
            if (ri >= 0) lw.second->fields[ri] = QStringLiteral("17");
            ++parksHave; ++nPromoted;
        }
    }
    if (parksHave < parksNeeded)
    {
        const auto comps = components(opening(clip(parkM), W, H), W, H, minCells(8000.0));
        const auto r = take(comps, 4, parksNeeded - parksHave, 0);
        nPark = r.first;
    }
    // 2) yards between houses
    if (opt.yards)
    {
        const auto comps = components(opening(clip(yardM), W, H), W, H, minCells(40.0));
        const auto r = take(comps, 1, -1, 0);
        nYard = r.first; aYard = r.second;
    }
    // 3) private-sector taiga
    if (opt.privateTaiga)
    {
        const auto comps = components(opening(clip(taigaM), W, H), W, H, minCells(60.0));
        const auto r = take(comps, 2, -1, 0);
        nTaiga = r.first; aTaiga = r.second;
    }
    // 4) forests: if there are fewer than the needed share of the area
    const double forestNeed = opt.forestShare * totalM2 - forestArea;
    if (forestNeed > 0)
    {
        const auto comps = components(opening(clip(forestM), W, H), W, H, minCells(3000.0));
        const auto r = take(comps, 3, -1, forestNeed);
        nForest = r.first; aForest = r.second;
    }

    // 5) wild forests for the camps and bases of bandits: only very large empty areas with margins from buildings and block development
    //    (so that a camp does not end up between high-rises); taiga and mixed forest alternate — the exporter has different sets of camps for them
    int nWild = 0;
    double aWild = 0;
    if (opt.wildForests && opt.wildMaxCount > 0)
    {
        Mask wildM(N, 0);
        for (qsizetype i = 0; i < N; ++i)
            if (freeM[i] && !label[i] && dAny[i] * dm >= opt.wildBuildingM && dBlock[i] * dm >= opt.wildBlockM && dWide[i] * dm >= 30.0 && dHard[i] * dm >= 25.0) wildM[i] = 1;
        // "very large" areas: connected empty regions not smaller than twice the minimum; in each of them the forest takes not the whole area but a round patch
        // around the point farthest from buildings (fields and wasteland around the city do not turn into a solid forest)
        Mask big(N, 0);
        for (const Component& c : components(opening(wildM, W, H), W, H, minCells(opt.wildMinAreaM2 * 2.0)))
            for (int i : c.cells) big[i] = 1;
        const double radiusCells = std::sqrt(opt.wildPatchM2 / (3.14159265358979 * cellM * cellM));
        const double apartCells = opt.wildApartM / cellM;                  // patches not closer to each other than wildApartM
        std::vector<QPoint> seeds;
        QVector<char> tried(N, 0);
        for (int guard = 0; nWild < opt.wildMaxCount && guard < opt.wildMaxCount * 4; ++guard)
        {
            qsizetype best = -1;
            double bestScore = -1;
            for (qsizetype i = 0; i < N; ++i)
            {
                if (!big[i] || label[i] || tried[i]) continue;
                const double sc = std::min({ dAny[i] * dm, dBlock[i] * dm * 0.5, dWide[i] * dm });
                if (sc <= bestScore) continue;
                const int x = int(i % W), y = int(i / W);
                bool near = false;
                for (const QPoint& s : seeds) if (std::hypot(double(s.x() - x), double(s.y() - y)) < apartCells) { near = true; break; }
                if (!near) { best = i; bestScore = sc; }
            }
            if (best < 0) break;
            const int sx = int(best % W), sy = int(best / W);
            seeds.push_back(QPoint(sx, sy));
            // patch: the reachable cells of the area within radiusCells of the point
            QVector<int> patch;
            std::vector<int> stack{ int(best) };
            QVector<char> seen(N, 0);
            seen[best] = 1;
            while (!stack.empty())
            {
                const int i = stack.back(); stack.pop_back();
                patch << i;
                const int x = i % W, y = i / W;
                const int nb[4] = { x > 0 ? i - 1 : -1, x + 1 < W ? i + 1 : -1, y > 0 ? i - W : -1, y + 1 < H ? i + W : -1 };
                for (int n : nb)
                {
                    if (n < 0 || seen[n] || !big[n] || label[n]) continue;
                    if (std::hypot(double(n % W - sx), double(n / W - sy)) > radiusCells) continue;
                    seen[n] = 1; stack.push_back(n);
                }
            }
            if (patch.size() < minCells(opt.wildMinAreaM2)) { tried[best] = 1; continue; }
            const char lab = (nWild % 2 == 0) ? 5 : 6;                // 5 — taiga (71111111), 6 — mixed forest (71111110)
            for (int i : patch) label[i] = lab;
            aWild += patch.size() * cellM * cellM;
            ++nWild;
        }
    }

    // --- into outlines: polygonizing the labels ---
    qsizetype nLab = 0;
    for (qsizetype i = 0; i < N; ++i) if (label[i]) ++nLab;
    if (nLab > 0)
    {
        std::unique_ptr<GDALDataset> lr(memDrv->Create("", W, H, 2, GDT_Byte, nullptr));
        lr->SetGeoTransform(gt);
        QVector<quint8> lab(N), msk(N);
        for (qsizetype i = 0; i < N; ++i) { lab[i] = quint8(label[i]); msk[i] = label[i] ? 255 : 0; }
        lr->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, W, H, lab.data(), W, H, GDT_Byte, 0, 0);
        lr->GetRasterBand(2)->RasterIO(GF_Write, 0, 0, W, H, msk.data(), W, H, GDT_Byte, 0, 0);
        GDALDriver* vdrv = GetGDALDriverManager()->GetDriverByName("Memory");
        if (vdrv)
        {
            std::unique_ptr<GDALDataset> vds(vdrv->Create("", 0, 0, 0, GDT_Unknown, nullptr));
            OGRLayer* poly = vds ? vds->CreateLayer("g", nullptr, wkbPolygon, nullptr) : nullptr;
            if (poly)
            {
                OGRFieldDefn fd("v", OFTInteger);
                poly->CreateField(&fd);
                GDALPolygonize(lr->GetRasterBand(1), lr->GetRasterBand(2), poly, 0, nullptr, nullptr, nullptr);
                auto layerFor = [&](const QString& name) -> MapLayer*
                {
                    for (MapLayer* l : qAsConst(mLayers)) if (l->name == name) return l;
                    const QString sfx = name.section(QLatin1Char('.'), -1);
                    if (!mWanted.isEmpty() && !mWanted.contains(sfx)) return nullptr;
                    MapLayer* l = new MapLayer;
                    l->name = name;
                    l->fieldNames << QStringLiteral("CODE") << QStringLiteral("REL");
                    l->geom = GeomKind::Polygon;
                    mLayers << l;
                    return l;
                };
                MapLayer* vegL = layerFor(QStringLiteral("osm.vegpol"));
                MapLayer* vgbL = layerFor(QStringLiteral("osm.vgbpol"));
                struct Spec { MapLayer* layer; const char* code; const char* rel; };
                const Spec specs[7] = { { nullptr, "", "" }, { vgbL, "71126190", "15" }, { vegL, "71111111", "22" }, { vegL, "71111110", "22" }, { vegL, "71126000", "17" },
                                        { vegL, "71111111", "22" }, { vegL, "71111110", "22" } };
                poly->ResetReading();
                OGRFeature* of;
                while ((of = poly->GetNextFeature()) != nullptr)
                {
                    const int v = of->GetFieldAsInteger(0);
                    OGRGeometry* g = of->GetGeometryRef() ? of->GetGeometryRef()->clone() : nullptr;
                    OGRFeature::DestroyFeature(of);
                    if (!g || v < 1 || v > 6 || !specs[v].layer) { delete g; continue; }
                    if (OGRGeometry* s = g->SimplifyPreserveTopology(std::min(cux, cuy) * 0.6)) { if (!s->IsEmpty()) { delete g; g = s; } else delete s; }
                    const int ci = specs[v].layer->fieldIndex(QStringLiteral("CODE")), ri = specs[v].layer->fieldIndex(QStringLiteral("REL"));
                    if (ci < 0) { delete g; continue; }
                    MapFeature* f = new MapFeature;
                    f->g = g;
                    f->fields.resize(specs[v].layer->fieldNames.size());
                    f->fields[ci] = QString::fromLatin1(specs[v].code);
                    if (ri >= 0) f->fields[ri] = QString::fromLatin1(specs[v].rel);
                    f->updateBounds();
                    specs[v].layer->features << f;
                }
            }
        }
    }
    log(QObject::tr("Vegetation topped up for OSM (grid %1 m): parks %2 (lawns turned into parks %3; there were %4 parks, %5 needed for %14 km² of built-up area), yards %6 (%7 m²), "
                    "taiga in the private sector %8 (%9 m²), forests %10 (%11 m²; there were %12 m² of forests out of the needed %13 m²), wild forests for camps %15 (%16 m²)")
            .arg(cellM, 0, 'f', 1).arg(nPark).arg(nPromoted).arg(parkCount).arg(parksNeeded).arg(nYard).arg(qRound(aYard)).arg(nTaiga).arg(qRound(aTaiga))
            .arg(nForest).arg(qRound(aForest)).arg(qRound(forestArea)).arg(qRound(opt.forestShare * totalM2)).arg(devKm2, 0, 'f', 1).arg(nWild).arg(qRound(aWild)));
}
