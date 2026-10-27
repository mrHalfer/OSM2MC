#include "osm/tmcosmrules.h"
#include <algorithm>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

bool OsmRules::isSpatialKey(const QString& key)
{
    return key.startsWith(QLatin1String("poi:")) || key.startsWith(QLatin1String("area:"));
}

QString OsmRules::baseKey(const QString& key)
{
    if (key.startsWith(QLatin1String("poi:"))) return key.mid(4);
    if (key.startsWith(QLatin1String("area:"))) return key.mid(5);
    return key;
}

//! The first number of a string ("12 m", "3,5") — ok=false if there are no numbers
static double firstNumber(const QString& s, bool* ok)
{
    static const QRegularExpression numRe(QStringLiteral("-?\\d+(?:[.,]\\d+)?"));
    const auto m = numRe.match(s);
    if (!m.hasMatch()) { *ok = false; return 0; }
    *ok = true;
    return QString(m.captured(0)).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
}

static bool compareNum(OsmCondition::Kind k, double v, double t)
{
    switch (k)
    {
    case OsmCondition::Gt: return v > t;
    case OsmCondition::Ge: return v >= t;
    case OsmCondition::Lt: return v < t;
    case OsmCondition::Le: return v <= t;
    default: return false;
    }
}

bool OsmCondition::matches(const QHash<QString, QString>& tags) const
{
    const auto it = tags.constFind(key);
    const bool has = it != tags.constEnd() && !it.value().isEmpty();
    const QString v = has ? it.value() : QString();
    if (OsmRules::isSpatialKey(key))
    {   // poi:/area: — the values are collected through ";" from all intersecting objects: the condition holds if any of them fits
        const QStringList vs = v.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        switch (kind)
        {
        case Any:       return has;
        case Absent:    return !has;
        case Equals:    return vs.contains(values.value(0));
        case NotEquals: return has && !vs.contains(values.value(0));
        case OneOf:     for (const QString& s : vs) if (values.contains(s)) return true; return false;
        case NoneOf:    if (!has) return false; for (const QString& s : vs) if (values.contains(s)) return false; return true;
        case Regex:     for (const QString& s : vs) if (re.match(s).hasMatch()) return true; return false;
        case Gt: case Ge: case Lt: case Le:
            for (const QString& s : vs) { bool ok; const double n = firstNumber(s, &ok); if (ok && compareNum(kind, n, number)) return true; }
            return false;
        }
        return false;
    }
    switch (kind)
    {
    case Any:       return has;
    case Absent:    return !has;
    case Equals:    return has && v == values.value(0);
    case NotEquals: return has && v != values.value(0);   // "!v": the tag EXISTS and is not equal to v (so that "building: !yes" does not catch objects with no building at all)
    case OneOf:     return has && values.contains(v);
    case NoneOf:    return has && !values.contains(v);    // ["!a","!b"]: the tag exists and is not from the list
    case Regex:     return has && re.match(v).hasMatch();
    case Gt: case Ge: case Lt: case Le:
    {
        bool ok = false;
        const double n = has ? firstNumber(v, &ok) : 0;
        return ok && compareNum(kind, n, number);        // no tag or no number in it — the condition is not met
    }
    }
    return false;
}

static bool parseCondition(const QString& key, const QJsonValue& jv, OsmCondition& c, QString* err)
{
    c.key = key;
    if (jv.isNull() || jv.isUndefined()) { c.kind = OsmCondition::Absent; return true; }
    if (jv.isArray())
    {
        const QJsonArray a = jv.toArray();
        QStringList plain, negated;
        for (const QJsonValue& e : a)
        {
            const QString s = e.toString();
            if (s.startsWith(QLatin1Char('!'))) negated << s.mid(1); else plain << s;
        }
        if (!plain.isEmpty() && !negated.isEmpty()) { if (err) *err = QObject::tr("tag %1: values and values with \"!\" cannot be mixed in a list").arg(key); return false; }
        if (!negated.isEmpty()) { c.kind = OsmCondition::NoneOf; c.values = negated; }
        else { c.kind = OsmCondition::OneOf; c.values = plain; }
        return true;
    }
    const QString s = jv.isBool() ? (jv.toBool() ? QStringLiteral("yes") : QStringLiteral("no")) : jv.toVariant().toString();
    if (s == QLatin1String("*")) c.kind = OsmCondition::Any;
    else if (s == QLatin1String("-")) c.kind = OsmCondition::Absent;
    else if (s.startsWith(QLatin1String(">=")) || s.startsWith(QLatin1String("<=")) || s.startsWith(QLatin1Char('>')) || s.startsWith(QLatin1Char('<')))
    {
        const bool eq = s.size() > 1 && s.at(1) == QLatin1Char('=');
        const QString num = s.mid(eq ? 2 : 1).trimmed();
        bool ok = false;
        c.number = QString(num).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
        if (!ok) { if (err) *err = QObject::tr("tag %1: a number is expected after \"%2\"").arg(key, s.left(eq ? 2 : 1)); return false; }
        c.kind = s.at(0) == QLatin1Char('>') ? (eq ? OsmCondition::Ge : OsmCondition::Gt) : (eq ? OsmCondition::Le : OsmCondition::Lt);
    }
    else if (s.startsWith(QLatin1Char('!'))) { c.kind = OsmCondition::NotEquals; c.values << s.mid(1); }
    else if (s.startsWith(QLatin1Char('~')))
    {
        c.kind = OsmCondition::Regex;
        c.re = QRegularExpression(s.mid(1));
        if (!c.re.isValid()) { if (err) *err = QObject::tr("tag %1: invalid regular expression \"%2\"").arg(key, s.mid(1)); return false; }
    }
    else { c.kind = OsmCondition::Equals; c.values << s; }
    return true;
}

bool OsmRules::load(const QString& path, QString* err)
{
    mRules.clear();
    mPath = path;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (err) *err = QObject::tr("Could not open the OSM rules file: %1").arg(path); return false; }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull() || !doc.isObject()) { if (err) *err = QObject::tr("OSM rules file %1: JSON error (%2, position %3)").arg(path, pe.errorString()).arg(pe.offset); return false; }

    {
        const QJsonObject g = doc.object().value(QStringLiteral("greenery")).toObject();
        mGreen = GreeneryOptions();
        mGreen.enabled = g.value(QStringLiteral("enabled")).toBool(true);
        mGreen.yards = g.value(QStringLiteral("yards")).toBool(true);
        mGreen.privateTaiga = g.value(QStringLiteral("private_taiga")).toBool(true);
        mGreen.forestShare = g.value(QStringLiteral("forest_share")).toDouble(mGreen.forestShare);
        mGreen.parksPerKm2 = g.value(QStringLiteral("parks_per_km2")).toDouble(mGreen.parksPerKm2);
        mGreen.wildForests = g.value(QStringLiteral("wild_forests")).toBool(true);
        mGreen.wildMinAreaM2 = g.value(QStringLiteral("wild_min_area_m2")).toDouble(mGreen.wildMinAreaM2);
        mGreen.wildBuildingM = g.value(QStringLiteral("wild_building_m")).toDouble(mGreen.wildBuildingM);
        mGreen.wildBlockM = g.value(QStringLiteral("wild_block_m")).toDouble(mGreen.wildBlockM);
        mGreen.wildPatchM2 = g.value(QStringLiteral("wild_patch_m2")).toDouble(mGreen.wildPatchM2);
        mGreen.wildApartM = g.value(QStringLiteral("wild_apart_m")).toDouble(mGreen.wildApartM);
        mGreen.wildMaxCount =g.value(QStringLiteral("wild_max_count")).toInt(mGreen.wildMaxCount);
        mGreen.cellM = std::max(1.0, g.value(QStringLiteral("cell_m")).toDouble(mGreen.cellM));
    }
    const QJsonObject nb = doc.object().value(QStringLiteral("neighbors")).toObject();
    mNeighborRadius = nb.value(QStringLiteral("radius_m")).toDouble(60.0);
    mNeighborIgnore.clear();
    for (const QJsonValue& v : nb.value(QStringLiteral("ignore_codes")).toArray()) mNeighborIgnore << v.toVariant().toString();

    const QJsonArray arr = doc.object().value(QStringLiteral("rules")).toArray();
    int idx = 0;
    for (const QJsonValue& rv : arr)
    {
        ++idx;
        const QJsonObject o = rv.toObject();
        OsmRule r;
        r.note = o.value(QStringLiteral("note")).toString();
        r.from = o.value(QStringLiteral("from")).toString(QStringLiteral("multipolygons")).toLower();
        r.layer = o.value(QStringLiteral("layer")).toString().toLower();
        r.as = o.value(QStringLiteral("as")).toString().toLower();
        r.cont = o.value(QStringLiteral("continue")).toBool(false);
        r.pass = o.value(QStringLiteral("pass")).toInt(1);
        if (r.pass != 1 && r.pass != 2) { if (err) *err = QObject::tr("Rule %1: pass must be 1 or 2").arg(idx); return false; }
        if (r.layer.isEmpty()) { if (err) *err = QObject::tr("Rule %1: layer is not specified").arg(idx); return false; }
        if (r.from != QLatin1String("multipolygons") && r.from != QLatin1String("lines") && r.from != QLatin1String("points"))
        { if (err) *err = QObject::tr("Rule %1: from must be multipolygons, lines or points").arg(idx); return false; }
        if (!r.as.isEmpty() && r.as != QLatin1String("polygon") && r.as != QLatin1String("line")
            && !(r.as.startsWith(QLatin1String("square:")) && r.as.mid(7).toDouble() > 0)
            && !(r.as.startsWith(QLatin1String("crosswalk:")) && r.as.mid(10).toDouble() > 0))
        { if (err) *err = QObject::tr("Rule %1: as must be polygon, line, square:<side, m> or crosswalk:<length, m>").arg(idx); return false; }
        const QJsonObject when = o.value(QStringLiteral("when")).toObject();
        for (auto it = when.constBegin(); it != when.constEnd(); ++it)
        {
            OsmCondition c;
            QString ce;
            if (!parseCondition(it.key(), it.value(), c, &ce)) { if (err) *err = QObject::tr("Rule %1: %2").arg(idx).arg(ce); return false; }
            r.conds << c;
        }
        const QJsonObject fields = o.value(QStringLiteral("fields")).toObject();
        for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
            r.fields << qMakePair(it.key(), it.value().isDouble() ? QString::number(it.value().toDouble(), 'g', 15) : it.value().toString());
        mRules << r;
    }
    return true;
}

QStringList OsmRules::tagKeys() const
{
    QSet<QString> keys;
    static const QRegularExpression tplRe(QStringLiteral("\\{([^}|]+)(?:\\|[^}]*)?\\}"));
    for (const OsmRule& r : mRules)
    {
        for (const OsmCondition& c : r.conds) keys.insert(baseKey(c.key));
        for (const auto& fv : r.fields)
        {
            auto m = tplRe.globalMatch(fv.second);
            while (m.hasNext())
                for (const QString& k : m.next().captured(1).split(QLatin1Char(','))) keys.insert(baseKey(k.trimmed()));
        }
    }
    keys.remove(QStringLiteral("osm_id"));
    keys.remove(QStringLiteral("_area"));           // a computed tag: the outline area, m²
    for (const QString& k : keys.values())
        if (k.startsWith(QLatin1String("nb:")) || k.startsWith(QLatin1String("db:"))) keys.remove(k);     // computed tags: neighbors, the enhancer
    QStringList l = keys.values();
    l.sort();
    return l;
}

QStringList OsmRules::spatialKeys(const QString& prefix) const
{
    QSet<QString> keys;
    static const QRegularExpression tplRe(QStringLiteral("\\{([^}|]+)(?:\\|[^}]*)?\\}"));
    auto take = [&](const QString& k) { if (k.startsWith(prefix)) keys.insert(k.mid(prefix.size())); };
    for (const OsmRule& r : mRules)
    {
        for (const OsmCondition& c : r.conds) take(c.key);
        for (const auto& fv : r.fields)
        {
            auto m = tplRe.globalMatch(fv.second);
            while (m.hasNext())
                for (const QString& k : m.next().captured(1).split(QLatin1Char(','))) take(k.trimmed());
        }
    }
    QStringList l = keys.values();
    l.sort();
    return l;
}

QStringList OsmRules::sourceLayers() const
{
    QSet<QString> s;
    for (const OsmRule& r : mRules) s.insert(r.from);
    QStringList l = s.values();
    l.sort();
    return l;
}

QStringList OsmRules::fieldNames(const QString& layer) const
{
    QStringList l;
    for (const OsmRule& r : mRules)
        if (r.layer == layer)
            for (const auto& fv : r.fields)
                if (!l.contains(fv.first)) l << fv.first;
    return l;
}

void OsmRules::parseHstore(const QString& s, QHash<QString, QString>& tags)
{
    static const QRegularExpression re(QStringLiteral("\"((?:[^\"\\\\]|\\\\.)*)\"=>\"((?:[^\"\\\\]|\\\\.)*)\""));
    auto unesc = [](QString v) { v.replace(QStringLiteral("\\\""), QStringLiteral("\"")); v.replace(QStringLiteral("\\\\"), QStringLiteral("\\")); return v; };
    auto m = re.globalMatch(s);
    while (m.hasNext())
    {
        const auto mm = m.next();
        const QString k = unesc(mm.captured(1));
        if (!tags.contains(k)) tags.insert(k, unesc(mm.captured(2)));
    }
}

//! A value template. Variants separated by "||" are tried from left to right — the first non-empty one is taken: "{height|num}||{building:levels|num*3}||9".
QString OsmRules::evalTemplate(const QString& tpl, const QHash<QString, QString>& tags)
{
    const QStringList alts = tpl.split(QStringLiteral("||"));
    for (int i = 0; i < alts.size(); ++i)
    {
        const QString v = evalOne(alts[i], tags).trimmed();
        if (!v.isEmpty() || i == alts.size() - 1) return v;
    }
    return QString();
}

//! One variant: "text {tag}", "{tag1,tag2}" — the first non-empty, "{tag|num}" — the first number, "{tag|int}" — the integer part, "{tag|num*3}" — the number × 3
QString OsmRules::evalOne(const QString& tpl, const QHash<QString, QString>& tags)
{
    QString out;
    int i = 0;
    while (i < tpl.size())
    {
        const int a = tpl.indexOf(QLatin1Char('{'), i);
        const int b = a < 0 ? -1 : tpl.indexOf(QLatin1Char('}'), a);
        if (a < 0 || b < 0) { out += tpl.mid(i); break; }
        out += tpl.mid(i, a - i);
        const QString inner = tpl.mid(a + 1, b - a - 1);
        const QString keysPart = inner.section(QLatin1Char('|'), 0, 0);
        const QString mode = inner.contains(QLatin1Char('|')) ? inner.section(QLatin1Char('|'), 1).trimmed() : QString();
        QString val;
        for (const QString& k : keysPart.split(QLatin1Char(',')))
        {
            const auto it = tags.constFind(k.trimmed());
            if (it != tags.constEnd() && !it.value().isEmpty()) { val = it.value(); break; }
        }
        if (mode.startsWith(QLatin1String("num")) || mode.startsWith(QLatin1String("int")))
        {   // "num", "int", "num*3" (multiply by 3, for example floors -> meters)
            const double mul = mode.contains(QLatin1Char('*')) ? mode.section(QLatin1Char('*'), 1).trimmed().toDouble() : 1.0;
            bool ok = false;
            const double n = firstNumber(val, &ok) * mul;
            if (ok) val = mode.startsWith(QLatin1String("int")) ? QString::number(int(n)) : QString::number(n, 'g', 8);
            else val.clear();
        }
        out += val;
        i = b + 1;
    }
    return out;
}

bool OsmRules::hasPass2() const
{
    for (const OsmRule& r : mRules) if (r.pass == 2) return true;
    return false;
}

QVector<OsmClassified> OsmRules::classify(const QString& from, const QHash<QString, QString>& tags, int pass, bool* matched) const
{
    QVector<OsmClassified> res;
    if (matched) *matched = false;
    for (const OsmRule& r : mRules)
    {
        if (r.from != from || r.pass != pass) continue;
        bool ok = true;
        for (const OsmCondition& c : r.conds)
            if (!c.matches(tags)) { ok = false; break; }
        if (!ok) continue;
        if (matched) *matched = true;
        if (r.layer == QLatin1String("-"))          // layer "-": the object is dropped (and the rules are not checked further)
        {
            if (!r.cont) break;
            continue;
        }
        OsmClassified c;
        c.layer = r.layer;
        c.as = r.as;
        for (const auto& fv : r.fields) c.fields << qMakePair(fv.first, evalTemplate(fv.second, tags));
        res << c;
        if (!r.cont) break;
    }
    return res;
}
