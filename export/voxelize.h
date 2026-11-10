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
#include <QSet>
#include <QVector>
#include <atomic>

// Outputs of the object voxelization: the exporter passes the shared
// containers once; every pointer is optional (null - the output is not
// collected), except colMin which every call site must provide.
struct VoxelOut
{
    QHash<ColKey, int>* colMin = nullptr;            // min voxel Y per XZ column (mandatory)
    QHash<ColKey, int>* colAnchor = nullptr;
    QSet<ColKey>* colTall = nullptr;                 // the columns of multi-storey buildings (5+ floors) — the greenery boost keeps away from them
    QVector<ChestSite>* chests = nullptr;
    QVector<PaintingSite>* paints = nullptr;
    QVector<MannequinSite>* mannequins = nullptr;
    QVector<SpawnerSite>* spawners = nullptr;
    int* storyCounter = nullptr;
    QVector<NpcSite>* npcs = nullptr;
    QVector<FrameSite>* frames = nullptr;
    QVector<BasementSite>* basements = nullptr;
    QVector<SignCand>* signs = nullptr;
    QVector<BannerSite>* banners = nullptr;
    QVector<LairZone>* zones = nullptr;
    QVector<IslandSite>* islands = nullptr;
    int* privMats = nullptr;                        // the private sector material split: [0] brick houses, [1] log houses,
                                                    //  [2] brick houses with a WALKABLE 2nd story (wall >= 8 rows: slab + a 3-row room)
};


// Returns the number of written blocks (0 - skipped: an empty mesh or a huge extent).
int voxelizeObject(const McWorldExporter::McObject& obj, const VxTransform& tf, quint16 id,
                   VoxelStore& store, const VoxelOut& out, const std::atomic_bool* cancel,
                   int sink = 0, int extraBlocks = 0, bool floors = false,
                   quint16 structId = 0, bool woodSingle = false, int privMode = 0,
                   int deepRole = 0, quint16 roofId = 0, quint16 trimId = 0);
