#pragma once
// Assembling the exporter object list (McObject) from the loaded project and the 3D settings.
// A port of the object collection from the tqt 3D window, exportMinecraftSave() (tqt, tqt): the object kind by layer suffix, MAT/CODE/LIFT/address,
// merging of elevator shafts, rebuilding each object's mesh with the same ModelBuilder::BuildSingleObjectMesh, the average over vertices as the color.
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <atomic>
#include "export/mcworldexporter.h"
#include "input/t3dsettings.h"
#include "input/tmcproject.h"

struct McSceneOptions
{
    double unitsToMetersX = 1.0;     //!< meters per map unit along X (1 — the units are already meters)
    double unitsToMetersY = 1.0;
};

struct McSceneResult
{
    QVector<McWorldExporter::McObject> objects;
    McWorldExporter::HeightSampler sampler;        //!< empty — flat ground
    QStringList warnings;
    double centerX = 0, centerY = 0;                 //!< the center of the objects in map units (the player spawn point)
    bool cancelled = false;
};

//! Collect the objects. progress(done, total) — as the meshes are rebuilt; cancel — the cancellation flag (may be nullptr).
//! false + err — nothing to export (no layer from the settings was found in the project, etc.).
bool buildMcScene(const TmcProject& project, const T3dSettings& settings, const McSceneOptions& opt, McSceneResult& out,
                  const std::function<void(int, int)>& progress, const std::atomic_bool* cancel, QString* err);

//! How many meters there are in a map coordinate unit along X and Y at the point (x, y) of the projection given by WKT (as in the tqt 3D window).
bool metersPerMapUnit(const QString& wkt, double x, double y, double& mx, double& my);

//! The final exporter string ("item; item; …") -> text by categories, one line per item.
QString formatMcExportInfo(const QString& info);
