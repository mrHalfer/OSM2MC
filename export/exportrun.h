#pragma once
// The body of McWorldExporter::Export moved out of mcworldexporter.cpp:
// ExportRun keeps the call parameters and run() builds the whole world.
#include "export/mcworldexporter.h"
#include <QString>
#include <QVector>
#include <atomic>

class ExportRun
{
public:
    ExportRun(const QVector<McWorldExporter::McObject>& objectsIn_,
              const McWorldExporter::HeightSampler& heightAt_,
              const QString& path_,
              const QString& levelName_,
              const std::atomic_bool* cancel_,
              McWorldExporter::Format format_,
              const McWorldExporter::ProgressFn& progress_,
              const McWorldExporter::WorldOptions& world_);

    // Builds the world; returns the result (the former McWorldExporter::Export body).
    McWorldExporter::Result run();

private:
    const QVector<McWorldExporter::McObject>& objectsIn;
    const McWorldExporter::HeightSampler& heightAt;
    const QString& path;
    const QString& levelName;
    const std::atomic_bool* cancel;
    McWorldExporter::Format format;
    const McWorldExporter::ProgressFn& progress;
    const McWorldExporter::WorldOptions& world;
};
