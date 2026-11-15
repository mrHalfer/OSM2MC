#pragma once
#include <QByteArray>
#include <QString>
#include <functional>

// The "tmc_tall" world datapack: overrides minecraft:dimension_type/overworld.json so the server builds the
// overworld as kWorldMinY..kWorldMaxY (see export/worldy.h) instead of the vanilla -64..319. The dimension_type
// JSON schema changed between the 1.21.x and the 26.x game formats, hence the version overlays in the pack.
void addTallWorldDatapack(const std::function<void(const QString&, const QByteArray&)>& addEntry);
