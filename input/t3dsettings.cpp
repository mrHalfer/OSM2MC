#include "input/t3dsettings.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QColor>

QString t3dStripSchema(const QString& name)
{
    const int dotPos = name.lastIndexOf(QLatin1Char('.'));
    return dotPos >= 0 ? name.mid(dotPos + 1) : name;
}

static ModelOutlineMode outlineFromMask(int mask)
{
    if (mask & int(ModelEffect::OutlineDark)) return ModelOutlineMode::Dark;
    if (mask & int(ModelEffect::OutlineLight)) return ModelOutlineMode::Light;
    return ModelOutlineMode::None;
}

bool parseT3dSettings(const QByteArray& json, T3dSettings& out, QString* err)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (!doc.isObject())
    {
        if (err) *err = QObject::tr("The 3D settings file is not JSON: %1").arg(pe.errorString());
        return false;
    }
    const QJsonObject o = doc.object();
    out = T3dSettings();
    out.zExaggeration = o["zExaggeration"].toDouble(1.0);

    // old files (effectFormat < 2): "effect" holds the index of an item, not a bit mask; 3 (Lamps) -> the RadialGlow bit
    const bool legacyEffectFormat = o["effectFormat"].toInt(1) < 2;

    for (const QJsonValue& v : o["vectorSources"].toArray())
    {
        const QJsonObject ro = v.toObject();
        T3dSource src;
        src.layer = t3dStripSchema(ro["layer"].toString());
        if (src.layer.isEmpty()) continue;

        ModelSettings::VectorSource vs;
        const bool isGround = ro["isGround"].toBool();
        const QString field = isGround ? QString() : ro["field"].toString();
        if (!isGround && field.isEmpty()) continue;                       // as in the tqt 3D dialog, settings(): a row without a height field is skipped
        vs.noExtrusion = isGround;
        vs.heightField = field;
        vs.heightMultiplier = ro["multiplier"].toDouble(1.0);
        vs.colorMode = ModelColorMode(ro["colorMode"].toInt(0));
        vs.singleColor = QColor(ro["singleColor"].toString(QStringLiteral("#cc5a4d")));
        vs.gradientLowColor = QColor(ro["color1"].toString(QStringLiteral("#469a56")));
        vs.gradientHighColor = QColor(ro["color2"].toString(QStringLiteral("#be3c37")));
        int effectValue = ro["effect"].toInt(0);
        if (legacyEffectFormat && effectValue == 3) effectValue = int(ModelEffect::RadialGlow);
        vs.effect = ModelEffects(effectValue);
        vs.lineWidth = ro["lineWidth"].toDouble(3.0);
        vs.extraExtrusion = ro["extraExtrusion"].toDouble(0.0);
        vs.outline = outlineFromMask(effectValue);
        vs.liftField = ro["liftField"].toString();
        vs.liftMultiplier = ro["liftMultiplier"].toDouble(1.0);
        vs.liftAdd = ro["liftAdd"].toDouble(0.0);
        vs.modelField = ro["modelField"].toString();
        vs.absorptionMode = ModelAbsorptionMode(ro["absorptionMode"].toInt(int(ModelAbsorptionMode::PerLayer)));
        vs.absorptionValue = ro["absorptionValue"].toDouble(20.0);
        vs.absorptionColumn = ro["absorptionColumn"].toString();
        vs.absorptionDefault = ro["absorptionDefault"].toDouble(20.0);
        vs.absorptionMaterial = ro["absorptionMaterial"].toObject().toVariantMap();
        src.vs = vs;
        out.sources << src;
    }
    if (out.sources.isEmpty())
    {
        if (err) *err = QObject::tr("The 3D settings have no vector source (vectorSources)");
        return false;
    }
    return true;
}
