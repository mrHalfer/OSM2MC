#pragma once
// Parsing of the 3D model settings (.t3d or the JSON from the Wdf3D= line of a tqt project) into builder sources.
// A mirror of the tqt 3D dialog's applySettingsJson() + settings(), without widgets.
#include <QString>
#include <QVector>
#include <QByteArray>
#include "scene/modelbuilder.h"

struct T3dSource
{
    QString layer;                                  //!< the layer name without a schema/prefix (what stands after the last dot), not lower-cased
    ModelSettings::VectorSource vs;            //!< vs.layer is filled in later, when bound to a project layer
};

struct T3dSettings
{
    double zExaggeration = 1.0;
    QVector<T3dSource> sources;                     //!< in file order (colorIndex and the object order depend on it)
};

//! JSON -> settings. false + err — not JSON / no vectorSources.
bool parseT3dSettings(const QByteArray& json, T3dSettings& out, QString* err);

//! The layer name suffix (after the last dot) — the same technique as StripSchema() in tqt
QString t3dStripSchema(const QString& layerName);
