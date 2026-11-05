#pragma once
// Voxelization of ONE object: rasterizes the mesh into a block grid, then builds
// the walls/windows/entrances/slabs/stairs/interiors and writes the result into
// the store (moved out of mcworldexporter.cpp).
#include "export/mcworldexporter.h"
#include "export/colkey.h"
#include "export/geom.h"
#include "export/sites.h"
#include "export/voxelstore.h"
#include <QHash>
#include <QVector>
#include <atomic>

// Returns the number of written blocks (0 - skipped: an empty mesh or a huge extent).
int voxelizeObject(const McWorldExporter::McObject& obj, const VxTransform& tf, quint16 id,
                          VoxelStore& store, QHash<ColKey, int>& colMin, const std::atomic_bool* cancel,
                          int sink = 0, QHash<ColKey, int>* colAnchor = nullptr, int extraBlocks = 0,
                          bool floors = false, int* storyCounter = nullptr, QVector<ChestSite>* chests = nullptr,
                          QVector<PaintingSite>* paints = nullptr,
                          QVector<MannequinSite>* mannequins = nullptr,
                          QVector<SpawnerSite>* spawners = nullptr,
                          quint16 structId = 0, bool woodSingle = false, int privMode = 0,
                          QVector<NpcSite>* npcs = nullptr, QVector<FrameSite>* frames = nullptr,
                          QVector<BasementSite>* basements = nullptr, QVector<SignCand>* signs = nullptr,
                          QVector<BannerSite>* banners = nullptr, QVector<LairZone>* zones = nullptr,
                          QVector<IslandSite>* islands = nullptr, int deepRole = 0);
