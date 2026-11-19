#include "scene/mcscene.h"
#include "scene/modelbuilder.h"            // ModelSettings::VectorSource, ModelEffect (the kLayerStyles below)
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
    bool polygon = false;
    Kind kind = Kind::Generic;
    QString material;
    bool linear = false;
    double roadScale = 1.0;
    int code = 0;
    bool elec = false;                              // Railway: the ELEC column (OSM "electrified" other than "no")
    bool bridge = false, tunnel = false;            // Road: the BRG/TUN columns (OSM "bridge"/"tunnel" other than "no")
    int elev = 0;                                   // Road: the LVL column (OSM "level"), clamped to -4..4
    int lyr = 0;                                    // Road: the LYR column (OSM "layer"), the bridge lift multiplier
    bool lift = false;
    bool onion = false;
    double roofH = 0.0;
    bool skillion = false;
    double roofDir = 0.0;
    bool part = false;
    QString roofColour, roofMaterial;
    QString streetType, street, number;
    bool intraBlock = false;
    int model = 0;                                    // point structures (kind Spot): the figure number from the rules catalog
};

MapFeature* cloneObject(const MapFeature* src)
{
    MapFeature* c = new MapFeature;
    c->g = src->g ? src->g->clone() : nullptr;
    c->fields = src->fields;
    c->xmin = src->xmin; c->xmax = src->xmax; c->ymin = src->ymin; c->ymax = src->ymax;
    return c;
}

// The mesh style of every catalog layer — the visual part of the old default.t3d vector sources,
// hardcoded since v2.0 (the rules catalog carries the per-code semantics: the Rel heights, the
// effects, the model numbers; how a layer is put into the mesh — the extrusion, the width, the
// decorative geometry bits — is the exporter's own code and does not vary with the data).
struct LayerStyle
{
    const char* layer;
    bool noExtrusion;                 // "on the ground": the Rel column of the catalog is not read — a flat cover
    const char* heightField;          // "REL" on the extruding layers
    int effect;                       // visual ModelEffect bits (the semantic swamp/prison/military are per code in the catalog)
    double lineWidth;                 // linear layers: the ribbon width, tuned "in blocks" (widened back to meters in buildOne)
    const char* liftField;
    double liftMultiplier;
};

const LayerStyle kLayerStyles[] = {
    { "hdrlin",  true,  "",     int(ModelEffect::None),                                                             3.5, "",     1.0 },
    { "rdslin",  true,  "",     int(ModelEffect::Shadow) | int(ModelEffect::RadialGlow)
                              | int(ModelEffect::OutlineDark) | int(ModelEffect::Supports),                          4.5, "",     4.5 },
    { "rrslin",  true,  "",     int(ModelEffect::Glow) | int(ModelEffect::RadialGlow)
                              | int(ModelEffect::OutlineLight) | int(ModelEffect::Supports),                         8.0, "",     8.0 },
    { "bldpol",  false, "REL",  int(ModelEffect::Shadow) | int(ModelEffect::Glow),                                   3.0, "MINH", 1.0 },  // MINH lifts a building:part off the ground
    { "blkpol",  true,  "",     int(ModelEffect::None),                                                              3.0, "",     1.0 },
    { "csrpol",  false, "REL",  int(ModelEffect::Shadow),                                                            3.0, "",     1.0 },
    { "csrpnt",  true,  "",     int(ModelEffect::None),                                                              3.0, "",     1.0 },
    { "csrlin",  true,  "",     int(ModelEffect::None),                                                              2.0, "",     1.0 },
    { "hdtpnt",  true,  "",     int(ModelEffect::None),                                                              3.0, "",     1.0 },
    { "rdcpnt",  true,  "",      int(ModelEffect::None),                                                             3.0, "",     1.0 },
    { "rdcpol",  true,  "",     int(ModelEffect::None),                                                              3.0, "",     1.0 },
    { "hdrpol",  true,  "",     int(ModelEffect::None),                                                              3.0, "",     1.0 },
    { "vegpol",  false, "REL",  int(ModelEffect::Shadow) | int(ModelEffect::Rounded),                                3.0, "",     1.0 },
    { "mrspol",  true,  "",     int(ModelEffect::None),                                                              3.0, "",     1.0 },
    { "lcapol",  true,  "",     int(ModelEffect::None),                                                              3.0, "",     1.0 },
    { "terpol",  true,  "",     int(ModelEffect::None),                                                              3.0, "",     1.0 },
    { "vgbpol",  false, "REL",  int(ModelEffect::Shadow) | int(ModelEffect::Rounded),                                3.0, "",     1.0 },
};

const LayerStyle* layerStyleFor(const QString& name)
{
    for (const LayerStyle& st : kLayerStyles)
        if (name == QLatin1String(st.layer))
            return &st;
    return nullptr;
}

//! The outline mode encoded in the effect mask (as the old .t3d parser derived it)
ModelOutlineMode outlineFromMask(int mask)
{
    if (mask & int(ModelEffect::OutlineDark)) return ModelOutlineMode::Dark;
    if (mask & int(ModelEffect::OutlineLight)) return ModelOutlineMode::Light;
    return ModelOutlineMode::None;
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

bool buildMcScene(const TmcProject& project, const TmcRulesRepo& repo, const McSceneOptions& opt, McSceneResult& out,
                  const std::function<void(int, int)>& progress, const std::atomic_bool* cancel, QString* err)
{
    out = McSceneResult();
    tmcStepLog("scene: start");

    // --- binding the catalog layers to the project layers (by name suffix, case-insensitive) ---
    // The catalog order (repo.order(), the old .t3d source order) is the iteration order: colorIndex and
    // the object order (which feeds the seeded decisions of the export) must follow exactly it.
    struct Bound { const TmcRulesRepo::Layer* desc; MapLayer* layer; int colorIndex; };
    QVector<Bound> bound;
    int colorIndex = 0;
    for (const QString& name : repo.order())
    {
        const TmcRulesRepo::Layer* ld = repo.layer(name);
        if (!ld)                                      // cannot happen: order() only names the loaded layers
            continue;
        MapLayer* layer = project.layerBySuffix(name);
        if (!layer)
        {
            out.warnings << QObject::tr("The layer '%1' from the rules catalog was not found in the project — skipped").arg(name);
            continue;
        }
        Bound b{ ld, layer, colorIndex };
        ++colorIndex;                                 // as in the tqt 3D window, setModel(): over all sources with a found layer
        bound << b;
    }
    if (bound.isEmpty())
    {
        if (project.rasters().isEmpty())
        {
            if (err) *err = QObject::tr("None of the layers from the rules catalog was found among the project layers");
            return false;
        }
        // no objects at all (the rules found nothing): the terrain matrix itself is the content — the scene is empty
        out.warnings << QObject::tr("No objects in the data: the terrain is exported without them.");
    }

    // --- collecting objects (order: layers as in the catalog, objects as in the layer) ---
    QVector<ExportItem> items;
    for (const Bound& b : bound)
    {
        MapLayer* layer = b.layer;
        const QString sfx = b.desc->name;
        const LayerStyle* st = layerStyleFor(sfx);    // every catalog layer has a style (the layer list is hardcoded)
        ModelSettings::VectorSource vs;               // the mesh style of the layer (the old .t3d source row)
        vs.layer = layer;
        if (st)
        {
            vs.noExtrusion = st->noExtrusion;
            vs.heightField = QString::fromLatin1(st->heightField);
            vs.effect = ModelEffects(st->effect);
            vs.outline = outlineFromMask(st->effect);
            vs.lineWidth = st->lineWidth;
            vs.liftField = QString::fromLatin1(st->liftField);
            vs.liftMultiplier = st->liftMultiplier;
        }
        // The base role of the layer: its family (a point layer builds a model figure per object, the old
        // modelField source mark) and its name (the exporter's per-layer pipeline). The semantic effect of
        // the object code (swamp/prison/military, the old per-source effect bits) overrides it per object below.
        Kind kind = Kind::Generic;
        if (b.desc->family == TmcRulesRepo::FamilyPoint) kind = Kind::Spot;
        else if (sfx == QLatin1String("bldpol")) kind = Kind::Building;
        else if (sfx == QLatin1String("vegpol") || sfx == QLatin1String("vgbpol")) kind = Kind::Vegetation;
        else if (sfx == QLatin1String("rdslin")) kind = Kind::Road;
        else if (sfx == QLatin1String("rrslin")) kind = Kind::Railway;
        else if (sfx == QLatin1String("csrlin")) kind = Kind::CSRLine;
        else if (sfx == QLatin1String("csrpol")) kind = Kind::Plate;
        else if (sfx == QLatin1String("rdcpol")) kind = Kind::ParkingLot;
        else if (sfx == QLatin1String("blkpol")) kind = Kind::BlockCover;
        else if (sfx == QLatin1String("lcapol")) kind = Kind::Surface;
        else if (sfx == QLatin1String("hdrpol") || sfx == QLatin1String("hdrlin")) kind = Kind::Water;
        const bool polygonLayer = layer->geom == GeomKind::Polygon;
        int modelsMissing = 0;                        // point objects with a code the catalog gives no model for
        int unlisted = 0;                             // non-point objects with a code absent from the catalog (the generic behavior)

        for (MapFeature* obj : qAsConst(layer->features))
        {
            if (!obj || !obj->g) continue;
            ExportItem e;
            e.obj = obj;
            e.vs = vs;
            e.colorIndex = b.colorIndex;
            e.polygon = polygonLayer;
            e.kind = kind;
            e.linear = (sfx == QLatin1String("hdrlin"));
            e.intraBlock = (sfx == QLatin1String("vgbpol"));

            // building material: the layer's material column from the catalog (bldpol: MAT), the standard MAT otherwise
            if (kind == Kind::Building)
            {
                const QString mcol = b.desc->materialColumn.isEmpty() ? QStringLiteral("MAT") : b.desc->materialColumn;
                const int fno = layer->fieldIndex(mcol);
                if (fno >= 0) e.material = obj->fields.value(fno).trimmed();
                if (e.material.isEmpty() && mcol != QLatin1String("MAT"))
                {
                    const int mno = layer->fieldIndex(QStringLiteral("MAT"));
                    if (mno >= 0) e.material = obj->fields.value(mno).trimmed();
                }
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
            // the catalog descriptor of the code: the semantic effect (a wetland/prison/military overlay)
            // and the model number of a point figure (the old point_models.csv lookup)
            const TmcRulesRepo::Object* desc = e.code ? repo.object(sfx, e.code) : nullptr;
            if (desc)
            {
                if (desc->effect == QLatin1String("swamp")) e.kind = Kind::Biome;
                else if (desc->effect == QLatin1String("prison")) e.kind = Kind::Prison;
                else if (desc->effect == QLatin1String("military")) e.kind = Kind::Military;
            }
            else if (e.code && kind != Kind::Spot)
                ++unlisted;                           // a code absent from the catalog: the generic behavior of its layer (v2.0 — a warning, not a drop)
            if (e.kind == Kind::Spot)
            {   // a point structure: its figure number comes from the object descriptor
                e.model = desc ? desc->model : 0;
                if (e.model <= 0) { ++modelsMissing; continue; }    // no model for the code — not an object
            }
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
                if (e.onion)
                {
                    const int cno2 = layer->fieldIndex(QStringLiteral("ROOFC")), mno2 = layer->fieldIndex(QStringLiteral("ROOFM"));
                    if (cno2 >= 0) e.roofColour = obj->fields.value(cno2).trimmed();
                    if (mno2 >= 0) e.roofMaterial = obj->fields.value(mno2).trimmed();
                }
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
                // elevated / underground roadway: the BRG/TUN/LVL/LYR columns (OSM "bridge"/"tunnel"/"level"/"layer");
                // the value "no" (and an empty column) — an ordinary surface road
                const int brn = layer->fieldIndex(QStringLiteral("BRG"));
                if (brn >= 0)
                {
                    const QString bv = obj->fields.value(brn).trimmed();
                    e.bridge = !bv.isEmpty() && bv.compare(QLatin1String("no"), Qt::CaseInsensitive) != 0;
                }
                const int tnn = layer->fieldIndex(QStringLiteral("TUN"));
                if (tnn >= 0)
                {
                    const QString tv = obj->fields.value(tnn).trimmed();
                    e.tunnel = !tv.isEmpty() && tv.compare(QLatin1String("no"), Qt::CaseInsensitive) != 0;
                }
                const int lv = layer->fieldIndex(QStringLiteral("LVL"));
                if (lv >= 0) e.elev = obj->fields.value(lv).trimmed().toInt();
                const int ly = layer->fieldIndex(QStringLiteral("LYR"));
                if (ly >= 0) e.lyr = obj->fields.value(ly).trimmed().toInt();
            }
            // rail electrification: the ELEC column (the OSM "electrified" tag other than "no")
            if (kind == Kind::Railway)
            {
                const int eno = layer->fieldIndex(QStringLiteral("ELEC"));
                if (eno >= 0)
                {
                    const QString ev = obj->fields.value(eno).trimmed();
                    e.elec = !ev.isEmpty() && ev.compare(QLatin1String("no"), Qt::CaseInsensitive) != 0;
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
        if (modelsMissing > 0)
            out.warnings << QObject::tr("%1 objects on layer '%2' were skipped: no model for their codes in the rules catalog")
                            .arg(modelsMissing).arg(layer->name);
        if (unlisted > 0)
            out.warnings << QObject::tr("%1 objects on layer '%2' have a code absent from the rules catalog — built with the generic layer behavior")
                            .arg(unlisted).arg(layer->name);
    }
    tmcStepLog("scene: objects collected");
    if (items.isEmpty() && project.rasters().isEmpty())
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
        if (project.rasters().isEmpty())
        {
            if (err) *err = QObject::tr("There are no objects with geometry");
            return false;
        }
        // no objects at all: the height matrix extent is the area — the export is the terrain itself
        const HeightRaster* r = project.rasters().first();
        bx0 = std::min(r->xB, r->xE); bx1 = std::max(r->xB, r->xE);
        by0 = std::min(r->yB, r->yE); by1 = std::max(r->yB, r->yE);
        out.warnings << QObject::tr("No objects in the data: the terrain is exported without them.");
    }
    out.centerX = 0.5 * (bx0 + bx1);
    out.centerY = 0.5 * (by0 + by1);
    const QVector3D origin(float(std::round(out.centerX)), float(std::round(out.centerY)), 0.0f);
    const ModelTerrain terrain(project.rasters());
    const bool haveTerrain = !terrain.isEmpty();

    // --- rebuilding the meshes and transfer into McObject (in parallel, the result goes by index, the order is deterministic) ---
    // The z exaggeration went away with the .t3d settings (it was always 1): heights stay real meters.
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
        if (ModelBuilder::BuildSingleObjectMesh(e.obj, vs, haveTerrain ? &terrain : nullptr, 1.0, origin, e.colorIndex,
                                                     0.0, 0.0, false, QRectF(), mesh, &rel)
            && mesh.indices.size() >= 3)
        {
            McWorldExporter::McObject mo;
            mo.vertices.reserve(mesh.vertices.size());
            for (const QVector3D& v : mesh.vertices)
                mo.vertices << QVector3D(v.x() + origin.x(), v.y() + origin.y(), v.z() + origin.z());
            // The 3D window colors are not read anywhere (the old t3d parser skipped them deliberately): OSMC paints
            // the objects itself. Only two colors ever reach the world (export/exportrun.cpp, export/voxelize.cpp):
            // the wall concrete of buildings (and the glass/panels material tinted by it) and the white carpet of
            // the pedestrian roads — the other kinds decide by their CODE, the object color means nothing to them.
            mo.color = (e.kind == Kind::Road) ? QColor(QStringLiteral("#ffffff")) : QColor(QStringLiteral("#8c99a6"));
            mo.indices = mesh.indices;
            mo.closed = e.polygon && std::abs(rel) > 1e-9;
            mo.kind = e.kind;
            mo.intraBlock = e.intraBlock;
            mo.linear = e.linear;
            mo.material = e.material;
            mo.extrudeH = std::abs(rel);
            mo.code = e.code;
            mo.model = e.model;
            mo.railElec = e.elec;
            mo.roadBridge = e.bridge;
            mo.roadTunnel = e.tunnel;
            mo.roadLevel = qBound(-4, e.elev, 4);
            mo.roadLayer = qBound(-4, e.lyr, 4);   // (user request) multiplies a bridge's lift (stacked interchanges)
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
            if (e.kind == Kind::Railway || e.kind == Kind::Road || e.kind == Kind::CSRLine)
                ModelBuilder::ExtractLinePaths(e.obj, mo.paths);
            mo.lift = e.lift;
            mo.onionRoof = e.onion;
            mo.roofH = e.roofH;
            mo.skillionRoof = e.skillion && e.roofH > 0.0;
            mo.roofDir = e.roofDir;
            mo.part = e.part;
            mo.roofColour = e.roofColour; mo.roofMaterial = e.roofMaterial;
            if (e.kind == Kind::Generic || e.kind == Kind::Building)
            {   // a part raised above the ground (min_height): the base lift from the layer's lift field
                double lf = vs.liftAdd;
                if (!vs.liftField.isEmpty() && e.obj && vs.layer)
                {
                    const int fno = vs.layer->fieldIndex(vs.liftField);
                    if (fno >= 0) lf += e.obj->fields.value(fno).toDouble() * vs.liftMultiplier;
                }
                if (e.kind == Kind::Building && mo.closed && lf >= 1.0) mo.raised = lf;
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
        if (project.rasters().isEmpty())
        {
            if (err) *err = QObject::tr("No object gave any geometry (check the Rel heights in the rules catalog)");
            return false;
        }
        // no objects at all: the terrain matrix itself is the content (the export goes on without objects)
    }
    if (haveTerrain)
        out.sampler = [terrain](double x, double y) -> double { return terrain.heightAt(x, y); };
    return true;
}
