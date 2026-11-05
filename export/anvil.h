#pragma once
// The serialization of the export: chunk NBT (the 1.13..1.16.5 and 26.x variants),
// entity chunk NBT, level.dat, world_gen_settings.dat and the region .mca file
// (moved out of mcworldexporter.cpp).
#include "export/caveset.h"
#include "export/colkey.h"
#include "export/lairset.h"
#include "export/mcworldexporter.h"
#include "export/sites.h"
#include "export/voxelstore.h"
#include <QHash>
#include <QPoint>
#include <QString>
#include <QVector>

QByteArray buildChunkNbt(int chunkX, int chunkZ, const QHash<int, QVector<quint16>>& sections,
                                McWorldExporter::Format format,
                                const QVector<ChestSite>& chestsHere = QVector<ChestSite>(),
                                const GroundChunk& ground = GroundChunk(),
                                const CaveSet* caves = nullptr,
                                const QVector<PaintingSite>& paintsHere = QVector<PaintingSite>(),
                                const QVector<SpawnerSite>& spawnersHere = QVector<SpawnerSite>(),
                                const QVector<FluidSite>& fluidsHere = QVector<FluidSite>(),
                                const QVector<SignSite>& signsHere = QVector<SignSite>(),
                                const QVector<BellSite>& bellsHere = QVector<BellSite>(),
                                const QVector<BannerSite>& bannersHere = QVector<BannerSite>(),
                                const QVector<LairZone>& zonesHere = QVector<LairZone>(),
                                const LairSet* lairs = nullptr,
                                const QHash<ColKey, quint8>* biomeCells = nullptr,
                                const QVector<CaveBe>& extraBes = QVector<CaveBe>(),
                                const QVector<QPoint3>& heartsHere = QVector<QPoint3>(),
                                bool blendEdge = false,
                                const QVector<PortalStartSite>& portalsHere = QVector<PortalStartSite>(),
                                const QHash<ColKey, QVector<QPair<qint16, quint16>>>* paleUnder = nullptr);
QByteArray buildEntityChunkNbt(int chunkX, int chunkZ,
                                      const QVector<MannequinSite>& mannequinsHere, bool legacy,
                                      const QVector<NpcSite>& npcsHere = QVector<NpcSite>(),
                                      const QVector<PaintingSite>& paintsHere = QVector<PaintingSite>(),
                                      const QVector<FrameSite>& framesHere = QVector<FrameSite>());
QByteArray buildLevelDat(const QString& levelName, int spawnX, int spawnY, int spawnZ,
                                McWorldExporter::Format format, int groundTopY,
                                const McWorldExporter::WorldOptions& world);
QByteArray buildWorldGenSettings(int groundTopY, const McWorldExporter::WorldOptions& world);
QByteArray buildRegionFile(const QVector<QPair<int, int>>& order,
                                  const QHash<ColKey, QByteArray>& payloadByPos, bool precompressed = false);
