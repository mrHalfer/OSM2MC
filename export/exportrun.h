#pragma once
// Builds the world: the body of McWorldExporter::Export moved out of
// mcworldexporter.cpp (the ExportRun class itself lives in exportrun.cpp).
#include "export/mcworldexporter.h"
#include <QString>
#include <QVector>
#include <atomic>

McWorldExporter::Result exportWorld(const QVector<McWorldExporter::McObject>& objectsIn,
                                    const McWorldExporter::HeightSampler& heightAt,
                                    const QString& path,
                                    const QString& levelName,
                                    const std::atomic_bool* cancel,
                                    McWorldExporter::Format format,
                                    const McWorldExporter::ProgressFn& progress,
                                    const McWorldExporter::WorldOptions& world);
