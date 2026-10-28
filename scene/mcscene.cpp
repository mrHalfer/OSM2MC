#include "scene/mcscene.h"
#include <QCoreApplication>
#include "core/steplog.h"
#include "ogrsf_frmts.h"
#include "ogr_spatialref.h"
#include <QtConcurrent/QtConcurrentMap>
#include <QtConcurrent/QtConcurrentRun>
#include <QThread>
#include <QFuture>
#include <QSharedPointer>
#include <QThreadPool>
#include <QHash>
#include <cmath>
#include <algorithm>
#include <memory>

using Kind = McWorldExporter::McObject::Kind;

namespace
{

struct ExportItem
{
    MapFeature* obj = nullptr;
    ModelSettings::VectorSource vs;
    int colorIndex = 0;
    double relMin = 0, relMax = 0;
    bool polygon = false;
    Kind kind = Kind::Generic;
    QString material;
    bool linear = false;
    double roadScale = 1.0;
    int code = 0;
    bool lift = false;
    bool onion = false;
    double roofH = 0.0;
    bool skillion = false;
    double roofDir = 0.0;
    bool part = false;
    QString streetType, street, number;
    bool intraBlock = false;
};

MapFeature* cloneObject(const MapFeature* src)
{
    MapFeature* c = new MapFeature;
    c->g = src->g ? src->g->clone() : nullptr;
    c->fields = src->fields;
    c->xmin = src->xmin; c->xmax = src->xmax; c->ymin = src->ymin; c->ymax = src->ymax;
    return c;
}

}   // namespace

bool metersPerMapUnit(const QString& wkt, double x, double y, double& mx, double& my)
{
    mx = my = 1.0;
    if (wkt.isEmpty()) return false;
    OGRSpatialReference src;
    const QByteArray b = wkt.toUtf8();
    const char* pw = b.constData();
    if (src.importFromWkt(&pw) != OGRERR_NONE) return false;
    src.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    if (src.IsGeographic())
    {
        const double lat = y * 3.14159265358979323846 / 180.0;
        mx = 111320.0 * std::cos(lat);
        my = 110574.0;
        return std::isfinite(mx) && mx > 1.0;
    }
    OGRSpatialReference wgs;
    wgs.importFromEPSG(4326);
    wgs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    std::unique_ptr<OGRCoordinateTransformation, void (*)(OGRCoordinateTransformation*)>
        ct(OGRCreateCoordinateTransformation(&src, &wgs),
           [](OGRCoordinateTransformation* t) { if (t) OCTDestroyCoordinateTransformation(t); });
    if (!ct) return false;
    const double d = 100.0;
    double lon[3] = { x, x + d, x }, lat[3] = { y, y, y + d };
    if (!ct->Transform(3, lon, lat)) return false;
    auto hav = [](double lo1, double la1, double lo2, double la2)
    {
        const double R = 6371008.8, rad = 3.14159265358979323846 / 180.0;
        const double dla = (la2 - la1) * rad, dlo = (lo2 - lo1) * rad;
        const double a = std::sin(dla / 2) * std::sin(dla / 2)
                       + std::cos(la1 * rad) * std::cos(la2 * rad) * std::sin(dlo / 2) * std::sin(dlo / 2);
        return 2 * R * std::asin(std::min(1.0, std::sqrt(a)));
    };
    mx = hav(lon[0], lat[0], lon[1], lat[1]) / d;
    my = hav(lon[0], lat[0], lon[2], lat[2]) / d;
    return std::isfinite(mx) && std::isfinite(my) && mx > 1e-6 && my > 1e-6;
}

QString formatMcExportInfo(const QString& info)
{
    if (info.trimmed().isEmpty()) return QObject::tr("Special places: —");
    struct Cat { const char* title; QStringList keys; QStringList items; };
    QVector<Cat> cats = {
        { QT_TRANSLATE_NOOP("McScene", "Pale Garden"), { QStringLiteral("скрипун"), QStringLiteral("особняк"), QStringLiteral("бледного сада"), QStringLiteral("pale_garden"), QStringLiteral("pale garden"), QStringLiteral("mansion"), QStringLiteral("creaking") }, {} },
        { QT_TRANSLATE_NOOP("McScene", "Dungeon entrances"), { QStringLiteral("stronghold"), QStringLiteral("ancient_city"), QStringLiteral("церкв"), QStringLiteral("церков"), QStringLiteral("лаборатор"), QStringLiteral("научн"), QStringLiteral("church"), QStringLiteral("laborator"), QStringLiteral("research") }, {} },
        { QT_TRANSLATE_NOOP("McScene", "Outposts and camps"), { QStringLiteral("аванпост"), QStringLiteral("лагер"), QStringLiteral("outpost"), QStringLiteral("camp") }, {} },
        { QT_TRANSLATE_NOOP("McScene", "Portals"), { QStringLiteral("портал"), QStringLiteral("portal") }, {} },
        { QT_TRANSLATE_NOOP("McScene", "Roads"), { QStringLiteral("дорог"), QStringLiteral("road"), QStringLiteral("zebra") }, {} },
        { QT_TRANSLATE_NOOP("McScene", "Forests, fields, terrain"), { QStringLiteral("лес"), QStringLiteral("поля"), QStringLiteral("рельеф"), QStringLiteral("зверей"), QStringLiteral("трава"), QStringLiteral("forest"), QStringLiteral("field"), QStringLiteral("terrain"), QStringLiteral("grass"), QStringLiteral("animals") }, {} },
        { QT_TRANSLATE_NOOP("McScene", "Other"), {}, {} } };
    for (const QString& raw : info.split(QStringLiteral("; "), Qt::SkipEmptyParts))
    {
        const QString it = raw.trimmed();
        if (it.isEmpty()) continue;
        const QString low = it.toLower();
        int target = cats.size() - 1;
        for (int i = 0; i + 1 < cats.size() && target == cats.size() - 1; ++i)
            for (const QString& k : cats[i].keys)
                if (low.contains(k)) { target = i; break; }
        cats[target].items << it;
    }
    QStringList out;
    for (const Cat& c : cats)
    {
        if (c.items.isEmpty()) continue;
        QString block = QCoreApplication::translate("McScene", c.title) + QStringLiteral(":");
        for (const QString& it : c.items) block += QStringLiteral("\n  • ") + it;
        out << block;
    }
    return out.join(QStringLiteral("\n\n"));
}

bool buildMcScene(const TmcProject& project, const T3dSettings& settings, const McSceneOptions& opt, McSceneResult& out,
                  const std::function<void(int, int)>& progress, const std::atomic_bool* cancel, QString* err)
{
    out = McSceneResult();
    tmcStepLog("scene: start");

    // --- binding the sources from the settings to the project layers (by name suffix, case-insensitive) ---
    struct Bound { T3dSource src; MapLayer* layer; int colorIndex; double relMin, relMax; };
    QVector<Bound> bound;
    int colorIndex = 0;
    for (const T3dSource& s : settings.sources)
    {
        MapLayer* layer = project.layerBySuffix(s.layer);
        if (!layer)
        {
            out.warnings << QObject::tr("The layer '%1' from the 3D settings was not found in the project — skipped").arg(s.layer);
            continue;
        }
        Bound b{ s, layer, colorIndex, 0.0, 0.0 };
        b.src.vs.layer = layer;
        ++colorIndex;                                 // as in the tqt 3D window, setModel(): over all sources with a found layer
        if (b.src.vs.colorMode == ModelColorMode::GradientByHeight)
        {   // the gradient scale range — the 5th/95th percentiles of the layer height values (as ModelBuilder::CollectLayerCandidates)
            const int fieldNo = b.src.vs.noExtrusion ? -1 : layer->fieldIndex(b.src.vs.heightField);
            QVector<double> rels;
            for (MapFeature* o : layer->features)
            {
                if (!o || !o->g) continue;
                double rel = 0.0;
                if (!b.src.vs.noExtrusion)
                {
                    bool ok = false;
                    const double raw = o->fields.value(fieldNo).toDouble(&ok);
                    rel = (ok ? raw : 0.0) * b.src.vs.heightMultiplier;
                }
                rel += b.src.vs.extraExtrusion;
                if (!b.src.vs.noExtrusion && rel == 0.0) rel = 0.5;
                rels << rel;
            }
            if (!rels.isEmpty())
            {
                std::sort(rels.begin(), rels.end());
                auto percentile = [&](double p) -> double
                {
                    const double idx = p * (rels.size() - 1);
                    const int lo = std::max(0, std::min(int(std::floor(idx)), int(rels.size()) - 1));
                    const int hi = std::max(0, std::min(int(std::ceil(idx)), int(rels.size()) - 1));
                    return rels[lo] + (rels[hi] - rels[lo]) * (idx - lo);
                };
                b.relMin = percentile(0.05); b.relMax = percentile(0.95);
                if (b.relMax <= b.relMin) { b.relMin = rels.first(); b.relMax = rels.last(); }
            }
        }
        if (b.src.vs.colorMode == ModelColorMode::ByClassifier)
            out.warnings << QObject::tr("Layer '%1': coloring by a classifier is not supported in tMC — the default color is used").arg(s.layer);
        bound << b;
    }
    if (bound.isEmpty())
    {
        if (err) *err = QObject::tr("None of the layers from the 3D settings was found among the project layers");
        return false;
    }

    // --- collecting objects (order: layers as in the settings, objects as in the layer) ---
    QVector<ExportItem> items;
    for (const Bound& b : bound)
    {
        MapLayer* layer = b.layer;
        const QString sfx = layer->name.section(QLatin1Char('.'), -1).toLower();
        Kind kind = Kind::Generic;
        if (sfx == QLatin1String("bldpol")) kind = Kind::Building;
        else if (sfx == QLatin1String("vegpol") || sfx == QLatin1String("vgbpol")) kind = Kind::Vegetation;
        else if (sfx == QLatin1String("rdslin")) kind = Kind::Road;
        else if (sfx == QLatin1String("rrslin")) kind = Kind::Railway;
        else if (sfx == QLatin1String("csrpol")) kind = Kind::Plate;
        else if (sfx == QLatin1String("rdcpol")) kind = Kind::ParkingLot;
        else if (sfx == QLatin1String("blkpol")) kind = Kind::BlockCover;
        else if (sfx == QLatin1String("hdrpol") || sfx == QLatin1String("hdrlin")) kind = Kind::Water;
        const bool polygonLayer = layer->geom == GeomKind::Polygon;

        for (MapFeature* obj : qAsConst(layer->features))
        {
            if (!obj || !obj->g) continue;
            ExportItem e;
            e.obj = obj;
            e.vs = b.src.vs;
            e.colorIndex = b.colorIndex;
            e.relMin = b.relMin; e.relMax = b.relMax;
            e.polygon = polygonLayer;
            e.kind = kind;
            e.linear = (sfx == QLatin1String("hdrlin"));
            e.intraBlock = (sfx == QLatin1String("vgbpol"));

            // building material: the column of the "Material from a column" mode, otherwise the standard MAT
            if (kind == Kind::Building && b.src.vs.absorptionMode == ModelAbsorptionMode::MaterialColumn && !b.src.vs.absorptionColumn.isEmpty())
            {
                const int fno = layer->fieldIndex(b.src.vs.absorptionColumn);
                if (fno >= 0) e.material = obj->fields.value(fno).trimmed();
            }
            if (kind == Kind::Building && e.material.isEmpty())
            {
                const int mno = layer->fieldIndex(QStringLiteral("MAT"));
                if (mno >= 0) e.material = obj->fields.value(mno).trimmed();
            }
            // CODE
            const int cno = layer->fieldIndex(QStringLiteral("CODE"));
            if (cno >= 0)
            {
                const QString cv = obj->fields.value(cno).trimmed();
                bool num = false;
                qlonglong c = cv.toLongLong(&num);
                if (!num) c = qRound64(cv.toDouble(&num));
                if (num) e.code = int(c);
            }
            // blkpol: only asphalt without markings (62231000) and squares (45300000)
            if (kind == Kind::BlockCover && e.code != 62231000 && e.code != 45300000) continue;
            // LIFT: the walls of elevator shafts are 1 block thick
            if (kind == Kind::Building)
            {
                const int lno = layer->fieldIndex(QStringLiteral("LIFT"));
                if (lno >= 0) e.lift = (qRound(obj->fields.value(lno).trimmed().toDouble()) == 1);
                const int rno = layer->fieldIndex(QStringLiteral("ROOF"));
                if (rno >= 0)
                {
                    const QString rv = obj->fields.value(rno).trimmed();
                    e.onion = (rv == QLatin1String("onion"));
                    e.skillion = (rv == QLatin1String("skillion"));
                }
                const int pno = layer->fieldIndex(QStringLiteral("PART"));
                if (pno >= 0) e.part = (obj->fields.value(pno).trimmed() == QLatin1String("part"));
                const int hno = layer->fieldIndex(QStringLiteral("ROOFH")), dno = layer->fieldIndex(QStringLiteral("ROOFD"));
                if ((e.onion || e.skillion) && hno >= 0) e.roofH = obj->fields.value(hno).toDouble();
                if (e.skillion && dno >= 0) e.roofDir = obj->fields.value(dno).toDouble();
            }
            // road width by the CODE class
            if (kind == Kind::Road)
            {
                switch (e.code)
                {
                case 45220000: case 45230000: case 45240000: case 61231000: case 61232000: e.roadScale = 2.0; break;
                case 45210000: case 61210000: case 61220000: case 61230000: e.roadScale = 2.5; break;
                default: break;
                }
            }
            // house address / street name
            if (kind == Kind::Building || kind == Kind::Road)
            {
                auto fieldText = [&](const char* name) -> QString
                {
                    const int no = layer->fieldIndex(QString::fromLatin1(name));
                    if (no < 0) return QString();
                    QString v = obj->fields.value(no).trimmed();
                    bool num = false;
                    const double d = v.toDouble(&num);
                    if (num && d == std::floor(d) && std::abs(d) < 1e9) v = QString::number(qlonglong(d));
                    return v == QLatin1String("0") ? QString() : v;
                };
                const int sno = layer->fieldIndex(QStringLiteral("STREET"));
                if (sno >= 0) e.street = obj->fields.value(sno).trimmed();
                const int tno = layer->fieldIndex(QStringLiteral("TYSTR"));
                if (tno >= 0) e.streetType = obj->fields.value(tno).trimmed();
                e.number = fieldText("NUMBR") + fieldText("ADDNUM");
            }
            items << e;
        }
    }
    tmcStepLog("scene: objects collected");
    if (items.isEmpty())
    {
        if (err) *err = QObject::tr("There are no objects to export");
        return false;
    }

    // --- elevator shafts: a building outline is first united with the outlines of its shafts ---
    QVector<QSharedPointer<MapFeature>> keepAlive;
    {
        QVector<int> liftIdx, hostIdx;
        for (int i = 0; i < items.size(); ++i)
        {
            const ExportItem& e = items[i];
            if (e.kind != Kind::Building || !e.obj || !e.obj->g) continue;
            if (e.lift) { liftIdx << i; continue; }
            const OGRwkbGeometryType gt = wkbFlatten(e.obj->g->getGeometryType());
            if (e.polygon && (gt == wkbPolygon || gt == wkbMultiPolygon)) hostIdx << i;
        }
        if (!liftIdx.isEmpty() && !hostIdx.isEmpty())
        {
            std::function<double(const OGRGeometry*)> measure = [&](const OGRGeometry* g) -> double
            {
                if (!g || g->IsEmpty()) return 0.0;
                switch (wkbFlatten(g->getGeometryType()))
                {
                case wkbPolygon: case wkbMultiPolygon:
                    return static_cast<const OGRSurface*>(g)->get_Area() * 1000.0;
                case wkbLineString: case wkbLinearRing:
                    return static_cast<const OGRCurve*>(g)->get_Length();
                case wkbMultiLineString: case wkbGeometryCollection: case wkbMultiPoint:
                {
                    double m = 0.0;
                    const OGRGeometryCollection* gc = static_cast<const OGRGeometryCollection*>(g);
                    for (int k = 0; k < gc->getNumGeometries(); ++k) m += measure(gc->getGeometryRef(k));
                    return m;
                }
                default: return 0.0;
                }
            };
            QHash<int, QVector<int>> liftsOfHost;
            for (int li : liftIdx)
            {
                OGREnvelope le; items[li].obj->g->getEnvelope(&le);
                int best = -1; double bestM = 0.0;
                for (int hi : hostIdx)
                {
                    if (items[hi].vs.layer != items[li].vs.layer) continue;
                    OGREnvelope he; items[hi].obj->g->getEnvelope(&he);
                    if (le.MaxX < he.MinX || le.MinX > he.MaxX || le.MaxY < he.MinY || le.MinY > he.MaxY) continue;
                    if (!items[hi].obj->g->Intersects(items[li].obj->g)) continue;
                    OGRGeometry* inter = items[hi].obj->g->Intersection(items[li].obj->g);
                    const double m = measure(inter);
                    delete inter;
                    if (m > bestM) { bestM = m; best = hi; }
                }
                if (best >= 0) liftsOfHost[best].append(li);
            }
            auto dropHoles = [&](const OGRPolygon* src, const QVector<int>& lifts, bool& changed) -> OGRPolygon*
            {
                OGRPolygon* np = new OGRPolygon;
                if (src->getExteriorRing()) np->addRing(const_cast<OGRPolygon*>(src)->getExteriorRing());
                for (int ri = 0; ri < src->getNumInteriorRings(); ++ri)
                {
                    OGRPolygon hole;
                    hole.addRing(const_cast<OGRPolygon*>(src)->getInteriorRing(ri));
                    bool hit = false;
                    for (int li : lifts)
                        if (hole.Intersects(items[li].obj->g)) { hit = true; break; }
                    if (hit) changed = true;
                    else np->addRing(const_cast<OGRPolygon*>(src)->getInteriorRing(ri));
                }
                return np;
            };
            for (auto it = liftsOfHost.constBegin(); it != liftsOfHost.constEnd(); ++it)
            {
                ExportItem& e = items[it.key()];
                OGRGeometry* cur = nullptr;
                bool changed = false;
                const OGRwkbGeometryType gt = wkbFlatten(e.obj->g->getGeometryType());
                if (gt == wkbPolygon)
                    cur = dropHoles(static_cast<OGRPolygon*>(e.obj->g), it.value(), changed);
                else
                {
                    OGRMultiPolygon* mp = static_cast<OGRMultiPolygon*>(e.obj->g);
                    OGRMultiPolygon* nm = new OGRMultiPolygon;
                    for (int pi = 0; pi < mp->getNumGeometries(); ++pi)
                        nm->addGeometryDirectly(dropHoles(static_cast<OGRPolygon*>(mp->getGeometryRef(pi)), it.value(), changed));
                    cur = nm;
                }
                for (int li : it.value())
                {
                    OGRGeometry* u = cur->Union(items[li].obj->g);
                    if (!u) continue;
                    delete cur;
                    cur = u;
                    changed = true;
                }
                const OGRwkbGeometryType rt = wkbFlatten(cur->getGeometryType());
                if (!changed || (rt != wkbPolygon && rt != wkbMultiPolygon)) { delete cur; continue; }
                MapFeature* cl = cloneObject(e.obj);
                delete cl->g;
                cl->g = cur;
                cl->updateBounds();
                keepAlive << QSharedPointer<MapFeature>(cl);
                e.obj = cl;
            }
        }
    }

    tmcStepLog("scene: elevator shafts");
    // --- terrain, scene center ---
    double bx0, by0, bx1, by1;
    if (!project.bounds(bx0, by0, bx1, by1))
    {
        if (err) *err = QObject::tr("There are no objects with geometry");
        return false;
    }
    out.centerX = 0.5 * (bx0 + bx1);
    out.centerY = 0.5 * (by0 + by1);
    const QVector3D origin(float(std::round(out.centerX)), float(std::round(out.centerY)), 0.0f);
    const double zExag = settings.zExaggeration > 1e-9 ? settings.zExaggeration : 1.0;
    const ModelTerrain terrain(project.rasters());
    const bool haveTerrain = !terrain.isEmpty();

    // --- rebuilding the meshes and transfer into McObject (in parallel, the result goes by index, the order is deterministic) ---
    const double invZ = 1.0 / zExag;
    const int total = int(items.size());
    QVector<McWorldExporter::McObject> built(total);
    QVector<char> okFlag(total, 0);
    std::atomic<int> done{ 0 };
    std::atomic_bool cancelled{ false };
    QVector<int> idx;
    idx.reserve(total);
    for (int i = 0; i < total; ++i) idx << i;

    auto buildOne = [&](int i)
    {
        if (cancelled.load()) return;
        if (cancel && cancel->load()) { cancelled = true; return; }
        const ExportItem& e = items[i];
        ModelMeshData mesh;
        double rel = 0;
        ModelSettings::VectorSource vs = e.vs;
        if (e.roadScale > 1.001) vs.lineWidth *= e.roadScale;
        // the line width is tuned "in blocks"; after the map is brought to meters the ribbons are widened back (except roads without markings)
        if (opt.unitsToMetersX > 1e-9 && opt.unitsToMetersX != 1.0)
        {
            const bool markedRoad = (e.code == 45210000 || e.code == 45220000 || e.code == 45230000
                                     || e.code == 61210000 || e.code == 61220000 || e.code == 61230000);
            if (!(e.kind == Kind::Road && !markedRoad))
                vs.lineWidth /= opt.unitsToMetersX;
        }
        if (ModelBuilder::BuildSingleObjectMesh(e.obj, vs, haveTerrain ? &terrain : nullptr, zExag, origin, e.colorIndex,
                                                     e.relMin, e.relMax, false, QRectF(), mesh, &rel)
            && mesh.indices.size() >= 3)
        {
            McWorldExporter::McObject mo;
            mo.vertices.reserve(mesh.vertices.size());
            double rs = 0, gs = 0, bs = 0;
            for (int k = 0; k < mesh.vertices.size(); ++k)
            {
                const QVector3D& v = mesh.vertices[k];
                mo.vertices << QVector3D(v.x() + origin.x(), v.y() + origin.y(), (v.z() + origin.z()) * float(invZ));
                if (k < mesh.colors.size())
                {
                    rs += mesh.colors[k].x(); gs += mesh.colors[k].y(); bs += mesh.colors[k].z();
                }
            }
            const double cn = std::max(1, int(mesh.colors.size()));
            mo.color = QColor::fromRgbF(std::min(1.0, rs / cn), std::min(1.0, gs / cn), std::min(1.0, bs / cn));
            mo.indices = mesh.indices;
            mo.closed = e.polygon && std::abs(rel) > 1e-9;
            mo.kind = e.kind;
            mo.intraBlock = e.intraBlock;
            mo.linear = e.linear;
            mo.material = e.material;
            mo.extrudeH = std::abs(rel) * invZ;
            mo.code = e.code;
            if (e.kind == Kind::Plate && mo.extrudeH < 1.0 && (e.code == 51230000 || e.code == 51133000 || e.code == 51410000))
            {   // csrpol 51230000/51133000/51410000 with zero height — a 15 m tower
                float zmin = std::numeric_limits<float>::max(), zmax = -zmin;
                for (const QVector3D& v : qAsConst(mo.vertices)) { zmin = std::min(zmin, v.z()); zmax = std::max(zmax, v.z()); }
                if (zmax - zmin > 1e-4f)
                {
                    const float k = 15.0f / (zmax - zmin);
                    for (QVector3D& v : mo.vertices) v.setZ(zmin + (v.z() - zmin) * k);
                    mo.extrudeH = 15.0;
                    mo.closed = true;
                }
            }
            if (e.kind == Kind::ParkingLot && e.obj && e.obj->g)
            {   // parking outlines (outer and inner rings) — the places are marked along them
                auto addRing = [&](OGRLinearRing* r)
                {
                    if (!r) return;
                    QVector<QPointF> pts;
                    for (int k = 0; k < r->getNumPoints(); ++k) pts << QPointF(r->getX(k), r->getY(k));
                    if (pts.size() >= 3) mo.paths << pts;
                };
                auto addPoly = [&](OGRPolygon* poly)
                {
                    addRing(poly->getExteriorRing());
                    for (int k = 0; k < poly->getNumInteriorRings(); ++k) addRing(poly->getInteriorRing(k));
                };
                const OGRwkbGeometryType gt = wkbFlatten(e.obj->g->getGeometryType());
                if (gt == wkbPolygon) addPoly(static_cast<OGRPolygon*>(e.obj->g));
                else if (gt == wkbMultiPolygon)
                {
                    OGRMultiPolygon* mp = static_cast<OGRMultiPolygon*>(e.obj->g);
                    for (int k = 0; k < mp->getNumGeometries(); ++k) addPoly(static_cast<OGRPolygon*>(mp->getGeometryRef(k)));
                }
            }
            if (e.kind == Kind::Railway || e.kind == Kind::Road)
                ModelBuilder::ExtractLinePaths(e.obj, mo.paths);
            mo.lift = e.lift;
            mo.onionRoof = e.onion;
            mo.roofH = e.roofH * invZ;
            mo.skillionRoof = e.skillion && e.roofH > 0.0;
            mo.roofDir = e.roofDir;
            mo.part = e.part;
            if (e.kind == Kind::Generic || e.kind == Kind::Building)
            {   // a part raised above the ground (min_height): the base lift from the layer's lift field
                double lf = vs.liftAdd;
                if (!vs.liftField.isEmpty() && e.obj && vs.layer)
                {
                    const int fno = vs.layer->fieldIndex(vs.liftField);
                    if (fno >= 0) lf += e.obj->fields.value(fno).toDouble() * vs.liftMultiplier;
                }
                if (e.kind == Kind::Building && mo.closed && lf >= 1.0) mo.raised = lf * invZ;
            }
            mo.streetType = e.streetType; mo.street = e.street; mo.number = e.number;
            built[i] = std::move(mo);
            okFlag[i] = 1;
        }
        const int d = ++done;
        if (progress && (d % 32 == 0 || d == total)) progress(d, total);
    };

    // own thread pool, so as not to conflict with the exporter pool
    QThreadPool pool;
    pool.setMaxThreadCount(std::max(1, QThread::idealThreadCount()));
    QVector<QFuture<void>> futs;
    std::atomic<int> nextIdx{ 0 };
    for (int t = 0; t < pool.maxThreadCount(); ++t)
        futs << QtConcurrent::run(&pool, [&]()
        {
            for (;;)
            {
                const int i = nextIdx.fetch_add(1);
                if (i >= total) break;
                buildOne(i);
            }
        });
    for (QFuture<void>& f : futs) f.waitForFinished();
    if (cancelled.load() || (cancel && cancel->load()))
    {
        out.cancelled = true;
        return true;
    }
    tmcStepLog("scene: meshes built (in parallel)");
    out.objects.reserve(total);
    for (int i = 0; i < total; ++i)
        if (okFlag[i]) out.objects << std::move(built[i]);
    if (out.objects.isEmpty())
    {
        if (err) *err = QObject::tr("No object gave any geometry (check the height field in the 3D settings)");
        return false;
    }
    if (haveTerrain)
        out.sampler = [terrain](double x, double y) -> double { return terrain.heightAt(x, y); };
    return true;
}
