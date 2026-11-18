#include "tmcrulesrepo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// The hardcoded layer list and its order (the old default.t3d vectorSources order; milpol+pripol
// merged into terpol at pripol's place; vgbpol — last, as in the old file). The pipeline iterates
// sources in exactly this order — a shuffle would drift the world (object order feeds the seeded
// decisions). vgbpol is created synthetically by the vegetation top-up, but the old .t3d listed it
// as a source, so it stays enumerated.
static const char* const kLayerOrder[] = {
    "hdrlin", "rdslin", "rrslin", "bldpol", "blkpol", "csrpol", "csrpnt", "csrlin",
    "hdtpnt", "rdcpnt", "rdcpol", "hdrpol", "vegpol", "mrspol", "lcapol", "terpol", "vgbpol",
};

TmcRulesRepo& TmcRulesRepo::instance()
{
    static TmcRulesRepo repo;
    return repo;
}

const TmcRulesRepo::Layer* TmcRulesRepo::layer(const QString& name) const
{
    const int i = byName_.value(name, -1);
    return i >= 0 ? &layers_[i] : nullptr;
}

const TmcRulesRepo::Object* TmcRulesRepo::object(const QString& layerName, int code) const
{
    const Layer* l = layer(layerName);
    if (!l) return nullptr;
    const int i = l->byCode.value(code, -1);
    return i >= 0 ? &l->objects[i] : nullptr;
}

const TmcRulesRepo::Object* TmcRulesRepo::objectByCode(int code) const
{
    return byCode_.value(code, nullptr);
}

QString TmcRulesRepo::discover(const QString& explicitDir)
{
    QStringList cand;
    if (!explicitDir.isEmpty())
        cand << explicitDir;
    const QString marker = QStringLiteral("bldpol.json");
    if (!explicitDir.isEmpty())
        cand << QDir(explicitDir).filePath(QStringLiteral("rules"));
    const QString app = QCoreApplication::applicationDirPath();
    cand << app + QStringLiteral("/rules") << QDir::currentPath() + QStringLiteral("/rules");
    for (const QString& d : cand)
        if (QFileInfo::exists(QDir(d).filePath(marker)))
            return QDir(d).absolutePath();
    return QString();
}

static TmcRulesRepo::Family parseFamily(const QString& s, bool* ok)
{
    if (s == QLatin1String("Point")) { *ok = true; return TmcRulesRepo::FamilyPoint; }
    if (s == QLatin1String("Line"))  { *ok = true; return TmcRulesRepo::FamilyLine; }
    if (s == QLatin1String("Poly"))  { *ok = true; return TmcRulesRepo::FamilyPoly; }
    *ok = false;
    return TmcRulesRepo::FamilyPoly;
}

bool TmcRulesRepo::load(const QString& rulesDir, QString* err, QStringList* warnings)
{
    layers_.clear();
    byName_.clear();
    order_.clear();
    byCode_.clear();
    dir_.clear();

    QDir dir(rulesDir);
    if (!dir.exists())
    {
        if (err) *err = QObject::tr("rules folder not found: %1").arg(rulesDir);
        return false;
    }

    QStringList wanted;                                     // the fixed layer list, in the enumeration order
    for (const char* const* n = kLayerOrder; n != kLayerOrder + int(sizeof(kLayerOrder) / sizeof(*kLayerOrder)); ++n)
        wanted << QLatin1String(*n);

    QStringList seen = dir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files, QDir::Name);
    for (const QString& w : wanted)
    {
        const QString fn = w + QStringLiteral(".json");
        if (!seen.removeAll(fn))                            // read it once; the rest are extras
        {
            if (err) *err = QObject::tr("rules catalog is incomplete: %1 is missing").arg(fn);
            return false;
        }
        QFile f(dir.filePath(fn));
        if (!f.open(QIODevice::ReadOnly))
        {
            if (err) *err = QObject::tr("cannot read %1").arg(fn);
            return false;
        }
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
        if (!doc.isObject())
        {
            if (err) *err = QObject::tr("%1: JSON parse error: %2").arg(fn, pe.errorString());
            return false;
        }
        const QJsonObject root = doc.object();

        Layer lay;
        lay.name = w;
        const QString declared = root.value(QStringLiteral("Layer")).toString();
        if (!declared.isEmpty() && declared != w && warnings)
            warnings->append(QObject::tr("%1: the Layer key says '%2', the file name wins").arg(fn, declared));
        bool famOk = false;
        lay.family = parseFamily(root.value(QStringLiteral("Family")).toString(), &famOk);
        if (!famOk && warnings)
            warnings->append(QObject::tr("%1: unknown Family, treating as Poly").arg(fn));
        lay.widthBase = root.value(QStringLiteral("WidthBase")).toDouble();
        lay.materialColumn = root.value(QStringLiteral("MaterialColumn")).toString();
        lay.dbCodes = root.value(QStringLiteral("DbCodes")).toBool();

        const QJsonArray objs = root.value(QStringLiteral("Objects")).toArray();
        for (const QJsonValue& v : objs)
        {
            const QJsonObject o = v.toObject();
            Object d;
            d.uid = o.value(QStringLiteral("Uid")).toString();
            d.code = o.value(QStringLiteral("Code")).toInt();
            if (d.code <= 0)
            {
                if (warnings) warnings->append(QObject::tr("%1: an object without a Code is skipped").arg(fn));
                continue;
            }
            d.name = o.value(QStringLiteral("Name")).toString();
            d.effect = o.value(QStringLiteral("Effect")).toString();
            d.model = o.value(QStringLiteral("Model")).toInt();
            const QJsonObject rel = o.value(QStringLiteral("Rel")).toObject();
            if (rel.contains(QStringLiteral("Value")))
            {
                d.relMode = RelFixed;
                d.relValue = rel.value(QStringLiteral("Value")).toDouble();
            }
            else if (!rel.isEmpty())
            {
                d.relMode = RelAuto;
                d.relColumn = rel.value(QStringLiteral("Column")).toString();
                if (rel.contains(QStringLiteral("Default")))          // an explicit zero is a height too (lawns, meadows)
                {
                    d.relHasDefault = true;
                    d.relDefault = rel.value(QStringLiteral("Default")).toDouble();
                }
                if (d.relColumn == QLatin1String("REL"))
                    lay.heightColumn = true;                  // the layer extrudes: its height comes from a data column
            }
            d.internal = o.value(QStringLiteral("Internal")).toBool();
            d.props = o.value(QStringLiteral("Props")).toObject().toVariantMap();

            if (lay.byCode.contains(d.code))
            {
                if (warnings) warnings->append(QObject::tr("%1: code %2 is described twice, the first one wins").arg(fn).arg(d.code));
                continue;
            }
            lay.byCode.insert(d.code, lay.objects.size());
            if (byCode_.contains(d.code) && warnings)
                warnings->append(QObject::tr("%1: code %2 also lives on another layer, layer lookup wins").arg(fn).arg(d.code));
            lay.objects.append(d);
        }

        byName_.insert(lay.name, layers_.size());
        layers_.append(lay);
        order_ << lay.name;
    }

    // index codes after all layers are in (pointers stay valid: layers_ is not touched below)
    for (const Layer& l : layers_)
        for (const Object& d : l.objects)
            byCode_.insert(d.code, &d);

    for (const QString& extra : seen)                       // a hand-made file: styles for an unknown layer do not exist
    {
        if (extra == QLatin1String("osmrules.json"))       // the "OSM tags -> layer/code" rules live here since v2.0
            continue;
        if (warnings) warnings->append(QObject::tr("%1: unknown layer file ignored (the layer list is fixed)").arg(extra));
    }

    dir_ = dir.absolutePath();
    return true;
}
