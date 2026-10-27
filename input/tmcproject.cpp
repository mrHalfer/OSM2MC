#include "input/tmcproject.h"
#include "ogrsf_frmts.h"
#include "ogr_spatialref.h"
#include "gdal_priv.h"
#include "osm/tmcosmrules.h"
#include "osm/tmcrel.h"
#include "osm/tmcaddress.h"
#include "osm/tmcenhancer.h"
#include <QFile>
#include <QMap>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QTextStream>
#include <QCoreApplication>
#include <algorithm>
#include <functional>
#include <cmath>
#include <limits>

namespace
{

QString suffixOf(const QString& layerName)
{
    return layerName.section(QLatin1Char('.'), -1).toLower();
}

//! Default projection for WGS84 data: the DefaultCrs= line from tMC.ini next to the exe (tqt's TQT.ini works too), otherwise World Mercator
QString defaultCrsText()
{
    QString value;
    for (const char* name : { "/tMC.ini", "/TQT.ini" })
    {
        QFile f(QCoreApplication::applicationDirPath() + QLatin1String(name));
        if (!f.open(QIODevice::ReadOnly)) continue;
        QTextStream in(&f);
        while (!in.atEnd())
        {
            const QString s = in.readLine();
            if (s.startsWith(QLatin1String("DefaultCrs="))) value = s.mid(11).trimmed();
        }
        if (!value.isEmpty()) break;
    }
    return value.isEmpty() ? QStringLiteral("EPSG:3395") : value;
}

int epsgOf(const OGRSpatialReference& s)
{
    const char* name = s.IsProjected() ? s.GetAuthorityName("PROJCS") : s.GetAuthorityName("GEOGCS");
    const char* code = s.IsProjected() ? s.GetAuthorityCode("PROJCS") : s.GetAuthorityCode("GEOGCS");
    if (name && code && qstrcmp(name, "EPSG") == 0) return QString::fromLatin1(code).toInt();
    return 0;
}

//! Read the text of a project file: UTF-8, and if it is not valid UTF-8 — the system code page
QString readText(const QString& file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    const QByteArray raw = f.readAll();
    QString s = QString::fromUtf8(raw);
    if (s.contains(QChar(0xFFFD))) s = QString::fromLocal8Bit(raw);
    return s;
}

}   // namespace

TmcProject::TmcProject() = default;

QByteArray TmcProject::peekEmbeddedT3d(const QString& tqtFile)
{
    const QString text = readText(tqtFile);
    for (const QString& ln : text.split(QLatin1Char('\n')))
    {
        const QString t = ln.trimmed();
        if (t.startsWith(QLatin1String("Wdf3D="))) return QByteArray::fromBase64(t.mid(6).toLatin1());
    }
    return QByteArray();
}

TmcProject::~TmcProject()
{
    qDeleteAll(mLayers);
    qDeleteAll(mRasters);
    delete mFilter;
}

MapLayer* TmcProject::layerBySuffix(const QString& suffix) const
{
    for (MapLayer* l : mLayers)
        if (suffixOf(l->name) == suffix.toLower()) return l;
    return nullptr;
}

int TmcProject::clipTo(const QVector<QPointF>& polygon)
{
    if (polygon.size() < 3) return 0;
    OGRLinearRing ring;
    for (const QPointF& p : polygon) ring.addPoint(p.x(), p.y());
    ring.closeRings();
    OGRPolygon poly;
    poly.addRing(&ring);
    OGREnvelope env;
    poly.getEnvelope(&env);
    int removed = 0;
    for (MapLayer* l : mLayers)
    {
        QVector<MapFeature*> keep;
        keep.reserve(l->features.size());
        for (MapFeature* f : l->features)
        {
            const bool inside = f->g && f->xmax >= env.MinX && f->xmin <= env.MaxX && f->ymax >= env.MinY && f->ymin <= env.MaxY && poly.Intersects(f->g);
            if (inside) keep << f; else { delete f; ++removed; }
        }
        l->features = keep;
    }
    return removed;
}

bool TmcProject::bounds(double& x0, double& y0, double& x1, double& y1) const
{
    x0 = y0 = std::numeric_limits<double>::max();
    x1 = y1 = -std::numeric_limits<double>::max();
    bool any = false;
    for (MapLayer* l : mLayers)
        for (MapFeature* o : l->features)
        {
            if (!o || !o->g) continue;
            x0 = std::min(x0, o->xmin); x1 = std::max(x1, o->xmax);
            y0 = std::min(y0, o->ymin); y1 = std::max(y1, o->ymax);
            any = true;
        }
    return any;
}

void TmcProject::addRaster(const QString& path)
{
    std::unique_ptr<HeightRaster> r(new HeightRaster);
    QString err;
    if (!r->open(path, mSrs.get(), &err))
    {
        log(QObject::tr("Raster skipped: %1").arg(err));
        return;
    }
    log(QObject::tr("Height matrix: %1 (%2×%3)").arg(QFileInfo(path).fileName()).arg(r->xSize).arg(r->ySize));
    mRasters << r.release();
}

bool TmcProject::addDefaultDtm(const QString& dir)
{
    if (!mRasters.isEmpty() || !mSrs) return !mRasters.isEmpty();
    const QString p = QDir(dir).filePath(QStringLiteral("DTM.tif"));
    if (!QFileInfo::exists(p)) return false;
    addRaster(p);
    return !mRasters.isEmpty();
}

bool TmcProject::lonLatToProject(double lon, double lat, double& x, double& y) const
{
    if (!mSrs) return false;
    OGRSpatialReference wgs;
    if (wgs.SetFromUserInput("EPSG:4326") != OGRERR_NONE) return false;
    wgs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    std::unique_ptr<OGRCoordinateTransformation> ct(OGRCreateCoordinateTransformation(&wgs, mSrs.get()));
    if (!ct) return false;
    x = lon; y = lat;
    return ct->Transform(1, &x, &y) != 0;
}

MapLayer* TmcProject::readLayer(const QString& dsPath, const QString& layerName)
{
    GDALAllRegister();
    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpenEx(dsPath.toUtf8().constData(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    if (!ds)
    {
        mWarnings << QObject::tr("Could not open the vector dataset: %1").arg(dsPath);
        return nullptr;
    }
    OGRLayer* lyr = layerName.isEmpty() ? ds->GetLayer(0) : ds->GetLayerByName(layerName.toUtf8().constData());
    if (!lyr) lyr = ds->GetLayer(0);
    if (!lyr)
    {
        mWarnings << QObject::tr("%1 has no layers").arg(dsPath);
        GDALClose(ds);
        return nullptr;
    }

    // coordinate transformation layer -> project (and the filter project -> layer)
    std::unique_ptr<OGRCoordinateTransformation> toProj, toLayer;
    OGRSpatialReference* layerSrs = lyr->GetSpatialRef() ? lyr->GetSpatialRef()->Clone() : nullptr;
    if (layerSrs && mSrs)
    {
        layerSrs->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        OGRSpatialReference* p = mSrs->Clone();
        p->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        if (!p->IsSame(layerSrs))
        {
            toProj.reset(OGRCreateCoordinateTransformation(layerSrs, p));
            toLayer.reset(OGRCreateCoordinateTransformation(p, layerSrs));
            if (!toProj) mWarnings << QObject::tr("Layer %1: no coordinate transformation to the project system — using it as is").arg(layerName);
        }
        p->Release();
    }
    if (layerSrs) layerSrs->Release();

    if (mFilter)
    {
        OGRGeometry* f = mFilter->clone();
        if (toLayer) f->transform(toLayer.get());
        lyr->SetSpatialFilter(f);
        delete f;
    }

    std::unique_ptr<MapLayer> L(new MapLayer);
    L->name = layerName.isEmpty() ? QString::fromUtf8(lyr->GetName()) : layerName;
    OGRFeatureDefn* defn = lyr->GetLayerDefn();
    const int nf = defn->GetFieldCount();
    for (int i = 0; i < nf; ++i) L->fieldNames << QString::fromUtf8(defn->GetFieldDefn(i)->GetNameRef());
    switch (wkbFlatten(lyr->GetGeomType()))
    {
    case wkbPolygon: case wkbMultiPolygon: L->geom = GeomKind::Polygon; break;
    case wkbLineString: case wkbMultiLineString: L->geom = GeomKind::Line; break;
    default: L->geom = GeomKind::None; break;
    }

    lyr->ResetReading();
    OGRFeature* f;
    while ((f = lyr->GetNextFeature()) != nullptr)
    {
        OGRGeometry* g = f->GetGeometryRef() ? f->GetGeometryRef()->clone() : nullptr;
        if (!g || g->IsEmpty()) { delete g; OGRFeature::DestroyFeature(f); continue; }
        if (toProj && g->transform(toProj.get()) != OGRERR_NONE) { delete g; OGRFeature::DestroyFeature(f); continue; }
        // a multi-geometry of one part becomes a single geometry (as in a tqt layer)
        const OGRwkbGeometryType gt = wkbFlatten(g->getGeometryType());
        if (gt == wkbMultiPolygon && static_cast<OGRMultiPolygon*>(g)->getNumGeometries() == 1)
        {
            OGRGeometry* part = static_cast<OGRMultiPolygon*>(g)->getGeometryRef(0)->clone();
            delete g; g = part;
        }
        else if (gt == wkbMultiLineString && static_cast<OGRMultiLineString*>(g)->getNumGeometries() == 1)
        {
            OGRGeometry* part = static_cast<OGRMultiLineString*>(g)->getGeometryRef(0)->clone();
            delete g; g = part;
        }
        std::unique_ptr<MapFeature> o(new MapFeature);
        o->g = g;
        o->updateBounds();
        o->fields.reserve(nf);
        for (int i = 0; i < nf; ++i)
            o->fields << (f->IsFieldSetAndNotNull(i) ? QString::fromUtf8(f->GetFieldAsString(i)) : QString());
        L->features << o.release();
        OGRFeature::DestroyFeature(f);
    }
    GDALClose(ds);
    log(QObject::tr("Layer %1: %2 objects").arg(L->name).arg(L->features.size()));
    return L.release();
}

bool TmcProject::finishProjectCrs()
{
    return mSrs != nullptr;
}

bool TmcProject::loadFolder(const QString& dir, QString* err)
{
    QDir d(dir);
    if (!d.exists()) { if (err) *err = QObject::tr("Folder not found: %1").arg(dir); return false; }
    mBaseName = d.dirName().isEmpty() ? QStringLiteral("tmc_export") : d.dirName();
    const QStringList shps = d.entryList(QStringList() << QStringLiteral("*.shp"), QDir::Files, QDir::Name);
    if (shps.isEmpty()) { if (err) *err = QObject::tr("There are no shapefiles in the folder %1").arg(QDir::toNativeSeparators(dir)); return false; }

    // the project CRS comes from the first suitable layer (.prj), for WGS84 — the default projection
    QStringList use;
    for (const QString& s : shps)
    {
        const QString ln = QFileInfo(s).completeBaseName();
        if (mWanted.isEmpty() || mWanted.contains(suffixOf(ln))) use << s;
    }
    if (use.isEmpty()) { if (err) *err = QObject::tr("The folder has no shapefiles with the needed layers"); return false; }
    {
        GDALAllRegister();
        GDALDataset* ds = static_cast<GDALDataset*>(GDALOpenEx(d.filePath(use.first()).toUtf8().constData(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
        if (ds && ds->GetLayer(0) && ds->GetLayer(0)->GetSpatialRef())
        {
            mSrs.reset(ds->GetLayer(0)->GetSpatialRef()->Clone());
            mSrs->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            if (mSrs->IsGeographic() && epsgOf(*mSrs) == 4326)
            {
                std::unique_ptr<OGRSpatialReference> def(new OGRSpatialReference);
                if (def->SetFromUserInput(defaultCrsText().toUtf8().constData()) == OGRERR_NONE)
                {
                    def->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
                    mSrs = std::move(def);
                    log(QObject::tr("The data is in WGS84 — the default projection %1").arg(defaultCrsText()));
                }
            }
        }
        if (ds) GDALClose(ds);
    }
    if (!mSrs) { if (err) *err = QObject::tr("Could not determine the coordinate system (no .prj for %1)").arg(use.first()); return false; }
    char* w = nullptr;
    mSrs->exportToWkt(&w);
    mWkt = QString::fromUtf8(w);
    CPLFree(w);

    for (const QString& s : use)
    {
        MapLayer* l = readLayer(d.filePath(s), QFileInfo(s).completeBaseName());
        if (l) mLayers << l;
    }
    if (mLayers.isEmpty()) { if (err) *err = QObject::tr("No layer was read"); return false; }
    return true;
}

bool TmcProject::isOsmFile(const QString& path)
{
    const QString l = path.toLower();
    return l.endsWith(QLatin1String(".osm")) || l.endsWith(QLatin1String(".pbf")) || l.endsWith(QLatin1String(".osm.pbf"))
           || l.endsWith(QLatin1String(".osm.bz2"));
}

bool TmcProject::loadOsm(const QString& file, const QString& rulesFile, const QString& iniFile, QString* err)
{
    OsmRules rules;
    if (!rules.load(rulesFile, err)) return false;
    if (rules.isEmpty())
    {
        if (err) *err = QObject::tr("The OSM rules file %1 has no rules — fill in the \"rules\" list (which tags correspond to which layer and code).").arg(QDir::toNativeSeparators(rulesFile));
        return false;
    }
    log(QObject::tr("OSM rules: %1 (%2 pcs.)").arg(QDir::toNativeSeparators(rulesFile)).arg(rules.rules().size()));

    QFileInfo fi(file);
    if (!fi.exists()) { if (err) *err = QObject::tr("OSM file not found: %1").arg(file); return false; }
    QString bn = fi.completeBaseName();
    if (bn.endsWith(QLatin1String(".osm"), Qt::CaseInsensitive)) bn.chop(4);
    mBaseName = bn;

    // project projection: OSM data in WGS84 -> the default projection (DefaultCrs from tMC.ini, otherwise EPSG:3395)
    {
        std::unique_ptr<OGRSpatialReference> def(new OGRSpatialReference);
        if (def->SetFromUserInput(defaultCrsText().toUtf8().constData()) != OGRERR_NONE)
        { if (err) *err = QObject::tr("Invalid default projection: %1").arg(defaultCrsText()); return false; }
        def->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        mSrs = std::move(def);
        char* w = nullptr;
        mSrs->exportToWkt(&w);
        mWkt = QString::fromUtf8(w);
        CPLFree(w);
        log(QObject::tr("OSM data (WGS84) — projection %1").arg(defaultCrsText()));
    }
    std::unique_ptr<OGRSpatialReference> wgs(new OGRSpatialReference);
    wgs->SetFromUserInput("EPSG:4326");
    wgs->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    std::unique_ptr<OGRCoordinateTransformation> toProj(OGRCreateCoordinateTransformation(wgs.get(), mSrs.get()));
    if (!toProj) { if (err) *err = QObject::tr("No WGS84 -> project projection transformation"); return false; }

    // OSM driver configuration: a temporary osmconf.ini (the tags of the rules become columns), or the ini given by the user
    QTemporaryDir tmp;
    QString cfg = iniFile;
    if (cfg.isEmpty())
    {
        if (!tmp.isValid()) { if (err) *err = QObject::tr("Could not create a temporary folder for osmconf.ini"); return false; }
        cfg = tmp.filePath(QStringLiteral("osmconf.ini"));
        QFile f(cfg);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) { if (err) *err = QObject::tr("Could not write %1").arg(cfg); return false; }
        const QString attrs = rules.tagKeys().join(QLatin1Char(','));
        QString ini;
        ini += QStringLiteral("closed_ways_are_polygons=aeroway,amenity,boundary,building,craft,geological,historic,landuse,leisure,military,natural,office,place,shop,sport,tourism,highway=platform,public_transport=platform,building:part,man_made,waterway=riverbank,waterway=dock\n");
        ini += QStringLiteral("attribute_name_laundering=no\n");     // building:levels stays building:levels
        for (const char* sec : { "points", "lines", "multipolygons" })
        {
            ini += QStringLiteral("\n[%1]\nosm_id=yes\nosm_version=no\nosm_timestamp=no\nosm_uid=no\nosm_user=no\nosm_changeset=no\nattributes=%2\n"
                                  "ignore=created_by,converted_by,source,time,ele,note,openGeoDB:,fixme,FIXME\n").arg(QLatin1String(sec), attrs);
        }
        for (const char* sec : { "multilinestrings", "other_relations" })
            ini += QStringLiteral("\n[%1]\nosm_id=yes\nosm_version=no\nosm_timestamp=no\nosm_uid=no\nosm_user=no\nosm_changeset=no\nattributes=name,type\n").arg(QLatin1String(sec));
        f.write(ini.toUtf8());
        f.close();
    }
    else log(QObject::tr("osmconf.ini: %1").arg(QDir::toNativeSeparators(cfg)));

    GDALAllRegister();
    const QByteArray cfgOpt = "CONFIG_FILE=" + QDir::toNativeSeparators(cfg).toUtf8();
    const char* openOpts[] = { cfgOpt.constData(), nullptr };
    const char* drivers[] = { "OSM", nullptr };
    CPLSetConfigOption("OSM_COMPRESS_NODES", "YES");
    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpenEx(fi.absoluteFilePath().toUtf8().constData(), GDAL_OF_VECTOR | GDAL_OF_READONLY,
                                                         drivers, openOpts, nullptr));
    if (!ds) { if (err) *err = QObject::tr("The OSM driver did not open the file %1 (%2)").arg(file, QString::fromUtf8(CPLGetLastErrorMsg())); return false; }

    // object tags: columns (from the ini) + the rest from other_tags
    struct LayerCols { QStringList names; int other = -1; int way = -1; };
    auto colsOf = [](OGRFeatureDefn* d)
    {
        LayerCols c;
        for (int i = 0; i < d->GetFieldCount(); ++i)
        {
            c.names << QString::fromUtf8(d->GetFieldDefn(i)->GetNameRef());
            if (c.names.last() == QLatin1String("other_tags")) c.other = i;
            if (c.names.last() == QLatin1String("osm_way_id")) c.way = i;
        }
        return c;
    };
    auto tagsOf = [](OGRFeature* f, const LayerCols& c)
    {
        QHash<QString, QString> tags;
        for (int i = 0; i < c.names.size(); ++i)
            if (i != c.other && f->IsFieldSetAndNotNull(i)) tags.insert(c.names[i], QString::fromUtf8(f->GetFieldAsString(i)));
        if (c.other >= 0 && f->IsFieldSetAndNotNull(c.other)) OsmRules::parseHstore(QString::fromUtf8(f->GetFieldAsString(c.other)), tags);
        if (!tags.contains(QStringLiteral("osm_id")) && c.way >= 0 && f->IsFieldSetAndNotNull(c.way))
            tags.insert(QStringLiteral("osm_id"), QString::fromUtf8(f->GetFieldAsString(c.way)));
        return tags;
    };

    // --- intersections: poi:* (points inside a building outline) and area:* (enclosing areas without a building tag) ---
    const QStringList poiKeys = rules.spatialKeys(QStringLiteral("poi:"));
    const QStringList areaKeys = rules.spatialKeys(QStringLiteral("area:"));
    struct PoiPt { double x, y; QHash<QString, QString> tags; };
    struct AreaObj { double x0, y0, x1, y1; OGRGeometry* g; QHash<QString, QString> tags; };
    QVector<PoiPt> pois;
    QVector<AreaObj> areas;
    QHash<qint64, QVector<int>> poiGrid;
    const double cell = 0.002;                                  // ≈ 200 m
    auto cellKey = [&](int ix, int iy) { return (qint64(ix) << 32) ^ qint64(quint32(iy)); };
    if (!poiKeys.isEmpty())
        if (OGRLayer* pl = ds->GetLayerByName("points"))
        {
            const LayerCols pc = colsOf(pl->GetLayerDefn());
            pl->ResetReading();
            OGRFeature* f;
            while ((f = pl->GetNextFeature()) != nullptr)
            {
                const OGRGeometry* g = f->GetGeometryRef();
                if (g && wkbFlatten(g->getGeometryType()) == wkbPoint)
                {
                    const QHash<QString, QString> all = tagsOf(f, pc);
                    PoiPt p;
                    for (const QString& k : poiKeys) { const auto it = all.constFind(k); if (it != all.constEnd() && !it.value().isEmpty()) p.tags.insert(k, it.value()); }
                    if (!p.tags.isEmpty())
                    {
                        p.x = static_cast<const OGRPoint*>(g)->getX(); p.y = static_cast<const OGRPoint*>(g)->getY();
                        poiGrid[cellKey(int(std::floor(p.x / cell)), int(std::floor(p.y / cell)))] << pois.size();
                        pois << p;
                    }
                }
                OGRFeature::DestroyFeature(f);
            }
            log(QObject::tr("POI points for intersections: %1").arg(pois.size()));
        }
    if (!areaKeys.isEmpty())
        if (OGRLayer* ml = ds->GetLayerByName("multipolygons"))
        {
            const LayerCols mc = colsOf(ml->GetLayerDefn());
            ml->ResetReading();
            OGRFeature* f;
            while ((f = ml->GetNextFeature()) != nullptr)
            {
                const OGRGeometry* g = f->GetGeometryRef();
                if (g && !g->IsEmpty())
                {
                    const QHash<QString, QString> all = tagsOf(f, mc);
                    if (!all.contains(QStringLiteral("building")))
                    {
                        AreaObj a;
                        for (const QString& k : areaKeys) { const auto it = all.constFind(k); if (it != all.constEnd() && !it.value().isEmpty()) a.tags.insert(k, it.value()); }
                        if (!a.tags.isEmpty())
                        {
                            OGREnvelope e; g->getEnvelope(&e);
                            a.x0 = e.MinX; a.y0 = e.MinY; a.x1 = e.MaxX; a.y1 = e.MaxY;
                            a.g = g->clone();
                            areas << a;
                        }
                    }
                }
                OGRFeature::DestroyFeature(f);
            }
            log(QObject::tr("Areas for intersections: %1").arg(areas.size()));
        }
    // an interior point of an outline: a scan line through the largest part, the middle of the longest interior segment
    // (PointOnSurface does not work in this GDAL build, and the centroid of a concave building may lie outside)
    auto repPointOf = [](const OGRGeometry* g, double& rx, double& ry) -> bool
    {
        const OGRwkbGeometryType gk = wkbFlatten(g->getGeometryType());
        const OGRPolygon* best = nullptr;
        if (gk == wkbPolygon) best = static_cast<const OGRPolygon*>(g);
        else if (gk == wkbMultiPolygon)
        {
            const OGRMultiPolygon* mp = static_cast<const OGRMultiPolygon*>(g);
            double bestA = -1;
            for (int i = 0; i < mp->getNumGeometries(); ++i)
            {
                const OGRPolygon* part = static_cast<const OGRPolygon*>(mp->getGeometryRef(i));
                const double pa = part->get_Area();
                if (pa > bestA) { bestA = pa; best = part; }
            }
        }
        if (!best || !best->getExteriorRing()) return false;
        OGREnvelope pe; best->getEnvelope(&pe);
        for (double fy : { 0.5, 0.35, 0.65, 0.2, 0.8 })
        {
            const double y = pe.MinY + fy * (pe.MaxY - pe.MinY);
            QVector<double> xs;
            auto scan = [&](const OGRLinearRing* r)
            {
                const int n = r->getNumPoints();
                for (int i = 0; i + 1 < n; ++i)
                {
                    const double ya = r->getY(i), yb = r->getY(i + 1);
                    if ((ya <= y) == (yb <= y)) continue;                      // the edge does not cross the line
                    xs << r->getX(i) + (y - ya) / (yb - ya) * (r->getX(i + 1) - r->getX(i));
                }
            };
            scan(best->getExteriorRing());
            for (int i = 0; i < best->getNumInteriorRings(); ++i) scan(best->getInteriorRing(i));
            std::sort(xs.begin(), xs.end());
            double bestLen = 0, bx = 0;
            for (int i = 0; i + 1 < xs.size(); i += 2)
                if (xs[i + 1] - xs[i] > bestLen) { bestLen = xs[i + 1] - xs[i]; bx = 0.5 * (xs[i] + xs[i + 1]); }
            if (bestLen > 0) { rx = bx; ry = y; return true; }
        }
        return false;
    };
    // add poi:* and area:* to the building tags
    auto enrich =[&](const OGRGeometry* g, QHash<QString, QString>& tags)
    {
        if (!tags.contains(QStringLiteral("building"))) return;
        QHash<QString, QStringList> acc;
        OGREnvelope e; g->getEnvelope(&e);
        if (!pois.isEmpty())
        {
            const int ix0 = int(std::floor(e.MinX / cell)), ix1 = int(std::floor(e.MaxX / cell));
            const int iy0 = int(std::floor(e.MinY / cell)), iy1 = int(std::floor(e.MaxY / cell));
            if (qint64(ix1 - ix0 + 1) * (iy1 - iy0 + 1) <= 400)
                for (int ix = ix0; ix <= ix1; ++ix)
                    for (int iy = iy0; iy <= iy1; ++iy)
                    {
                        const auto it = poiGrid.constFind(cellKey(ix, iy));
                        if (it == poiGrid.constEnd()) continue;
                        for (int pi : it.value())
                        {
                            const PoiPt& p = pois[pi];
                            if (p.x < e.MinX || p.x > e.MaxX || p.y < e.MinY || p.y > e.MaxY) continue;
                            OGRPoint pt(p.x, p.y);
                            if (!g->Contains(&pt)) continue;
                            for (auto t = p.tags.constBegin(); t != p.tags.constEnd(); ++t) acc[QStringLiteral("poi:") + t.key()] << t.value();
                        }
                    }
        }
        if (!areas.isEmpty())
        {
            double rx0 = 0, ry0 = 0;
            const bool haveRep = repPointOf(g, rx0, ry0);
            std::unique_ptr<OGRPoint> rep(new OGRPoint(rx0, ry0));
            if (haveRep)
            {
                const double rx = rep->getX(), ry = rep->getY();
                for (const AreaObj& a : areas)
                {
                    if (rx < a.x0 || rx > a.x1 || ry < a.y0 || ry > a.y1) continue;
                    if (!a.g->Contains(rep.get())) continue;
                    for (auto t = a.tags.constBegin(); t != a.tags.constEnd(); ++t) acc[QStringLiteral("area:") + t.key()] << t.value();
                }
            }
        }
        for (auto it = acc.constBegin(); it != acc.constEnd(); ++it)
        {
            QStringList u = it.value();
            u.removeDuplicates();
            tags.insert(it.key(), u.join(QLatin1Char(';')));
        }
    };

    QMap<QString, MapLayer*> out;                 // tqt layer suffix -> layer
    QMap<QString, int> stat, skippedGeom;
    const bool debugOsm = qEnvironmentVariableIsSet("TMC_OSM_DEBUG");
    const QString unmatchedFile = qEnvironmentVariable("TMC_OSM_UNMATCHED");      // file for the report of uncovered objects (optional)
    QMap<QString, int> unmatchedStat;

    // table of typical heights by code (heights.csv) — a fallback source of REL when a building has neither height nor building:levels
    TmcHeightTable clutters;
    {
        const QString cp = TmcHeightTable::findDefault();
        if (!cp.isEmpty() && clutters.load(cp)) log(QObject::tr("Height table: %1 (%2 codes)").arg(QDir::toNativeSeparators(cp)).arg(clutters.size()));
        else mWarnings << QObject::tr("The height table heights.csv was not found: heights of buildings without height and building:levels — the default");
    }

    // enhancer: building points (type, height, floors) from a ready .dat; the rules and the height calculation get them as db:* tags
    TmcEnhancer enhancer;
    QSet<qint64> enhClaimed;
    int enhBuildings = 0, enhMatched = 0;
    bool haveEnh = false;
    if (!mEnhancerOff)
    {
        const QString ep = mEnhancerPath.isEmpty() ? TmcEnhancer::findDefault() : mEnhancerPath;
        if (!ep.isEmpty())
        {
            QString ee;
            if (enhancer.load(ep, &ee)) { haveEnh = true; log(QObject::tr("Enhancer: %1 (%2 buildings)").arg(QDir::toNativeSeparators(ep)).arg(enhancer.recordCount())); }
            else mWarnings << ee;
        }
    }

    // neighbors for the "guess": recognized buildings (center in meters, code, height) and deferred buildings without signs
    struct Confident { double x, y; QString code; double rel; };
    struct Deferred { QHash<QString, QString> tags; OGRGeometry* g; };
    QVector<Confident> confident;
    QVector<Deferred> deferred;
    QHash<qint64, QVector<int>> confGrid;
    const double nbCell = 25.0;                                 // m
    double nbLat = std::numeric_limits<double>::quiet_NaN();    // latitude for converting degrees to meters (from the first building)
    auto cellKeyM = [](int ix, int iy) { return (qint64(ix) << 32) ^ qint64(quint32(iy)); };

    // A pedestrian crossing from a highway=crossing node: a line across the nearest road (rdslin, not alleys/cycleways/dirt roads) sideM meters long.
    // The geometry is returned already in the project projection. nullptr — no road nearby (within 4 m) or a crossing line already exists within 8 m.
    auto makeCrosswalk = [&](const OGRPoint* pt, double sideM) -> OGRGeometry*
    {
        OGRPoint p0(pt->getX(), pt->getY()), p1(pt->getX(), pt->getY() + 10.0 / 111320.0);
        if (p0.transform(toProj.get()) != OGRERR_NONE || p1.transform(toProj.get()) != OGRERR_NONE) return nullptr;
        const double upm = std::hypot(p1.getX() - p0.getX(), p1.getY() - p0.getY()) / 10.0;       // projection units per meter
        MapLayer* rl = out.value(QStringLiteral("rdslin"));
        if (upm <= 0 || !rl) return nullptr;
        const int codeNo = rl->fieldIndex(QStringLiteral("CODE"));
        static const QSet<QString> notVehicle = { QStringLiteral("45260000"), QStringLiteral("45261000"), QStringLiteral("61330000"), QStringLiteral("61420000") };
        const double px = p0.getX(), py = p0.getY();
        const double reach = 12.0 * upm;
        double bestD = 1e300, bdx = 0, bdy = 0;
        bool nearCross = false;
        auto segs = [&](const OGRLineString* ls, bool isCross)
        {
            for (int i = 0; i + 1 < ls->getNumPoints(); ++i)
            {
                const double ax = ls->getX(i), ay = ls->getY(i), bx = ls->getX(i + 1), by = ls->getY(i + 1);
                const double vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy;
                double t = l2 > 0 ? ((px - ax) * vx + (py - ay) * vy) / l2 : 0.0;
                t = std::max(0.0, std::min(1.0, t));
                const double d = std::hypot(px - (ax + t * vx), py - (ay + t * vy));
                if (isCross) { if (d < 8.0 * upm) nearCross = true; }
                else if (d < bestD) { bestD = d; bdx = vx; bdy = vy; }
            }
        };
        for (const MapFeature* o : rl->features)
        {
            if (!o->g || o->xmax < px - reach || o->xmin > px + reach || o->ymax < py - reach || o->ymin > py + reach) continue;
            const QString code = o->fields.value(codeNo);
            const bool isCross = (code == QLatin1String("45262000"));
            if (!isCross && notVehicle.contains(code)) continue;
            const OGRwkbGeometryType t = wkbFlatten(o->g->getGeometryType());
            if (t == wkbLineString) segs(static_cast<const OGRLineString*>(o->g), isCross);
            else if (t == wkbMultiLineString)
            {
                const OGRMultiLineString* ml = static_cast<const OGRMultiLineString*>(o->g);
                for (int i = 0; i < ml->getNumGeometries(); ++i) segs(static_cast<const OGRLineString*>(ml->getGeometryRef(i)), isCross);
            }
        }
        const double l = std::hypot(bdx, bdy);
        if (nearCross || bestD > 4.0 * upm || l <= 0) return nullptr;
        const double nx = -bdy / l, ny = bdx / l, half = 0.5 * sideM * upm;
        OGRLineString* res = new OGRLineString;
        res->addPoint(px - nx * half, py - ny * half);
        res->addPoint(px + nx * half, py + ny * half);
        return res;
    };

    // create the tqt layer objects from the classification results (geometry g0 is in WGS84)
    auto addObjects =[&](const QVector<OsmClassified>& cl, const OGRGeometry* g0, const QHash<QString, QString>& objTags)
    {
        for (const OsmClassified& c0 : cl)
        {
            OsmClassified c = c0;
            // REL: "auto" — height from tags (height -> building:levels by the floor conversion formulas -> heights.csv by CODE)
            QString codeStr;
            bool relAuto = false;
            for (const auto& fv : c.fields) if (fv.first == QLatin1String("CODE")) codeStr = fv.second;
            for (auto& fv : c.fields)
                if (fv.first == QLatin1String("REL") && fv.second.trimmed().toLower() == QLatin1String("auto"))
                {
                    relAuto = true;
                    fv.second = tmcAutoRel(objTags, codeStr.toInt(), clutters);
                    if (debugOsm) log(QStringLiteral("  [rel %1] CODE=%2 REL=%3").arg(objTags.value(QStringLiteral("osm_id")), codeStr, fv.second));
                }
            // a building taller than two floors cannot be a shed (44000000): by floors from the tags/enhancer, otherwise by height — turn it into 44310000
            if (c.layer == QLatin1String("bldpol") && codeStr == QLatin1String("44000000"))
            {
                double lv = 0, rel = 0;
                bool tall = false;
                for (const char* key : { "building:levels", "db:floor" })
                {
                    const QString v = objTags.value(QString::fromLatin1(key));
                    static const QRegularExpression num(QStringLiteral("\\d+(?:[.,]\\d+)?"));
                    const auto m = num.match(v);
                    if (m.hasMatch()) { lv = QString(m.captured(0)).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(); if (lv > 0) break; }
                }
                if (lv > 0) tall = lv > 2.4;
                else
                {
                    for (const auto& fv : c.fields) if (fv.first == QLatin1String("REL")) rel = fv.second.toDouble();
                    tall = rel > 7.5;                               // ~3 floors of 2.5–3.3 m
                }
                if (tall)
                {
                    for (auto& fv : c.fields) if (fv.first == QLatin1String("CODE")) fv.second = QStringLiteral("44310000");
                    codeStr = QStringLiteral("44310000");
                    if (relAuto)                                    // height by the formulas of the new code
                        for (auto& fv : c.fields) if (fv.first == QLatin1String("REL")) fv.second = tmcAutoRel(objTags, 44310000, clutters);
                }
            }
            // address plate (buildings) and street sign (roads): type, name and number from addr:* / name; explicit fields of the rule take priority
            static const bool noAddr = qEnvironmentVariableIsSet("TMC_NO_ADDR");       // debugging: turn the address parsing off (compare time and size)
            if (!noAddr && (c.layer == QLatin1String("bldpol") || c.layer == QLatin1String("rdslin")))
            {
                const bool bld = c.layer == QLatin1String("bldpol");
                const TmcAddress a = bld ? tmcBuildingAddress(objTags) : tmcRoadName(objTags);
                auto fill = [&](const char* name, const QString& v)
                {
                    if (v.isEmpty()) return;
                    for (auto& fv : c.fields)
                        if (fv.first == QLatin1String(name)) { if (fv.second.trimmed().isEmpty()) fv.second = v; return; }
                    c.fields << qMakePair(QString::fromLatin1(name), v);
                };
                fill("TYSTR", a.type); fill("STREET", a.name);
                if (bld) fill("NUMBR", a.number);
                if (debugOsm && !a.isEmpty()) log(QStringLiteral("  [addr %1] %2|%3|%4").arg(objTags.value(QStringLiteral("osm_id")), a.type, a.name, a.number));
            }
            if (!mWanted.isEmpty() && !mWanted.contains(c.layer)) continue;
            OGRGeometry* g = g0->clone();
            bool projected = false;                                 // the geometry is already in the project projection (crosswalk)
            const OGRwkbGeometryType gt = wkbFlatten(g->getGeometryType());
            // geometry type conversion by "as"
            if (c.as == QLatin1String("polygon") && gt == wkbLineString)
            {
                OGRLineString* ls = static_cast<OGRLineString*>(g);
                if (ls->getNumPoints() >= 4 && ls->get_IsClosed())
                {
                    OGRPolygon* pg = new OGRPolygon;
                    OGRLinearRing* ring = new OGRLinearRing;
                    ring->addSubLineString(ls);
                    pg->addRingDirectly(ring);
                    delete g; g = pg;
                }
            }
            else if (c.as == QLatin1String("line") && (gt == wkbPolygon || gt == wkbMultiPolygon))
            {
                OGRGeometry* b = g->Boundary();
                delete g; g = b;
            }
            else if (c.as.startsWith(QLatin1String("crosswalk:")) && gt == wkbPoint)
            {   // a crossing node -> a line across the nearest road (already in the project projection)
                OGRGeometry* cw = makeCrosswalk(static_cast<const OGRPoint*>(g), c.as.mid(10).toDouble());
                delete g; g = cw; projected = true;
            }
            else if (c.as.startsWith(QLatin1String("square:")) && gt == wkbPoint)
            {   // a point -> a square with a side of N meters around it (for example a playground marked by a single point)
                const OGRPoint* pt = static_cast<const OGRPoint*>(g);
                const double hy = 0.5 * c.as.mid(7).toDouble() / 111320.0;
                const double hx = hy / std::max(0.1, std::cos(pt->getY() * 3.14159265358979 / 180.0));
                OGRLinearRing* ring = new OGRLinearRing;
                ring->addPoint(pt->getX() - hx, pt->getY() - hy); ring->addPoint(pt->getX() + hx, pt->getY() - hy);
                ring->addPoint(pt->getX() + hx, pt->getY() + hy); ring->addPoint(pt->getX() - hx, pt->getY() + hy);
                ring->closeRings();
                OGRPolygon* pg = new OGRPolygon;
                pg->addRingDirectly(ring);
                delete g; g = pg;
            }
            if (!g) continue;
            const OGRwkbGeometryType t = wkbFlatten(g->getGeometryType());
            GeomKind ot = GeomKind::None;
            if (t == wkbPolygon || t == wkbMultiPolygon) ot = GeomKind::Polygon;
            else if (t == wkbLineString || t == wkbMultiLineString) ot = GeomKind::Line;
            else if (t == wkbPoint || t == wkbMultiPoint) ot = GeomKind::Point;
            MapLayer*& L = out[c.layer];
            if (!L)
            {
                L = new MapLayer;
                L->name = QStringLiteral("osm.") + c.layer;
                L->fieldNames = rules.fieldNames(c.layer);
                if (c.layer == QLatin1String("bldpol") || c.layer == QLatin1String("rdslin"))
                    for (const char* f : { "TYSTR", "STREET", "NUMBR" })
                        if (!L->fieldNames.contains(QLatin1String(f), Qt::CaseInsensitive)) L->fieldNames << QString::fromLatin1(f);
                L->geom = ot;
            }
            if (ot != L->geom) { ++skippedGeom[c.layer]; delete g; continue; }       // the geometry type does not match the layer type
            if (!projected && g->transform(toProj.get()) != OGRERR_NONE) { delete g; continue; }
            if (t == wkbMultiPolygon && static_cast<OGRMultiPolygon*>(g)->getNumGeometries() == 1)
            {
                OGRGeometry* part = static_cast<OGRMultiPolygon*>(g)->getGeometryRef(0)->clone();
                delete g; g = part;
            }
            else if (t == wkbMultiLineString && static_cast<OGRMultiLineString*>(g)->getNumGeometries() == 1)
            {
                OGRGeometry* part = static_cast<OGRMultiLineString*>(g)->getGeometryRef(0)->clone();
                delete g; g = part;
            }
            std::unique_ptr<MapFeature> o(new MapFeature);
            o->g = g;
            o->updateBounds();
            o->fields.resize(L->fieldNames.size());
            for (const auto& fv : c.fields)
            {
                const int fi2 = L->fieldIndex(fv.first);
                if (fi2 >= 0) o->fields[fi2] = fv.second;
            }
            L->features << o.release();
            ++stat[c.layer];
        }
    };

    for (const QString& from : rules.sourceLayers())
    {
        OGRLayer* lyr = ds->GetLayerByName(from.toUtf8().constData());
        if (!lyr) { mWarnings << QObject::tr("The OSM file has no layer %1").arg(from); continue; }
        OGRFeatureDefn* defn = lyr->GetLayerDefn();
        const LayerCols cols = colsOf(defn);
        log(QObject::tr("Reading OSM layer %1 …").arg(from));
        lyr->ResetReading();
        OGRFeature* f;
        while ((f = lyr->GetNextFeature()) != nullptr)
        {
            QHash<QString, QString> tags = tagsOf(f, cols);
            OGRGeometry* g0 = f->GetGeometryRef();
            if (g0 && !g0->IsEmpty() && from == QLatin1String("multipolygons"))
            {
                if (!pois.isEmpty() || !areas.isEmpty()) enrich(g0, tags);
                if (haveEnh && tags.contains(QStringLiteral("building")))
                {   // db:code / db:rel / db:floor — data of the nearest enhancer point; it has no priority over OSM (the rules and REL=auto decide)
                    ++enhBuildings;
                    TmcEnhancer::Rec er;
                    if (enhancer.match(g0, enhClaimed, &er))
                    {
                        ++enhMatched;
                        tags.insert(QStringLiteral("db:code"), QString::number(TmcEnhancer::normalizeCode(er.code)));
                        if (er.rel > 0) tags.insert(QStringLiteral("db:rel"), QString::number(er.rel, 'f', 1));
                        if (er.floor > 0) tags.insert(QStringLiteral("db:floor"), QString::number(er.floor));
                    }
                }
                // _area — outline area, m² (degrees -> meters by the latitude of the center; for the "small/large building" thresholds)
                OGREnvelope e; g0->getEnvelope(&e);
                const double latC = 0.5 * (e.MinY + e.MaxY);
                const OGRwkbGeometryType gt0 = wkbFlatten(g0->getGeometryType());
                const double deg2 = gt0 == wkbPolygon ? static_cast<const OGRPolygon*>(g0)->get_Area()
                                  : gt0 == wkbMultiPolygon ? static_cast<const OGRMultiPolygon*>(g0)->get_Area() : 0.0;
                const double a = deg2 * 111320.0 * 111320.0 * std::cos(latC * 3.14159265358979 / 180.0);
                tags.insert(QStringLiteral("_area"), QString::number(a, 'f', 1));
                // _perim — perimeter (with inner rings), m: needed by the floor-based height formulas
                const double cosk = std::cos(latC * 3.14159265358979 / 180.0);
                auto ringLen = [&](const OGRLinearRing* r)
                {
                    double s = 0;
                    for (int i = 0; i + 1 < r->getNumPoints(); ++i)
                        s += std::hypot((r->getX(i + 1) - r->getX(i)) * 111320.0 * cosk, (r->getY(i + 1) - r->getY(i)) * 111320.0);
                    return s;
                };
                auto polyLen = [&](const OGRPolygon* p)
                {
                    double s = p->getExteriorRing() ? ringLen(p->getExteriorRing()) : 0.0;
                    for (int i = 0; i < p->getNumInteriorRings(); ++i) s += ringLen(p->getInteriorRing(i));
                    return s;
                };
                double perim = 0;
                if (gt0 == wkbPolygon) perim = polyLen(static_cast<const OGRPolygon*>(g0));
                else if (gt0 == wkbMultiPolygon)
                {
                    const OGRMultiPolygon* mp = static_cast<const OGRMultiPolygon*>(g0);
                    for (int i = 0; i < mp->getNumGeometries(); ++i) perim += polyLen(static_cast<const OGRPolygon*>(mp->getGeometryRef(i)));
                }
                tags.insert(QStringLiteral("_perim"), QString::number(perim, 'f', 1));
            }
            bool anyRule = false;
            const QVector<OsmClassified> cl = rules.classify(from, tags, 1, &anyRule);
            if (!unmatchedFile.isEmpty() && !anyRule && !tags.contains(QStringLiteral("building")))
            {   // TMC_OSM_UNMATCHED=file: what the rules did NOT cover — a counter by the "main" tag (the first of the list), separately for outlines/lines/points
                static const char* primary[] = { "highway", "railway", "amenity", "shop", "leisure", "landuse", "natural", "waterway", "man_made", "power", "barrier",
                                                 "tourism", "office", "craft", "healthcare", "historic", "public_transport", "place", "aeroway", "emergency", "sport",
                                                 "boundary", "route", "type" };
                QString key;
                for (const char* k : primary)
                {
                    const auto it = tags.constFind(QLatin1String(k));
                    if (it != tags.constEnd() && !it.value().isEmpty()) { key = QLatin1String(k) + QLatin1Char('=') + it.value(); break; }
                }
                ++unmatchedStat[from + QLatin1Char('\t') + (key.isEmpty() ? QStringLiteral("(no main tags)") : key)];
            }
            if (debugOsm && tags.contains(QStringLiteral("building")))
            {   // TMC_OSM_DEBUG=1: one line per building — which tags the rules saw and which layer/code resulted
                QStringList kv;
                for (auto t = tags.constBegin(); t != tags.constEnd(); ++t) if (t.key() != QLatin1String("other_tags")) kv << t.key() + QLatin1Char('=') + t.value();
                kv.sort();
                log(QStringLiteral("  [osm %1] %2 -> %3").arg(tags.value(QStringLiteral("osm_id")), kv.join(QStringLiteral(", ")),
                                                           cl.isEmpty() ? QStringLiteral("(none)") : cl.first().layer + QLatin1Char(' ') + cl.first().fields.value(0).second));
            }
            const bool isBld = from == QLatin1String("multipolygons") && tags.contains(QStringLiteral("building"));
            if (isBld && g0 && !g0->IsEmpty())
            {
                if (cl.isEmpty())
                {   // not recognized by the rules of pass 1 — defer until the "guess" by neighbors (if there are pass-2 rules)
                    if (rules.hasPass2()) deferred << Deferred{ tags, g0->clone() };
                }
                else if (rules.hasPass2())
                {   // a recognized building becomes a "neighbor" for the deferred ones
                    double rx, ry;
                    QString code, rel;
                    for (const auto& fv : cl.first().fields) { if (fv.first == QLatin1String("CODE")) code = fv.second; else if (fv.first == QLatin1String("REL")) rel = fv.second; }
                    if (!code.isEmpty() && !rules.neighborIgnoreCodes().contains(code) && repPointOf(g0, rx, ry))
                    {
                        if (std::isnan(nbLat)) nbLat = ry;
                        Confident c{ rx * 111320.0 * std::cos(nbLat * 3.14159265358979 / 180.0), ry * 111320.0, code, rel.toDouble() };
                        confGrid[cellKeyM(int(std::floor(c.x / nbCell)), int(std::floor(c.y / nbCell)))] << confident.size();
                        confident << c;
                    }
                }
            }
            if (!cl.isEmpty() && g0 && !g0->IsEmpty()) addObjects(cl, g0, tags);
            OGRFeature::DestroyFeature(f);
        }

        // --- pass 2: a "guess" by neighbors and area for buildings not recognized by tags ---
        if (!deferred.isEmpty())
        {
            const double R = rules.neighborRadius();
            int resolved = 0;
            for (Deferred& d : deferred)
            {
                QHash<QString, QString> tags = d.tags;
                double rx, ry;
                tags.insert(QStringLiteral("nb:n"), QStringLiteral("0"));
                if (!std::isnan(nbLat) && repPointOf(d.g, rx, ry))
                {
                    const double px = rx * 111320.0 * std::cos(nbLat * 3.14159265358979 / 180.0), py = ry * 111320.0;
                    const int span = int(std::ceil(R / nbCell));
                    const int cx = int(std::floor(px / nbCell)), cy = int(std::floor(py / nbCell));
                    QHash<QString, double> weight;          // code -> weighted number of neighbors (closer — more important)
                    QHash<QString, int> count;
                    QHash<QString, double> relSum;
                    int total = 0;
                    double nearest = 1e18;
                    for (int ix = cx - span; ix <= cx + span; ++ix)
                        for (int iy = cy - span; iy <= cy + span; ++iy)
                        {
                            const auto it = confGrid.constFind(cellKeyM(ix, iy));
                            if (it == confGrid.constEnd()) continue;
                            for (int ci : it.value())
                            {
                                const Confident& c = confident[ci];
                                const double dist = std::hypot(c.x - px, c.y - py);
                                if (dist > R) continue;
                                weight[c.code] += 1.0 / (1.0 + dist / 20.0);
                                count[c.code] += 1;
                                relSum[c.code] += c.rel;
                                ++total;
                                nearest = std::min(nearest, dist);
                            }
                        }
                    if (total > 0)
                    {
                        QString bestCode;
                        double bestW = -1;
                        for (auto w = weight.constBegin(); w != weight.constEnd(); ++w) if (w.value() > bestW) { bestW = w.value(); bestCode = w.key(); }
                        tags.insert(QStringLiteral("nb:n"), QString::number(total));
                        tags.insert(QStringLiteral("nb:code"), bestCode);
                        tags.insert(QStringLiteral("nb:share"), QString::number(100.0 * count.value(bestCode) / total, 'f', 0));
                        tags.insert(QStringLiteral("nb:rel"), QString::number(relSum.value(bestCode) / std::max(1, count.value(bestCode)), 'f', 1));
                        tags.insert(QStringLiteral("nb:dist"), QString::number(nearest, 'f', 0));
                    }
                }
                const QVector<OsmClassified> cl2 = rules.classify(from, tags, 2);
                if (debugOsm)
                {
                    QStringList kv;
                    for (auto t = tags.constBegin(); t != tags.constEnd(); ++t) if (t.key() != QLatin1String("other_tags")) kv << t.key() + QLatin1Char('=') + t.value();
                    kv.sort();
                    log(QStringLiteral("  [osm %1] (pass 2) %2 -> %3").arg(tags.value(QStringLiteral("osm_id")), kv.join(QStringLiteral(", ")),
                                                                            cl2.isEmpty() ? QStringLiteral("(none)") : cl2.first().layer + QLatin1Char(' ') + cl2.first().fields.value(0).second));
                }
                if (!cl2.isEmpty()) { addObjects(cl2, d.g, tags); ++resolved; }
                delete d.g;
            }
            log(QObject::tr("Buildings without signs: %1, of which recognized by neighbors and area: %2 (recognized neighbors: %3)").arg(deferred.size()).arg(resolved).arg(confident.size()));
            deferred.clear();
        }
    }
    for (AreaObj& a : areas) delete a.g;
    GDALClose(ds);
    if (haveEnh)
        log(QObject::tr("Enhancer: OSM buildings %1, matched with points %2 (used only when the type or height could not be determined from OSM)").arg(enhBuildings).arg(enhMatched));
    if (!unmatchedFile.isEmpty())
    {
        QFile uf(unmatchedFile);
        if (uf.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            QTextStream us(&uf);
            us.setCodec("UTF-8");
            us << "count\tlayer\ttag\n";
            for (auto it = unmatchedStat.constBegin(); it != unmatchedStat.constEnd(); ++it) us << it.value() << '\t' << it.key() << '\n';
        }
    }

    // BUILDING PARTS (building:part): in OSM, for a tower on a podium and other composite buildings the heights are given not by the outline but by its parts (usually higher than the outline).
    // The parts are separate buildings inheriting the type, material and address of the outline; the outline itself shrinks by the area of the parts (a difference), so the volumes do not
    // overlap and every part gets its own height. If the parts cover almost the whole outline, the outline is removed completely.
    {
        MapLayer* bl = out.value(QStringLiteral("bldpol"));
        const int pi = bl ? bl->fieldIndex(QStringLiteral("PART")) : -1;
        if (bl && pi >= 0)
        {
            const int ci = bl->fieldIndex(QStringLiteral("CODE")), ri = bl->fieldIndex(QStringLiteral("REL")), phi = bl->fieldIndex(QStringLiteral("PH"));
            const int mi = bl->fieldIndex(QStringLiteral("MAT")), li = bl->fieldIndex(QStringLiteral("LIFT"));
            const int si = bl->fieldIndex(QStringLiteral("STREET")), ti = bl->fieldIndex(QStringLiteral("TYSTR")), ni = bl->fieldIndex(QStringLiteral("NUMBR"));
            auto polyLike = [](const OGRGeometry* g)
            { const OGRwkbGeometryType t = g ? wkbFlatten(g->getGeometryType()) : wkbUnknown; return t == wkbPolygon || t == wkbMultiPolygon; };
            std::function<double(const OGRGeometry*)> areaOf = [&areaOf](const OGRGeometry* g) -> double
            {   // the result of an intersection/difference may be a line, a point or a set — the area is counted over polygons only
                if (!g || g->IsEmpty()) return 0.0;
                switch (wkbFlatten(g->getGeometryType()))
                {
                case wkbPolygon: return static_cast<const OGRPolygon*>(g)->get_Area();
                case wkbMultiPolygon: return static_cast<const OGRMultiPolygon*>(g)->get_Area();
                case wkbGeometryCollection:
                {
                    double a = 0.0;
                    const OGRGeometryCollection* gc = static_cast<const OGRGeometryCollection*>(g);
                    for (int k = 0; k < gc->getNumGeometries(); ++k) a += areaOf(gc->getGeometryRef(k));
                    return a;
                }
                default: return 0.0;
                }
            };
            QVector<MapFeature*> hosts, parts;
            for (MapFeature* f : bl->features)
            {
                if (!f->g || !polyLike(f->g)) continue;
                if (f->fields.value(pi) == QLatin1String("1")) parts << f; else hosts << f;
            }
            QHash<MapFeature*, QVector<MapFeature*>> partsOf;
            int orphans = 0;
            for (MapFeature* p : parts)
            {
                MapFeature* best = nullptr;
                double bestA = 0.0;
                for (MapFeature* h : hosts)
                {
                    if (h->xmax < p->xmin || h->xmin > p->xmax || h->ymax < p->ymin || h->ymin > p->ymax) continue;
                    if (!h->g->Intersects(p->g)) continue;
                    OGRGeometry* inter = h->g->Intersection(p->g);
                    const double a = areaOf(inter);
                    delete inter;
                    if (a > bestA) { bestA = a; best = h; }
                }
                if (best && bestA >= 0.3 * areaOf(p->g)) partsOf[best] << p; else ++orphans;
            }
            QSet<MapFeature*> removeSet;
            int nHosts = 0, nParts = 0;
            for (auto it = partsOf.constBegin(); it != partsOf.constEnd(); ++it)
            {
                MapFeature* h = it.key();
                const QVector<MapFeature*>& ps = it.value();
                // the union of the parts and the remainder of the outline
                OGRGeometry* uni = nullptr;
                for (MapFeature* p : ps)
                {
                    if (!uni) { uni = p->g->clone(); continue; }
                    OGRGeometry* u = uni->Union(p->g);
                    if (u) { delete uni; uni = u; }
                }
                const double hostA = areaOf(h->g);
                OGRGeometry* rest = uni ? h->g->Difference(uni) : nullptr;
                delete uni;
                const double restA = areaOf(rest);
                if (restA < 12.0 || restA < 0.12 * hostA) { removeSet.insert(h); delete rest; }
                else if (rest && polyLike(rest)) { delete h->g; h->g = rest; h->updateBounds(); }
                else delete rest;
                // inheritance: the largest part gets the address of the outline, the others have none (one plate per building)
                MapFeature* biggest = ps.first();
                for (MapFeature* p : ps) if (areaOf(p->g) > areaOf(biggest->g)) biggest = p;
                for (MapFeature* p : ps)
                {
                    if (ci >= 0) p->fields[ci] = h->fields.value(ci);
                    if (mi >= 0) p->fields[mi] = h->fields.value(mi);
                    if (li >= 0) p->fields[li] = QString();
                    const bool addr = (p == biggest);
                    if (si >= 0) p->fields[si] = addr ? h->fields.value(si) : QString();
                    if (ti >= 0) p->fields[ti] = addr ? h->fields.value(ti) : QString();
                    if (ni >= 0) p->fields[ni] = addr ? h->fields.value(ni) : QString();
                    // the part has no height or floors of its own — it takes the outline height
                    if (ri >= 0 && phi >= 0 && p->fields.value(phi).trimmed().isEmpty()) p->fields[ri] = h->fields.value(ri);
                    ++nParts;
                }
                ++nHosts;
            }
            for (MapFeature* f : bl->features) if (f->fields.value(pi) == QLatin1String("1")) f->fields[pi] = QString();     // the service marks are no longer needed
            if (!removeSet.isEmpty())
            {
                QVector<MapFeature*> keep;
                keep.reserve(bl->features.size());
                for (MapFeature* f : bl->features) { if (removeSet.contains(f)) delete f; else keep << f; }
                bl->features = keep;
            }
            if (nParts > 0 || orphans > 0)
                log(QObject::tr("Building parts (building:part): %1 parts attached to %2 buildings (the outline was replaced by parts for %3), standalone parts without a building: %4")
                        .arg(nParts).arg(nHosts).arg(removeSet.size()).arg(orphans));
        }
    }

    // buildings with a house number but no street (in OSM the street is often given only by an associatedStreet relation): take the nearest named road
    {
        MapLayer* bl = out.value(QStringLiteral("bldpol"));
        MapLayer* rl = out.value(QStringLiteral("rdslin"));
        if (bl && rl)
        {
            const int bs = bl->fieldIndex(QStringLiteral("STREET")), bt = bl->fieldIndex(QStringLiteral("TYSTR")), bn = bl->fieldIndex(QStringLiteral("NUMBR"));
            const int rs = rl->fieldIndex(QStringLiteral("STREET")), rt = rl->fieldIndex(QStringLiteral("TYSTR"));
            QVector<const MapFeature*> named;
            if (rs >= 0) for (const MapFeature* r : rl->features) if (r->g && !r->fields.value(rs).isEmpty()) named << r;
            int filled = 0;
            const double reach = 100.0;                         // projection units (meters, for Mercator about 60 m at our latitudes)
            if (bs >= 0 && bn >= 0 && !named.isEmpty())
                for (MapFeature* b : bl->features)
                {
                    if (!b->g || !b->fields.value(bs).isEmpty() || b->fields.value(bn).isEmpty()) continue;
                    OGRPoint c(0.5 * (b->xmin + b->xmax), 0.5 * (b->ymin + b->ymax));
                    double best = reach;
                    const MapFeature* hit = nullptr;
                    for (const MapFeature* r : named)
                    {
                        if (r->xmax < c.getX() - reach || r->xmin > c.getX() + reach || r->ymax < c.getY() - reach || r->ymin > c.getY() + reach) continue;
                        const double d = r->g->Distance(&c);
                        if (d < best) { best = d; hit = r; }
                    }
                    if (!hit) continue;
                    b->fields[bs] = hit->fields.value(rs);
                    if (bt >= 0 && rt >= 0) b->fields[bt] = hit->fields.value(rt);
                    ++filled;
                }
            if (filled > 0) log(QObject::tr("Addresses: the street was taken from the nearest road for %1 buildings (in OSM they have a house number but no street)").arg(filled));
        }
    }

    for (auto it = out.begin(); it != out.end(); ++it)
    {
        log(QObject::tr("Layer %1: %2 objects").arg(it.value()->name).arg(it.value()->features.size()));
        const int codeNo = it.value()->fieldIndex(QStringLiteral("CODE"));
        if (codeNo >= 0)
        {   // summary by codes: shows quickly how the rules sorted the objects
            QMap<QString, int> byCode;
            for (const MapFeature* o : it.value()->features) ++byCode[o->fields.value(codeNo)];
            QStringList parts;
            for (auto c = byCode.constBegin(); c != byCode.constEnd(); ++c) parts << QStringLiteral("%1 × %2").arg(c.key().isEmpty() ? QStringLiteral("(empty)") : c.key()).arg(c.value());
            log(QObject::tr("  codes: %1").arg(parts.join(QStringLiteral("; "))));
        }
        if (skippedGeom.value(it.key()) > 0)
            mWarnings << QObject::tr("Layer %1: %2 objects skipped — the geometry type did not match the first rule of the layer (use \"as\")").arg(it.key()).arg(skippedGeom.value(it.key()));
        if (it.value()->features.isEmpty()) delete it.value(); else mLayers << it.value();
    }
    if (mLayers.isEmpty()) { if (err) *err = QObject::tr("The OSM rules found no objects"); return false; }
    { GreeneryOptions go = rules.greenery(); if (mGreeneryOff) go.enabled = false; synthesizeGreenery(go); }          // OSM gives noticeably less greenery than our data: top it up (see tmcgreen.cpp)
    return true;
}

bool TmcProject::loadTqt(const QString& file, QString* err)
{
    const QString text = readText(file);
    if (text.isEmpty()) { if (err) *err = QObject::tr("Could not read the project file: %1").arg(file); return false; }
    const QFileInfo fi(file);
    mBaseName = fi.completeBaseName();
    const QDir base = fi.absoluteDir();
    const QStringList lines = text.split(QLatin1Char('\n'));

    struct Entry { bool raster; QString path, name; bool visible; };
    QVector<Entry> entries;
    QString wktText, filterText, filterGrText;
    int layersToRead = 0;
    static const QRegularExpression layerRe(QStringLiteral("^(Shape|Rastr)=\\{\"([^\"]*)\",\"([^\"]*)\"\\}"));
    static const QRegularExpression visRe(QStringLiteral(";Visible=(\\d)"));
    auto braceText = [](const QString& s) -> QString
    {
        const int a = s.indexOf(QLatin1Char('{')), b = s.lastIndexOf(QLatin1Char('}'));
        return (a >= 0 && b > a) ? s.mid(a + 1, b - a - 1) : QString();
    };
    for (QString ln : lines)
    {
        ln = ln.trimmed();
        if (layersToRead > 0)
        {
            const QRegularExpressionMatch m = layerRe.match(ln);
            if (m.hasMatch())
            {
                Entry e;
                e.raster = m.captured(1) == QLatin1String("Rastr");
                e.path = m.captured(2);
                e.name = m.captured(3);
                const QRegularExpressionMatch v = visRe.match(ln);
                e.visible = !v.hasMatch() || v.captured(1) != QLatin1String("0");
                entries << e;
            }
            --layersToRead;
            continue;
        }
        if (ln.startsWith(QLatin1String("Layers="))) layersToRead = ln.mid(7).toInt();
        else if (ln.startsWith(QLatin1String("SpatialFilterGR="))) filterGrText = braceText(ln);
        else if (ln.startsWith(QLatin1String("SpatialFilter="))) filterText = braceText(ln);
        else if (ln.startsWith(QLatin1String("Wkt"))) { const QString t = braceText(ln); if (!t.isEmpty()) wktText = t; }
        else if (ln.startsWith(QLatin1String("Wdf3D="))) mEmbeddedT3d = QByteArray::fromBase64(ln.mid(6).toLatin1());
    }
    if (entries.isEmpty()) { if (err) *err = QObject::tr("The project has no layers (the Layers= line)"); return false; }

    auto resolve = [&](const QString& p) -> QString
    {
        if (QFileInfo(p).isAbsolute() || p.contains(QStringLiteral("PG:"))) return p;
        return base.filePath(p);
    };

    // project coordinate system
    if (!wktText.isEmpty())
    {
        std::unique_ptr<OGRSpatialReference> s(new OGRSpatialReference);
        const QByteArray b = wktText.toUtf8();
        const char* pw = b.constData();
        if (s->importFromWkt(&pw) == OGRERR_NONE)
        {
            s->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            mSrs = std::move(s);
            mCrsFromProject = true;
        }
    }
    if (!mSrs)
    {   // from the first vector layer (.prj), as when a project is created in tqt
        GDALAllRegister();
        for (const Entry& e : entries)
        {
            if (e.raster) continue;
            GDALDataset* ds = static_cast<GDALDataset*>(GDALOpenEx(resolve(e.path).toUtf8().constData(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
            if (ds && ds->GetLayer(0) && ds->GetLayer(0)->GetSpatialRef())
            {
                mSrs.reset(ds->GetLayer(0)->GetSpatialRef()->Clone());
                mSrs->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
                if (mSrs->IsGeographic() && epsgOf(*mSrs) == 4326)
                {
                    std::unique_ptr<OGRSpatialReference> def(new OGRSpatialReference);
                    if (def->SetFromUserInput(defaultCrsText().toUtf8().constData()) == OGRERR_NONE)
                    {
                        def->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
                        mSrs = std::move(def);
                    }
                }
            }
            if (ds) GDALClose(ds);
            if (mSrs) break;
        }
    }
    if (!mSrs) { if (err) *err = QObject::tr("Could not determine the project coordinate system"); return false; }
    { char* w = nullptr; mSrs->exportToWkt(&w); mWkt = QString::fromUtf8(w); CPLFree(w); }

    // spatial filter (in the project CRS; SpatialFilterGR is in WGS84 degrees)
    auto importFilter = [&](const QString& wkt, bool geographic)
    {
        if (wkt.isEmpty() || mFilter) return;
        const QByteArray b = wkt.toUtf8();
        const char* pw = b.constData();
        OGRGeometry* g = nullptr;
        if (OGRGeometryFactory::createFromWkt(&pw, nullptr, &g) != OGRERR_NONE || !g) { delete g; return; }
        if (geographic)
        {
            OGRSpatialReference wgs;
            wgs.importFromEPSG(4326);
            wgs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            std::unique_ptr<OGRCoordinateTransformation> ct(OGRCreateCoordinateTransformation(&wgs, mSrs.get()));
            if (!ct || g->transform(ct.get()) != OGRERR_NONE) { delete g; return; }
        }
        mFilter = g;
    };
    importFilter(filterText, false);
    importFilter(filterGrText, true);
    if (mFilter) log(QObject::tr("The project spatial filter was applied"));

    for (const Entry& e : entries)
    {
        const QString path = resolve(e.path);
        if (e.raster) { addRaster(path); continue; }
        const QString ln = e.name.isEmpty() ? QFileInfo(path).completeBaseName() : e.name;
        if (!mWanted.isEmpty() && !mWanted.contains(suffixOf(ln))) continue;
        if (!e.visible) { log(QObject::tr("Layer %1 is switched off in the project — skipped (as in the tqt 3D window)").arg(ln)); continue; }
        MapLayer* l = readLayer(path, e.name);
        if (l) mLayers << l;
    }
    if (mLayers.isEmpty()) { if (err) *err = QObject::tr("No needed project layer was read"); return false; }
    return true;
}
