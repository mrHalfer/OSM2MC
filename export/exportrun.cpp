#include "export/exportrun.h"
#include "core/steplog.h"
#include <QPainterPath>
#include <QPolygonF>
#include "export/climate.h"
#include "export/colkey.h"
#include "export/blocks.h"
#include "export/ground.h"
#include "export/voxelstore.h"
#include "export/caveset.h"
#include "export/lairset.h"
#include "export/paledonor.h"
#include "export/sites.h"
#include "export/geom.h"
#include "export/rails.h"
#include "export/anvil.h"
#include "export/buildingmerge.h"
#include "export/facade.h"
#include "export/voxelize.h"
#include <cstring>

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QRegularExpression>
#include <QPointF>
#include <QSet>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <QtConcurrent/QtConcurrentMap>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include "export/format/bytes.h"
#include "export/format/nbtwriter.h"
#include "export/format/minizip.h"

// The body used these McWorldExporter nested types unqualified (it used to
// be a member of McWorldExporter itself).
using Result = McWorldExporter::Result;
using McObject = McWorldExporter::McObject;
using Format = McWorldExporter::Format;

// Everything the export needs: the call parameters and the shared state of the
// stages (promoted from the run() locals); stage methods are added step by step.
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

    struct VegSite { QVector<ColSpan> spans; int treeH; int minX, maxX, minZ, maxZ; bool spruce = false; bool park = false; bool intra = false; bool loose = false; bool dense = false; int code = 0; };
    struct ParkSite { QSet<ColKey> cols; QVector<QVector<QPointF>> rings; };   // rdcpol: the patch and the outlines (block coordinates)
    enum SpotModel : int { kStatue = 1, kStele = 2, kFountain = 3, kTower = 4, kComposter = 5,
                           kLightPole = 6, kBench = 7, kSculpture = 8, kBusStop = 9 };
    struct SpotSite { int x, z, model; };
    struct PortalForest { QVector<ColSpan> spans; double areaM2; quint32 h; };
    struct ParkCand { int cx, cz; double areaM2; quint32 h; };
    struct RoadJob
    {
        QHash<ColKey, int> road;      // a roadway cell (with widening) -> Y above the surface
        QHash<ColKey, int> core;      // the ribbon "core" without widening (from the mesh) -> Y; the width is recomputed dynamically from it
        int ext = 0;                  // the standard widening on each side (blocks), 0 — no widening (pedestrian, bike)
        quint16 cid = 0;              // carpet color references (for choosing wool/markings)
        quint16 woolId = 0;           // the roadway block — wool of the same color (into the surface layer of the ground)
        quint16 markId = 0;           // 0 = no marking, otherwise the id of the marking carpet (lies ON the wool)
        bool axisX = true;            // the long axis of the ribbon (splitting into cross slices)
        bool fullCarpet = false;      // a bike path: on top of the red wool there is also a continuous carpet of the same color (cid)
        bool car = false;             // a motor road (asphalt or dirt): pedestrian paths and bike lines are not
        QVector<QVector<QPointF>> lines;   // the axis lines of the object (block coordinates): the center marking follows them
        QVector<QPointF> ends;        // the ends of the axis lines (block coordinates) — we do not lay a curb next to them
        double endR = 0.0;            // the "end" radius: half the road width + a reserve
        int nameId = -1;              // the index in roadNames (TYSTR+STREET from rdslin), -1 — a street without a name
    };
    struct RoadLine { int obj; int nameId; bool ped; QVector<QPointF> pts; };   // the axis line of a road in block coordinates
    struct JuncPt { int x, z, a, b; };
    struct VoxJob
    {
        const McObject* o = nullptr;
        quint16 id = 0;
        int sink = 0, extraBlocks = 0;
        bool building = false, woodSingle = false;
        int privMode = 0;
        int deepRole = 0;                 // see BasementSite::forced
        quint16 structId = 0;
        int storyBase = 0, storyCounter = 0, added = 0;
        VoxelStore store;
        QHash<ColKey, int> colMin, colAnchor;
        QVector<ChestSite> chests;
        QVector<PaintingSite> paints;
        QVector<MannequinSite> mannequins;
        QVector<SpawnerSite> spawners;
        QVector<NpcSite> npcs;
        QVector<FrameSite> frames;
        QVector<BasementSite> basements;
        QVector<SignCand> signs;
        QVector<BannerSite> banners;
        QVector<LairZone> zones;
        QVector<IslandSite> islands;
    };
    struct LiftJob { const McObject* o; quint16 id; int sink, extra; quint16 structId; bool woodSingle; int privMode; };
    struct KindTimer { qint64& ns; QElapsedTimer t; KindTimer(qint64& s) : ns(s) { t.start(); } ~KindTimer() { ns += t.nsecsElapsed(); } };
    struct StationSite { QRect bb; QPoint c; char canopy = 0; };
    struct PaleCol { int top; qint16 dy; quint8 w; quint16 surf; qint32 di = 0; qint16 pid = -1; };   // pid >= 0 — a patch (outpost/camp), otherwise the Pale Garden donor
    struct PatchPlace { int pi; int x0, z0; };
    struct ZoneTrees { quint16 oakLog, oakLeaf, birchLog, birchLeaf; int density; bool cone; bool jungle; int palmShare; int bigOakShare; };   // palmShare — the percent of palms among the planted trees (0 — none); bigOakShare — the percent of big oaks in broadleaf forests (0 — none)
    struct RegionData { QVector<QPair<int, int>> order; QHash<ColKey, QByteArray> payload; };
    struct ChunkBuild { QByteArray nbt; int blocks = 0; double sumX = 0.0, sumZ = 0.0;
                        qint64 objBlocks = 0; int maxY = -64; };
    struct TplInst { QString id; int cx, cz; };

    Result res;
    bool legacy;
    CaveSet caveSet;
    QString cavesUsed;
    LairSet lairSet;
    QVector<PaleDonor> patchesLoaded;                              // outposts and camps (patch_*.bin next to cave_templates.bin)
    PaleDonor paleDonor;
    double mnx, mxx;
    double mny, mxy, mnz, mxz;
    int totalVerts;
    VxTransform tf;
    double span;
    VoxelStore store;
    QHash<ColKey, int> colMin;
    QHash<ColKey, int> colAnchor;     // the original (before the foundation) bottom of closed objects — the ground level by it
    QVector<VegSite> forests;
    QVector<VegSite> grassPale;       // meadow/grass vegetation without height with an allowed code — spare candidates for the Pale Garden (see the Pale Garden forest choice)
    QHash<ColKey, quint16> colPlant;  // a column -> the lawn plant (the type by the polygon area, see step 2)
    QVector<ParkSite> parks;
    QHash<ColKey, quint16> colWool;   // a column -> the wool id (csrpol plates without height — instead of turf)
    QVector<BasementSite> basements;  // basements of private houses (built after the ground)
    QVector<LairZone> lairZones;      // raider spawn zones (large industrial-zone lairs)
    QVector<IslandSite> islandSites;  // floating islands over kindergartens
    QVector<BannerSite> bannerSites;  // the banners of bandit lairs (outside and inside)
    QVector<SignCand> signCands;      // candidates for address plates (the side is chosen after the roads)
    QVector<QPoint> privCenters;      // the centers of private houses (block X/Z) — the basis for "villages"
    QVector<QRect> privBoxes;         // their extents in blocks (x0, z0, w, h) — for demolishing a house under a square
    QVector<QList<qint64>> pads;      // csrpol patches without height — we surround them with a wooden fence with gates
    QVector<char> padIsLake;          // 1 — this is a lake (csrpol 51480000: water + a fence), 0 — an ordinary plate
    QSet<ColKey> noTreePadCols;       // the columns of apiaries and farms (csrpol 52400000/52100000): we do not plant trees there
    QVector<int> padCode;             // the CODE of the plate object (53700000/53710000 — sports/children's: we place equipment)
    QVector<SpotSite> spots;
    QSet<ColKey> swampCols;
    QSet<ColKey> cemeteryCols;              // csrpol 53510000 (landuse=cemetery): dark oaks, crosses, jack o'lanterns,
    QVector<PortalForest> portalForests;
    QVector<ParkCand> parkCands;                                  // parks 71126000: candidates for floating islands
    QSet<ColKey> roadCols;            // the columns of the roadway (wool in the ground) — we put lamp posts along them
    QSet<ColKey> autoRoadCols;        // the roadway of MOTOR roads only (paths and bike lines aside) — bus stops face them
    QSet<ColKey> platformCols;        // rdcpol 62134000 (railway=platform): the paving columns — half-slab in 3.576
    QSet<ColKey> railColsAll;         // the rail columns - we plant trees (rule 3.51) without running onto the tracks
    QVector<QVector<QPoint>> subwayTunnels;   // the underground metro (code 61123100): cell chains in block coords,
    QHash<ColKey, quint16> railBedCols;       // electrified surface rails and trams: the ground block under the
    QHash<ColKey, int> colWater;      // a column -> the depth of the water pit (hydrography), blocks
    QSet<ColKey> colWaterArt;         // the columns of artificial reservoirs (all outline corners are right): without a smooth descent, sandy shores and sandy bottom
    QSet<ColKey> colWaterNat;         // the columns of natural reservoirs (resolving intersections)
    QVector<ChestSite> chests;        // chests with loot (the positions of already written IdChest blocks)
    QVector<PaintingSite> paints;     // the paintings of interiors (the motif is in block_entities)
    QVector<MannequinSite> mannequins;// mall mannequins (armor_stand entities, not blocks)
    QVector<NpcSite> npcs;            // villagers/traders/llamas (entities)
    QVector<FrameSite> frames;        // item frames on mall stands (entities)
    QVector<SpawnerSite> spawners;    // monster spawners (the monster type is in block_entities)
    QVector<WaterBodyRec> waterBodies;    // water bodies for the aquatic fauna (lakes and areal rivers, filled by the Water branch)
    QVector<QPair<QString, QString>> roadNames;     // (TYSTR, STREET) of unique streets
    QHash<QString, int> roadNameIdx;
    QVector<RoadLine> roadLines;                    // ped — a pedestrian/bike path (signs only if it has a name)
    int roadObjCounter;
    QVector<JuncPt> juncPts;                        // real intersections: crossings of the AXIS lines (a,b — nameId or -1)
    QVector<RoadJob> roadsHigh;       // dark gray roadways + highways (drawn first)
    QVector<RoadJob> roadsEarth;      // dirt/field/forest roads (podzol) — the lowest priority: motor roads and pedestrian ones do not overlap
    QVector<RoadJob> roadsLow;        // pedestrian/bike paths (after the roadway, only air)
    QVector<QVector<QPointF>> crosswalks;   // pedestrian crossings (rdslin CODE 45262000): axes in block coordinates, across the roadway — drawn as a zebra, see "PEDESTRIAN CROSSINGS"
    int storyCounter;             // the total floors over the city: every 5th one has a treasure (see voxelizeObject)
    QVector<LiftJob> liftJobs;
    QVector<VoxJob*> pending;
    qint64 msFlushRun, msFlushMerge, nFlush;
    QVector<McObject> objectsRaised;                        // implicit data sharing: only the raised buildings are copied
    QVector<McObject> objects;
    QVector<int> deepRoleOf;
    QVector<QList<qint64>> pre;
    QVector<QVector<ColSpan>> preSpans;           // forest (height >= 2.5) — by segments, we do not store the column list
    int objDone;
    int objTotal;
    qint64 nsByKind[11] = {}, cntByKind[11] = {};                  // debugging (MCX_LOG_STEPS): the time of sequential object preparation by kind
    QVector<StationSite> stations;
    QVector<char> stationBuilt;
    bool logSteps;
    QHash<ColKey, QVector<FluidSite>> fluidsByChunk;   // water/lava sources of the junction — fluid_ticks by chunks
    bool hasTerrain;
    int yShift;   // the "normal world": by how much the whole export is raised (decided after the survey, below);
    QList<qint64> touchedChunks;
    QList<qint64> fillChunks;
    int rcx0, rcz0;
    int rcx1, rcz1;
    int rectM;
    int gSurvey;
    int gTotal;
    qint64 vegCells64;
    int vegCells;
    int progTotal;               // the "ground" stage has its own scale; later the steps have their own stages (below)
    int planeCols;
    QVector<int> planeSamples;
    int genGroundTop, planeMax;
    QSet<ColKey> paleCells;
    QHash<ColKey, QVector<QPoint3>> heartsByChunk;                // creaking hearts -> block_ticks by chunks
    QHash<ColKey, QVector<QPair<qint16, quint16>>> paleUnder;     // the donor underground cells under the Pale Garden (pits, voids, water near the surface): column -> (y, id)
    int paleIdx;
    QSet<ColKey> taigaCells;
    QHash<ColKey, quint8> biomeCells;
    QVector<PatchPlace> patchPlaces;
    QSet<ColKey> patchCols;                                        // patch columns (with a margin of 3) — trees/flora are not planted there
    QHash<ColKey, QVector<CaveBe>> extraBesByChunk;                // block entities of the patches (outpost chests)
    QVector<PortalStartSite> portalStarts;                         // ruined portals in forests (structure starts for /locate)
    QHash<ColKey, QVector<PortalStartSite>> portalsByChunk;        // the same by chunks touched by the frame
    QHash<ColKey, PaleCol> paleCol;
    bool paleDonorUsed;
    int erX0, erZ0, erW, erH;
    QVector<qint8> emptyDelta;
    int efx0, efx1, efz0, efz1;            // the exported rectangle of chunks together with the forest chunks
    int snX0, snZ0, snW, snH;
    QVector<quint8> sandNear;                                           // 0 — no water nearby
    bool sandGrid;
    QSet<ColKey> chunksMin, chunksWool, chunksWater, chunksBed;
    QHash<ColKey, GroundChunk> ground;
    int nStations;
    QSet<ColKey> sprFill;
    quint16 birchLogRaw;
    int vegCols;
    QVector<BellSite> bellSites;
    QVector<SignSite> signSites;
    QHash<ColKey, QVector<BannerSite>> bannersByChunk;
    QHash<ColKey, QVector<LairZone>> zonesByChunk;          // raider spawn zones -> all the chunks they touch
    QHash<ColKey, QVector<BellSite>> bellsByChunk;
    QHash<ColKey, QVector<SignSite>> signsByChunk;
    QHash<ColKey, QVector<quint8>> taperDrop;   // which columns got a chamfer — needed by pass 3.59c
    QHash<ColKey, RegionData> regions;
    QPainterPath clipPath;                  // the world clip polygon of the chunk assembly
    QList<qint64> writeChunks;
    QSet<ColKey> writeSet;
    bool blendBorder;
    QHash<ColKey, QVector<ChestSite>> chestsByChunk;   // chests by chunks (block_entities — at the chunk level)
    QHash<ColKey, QVector<PaintingSite>> paintsByChunk;     // paintings — the same block_entities
    QHash<ColKey, QVector<FrameSite>> frameByChunk;         // frames with items — also entities/r.*.mca
    QHash<ColKey, QVector<NpcSite>> npcByChunk;             // villagers/traders/llamas — also entities/r.*.mca
    QHash<ColKey, QVector<MannequinSite>> menByChunk;       // mannequins — entities/r.*.mca (step 4b)
    QHash<ColKey, QVector<SpawnerSite>> spawnByChunk;       // spawners — their own block_entities
    int chunkTotal;
    std::atomic<int> chunkDone;
    std::atomic_bool buildAbort;
    QHash<int, QVector<quint16>> noSections;      // ground-only chunks (the apron) — there are no object sections
    int cores;
    int nBlocks;
    int per;
    QVector<QVector<ChunkBuild>> blockOut;
    QVector<QFuture<void>> futs;
    int blockTotal;
    double objCx, objCz;
    qint64 objCount, objMaxY;
    int idx;
    int spawnX, spawnY, spawnZ;
    MiniZip zip;
    QByteArray levelDat;
    QVector<TplInst> tplInst;
    QString regionDir;
    QHash<ColKey, RegionData> eRegions;
    int regTotal;
    int packTotal;
    int regDone;
    QByteArray zipData;

    // 4. Chunks -> regions: the chunk NBT assembly (false - cancelled).
    bool assembleChunks();
    // 5. The save archive: level.dat, regions, entities, the datapack.
    bool writeZip();
};

ExportRun::ExportRun(const QVector<McWorldExporter::McObject>& objectsIn_,
                     const McWorldExporter::HeightSampler& heightAt_,
                     const QString& path_,
                     const QString& levelName_,
                     const std::atomic_bool* cancel_,
                     McWorldExporter::Format format_,
                     const McWorldExporter::ProgressFn& progress_,
                     const McWorldExporter::WorldOptions& world_)
    : objectsIn(objectsIn_), heightAt(heightAt_), path(path_), levelName(levelName_),
      cancel(cancel_), format(format_), progress(progress_), world(world_)
{
}

bool ExportRun::assembleChunks()
{
    // 4. chunks -> regions (r.<chunkX>>5>.<chunkZ>>5>.mca)
    if (cancel && cancel->load()) { res.cancelled = true; res.error = QObject::tr("Export cancelled."); return false; }
    // We write the chunks: all that got ground metadata (fillChunks = touched by objects + the apron/
    // finishing of the rectangle in noise), plus the chunks where plantings were added after the fill
    // (store.set creates new keys itself).
    writeChunks = fillChunks;
    {
        const QSet<ColKey> inFill(fillChunks.constBegin(), fillChunks.constEnd());
        for (auto it = store.chunks.constBegin(); it != store.chunks.constEnd(); ++it)
            if (!inFill.contains(it.key())) writeChunks << it.key();   // e.g. a crown beyond the forest spot
    }
    // The polygon boundary: chunks that it does not touch are not written (neither blocks nor entities)
    if (world.clipPolygon.size() >= 3)
    {
        QPolygonF poly;
        for (const QPointF& p : world.clipPolygon) poly << QPointF(toBlockX(tf, p.x()), toBlockZ(tf, p.y()));
        clipPath.addPolygon(poly);
        clipPath.closeSubpath();
        const int before = int(writeChunks.size());
        const QSet<ColKey> allChunks(writeChunks.constBegin(), writeChunks.constEnd());
        writeChunks.erase(std::remove_if(writeChunks.begin(), writeChunks.end(), [&](qint64 k)
        { return !clipPath.intersects(QRectF(keyFirst(k) * 16.0, keySecond(k) * 16.0, 16.0, 16.0)); }), writeChunks.end());
        {   // Buildings are filtered, not cut: a building that the boundary crosses stays whole, so all the chunks it occupies are written too (it would otherwise be sliced with no wall at the cut)
            QSet<ColKey> have(writeChunks.constBegin(), writeChunks.constEnd());
            int nWhole = 0;
            for (const McObject& o : objects)
            {
                if (o.kind != McObject::Building || !o.closed || o.vertices.isEmpty()) continue;
                double ax0 = 1e18, ax1 = -1e18, az0 = 1e18, az1 = -1e18;
                for (const QVector3D& v : o.vertices)
                {
                    const double bx = toBlockX(tf, v.x()), bz = toBlockZ(tf, v.y());
                    ax0 = std::min(ax0, bx); ax1 = std::max(ax1, bx); az0 = std::min(az0, bz); az1 = std::max(az1, bz);
                }
                for (int cz = int(std::floor(az0)) >> 4; cz <= int(std::floor(az1)) >> 4; ++cz)
                    for (int cx = int(std::floor(ax0)) >> 4; cx <= int(std::floor(ax1)) >> 4; ++cx)
                    {
                        const qint64 k = pairKey(cx, cz);
                        if (allChunks.contains(k) && !have.contains(k)) { have.insert(k); writeChunks << k; ++nWhole; }
                    }
            }
            // The same for lakes and ponds (not rivers and not big reservoirs): a water body cut by the boundary stays whole, otherwise half of it ends up without water
            for (const McObject& o : objects)
            {
                if (o.kind != McObject::Water || o.linear || o.vertices.isEmpty()) continue;
                double ax0 = 1e18, ax1 = -1e18, az0 = 1e18, az1 = -1e18;
                for (const QVector3D& v : o.vertices)
                {
                    const double bx = toBlockX(tf, v.x()), bz = toBlockZ(tf, v.y());
                    ax0 = std::min(ax0, bx); ax1 = std::max(ax1, bx); az0 = std::min(az0, bz); az1 = std::max(az1, bz);
                }
                const int c0 = int(std::floor(ax0)) >> 4, c1 = int(std::floor(ax1)) >> 4, d0 = int(std::floor(az0)) >> 4, d1 = int(std::floor(az1)) >> 4;
                if (qint64(c1 - c0 + 1) * (d1 - d0 + 1) > 10000) continue;           // a big reservoir (more than ~2.5 km² of the extent) is cut as before
                for (int cz = d0; cz <= d1; ++cz)
                    for (int cx = c0; cx <= c1; ++cx)
                    {
                        const qint64 k = pairKey(cx, cz);
                        if (allChunks.contains(k) && !have.contains(k)) { have.insert(k); writeChunks << k; ++nWhole; }
                    }
            }
            if (nWhole > 0) res.lairInfo += (res.lairInfo.isEmpty() ? QString() : QStringLiteral("; ")) + QObject::tr("chunks added so that the buildings and lakes on the boundary stay whole: %1").arg(nWhole);
        }
        res.lairInfo += (res.lairInfo.isEmpty() ? QString() : QStringLiteral("; "))
            + QObject::tr("polygon boundary: %1 of %2 chunks written").arg(writeChunks.size()).arg(before);
    }
    // Smoothing the border with the unwritten world (only the noise surroundings): for chunks at the export edge we write blending_data. Disabled by MCX_NOBLEND=1 (for comparison).
    writeSet = QSet<ColKey>(writeChunks.constBegin(), writeChunks.constEnd());
    blendBorder = world.normalSurroundings && !qEnvironmentVariableIsSet("MCX_NOBLEND");
    for (const ChestSite& cs : chests)
        chestsByChunk[pairKey(cs.x >> 4, cs.z >> 4)].append(cs);
    for (const PaintingSite& pt : paints)
        paintsByChunk[pairKey(pt.x >> 4, pt.z >> 4)].append(pt);
    for (const FrameSite& fr : frames)
        frameByChunk[pairKey(fr.x >> 4, fr.z >> 4)].append(fr);
    for (const NpcSite& ns : npcs)
        npcByChunk[pairKey(ns.x >> 4, ns.z >> 4)].append(ns);
    for (const MannequinSite& ms : mannequins)
        menByChunk[pairKey(ms.x >> 4, ms.z >> 4)].append(ms);
    for (const SpawnerSite& sp : spawners)
        spawnByChunk[pairKey(sp.x >> 4, sp.z >> 4)].append(sp);
    // The chunk assembly is completely independent (store/ground/chestsByChunk are only read), so it
    // runs in several threads; the regions are then assembled sequentially in the SAME order —
    // the archive stays deterministic.
    chunkTotal = int(writeChunks.size());
    chunkDone = 0;
    buildAbort = false;
    auto buildOne = [&](qint64 k) -> ChunkBuild
    {
        ChunkBuild cb;
        if (buildAbort.load() || (cancel && cancel->load()))
        {
            buildAbort.store(true);                     // cancel — cooperative, we finish the threads to the end
            return cb;
        }
        const int cx = keyFirst(k), cz = keySecond(k);
        const auto sit = store.chunks.constFind(k);
        if (sit != store.chunks.constEnd())
        {   // the center of mass of the object blocks + the maximal height — the spawn is put above this point
            for (auto ssit = sit->constBegin(); ssit != sit->constEnd(); ++ssit)
            {
                const int secBaseY = ssit.key() << 4;   // an arithmetic shift — correct for y<0 too
                const QVector<quint16>& sec = *ssit;
                for (int i = 0; i < sec.size(); ++i)
                {
                    const quint16 id = sec[i];
                    if (id == IdAir) continue;
                    ++cb.blocks;
                    if (id < IdPaletteStart || id == IdWater) continue;   // water is landscape, not an "object"
                    cb.sumX += double(cx * 16 + (i & 15));
                    cb.sumZ += double(cz * 16 + ((i >> 4) & 15));
                    const int y = secBaseY + ((i >> 8) & 15);
                    if (y > cb.maxY) cb.maxY = y;
                    ++cb.objBlocks;
                }
            }
        }
        cb.nbt = zlibStream(buildChunkNbt(cx, cz, sit == store.chunks.constEnd() ? noSections : sit.value(),
                               format, chestsByChunk.value(pairKey(cx, cz)), ground.value(k),
                               caveSet.empty() ? nullptr : &caveSet,
                               QVector<PaintingSite>(),                        // paintings — in entities/, not in BE
                               spawnByChunk.value(pairKey(cx, cz)),
                               fluidsByChunk.value(pairKey(cx, cz)),
                               signsByChunk.value(pairKey(cx, cz)),
                               bellsByChunk.value(pairKey(cx, cz)),
                               bannersByChunk.value(pairKey(cx, cz)),
                               zonesByChunk.value(pairKey(cx, cz)),
                               lairSet.empty() ? nullptr : &lairSet,
                               biomeCells.isEmpty() ? nullptr : &biomeCells,
                               extraBesByChunk.value(pairKey(cx, cz)),
                               heartsByChunk.value(pairKey(cx, cz)),
                               blendBorder && ((!writeSet.contains(pairKey(cx - 1, cz))) + (!writeSet.contains(pairKey(cx + 1, cz)))
                                   + (!writeSet.contains(pairKey(cx, cz - 1))) + (!writeSet.contains(pairKey(cx, cz + 1))) > 0),
                               portalsByChunk.value(pairKey(cx, cz)),
                               paleUnder.isEmpty() ? nullptr : &paleUnder));   // the compression here, in parallel: earlier it went sequentially in the "zip writing"
        const int done = chunkDone.fetch_add(1) + 1;
        if (progress && (done % 16 == 0 || done >= chunkTotal))
            progress(done, chunkTotal, 2);              // the "chunk assembly" phase
        return cb;
    };
    // We divide the work into blocks of ~16 chunks: there are not thousands of tasks, the distribution over cores is even.
    // Our own QThreadPool — we do not compete with the global application pool (the export itself runs in it).
    QThreadPool pool;
    cores = std::max(1, QThread::idealThreadCount());
    pool.setMaxThreadCount(cores);
    nBlocks = std::max(1, std::min((chunkTotal + 15) / 16, cores * 4));
    per = (chunkTotal + nBlocks - 1) / nBlocks;
    blockOut = QVector<QVector<ChunkBuild>>(nBlocks);
    for (int b = 0; b < nBlocks; ++b)
    {
        const int lo = b * per, hi = std::min(chunkTotal, lo + per);
        futs << QtConcurrent::run(&pool, [&, lo, hi, b]
        {
            QVector<ChunkBuild>& out = blockOut[b];
            out.reserve(hi - lo);
            for (int i = lo; i < hi; ++i) out << buildOne(writeChunks.at(i));
        });
    }
    for (QFuture<void>& f : futs) f.waitForFinished();
    if (buildAbort.load())
    {
        res.cancelled = true;
        res.error = QObject::tr("Export cancelled.");
        return false;
    }
    blockTotal = 0;
    // The center of mass of the object blocks (id >= IdPaletteStart — concrete/brick; the ground is metadata, it
    // never entered the center) + the maximal height: the spawn above this point, in the middle of the scene.
    objCx = 0.0;
    objCz = 0.0;
    objCount = 0;
    objMaxY = -64;
    idx = 0;
    for (int b = 0; b < nBlocks; ++b)
        for (const ChunkBuild& cb : blockOut[b])
        {
            blockTotal += cb.blocks;
            objCx += cb.sumX; objCz += cb.sumZ; objCount += cb.objBlocks;
            if (cb.maxY > objMaxY) objMaxY = cb.maxY;
            const qint64 k = writeChunks.at(idx++);
            const int cx = keyFirst(k), cz = keySecond(k);
            const qint64 rk = pairKey(cx >> 5, cz >> 5);
            RegionData& rd = regions[rk];
            rd.order << qMakePair(cx & 31, cz & 31);
            rd.payload.insert(pairKey(cx & 31, cz & 31), cb.nbt);
        }
    res.blockCount = blockTotal;

    spawnX = 0;
    spawnY = 258;
    spawnZ = 0;
    // Spawn on the surface: the ground top of the column or the topmost block above it (the roof); if the column is occupied by a building — the nearest free one
    // (a spiral up to 48 blocks), otherwise the player would appear on a roof or (with terrain) underground.
    auto surfaceSpawn = [&](int x0, int z0, int& outX, int& outY, int& outZ) -> bool
    {
        for (int r = 0; r <= 48; ++r)
            for (int dz = -r; dz <= r; ++dz)
                for (int dx = -r; dx <= r; ++dx)
                {
                    if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                    const int x = x0 + dx, z = z0 + dz;
                    const auto git = ground.constFind(pairKey(x >> 4, z >> 4));
                    if (git == ground.constEnd()) continue;
                    const int col = (z & 15) * 16 + (x & 15);
                    if (git->wd[col] > 0) continue;                       // not into water
                    const int top = git->top[col];
                    bool free = true;
                    for (int y = top + 1; y <= std::min(top + 12, 319) && free; ++y)
                        if (store.get(x, y, z)) free = false;
                    if (!free) continue;
                    outX = x; outZ = z; outY = std::min(top + 2, 310);
                    return true;
                }
        return false;
    };
    if (objCount > 0)
    {
        spawnX = int(std::llround(objCx / double(objCount)));
        spawnZ = int(std::llround(objCz / double(objCount)));
        spawnY = int(std::min<qint64>(objMaxY + 3, 310));   // a fallback: above the very top of the scene
        surfaceSpawn(spawnX, spawnZ, spawnX, spawnY, spawnZ);
    }
    if (world.hasSpawnPoint)
    {   // the spawn in the center of the view of the 3D window/map (if this column got into the export), not in the middle of the whole area
        const int sx0 = int(std::floor(toBlockX(tf, world.spawnMapX))), sz0 = int(std::floor(toBlockZ(tf, world.spawnMapY)));
        int nx = spawnX, ny = spawnY, nz = spawnZ;
        if (surfaceSpawn(sx0, sz0, nx, ny, nz)) { spawnX = nx; spawnY = ny; spawnZ = nz; }
    }

    tmcStepLog("post: chunks assembled");
    return true;
}

bool ExportRun::writeZip()
{
    // 5. The save zip archive. The 26.x layout: level.dat (+ the old one),
    // data/minecraft/world_gen_settings.dat, chunks in dimensions/minecraft/overworld/region/.
    // The legacy 1.21.x layout (as in a world created by the 1.21.11 client): level.dat (+ the old one, with
    // WorldGenSettings inside), chunks in region/ in the root. The other state files (game_rules, weather,
    // raids, DIM-1/DIM1, ...) are created by the game itself at the first opening — checked with vanilla servers.
    if (cancel && cancel->load()) { res.cancelled = true; res.error = QObject::tr("Export cancelled."); return false; }
    MiniDir dir(path);                          // unpackedFolder: the same entries become files under the world folder
    auto addEntry = [&](const QString& name, const QByteArray& data)
    { if (world.unpackedFolder) dir.add(name, data); else zip.add(name, data); };
    levelDat = buildLevelDat(levelName, spawnX, spawnY, spawnZ, format, genGroundTop, world);
    addEntry(QStringLiteral("level.dat"), levelDat);
    addEntry(QStringLiteral("level.dat_old"), levelDat);
    if (!legacy)
        addEntry(QStringLiteral("data/minecraft/world_gen_settings.dat"), buildWorldGenSettings(genGroundTop, world));
    // /locate and the Eye of Ender: our stronghold/city are written into chunks as "structure starts", but the game can find them by the placement grid
    // only if the structure_set has a position exactly in this chunk. The datapack in the world ADDS (the vanilla ones are not touched) its own structure_set
    // tmc:stronghold_here / tmc:ancient_city_here with the same structures: random_spread with a huge spacing (a 2048×2048 chunk cell) and a
    // chosen salt so that the cell position coincides with the start chunk of our structure. /locate and the Eye of Ender collect the positions
    // of all the structure sets, so they find both ours and the ordinary ones.
    // real trial chambers and cities from the cave template: for /locate each instance gets its own structure_set (mineshafts are searched with the vanilla set: it checks every chunk)
    if (!caveSet.empty() && !caveSet.tplStarts.isEmpty())
    {
        QVector<CaveSet::WorldStart> tmpWs;
        for (const qint64 wk : writeChunks)
        {
            caveSet.startsAt(keyFirst(wk), keySecond(wk), tmpWs);
            for (const CaveSet::WorldStart& ws : tmpWs)
                if (ws.here && (ws.id == QLatin1String("minecraft:trial_chambers") || ws.id == QLatin1String("minecraft:ancient_city")))
                    tplInst.append(TplInst{ ws.id, keyFirst(wk), keySecond(wk) });
            if (tplInst.size() >= 120) break;
        }
    }
    if (!world.attribution.isEmpty())
        addEntry(QStringLiteral("ATTRIBUTION.txt"), world.attribution.toUtf8());       // Minecraft ignores unknown files in the save folder
    if (!world.exportInfo.isEmpty())
        addEntry(QStringLiteral("export_info.json"), world.exportInfo.toUtf8());       // the info about this export (app/exportmeta.h)
    if (world.locateDatapack && (!lairSet.placed.isEmpty() || !portalStarts.isEmpty() || !tplInst.isEmpty()))
    {
        for (int ti = 0; ti < tplInst.size(); ++ti)
        {
            const int salt = findPlacementSalt(qint64(world.seed), tplInst[ti].cx, tplInst[ti].cz, 2048);
            const QByteArray js = QStringLiteral("{\"placement\":{\"type\":\"minecraft:random_spread\",\"salt\":%1,\"separation\":0,\"spacing\":2048,\"frequency\":0.0},"
                                                 "\"structures\":[{\"structure\":\"%2\",\"weight\":1}]}").arg(salt).arg(tplInst[ti].id).toUtf8();
            addEntry(QStringLiteral("datapacks/tmc_lairs/data/tmc/worldgen/structure_set/tpl_%1_%2.json").arg(tplInst[ti].id.section(QLatin1Char(':'), -1)).arg(ti), js);
        }
        addEntry(QStringLiteral("datapacks/tmc_lairs/pack.mcmeta"),
                QByteArray("{\"pack\":{\"description\":\"tMC: strongholds, ancient cities and ruined portals located\",\"min_format\":82,\"max_format\":121}}"));
        // ruined portals in forests: each gets its own structure_set with a salt at which the cell position coincides with the start chunk (/locate structure minecraft:ruined_portal)
        for (int pi = 0; pi < portalStarts.size(); ++pi)
        {
            const int salt = findPlacementSalt(qint64(world.seed), portalStarts[pi].cx, portalStarts[pi].cz, 2048);
            const QByteArray js = QStringLiteral("{\"placement\":{\"type\":\"minecraft:random_spread\",\"salt\":%1,\"separation\":0,\"spacing\":2048,\"frequency\":0.0},"
                                                 "\"structures\":[{\"structure\":\"minecraft:ruined_portal\",\"weight\":1}]}").arg(salt).toUtf8();
            addEntry(QStringLiteral("datapacks/tmc_lairs/data/tmc/worldgen/structure_set/ruined_portal_here_%1.json").arg(pi), js);
        }
        for (const LairPlace& pl : lairSet.placed)
        {
            const LairStruct& s = lairSet.st[pl.s];
            const int scx = ((s.room[0] + pl.tx + s.room[3] + pl.tx) / 2) >> 4, scz = ((s.room[2] + pl.tz + s.room[5] + pl.tz) / 2) >> 4;   // as in LairSet::startsTouching
            const int salt = findPlacementSalt(qint64(world.seed), scx, scz, 2048);
            const bool city = (s.id == QLatin1String("ancient_city"));
            const QByteArray js = QStringLiteral("{\"placement\":{\"type\":\"minecraft:random_spread\",\"salt\":%1,\"separation\":0,\"spacing\":2048,\"frequency\":0.0},"
                                                 "\"structures\":[{\"structure\":\"minecraft:%2\",\"weight\":1}]}").arg(salt).arg(s.id).toUtf8();
            addEntry(QStringLiteral("datapacks/tmc_lairs/data/tmc/worldgen/structure_set/%1.json")
                        .arg(city ? QStringLiteral("ancient_city_here") : QStringLiteral("stronghold_here")), js);
        }
    }
    regionDir = legacy ? QStringLiteral("region/")
                                     : QStringLiteral("dimensions/minecraft/overworld/region/");
    // We assemble the entities (mannequins, villagers, paintings, frames) into regions IN ADVANCE, so that the "packing" phase counter is one
    // for everything: block regions + entity regions.
    {
        QSet<ColKey> entChunks;                                   // chunks where there is any entity at all
        for (auto it = menByChunk.constBegin(); it != menByChunk.constEnd(); ++it) entChunks.insert(it.key());
        for (auto it = npcByChunk.constBegin(); it != npcByChunk.constEnd(); ++it) entChunks.insert(it.key());
        for (auto it = paintsByChunk.constBegin(); it != paintsByChunk.constEnd(); ++it) entChunks.insert(it.key());
        for (auto it = frameByChunk.constBegin(); it != frameByChunk.constEnd(); ++it) entChunks.insert(it.key());
        for (qint64 key : entChunks)
        {
            const int cx = keyFirst(key), cz = keySecond(key);
            if (!clipPath.isEmpty() && !writeSet.contains(key)) continue;       // the chunk is outside the polygon boundary
            RegionData& rd = eRegions[pairKey(cx >> 5, cz >> 5)];
            const qint64 ck = pairKey(cx & 31, cz & 31);
            if (!rd.payload.contains(ck))
                rd.order << qMakePair(cx & 31, cz & 31);
            rd.payload.insert(ck, buildEntityChunkNbt(cx, cz, menByChunk.value(key),
                                                      format == McWorldExporter::Format::Legacy121x,
                                                      npcByChunk.value(key), paintsByChunk.value(key), frameByChunk.value(key)));
        }
    }
    tmcStepLog("post: entities assembled into regions");
    regTotal = int(regions.size());
    packTotal = regTotal + int(eRegions.size());
    regDone = 0;
    for (auto it = regions.constBegin(); it != regions.constEnd(); ++it)
    {
        if (progress && (regDone % 4 == 0 || regDone + 1 == regTotal))
            progress(regDone, packTotal, 3);                      // the "packing regions into the zip" phase
        ++regDone;
        if (cancel && cancel->load())
        {
            res.cancelled = true;
            res.error = QObject::tr("Export cancelled.");
            return false;
        }
        const int rx = keyFirst(it.key()), rz = keySecond(it.key());
        const QByteArray mca = buildRegionFile(it->order, it->payload, true);   // the chunks are already compressed
        addEntry(regionDir + QStringLiteral("r.%1.%2.mca").arg(rx).arg(rz), mca);
        ++res.regionCount;
    }
    tmcStepLog("post: block regions packed into the zip");
    // 4b. entities/r.<rx>.<rz>.mca — display mannequins. Since 1.17 the server reads entities ONLY
    // from here (the Entities tag in a region chunk is ignored — the smoke test showed it). The layout
    // of both formats is the same, only the root differs: legacy — entities/ in the archive root,
    // 26.x — dimensions/minecraft/overworld/entities/ (as in worlds created by servers).
    {
        const QString entDir = legacy ? QStringLiteral("entities/")
                                      : QStringLiteral("dimensions/minecraft/overworld/entities/");
        int eDone = 0;
        for (auto it = eRegions.constBegin(); it != eRegions.constEnd(); ++it)
        {
            if (progress)
                progress(regTotal + eDone, packTotal, 3);              // the same "packing" phase
            ++eDone;
            const int rx = keyFirst(it.key()), rz = keySecond(it.key());
            addEntry(entDir + QStringLiteral("r.%1.%2.mca").arg(rx).arg(rz),
                    buildRegionFile(it->order, it->payload));
        }
    }
    tmcStepLog("post: entities packed");
    if (world.unpackedFolder)
    {   // files under the world folder — no archive, no zip limits
        if (!dir.badWhy.isEmpty())
        {
            res.error = QObject::tr("Could not write the file %1: %2").arg(QDir::toNativeSeparators(dir.badFile), dir.badWhy);
            return false;
        }
        if (progress) progress(0, 0, 11);
        if (progress) progress(0, 0, 12);
        tmcStepLog("post: world folder written");
        res.ok = true;
        res.zipBytes = qint64(dir.totalRaw);
        return false;
    }
    // The limits of the classic zip (without zip64): offsets/sizes are uint32, the number of entries is uint16.
    // For our format it is ~7000 region files — in reality there is never more, but an honest
    // error is better than silently breaking the archive.
    if (res.regionCount + 1 > 65534 || zip.totalBlob + 1024 * 1024 >= 0xFFFE0000ull)
    {
        res.error = QObject::tr("The scene is too big for a save zip archive (%1 region files, ~%2 MB). "
                                "Export a smaller area.")
                        .arg(res.regionCount)
                        .arg(zip.totalBlob / (1024 * 1024));
        return false;
    }
    if (progress) progress(0, 0, 11);                              // the "archive assembly" stage
    zipData = zip.finish();

    tmcStepLog("post: zip.finish() ready");
    if (progress) progress(0, 0, 12);                              // the "writing the file to disk" stage
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        res.error = QObject::tr("Could not write the file %1: %2").arg(path, f.errorString());
        return false;
    }
    if (f.write(zipData) != zipData.size())
    {
        res.error = QObject::tr("The file %1 was not written completely (quota/disk?).").arg(path);
        f.close();
        return false;
    }
    f.close();

    tmcStepLog("post: file written");
    res.ok = true;
    res.zipBytes = zipData.size();
    return true;
}

McWorldExporter::Result ExportRun::run()
{
    legacy = (format == Format::Legacy121x);

    // Caves (a user request): we fill the underground of all generated chunks with "casts" from
    // the game world (cave_templates.bin from extract_caves.py). The loading is here — to fail at once,
    // before the expensive voxelization; an empty cavesFile — we look for the file next to the exe.
    // A FLAT WORLD — NO CAVES AT ALL (the user's decision): there is no terrain and no seed there, the underground
    // is our "analytic" fill, and game-generated caves are not needed in a superflat
    // (they only bit through the crust under objects — "bad" columns). Caves live only in noise.
    if (world.caves && world.normalSurroundings)
    {
        QString cavesPath = world.cavesFile;
        if (cavesPath.isEmpty())
        {
            cavesPath = QCoreApplication::applicationDirPath() + QStringLiteral("/cave_templates.bin");
            if (!QFile::exists(cavesPath))
                cavesPath = QStringLiteral("cave_templates.bin");   // dev builds/tooling: we look in the cwd
        }
        QString cavesErr;
        cavesUsed = cavesPath;
        if (!caveSet.load(cavesPath, world.seed, &cavesErr))
        {
            res.error = cavesErr;
            return res;
        }
    }
    // The secret passages of churches to the stronghold and the Ancient City (lair_structures.bin; no file — the feature is simply off).
    if (!caveSet.empty())
    {
        // we look next to the caves file (it is certainly found), next to the exe and in the current folder
        QString lairPath;
        const QStringList lairDirs = { QFileInfo(cavesUsed).absolutePath(), QCoreApplication::applicationDirPath(), QDir::currentPath() };
        for (const QString& dir : lairDirs)
            if (QFile::exists(dir + QStringLiteral("/lair_structures.bin"))) { lairPath = dir + QStringLiteral("/lair_structures.bin"); break; }
        if (lairPath.isEmpty())
            res.lairInfo = QObject::tr("lair_structures.bin not found (put it next to cave_templates.bin; searched in: %1)").arg(lairDirs.join(QStringLiteral(", ")));
        else if (!lairSet.load(lairPath, caveSet))
        {
            lairSet = LairSet();
            res.lairInfo = QObject::tr("lair_structures.bin is damaged");
        }
    }
    else
        res.lairInfo = QObject::tr("caves must be enabled together with the normal surroundings (a noise world)");
    // The real Pale Garden for the Pale Garden forest (no file — a procedural one)
    if (!caveSet.empty())
    {
        const QStringList paleDirs = { QFileInfo(cavesUsed).absolutePath(), QCoreApplication::applicationDirPath(), QDir::currentPath() };
        for (const QString& dir : paleDirs)
            if (QFile::exists(dir + QStringLiteral("/pale_garden.bin")))
            {
                if (!paleDonor.load(dir + QStringLiteral("/pale_garden.bin"), caveSet)) paleDonor = PaleDonor();
                break;
            }
        for (const QString& dir : paleDirs)
        {
            const QStringList pf = QDir(dir).entryList(QStringList() << QStringLiteral("patch_*.bin"), QDir::Files, QDir::Name);
            if (pf.isEmpty()) continue;
            for (const QString& fn : pf)
            {
                PaleDonor pd;
                if (pd.load(dir + QLatin1Char('/') + fn, caveSet)) { pd.name = fn; patchesLoaded.append(pd); }
            }
            break;
        }
    }

    tmcStepLog("export: start");
    // 1. The extent of all objects (in map world coordinates)
    mnx = std::numeric_limits<double>::max();
    mxx = -mnx;
    mny = mnx;
    mxy = -mny;
    mnz = mny;
    mxz = -mnz;
    totalVerts = 0;
    for (const McObject& o : objectsIn)
    {
        totalVerts += o.vertices.size();
        for (const QVector3D& v : o.vertices)
        {
            if (!std::isfinite(v.x()) || !std::isfinite(v.y()) || !std::isfinite(v.z())) continue;
            mnx = std::min(mnx, double(v.x())); mxx = std::max(mxx, double(v.x()));
            mny = std::min(mny, double(v.y())); mxy = std::max(mxy, double(v.y()));
            mnz = std::min(mnz, double(v.z())); mxz = std::max(mxz, double(v.z()));
        }
    }
    if (totalVerts == 0)
    {
        res.error = QObject::tr("Nothing to export: all meshes are empty.");
        return res;
    }

    tf.cx0 = (mnx + mxx) / 2.0;
    tf.cy0 = (mny + mxy) / 2.0;
    tf.yBase = mnz;
    // Map units -> meters (1 block = 1 m): without this Mercator inflates the horizontal sizes by 1.77 times.
    tf.sx = (world.unitsToMetersX > 1e-9) ? world.unitsToMetersX : 1.0;
    tf.sz = (world.unitsToMetersY > 1e-9) ? world.unitsToMetersY : 1.0;
    span = mxz - mnz;
    if (span > 240.0) tf.zScale = 240.0 / span;   // height compression to fit into 0..255

    // 2. Voxelization of objects — by role (see McObject::Kind):
    //    Building/Generic — the mesh shell (floor/walls/roof, empty inside);
    //    Vegetation       — the mesh is NOT voxelized: the polygon "patch" goes to a forest (there is a REL height)
    //                       or to grass (without it);
    //    Road/Railway     — of a road/overpass ribbon only the TOP cell of each
    //                       column remains: a carpet (the nearest to the layer color) or a rail is laid on it;
    //    Plate (csrpol)   — with a height like Building, but always brick; without a height — the polygon patch
    //                       into the surface layer of the ground instead of turf (see colWool, step 3);
    //    Water (hdrpol/hdrlin) — the mesh is not voxelized: the "patch" goes into a pit with water (see colWater,
    //                       step 3), the depth = area/6, but 3..15 blocks.
    // loose — the "small" vegetation of OSM data (tMC adds it itself): a yard between houses (code 71126190, trees like vgbpol but denser) and
    // forced taiga in the private sector (code 71111111, spruces at any area). Such patches are small and narrow, so the forest's own terrain
    // (fading out at the boundary over 28 blocks) would be almost flat — they are given the terrain of empty lands and are not counted as "occupied" for it.
    // Objects from points on the map (sources with a modelField, e.g. csrpnt/hdtpnt/rdcpnt): a memorial, a monument,
    // a fountain, a communication tower, a waste bin, a lighting mast, a bench, a sculpture, a bus stop. The anchor is the center of the point's square; the structure
    // is built in 3.575 on the ready ground; the model number comes from point_models.csv (the code -> model table).
    // 1 — a memorial statue, 2 — a monument stele, 3 — a fountain, 4 — a communication tower, 5 — a composter.
    // The swamp biome (sources with ModelEffect::Swamp, e.g. mrspol): every column under the layer's polygons.
    // An OVERLAY, not an object: the vegetation step turns the trees of any forest under the contour into swamp
    // oaks with vines, plants rare oaks and mushrooms on the open swamp ground, and skips water/roads/structures.
                                            // monster spawns and rare loot chests (the dark forest biome overlay)
    // Forests under portals to the Nether (vegpol, CODE 71111110/71126000): the patch columns + the area in REAL m².
    // The frame is placed in 3.57 (the ground and the trees are ready).
                                              //  the tunnels are dug after the ground fill (the surface is needed)
                                              //  track is replaced (stone; stone bricks for trams) - see makeGround
    // Road priority (a user request): dark gray roadways and their markings are MORE IMPORTANT than the light gray
    // pedestrian and bike paths, so drawing is moved out of the common loop into passes after it:
    // 1) all bodies of "the rest"/highways, 2) their markings, 3) only then pedestrian/bike paths
    // (they occupy the remaining air cells). In the old order a path met earlier in the list
    // "ate" the highway cells together with the marking lines.
    roadObjCounter = 0;
    storyCounter = 0;
    auto colHash = [](qint64 k) -> quint32
    {
        const qint32 x = keyFirst(k), z = keySecond(k);
        quint32 h = quint32(x) * 374761393u + quint32(z) * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    };
    // Parallel voxelization of buildings: each building is computed in a private store (VoxJob) in
    // a thread pool, then the results are MERGED into the common store strictly in the original object order —
    // the outcome does not depend on thread scheduling. The through floor counter (treasures/spawners) in a batch
    // is handed out by an estimate (the base of each building), after the merge the real total is taken.
    // Elevator shafts (bldpol, LIFT=1) are built SEPARATELY and after all buildings: only ABOVE the roof of the building they belong to
    // (the whole part of the shaft below and inside the owner's roof is discarded). Without a building under the shaft — it is built entirely, as before.
    struct PendingGuard { QVector<VoxJob*>& v; ~PendingGuard() { qDeleteAll(v); } } pendingGuard{ pending };
    QThreadPool voxPool;
    voxPool.setMaxThreadCount(std::max(1, QThread::idealThreadCount()));
    msFlushRun = 0;
    msFlushMerge = 0;
    nFlush = 0;
    auto flushJobs = [&]()
    {
        if (pending.isEmpty()) return;
        QElapsedTimer tFl; tFl.start();
        ++nFlush;
        QVector<QFuture<void>> futs;
        futs.reserve(pending.size());
        for (VoxJob* j : pending)
            futs << QtConcurrent::run(&voxPool, [j, this]()
            {
                j->storyCounter = j->storyBase;
                VoxelOut vo;
                vo.colMin = &j->colMin;
                if (j->sink) vo.colAnchor = &j->colAnchor;
                vo.chests = &j->chests; vo.paints = &j->paints;
                vo.mannequins = &j->mannequins; vo.spawners = &j->spawners;
                if (j->building) vo.storyCounter = &j->storyCounter;
                vo.npcs = &j->npcs; vo.frames = &j->frames; vo.basements = &j->basements;
                vo.signs = &j->signs; vo.banners = &j->banners; vo.zones = &j->zones; vo.islands = &j->islands;
                j->added = voxelizeObject(*j->o, tf, j->id, j->store, vo, cancel, j->sink,
                                          j->extraBlocks, j->building,
                                          j->structId, j->woodSingle, j->privMode, j->deepRole);
                if (j->added > 0 && j->o->kind == McObject::Building && (j->o->code == 44312000 || j->o->code == 44313000 || j->o->code == 44315000))
                    decorateWalls(j->store, j->o->code, j->id);
            });
        for (QFuture<void>& f : futs) f.waitForFinished();
        msFlushRun += tFl.restart();
        for (VoxJob* j : pending)
        {   // merging in object order: a later object overlaps an earlier one (as before)
            if (j->added == 0) { ++res.skippedCount; continue; }
            ++res.objectCount;
            for (auto cit = j->store.chunks.begin(); cit != j->store.chunks.end(); ++cit)
            {
                auto git = store.chunks.find(cit.key());
                if (git == store.chunks.end()) { store.chunks.insert(cit.key(), cit.value()); continue; }
                for (auto sit = cit->begin(); sit != cit->end(); ++sit)
                {
                    auto dit = git->find(sit.key());
                    if (dit == git->end()) { git->insert(sit.key(), sit.value()); continue; }
                    QVector<quint16>& dst = dit.value();
                    const QVector<quint16>& src = sit.value();
                    for (int i = 0; i < src.size(); ++i)
                        if (src[i]) dst[i] = src[i];
                }
            }
            for (auto it = j->colMin.constBegin(); it != j->colMin.constEnd(); ++it)
            {
                auto m = colMin.find(it.key());
                if (m == colMin.end() || it.value() < m.value()) colMin[it.key()] = it.value();
            }
            for (auto it = j->colAnchor.constBegin(); it != j->colAnchor.constEnd(); ++it)
            {
                auto m = colAnchor.find(it.key());
                if (m == colAnchor.end() || it.value() < m.value()) colAnchor[it.key()] = it.value();
            }
            chests += j->chests; paints += j->paints;
            mannequins += j->mannequins; spawners += j->spawners; npcs += j->npcs; frames += j->frames;
            basements += j->basements; signCands += j->signs; bannerSites += j->banners; lairZones += j->zones; islandSites += j->islands;
            if (j->building) storyCounter += j->storyCounter - j->storyBase;
        }
        qDeleteAll(pending);
        pending.clear();
        msFlushMerge += tFl.elapsed();
    };
    // Rasterizing the "patches" of vegetation / water / csrpol plates without height (tens of millions of columns
    // on large data) — in advance and in parallel: the objects are independent, and the sequential loop below
    // then only lays the ready lists into maps. Memory: the column lists live until the object is processed.
    // Industrial zone and churches: touching buildings of one code (44340000 / 44319000) are glued into complexes (a common outline, floors and partitions)
    objectsRaised = objectsIn;
    raiseLowBuildingParts(objectsRaised, tf, 7.6);
    for (McObject& o : objectsRaised)
    {   // small building parts (the area of the plan up to ~400 m² or a narrow one): the walls are 1 block thick
        if (!o.part || o.kind != McObject::Building || !o.closed || o.lift || o.code == 44000000) continue;
        double ax0 = 1e18, ax1 = -1e18, az0 = 1e18, az1 = -1e18;
        for (const QVector3D& v : o.vertices)
        {
            const double bx = toBlockX(tf, v.x()), bz = toBlockZ(tf, v.y());
            ax0 = std::min(ax0, bx); ax1 = std::max(ax1, bx); az0 = std::min(az0, bz); az1 = std::max(az1, bz);
        }
        const double w = ax1 - ax0, d = az1 - az0;
        o.thin = std::min(w, d) <= 9.0 || w * d <= 400.0;
    }
    objects = mergeIndustrial(objectsRaised, tf, cancel);
    // The points of the stronghold / the Ancient City set by the user (a chunk "x z" or "x y z" as in F3): the dungeon is built under the NEAREST building
    // whatever its code (a few nearest suitable ones are marked, because a small or cramped building may not fit it; the nearest one that worked is used later).
    // The priority of churches and research buildings is only for the case when no point is set.
    deepRoleOf = QVector<int>(objects.size(), 0);
    {
        auto chunkPoint = [](const QString& text, QPointF* pt) -> bool
        {
            static const QRegularExpression numRe(QStringLiteral("-?\\d+"));
            QVector<int> nums;
            auto it = numRe.globalMatch(text);
            while (it.hasNext()) nums.append(it.next().captured().toInt());
            if (nums.size() < 2) return false;
            const QPoint ch = nums.size() >= 3 ? QPoint(nums[0], nums[2]) : QPoint(nums[0], nums[1]);
            *pt = QPointF(ch.x() * 16 + 8, ch.y() * 16 + 8);
            return true;
        };
        struct Cand { int idx; double d; };
        auto nearestBuildings = [&](const QPointF& pt, int skip) -> QVector<int>
        {
            QVector<Cand> cs;
            for (int i = 0; i < objects.size(); ++i)
            {
                const McObject& q = objects[i];
                if (q.kind != McObject::Building || !q.closed || q.lift || q.raised > 0.0 || q.extrudeH < 5.0 || i == skip) continue;
                double ax0 = 1e18, ax1 = -1e18, az0 = 1e18, az1 = -1e18;
                for (const QVector3D& v : q.vertices)
                {
                    const double bx = toBlockX(tf, v.x()), bz = toBlockZ(tf, v.y());
                    ax0 = std::min(ax0, bx); ax1 = std::max(ax1, bx); az0 = std::min(az0, bz); az1 = std::max(az1, bz);
                }
                if (ax1 - ax0 < 8.0 || az1 - az0 < 8.0) continue;           // too small for a maze entrance
                cs.append({ i, std::hypot((ax0 + ax1) * 0.5 - pt.x(), (az0 + az1) * 0.5 - pt.y()) });
            }
            std::sort(cs.begin(), cs.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
            QVector<int> out;
            for (int k = 0; k < cs.size() && k < 3; ++k) out.append(cs[k].idx);
            return out;
        };
        QPointF ps, pc;
        int firstS = -1;
        if (chunkPoint(world.strongholdChunk, &ps))
        {
            const QVector<int> v = nearestBuildings(ps, -1);
            for (int i : v) deepRoleOf[i] = 1;
            if (!v.isEmpty()) firstS = v.first();
        }
        if (chunkPoint(world.cityChunk, &pc))
            for (int i : nearestBuildings(pc, firstS)) deepRoleOf[i] = 2;       // not the same building as the stronghold's first choice
        // The reserve for the Ancient City (church -> research building -> school): the 3 biggest schools are given a laboratory for certain, otherwise by bad luck
        // not a single school would have one, and without churches and research buildings the city would not be built at all.
        {
            struct SchoolCand { int idx; double area; };
            QVector<SchoolCand> sc;
            for (int i = 0; i < objects.size(); ++i)
            {
                const McObject& q = objects[i];
                if (q.kind != McObject::Building || !q.closed || q.lift || q.code != 44312000 || q.extrudeH < 5.0 || deepRoleOf[i]) continue;
                double ax0 = 1e18, ax1 = -1e18, az0 = 1e18, az1 = -1e18;
                for (const QVector3D& v : q.vertices)
                {
                    const double bx = toBlockX(tf, v.x()), bz = toBlockZ(tf, v.y());
                    ax0 = std::min(ax0, bx); ax1 = std::max(ax1, bx); az0 = std::min(az0, bz); az1 = std::max(az1, bz);
                }
                if (ax1 - ax0 < 8.0 || az1 - az0 < 8.0) continue;
                sc.append({ i, (ax1 - ax0) * (az1 - az0) });
            }
            std::sort(sc.begin(), sc.end(), [](const SchoolCand& a, const SchoolCand& b) { return a.area != b.area ? a.area > b.area : a.idx < b.idx; });
            for (int k = 0; k < sc.size() && k < 3; ++k) deepRoleOf[sc[k].idx] = 3;
        }
    }
    pre = QVector<QList<qint64>>(objects.size());
    preSpans = QVector<QVector<ColSpan>>(objects.size());
    {
        QVector<int> todo;
        for (int i = 0; i < objects.size(); ++i)
        {
            const McObject& q = objects[i];
            if (q.kind == McObject::Vegetation || q.kind == McObject::Water
                || (q.kind == McObject::Plate && (q.extrudeH < 1.0 || q.code == 71323000)) || q.kind == McObject::ParkingLot || q.kind == McObject::BlockCover
                || q.kind == McObject::Spot || q.kind == McObject::Biome)
                todo.append(i);
        }
        std::atomic<int> next { 0 };
        QVector<QFuture<void>> fs;
        for (int t = 0; t < std::max(1, QThread::idealThreadCount()); ++t)
            fs << QtConcurrent::run(&voxPool, [&]()
            {
                for (;;)
                {
                    const int k = next.fetch_add(1);
                    if (k >= todo.size() || (cancel && cancel->load())) break;
                    footprintColumns(objects[todo[k]], tf, pre[todo[k]], cancel);
                    if (objects[todo[k]].kind == McObject::Vegetation && objects[todo[k]].extrudeH >= 2.5)
                    {
                        preSpans[todo[k]] = spansFromCols(pre[todo[k]]);
                        pre[todo[k]] = QList<qint64>();   // free the memory at once
                    }
                }
            });
        for (QFuture<void>& f : fs) f.waitForFinished();
        if (cancel && cancel->load()) { res.cancelled = true; res.error = QObject::tr("Export cancelled."); return res; }
    }
    objDone = 0;
    objTotal = objects.size();
    // Station buildings (CODE 44330000, building=train_station): the outline bbox and the center in block
    // coordinates. Two consumers: the subway straightener inserts a station center into the line's polyline
    // (every line near a station turns at it and passes exactly under it), the "station halls" pass after the
    // subway tunnels digs a decorated underground hall under the building.
    for (const McObject& q : objects)
    {
        if (q.code != 44330000) continue;
        int x0 = std::numeric_limits<int>::max(), x1 = std::numeric_limits<int>::min();
        int z0 = x0, z1 = x1;
        for (const QVector3D& v : q.vertices)         // the building outline lives in the mesh vertices
        {
            const int x = int(std::floor(toBlockX(tf, v.x()))), z = int(std::floor(toBlockZ(tf, v.y())));
            x0 = std::min(x0, x); x1 = std::max(x1, x); z0 = std::min(z0, z); z1 = std::max(z1, z);
        }
        if (x1 < x0 || z1 < z0) continue;
        // the entrance hut too small for a staircase and a vestibule INSIDE it (a user request:
        // "the arched canopy instead of the building is only for when the stair and the lobby do not
        // fit into the building"): the station pass sweeps such a hut and builds the compact square
        // entrance object on its footprint. The stairwell takes 9x9 with its fence, so a building
        // under 13 blocks on the short side cannot host it; the height bound keeps the sweep of the
        // voxelize pass in range (a 4.5 m hut measures 7.6 mesh units with the scene's z exaggeration,
        // a taller thin tower simply keeps its stair inside)
        StationSite site{ QRect(x0, z0, x1 - x0 + 1, z1 - z0 + 1), QPoint((x0 + x1) / 2, (z0 + z1) / 2),
                          char(q.extrudeH > 0.5 && q.extrudeH <= 9.0 &&
                               std::min(x1 - x0 + 1, z1 - z0 + 1) <= 12) };
        stations.append(site);
    }
    stationBuilt = QVector<char>(stations.size(), 0);
    for (const McObject& o : objects)
    {
        KindTimer kindTimer(nsByKind[std::min(10, int(o.kind))]);
        ++cntByKind[std::min(10, int(o.kind))];
        if (progress && (objDone % 16 == 0 || objDone + 1 == objTotal))
            progress(objDone, objTotal, 0);                       // the "objects" phase
        ++objDone;
        if (cancel && cancel->load()) { res.cancelled = true; res.error = QObject::tr("Export cancelled."); return res; }
        if (o.kind == McObject::ParkingLot)
        {   // rdcpol: over the polygon patch the turf is replaced with dark gray concrete (the surface layer of the ground), the place marking — after the ground
            QList<qint64> cols = std::move(pre[objDone - 1]);
            if (cols.isEmpty()) { ++res.skippedCount; continue; }
            if (o.code == 62134000)
            {   // a railway platform (railway=platform): just a stone-brick slab over the patch — no lot marking,
                // the park site is not registered (a request: "just a slab of stone bricks"). The columns are kept:
                // 3.576 tops the paving with a stone-brick half slab (a request: the platform is half a block higher)
                static const quint16 kPlatform = quint16(IdPaletteStart + paletteIndexOf("minecraft:stone_bricks"));
                for (qint64 ck : cols)
                    if (!colWool.contains(ck))
                    {
                        colWool.insert(ck, kPlatform);
                        platformCols.insert(ck);
                    }
                ++res.objectCount;
                continue;
            }
            static const quint16 kDarkConcrete = quint16(IdPaletteStart + paletteIndexOf("minecraft:gray_concrete"));
            ParkSite ps;
            for (qint64 ck : cols)
            {
                if (!colWool.contains(ck)) colWool.insert(ck, kDarkConcrete);
                ps.cols.insert(ck);
            }
            for (const QVector<QPointF>& ring : o.paths)
            {
                QVector<QPointF> bp;
                for (const QPointF& w : ring) bp << QPointF(toBlockX(tf, w.x()), toBlockZ(tf, w.y()));
                if (bp.size() >= 3) ps.rings << bp;
            }
            parks.append(ps);
            ++res.objectCount;
            continue;
        }
        if (o.kind == McObject::BlockCover)
        {   // blkpol: 62231000 — asphalt without markings (dark gray concrete), 45300000 — a square paved with stone bricks; instead of turf
            QList<qint64> cols = std::move(pre[objDone - 1]);
            if (cols.isEmpty()) { ++res.skippedCount; continue; }
            const bool square = (o.code == 45300000);
            if (!square && o.code != 62231000) { ++res.skippedCount; continue; }
            const quint16 cover = quint16(IdPaletteStart + paletteIndexOf(square ? "minecraft:stone_bricks" : "minecraft:gray_concrete"));
            for (qint64 ck : cols)
            {
                if (square) colWool.insert(ck, cover);                     // the square over the asphalt
                else if (!colWool.contains(ck)) colWool.insert(ck, cover);
            }
            ++res.objectCount;
            continue;
        }
        if (o.kind == McObject::Biome)
        {   // a wetland source (the "swamp" effect on it): only mark the columns — the biome changes what GROWS
            // on them in the vegetation step, the polygon itself builds nothing. It is an overlay: it may lie on
            // top of forests, water and other objects, those stay and only get the swamp treatment.
            QList<qint64> cols = std::move(pre[objDone - 1]);
            if (cols.isEmpty()) { ++res.skippedCount; continue; }
            for (qint64 ck : cols) swampCols.insert(ck);
            ++res.objectCount;
            continue;
        }
        if (o.kind == McObject::Spot)
        {   // a point object (a source with a modelField): the rasterized square of the point gives the ANCHOR only
            // (the rule's "as: square:N" centers it on the point). The model was resolved in mcscene by the code
            // from the modelField column (point_models.csv). The structure is built in 3.575 on the ready ground;
            // the neighborhood is reserved against trees already here.
            if (o.model < kStatue || o.model > kBusStop) { ++res.skippedCount; continue; }
            QList<qint64> cols = std::move(pre[objDone - 1]);
            if (cols.isEmpty()) { ++res.skippedCount; continue; }
            int x0 = std::numeric_limits<int>::max(), x1 = std::numeric_limits<int>::min();
            int z0 = x0, z1 = x1;
            for (qint64 ck : cols)
            {
                x0 = std::min(x0, keyFirst(ck)); x1 = std::max(x1, keyFirst(ck));
                z0 = std::min(z0, keySecond(ck)); z1 = std::max(z1, keySecond(ck));
            }
            spots.append(SpotSite{ int(std::lround((x0 + x1) / 2.0)), int(std::lround((z0 + z1) / 2.0)), o.model });
            for (int x = x0 - 3; x <= x1 + 3; ++x)
                for (int z = z0 - 3; z <= z1 + 3; ++z)
                    noTreePadCols.insert(pairKey(x, z));   // +3: the crowns (even the wide swamp oak ones) do not overhang a statue
            ++res.objectCount;
            continue;
        }
        if (o.kind == McObject::Plate && (o.extrudeH < 1.0 || o.code == 71323000))
        {
            // a csrpol without height — a "plate": we do not build a mesh at all, over the polygon patch the top layer
            // of the ground (turf) will later be replaced with wool of a calm gray palette (see below), and the patch itself
            // is surrounded with a fence with gates (step 3.57).
            // A threshold of 1.0, not 0.5: BuildSingleObjectMesh replaces an honest zero REL with a thin
            // slab kMinExtrusionHeight=0.5 and gives exactly it in outRel — such "slabs" are thinner than
            // the foundation and would go under the ground entirely.
            QList<qint64> cols = std::move(pre[objDone - 1]);          // rasterized in advance, in parallel
            if (cols.isEmpty()) { ++res.skippedCount; continue; }
            const qint64 firstCol = *std::min_element(cols.constBegin(), cols.constEnd());
            // The calm palette of plates (a user request): no rainbow colors — only
            // light gray, gray and black wool (a deterministic choice by the patch hash).
            static const char* const kGrayWools[] = { "minecraft:light_gray_wool",
                                                      "minecraft:gray_wool", "minecraft:black_wool" };
            int wIdx = 0;
            for (int i = 0; i < kWoolsCount; ++i)
                if (qstrcmp(kWools[i].name, kGrayWools[colHash(firstCol) % 3u]) == 0) { wIdx = i; break; }
            quint16 wid = quint16(IdWoolBase + wIdx);
            // The coverage by csrpol code: grass stays (a cemetery, the territories of social and religious facilities, an apiary, a farm), stone (a memorial), dark concrete (a gas station, an airport,
            // an industrial enterprise, a substation); the rest (sports, etc.) — calm gray wool
            const bool grassPad = (o.code == 53510000 || o.code == 53600000 || o.code == 53400000 || o.code == 52400000 || o.code == 52100000
                                   || o.code == 51111000 || o.code == 51123000 || o.code == 71323000
                                   || o.code == 53210000);   // a quarry/a spoil heap/a greenhouse/a military ground: the surface is set by their own logic
            if (o.code == 53530000) wid = quint16(IdPaletteStart + paletteIndexOf("minecraft:stone_bricks"));
            else if (o.code == 51220000 || o.code == 53110000 || o.code == 51130000 || o.code == 51460000) wid = quint16(IdPaletteStart + paletteIndexOf("minecraft:gray_concrete"));
            const bool lake = (o.code == 51480000);                  // a lake: water instead of wool, the fence — as for plates
            if (lake)
            {
                const int dLake = std::max(1, std::min(5, int(cols.size() / 18.0 + 0.5)));   // as for hdrpol (lakes)
                for (qint64 ck : cols)
                {
                    auto wit = colWater.find(ck);
                    if (wit == colWater.end() || dLake > wit.value()) colWater[ck] = dLake;
                }
            }
            else if (!grassPad)
                for (qint64 ck : cols)
                    colWool.insert(ck, wid);
            if (o.code == 52400000 || o.code == 52100000 || o.code == 51111000 || o.code == 51123000 || o.code == 71323000
                || o.code == 53210000)   // a military ground: the grass stays, but a base is not a forest
                for (qint64 ck : cols) noTreePadCols.insert(ck);
            if (o.code == 53510000)      // a cemetery: the biome, groves, crosses and monsters are built in 3.5
                for (qint64 ck : cols) cemeteryCols.insert(ck);
            pads.append(cols);
            padIsLake.append(lake ? 1 : 0);
            padCode.append(o.code);
            ++res.objectCount;
            continue;
        }
        if (o.kind == McObject::Vegetation)
        {
            QList<qint64> cols = std::move(pre[objDone - 1]);          // rasterized in advance, in parallel
            QVector<ColSpan> spans = std::move(preSpans[objDone - 1]);  // the forest — already by segments
            if (cols.isEmpty() && spans.isEmpty()) { ++res.skippedCount; continue; }
            if (o.code == 71111110 || o.code / 1000 == 71126)     // vegpol 71111110/71126000 and vgbpol 71126xxx
            {   // a candidate for a portal into the Nether: the area in real m² (the coordinates are already converted to meters —
                // WorldOptions::unitsToMetersX/Y, a column = 1 m²)
                if (spans.isEmpty()) spans = spansFromCols(cols);
                double area = 0;
                for (const ColSpan& c : spans) area += double(c.z1 - c.z0 + 1);
                portalForests.append(PortalForest{ spans, area, colHash(pairKey(spans.first().x, spans.first().z0)) });
                if (o.code == 71126000 && area >= 6000.0)
                {   // the center of mass of a park (by columns) — the place for an island
                    double sx2 = 0.0, sz2 = 0.0;
                    for (const ColSpan& c : spans) { const double n = c.z1 - c.z0 + 1; sx2 += n * c.x; sz2 += n * 0.5 * (c.z0 + c.z1); }
                    parkCands.append(ParkCand{ int(std::lround(sx2 / area)), int(std::lround(sz2 / area)), area,
                                               colHash(pairKey(spans.first().x, spans.first().z0) ^ 0x15A9ull) });
                }
            }
            if (o.code == 71126000)
            {   // A PARK: always with trees (mixed: oak, birch, spruce — rarely) and a lawn, regardless of the height in the data
                if (spans.isEmpty()) spans = spansFromCols(cols);
                VegSite vs;
                vs.spans = spans;
                vs.treeH = 5;
                vs.park = true;
                vs.minX = spans.first().x; vs.maxX = spans.last().x;
                vs.minZ = std::numeric_limits<int>::max(); vs.maxZ = std::numeric_limits<int>::min();
                for (const ColSpan& c : spans) { vs.minZ = std::min(vs.minZ, c.z0); vs.maxZ = std::max(vs.maxZ, c.z1); }
                forests << vs;
                for (const ColSpan& c : spans)
                    for (int z = c.z0; z <= c.z1; ++z)
                    {
                        const qint64 ck = pairKey(c.x, z);
                        if (!colPlant.contains(ck)) colPlant.insert(ck, quint16(IdGrassPlant));
                    }
            }
            else if (o.extrudeH >= 2.5)
            {
                VegSite vs;
                vs.spans = spans;
                vs.treeH = std::max(4, std::min(7, int(o.extrudeH + 0.5)));
                vs.intra = o.intraBlock;
                vs.code = o.code;
                if (o.code == 71111110)
                {   // a forest of this code — spruces if the area is decent (>= 5000 columns ~ m²); small ones stay oaks. We do not touch the other codes.
                    qint64 area = 0;
                    for (const ColSpan& c : spans) area += c.z1 - c.z0 + 1;
                    vs.spruce = area >= 5000;
                }
                if (o.code == 71111111) { vs.spruce = true; vs.loose = true; }     // forced taiga of the private sector (OSM data): spruces at any area
                if (o.code == 71126190) { vs.intra = true; vs.loose = true; vs.dense = true; }   // a yard between houses (OSM data): vgbpol trees, but denser
                vs.minX = spans.first().x; vs.maxX = spans.last().x;
                vs.minZ = std::numeric_limits<int>::max(); vs.maxZ = std::numeric_limits<int>::min();
                for (const ColSpan& c : spans) { vs.minZ = std::min(vs.minZ, c.z0); vs.maxZ = std::max(vs.maxZ, c.z1); }
                forests << vs;
            }
            else
            {
                // The grass type by polygon area (a column ≈ 1 m²): >100000 m² — ripe wheat,
                // >50000 m² — overgrown grass (tall_grass), otherwise — ordinary short. When
                // polygons intersect the "senior" type wins (the ids grow: short < tall < wheat).
                if (isPaleForestCode(o.code))
                {   // a candidate for the Pale Garden (chosen only if the area is more than twice the best forest with height)
                    VegSite gv;
                    gv.spans = spans.isEmpty() ? spansFromCols(cols) : spans;
                    if (!gv.spans.isEmpty())
                    {
                        gv.treeH = 5;
                        gv.code = o.code;
                        gv.minX = gv.spans.first().x; gv.maxX = gv.spans.last().x;
                        gv.minZ = std::numeric_limits<int>::max(); gv.maxZ = std::numeric_limits<int>::min();
                        for (const ColSpan& c : gv.spans) { gv.minZ = std::min(gv.minZ, c.z0); gv.maxZ = std::max(gv.maxZ, c.z1); }
                        grassPale << gv;
                    }
                }
                const quint16 pid = cols.size() > 100000 ? quint16(IdWheat)
                                  : cols.size() > 50000  ? quint16(IdTallGrass)
                                                         : quint16(IdGrassPlant);
                for (qint64 ck : cols)
                {
                    auto pit = colPlant.find(ck);
                    if (pit == colPlant.end() || pid > pit.value())
                        colPlant[ck] = pid;
                }
            }
            ++res.objectCount;
            continue;
        }
        if (o.kind == McObject::Water)
        {
            // Hydrography: over the polygon/ribbon "patch" — a pit with water. The depth is proportional to the area
            // (1/6 of a block of area per block of depth), but not shallower than 3 and not deeper than 15 blocks; rivers
            // (linear, hdrlin) — three times shallower (1/18, from 1 block), otherwise a narrow ribbon would give a canyon.
            QList<qint64> cols = std::move(pre[objDone - 1]);          // rasterized in advance, in parallel
            if (cols.isEmpty()) { ++res.skippedCount; continue; }
            // lakes: small ones (up to ~1000 m²) are still small (up to 5 blocks), large ones are deeper logarithmically — 10 thousand m² ≈ 8, 100 thousand ≈ 12, 1 million ≈ 15-16 (up to 20)
            const double nCols = double(cols.size());
            const int lakeD = nCols <= 1000.0 ? std::max(1, std::min(5, int(nCols / 18.0 + 0.5)))
                                              : std::min(20, int(5.0 + 3.5 * std::log10(nCols / 1000.0) + 0.5));
            const int d = o.linear ? std::max(1, std::min(15, int(cols.size() / 18.0 + 0.5))) : lakeD;
            // a reservoir with strictly right outline angles (a tolerance of 6°) — artificial (a pond with a fountain, an embankment): a flat bottom and no sandy shores
            QElapsedTimer tWater; tWater.start();
            const bool artWater = !o.linear && isRectilinearOutline(o, tf.sx, tf.sz, 6.0);
            const qint64 msRect = tWater.restart();
            // large reservoirs — millions of columns: we enlarge the hashes in advance in one step (without repeated rebuilding as they grow)
            // and do not spend calls on colWaterArt.remove() while there are no artificial reservoirs
            colWater.reserve(int(std::min<qint64>(colWater.size() + cols.size(), 2000000000ll)));
            if (!artWater) colWaterNat.reserve(int(std::min<qint64>(colWaterNat.size() + cols.size(), 2000000000ll)));
            const bool artEmpty = colWaterArt.isEmpty();
            for (qint64 ck : cols)
            {
                auto wit = colWater.find(ck);
                if (wit == colWater.end()) colWater.insert(ck, d);
                else if (d > wit.value()) wit.value() = d;     // intersecting reservoirs — we take the deeper one
                if (artWater) { if (!colWaterNat.contains(ck)) colWaterArt.insert(ck); }
                else { colWaterNat.insert(ck); if (!artEmpty) colWaterArt.remove(ck); }
            }
            if (!o.linear && (o.code == 31120000 || o.code == 31131000 || o.code == 31132000 || o.code == 31410000))
            {   // aquatic fauna: the water body parameters for step 3.586 (swimming pools, canals and ditches are not here; linear rivers too)
                WaterBodyRec wb; wb.code = o.code; wb.d = d; wb.nCols = cols.size();
                wb.seed = pairKey(keyFirst(cols.first()), keySecond(cols.first())) ^ (qint64(unsigned(o.code)) << 32);
                const int sStep = std::max(128, int(cols.size() / 256) + 1);
                for (int si = 0; si < cols.size(); si += sStep) wb.samples.append(cols[si]);
                waterBodies.append(std::move(wb));
            }
            {
                char buf[200];
                snprintf(buf, sizeof buf, "water: object %d, columns %lld, isRectilinear %lld ms, hash fill %lld ms, total in colWater %lld", objDone, (long long)cols.size(), (long long)msRect, (long long)tWater.elapsed(), (long long)colWater.size());
                tmcStepLog(buf);
            }
            ++res.objectCount;
            continue;
        }
        quint16 id = nearestBlockId(o.color);
        quint16 structId = 0;                                 // the structural part (foundation/floors/roof), 0 — like the walls
        bool matWood = false;
        int privMode = 0;                                     // 1 — a private house with a floor (brick), 2 — one-storey (logs)
        if (!o.material.trimmed().isEmpty() && !(o.code == 44400002 || o.code == 44400100))
        {
            quint16 mid = materialBlockId(o.material, o.color);
            // The wood by MAT is not applied to the other codes (wood is only for private 44400002/100,
            // for them the material is decided by the floor logic above, and for churches 44319000): we take the object color from the 3D window.
            if (mid == IdOakPlanks && o.code != 44319000) mid = 0;
            if (mid)
            {
                id = mid;                                     // "Material from a column" is more important than the color
                if (mid != IdDirt && mid != IdOakPlanks)      // all except earth and wood: the foundation,
                    structId = quint16(IdPaletteStart + paletteIndexOf("minecraft:light_gray_concrete"));
            }                                                 // floors and roof — gray concrete (glass: only the outer walls are glass, the inside is concrete)
        }
        // The structures of research institutes (44315000) — walls of stone bricks (they stand out among the buildings), the structural part is gray concrete.
        if (o.kind == McObject::Building && o.code == 44315000)
        {
            id = quint16(IdPaletteStart + paletteIndexOf("minecraft:stone_bricks"));
            structId = quint16(IdPaletteStart + paletteIndexOf("minecraft:light_gray_concrete"));
        }
        // Churches (44319000) — ALWAYS stone brick (not the ordinary brick of houses), unless wood is specified in the data (then planks).
        if (o.kind == McObject::Building && o.code == 44319000 && id != IdOakPlanks)
        {
            id = quint16(IdPaletteStart + paletteIndexOf("minecraft:stone_bricks"));
            structId = quint16(IdPaletteStart + paletteIndexOf("minecraft:light_gray_concrete"));
        }
        // Private houses 44400002/44400100: the wall material is decided by the need for a floor — the decision itself
        // is made in voxelizeObject from the real geometry (see privMode there). The MAT column is not taken into account for them.
        if (o.kind == McObject::Building && o.closed && (o.code == 44400002 || o.code == 44400100))
            privMode = 1;
        if (o.kind == McObject::Plate)
        {   // csrpol with a height — brick, always
            id = quint16(IdPaletteStart + paletteIndexOf("minecraft:bricks"));
        }
        if (o.kind == McObject::Road && o.code == 45262000)
        {   // A PEDESTRIAN CROSSING (tMC, OSM data footway=crossing): a line across the roadway. By itself it voxelizes nothing —
            // the zebra is drawn by a separate pass over the ready motor-road roadways (see "PEDESTRIAN CROSSINGS").
            for (const QVector<QPointF>& pl : o.paths)
            {
                QVector<QPointF> bp;
                for (const QPointF& w : pl) bp << QPointF(toBlockX(tf, w.x()), toBlockZ(tf, w.y()));
                if (bp.size() >= 2) crosswalks.append(bp);
            }
            ++res.objectCount;
            continue;
        }
        if (o.kind == McObject::Road || o.kind == McObject::Railway)
        {
            if (o.kind == McObject::Railway)
                flushJobs();                                    // rails look into the store: buildings earlier — already in it
            VoxelStore tmp;
            QHash<ColKey, int> tmpMin;
            VoxelOut vo;                                  // only colMin is collected here
            vo.colMin = &tmpMin;
            const int added = voxelizeObject(o, tf, id, tmp, vo, cancel);
            if (added == 0) { ++res.skippedCount; continue; }
            QHash<ColKey, int> topY;                          // column -> Y of the top cell of the strip
            for (auto cit = tmp.chunks.constBegin(); cit != tmp.chunks.constEnd(); ++cit)
            {
                const int bx0 = keyFirst(cit.key()) << 4, bz0 = keySecond(cit.key()) << 4;
                for (auto sit = cit->constBegin(); sit != cit->constEnd(); ++sit)
                {
                    const int baseY = sit.key() << 4;
                    const QVector<quint16>& sec = sit.value();
                    for (int i = 0; i < sec.size(); ++i)
                    {
                        if (!sec[i]) continue;
                        const qint64 ck = pairKey(bx0 + (i & 15), bz0 + ((i >> 4) & 15));
                        const int y = baseY + ((i >> 8) & 15);
                        auto t2 = topY.find(ck);
                        if (t2 == topY.end() || y > t2.value())
                            topY[ck] = y;
                    }
                }
            }
            if (o.kind == McObject::Road)
            {
                // rdslin by codes (user requests): bike path 45261000 — dark red;
                // pedestrian 45260000/45250000/61420000 — as is (carpet by color); LIGHT GRAY
                // 61310000/61320000/61330000 — forced lighter (like 45260000, request), the color
                // of the source object does not matter; the other codes — 2 blocks wider on each side
                // and dark gray; the main roads 45210000/45220000/45230000/61210000/61220000/61230000
                // — in addition gray marking lines in the middle and along the edges of the roadway.
                const bool bike = (o.code == 45261000);
                const bool ped  = (o.code == 45260000 || o.code == 45250000 || o.code == 61420000);
                const bool lgray = (o.code == 61310000 || o.code == 61320000 || o.code == 61330000);
                const bool mark = (o.code == 45210000 || o.code == 45220000 || o.code == 45230000
                                || o.code == 61210000 || o.code == 61220000 || o.code == 61230000);
                int bikeIdx = -1, grayIdx = -1, markIdx = -1, whiteIdx = -1;
                for (int i = 0; i < kCarpetsCount; ++i)
                {
                    if (qstrcmp(kCarpets[i].name, "minecraft:red_carpet") == 0) bikeIdx = i;
                    else if (qstrcmp(kCarpets[i].name, "minecraft:gray_carpet") == 0) grayIdx = i;
                    else if (qstrcmp(kCarpets[i].name, "minecraft:light_gray_carpet") == 0) markIdx = i;
                    if (qstrcmp(kCarpets[i].name, "minecraft:white_carpet") == 0) whiteIdx = i;
                }
                quint16 cid = nearestCarpetId(o.color);         // pedestrian/unknown — by color
                if (bike && bikeIdx >= 0) cid = quint16(IdCarpetBase + bikeIdx);
                else if (lgray && markIdx >= 0)                 // forced light gray
                    cid = quint16(IdCarpetBase + markIdx);      // (woolForCarpetId -> light_gray_wool)
                else if (!ped && grayIdx >= 0) cid = quint16(IdCarpetBase + grayIdx);
                QHash<ColKey, int> road = topY;                 // the roadway with the widening taken into account
                int mx0 = 0, mx1 = -1, mz0 = 0, mz1 = -1;
                bool axisX = true;
                if (!topY.isEmpty())
                {
                    mx0 = mz0 = std::numeric_limits<int>::max();
                    mx1 = mz1 = std::numeric_limits<int>::min();
                    for (auto t2 = topY.constBegin(); t2 != topY.constEnd(); ++t2)
                    {
                        const int bx = keyFirst(t2.key()), bz = keySecond(t2.key());
                        mx0 = std::min(mx0, bx); mx1 = std::max(mx1, bx);
                        mz0 = std::min(mz0, bz); mz1 = std::max(mz1, bz);
                    }
                    axisX = (mx1 - mx0) >= (mz1 - mz0);         // the long axis of the strip
                    // "the others" — +2 cells on each side; without marking and when converted to meters — only +1
                    // (request: roads without marking became too wide after the coordinates were recalculated)
                    const int roadExt = (!mark && tf.sx != 1.0) ? 1 : 2;
                    if (!bike && !ped)
                        for (auto t2 = topY.constBegin(); t2 != topY.constEnd(); ++t2)
                            for (int d = 1; d <= roadExt; ++d)
                                if (axisX)
                                {
                                    road.insert(pairKey(keyFirst(t2.key()), keySecond(t2.key()) + d), t2.value());
                                    road.insert(pairKey(keyFirst(t2.key()), keySecond(t2.key()) - d), t2.value());
                                }
                                else
                                {
                                    road.insert(pairKey(keyFirst(t2.key()) + d, keySecond(t2.key())), t2.value());
                                    road.insert(pairKey(keyFirst(t2.key()) - d, keySecond(t2.key())), t2.value());
                                }
                }
                // (User request) The roadway is NOT a carpet but a full WOOL block of the same
                // color, sunk into the terrain: we put it into the surface layer of the ground (colWool → makeGround),
                // i.e. the wool replaces the turf and is flush with the ground. The carpet remains only for
                // the marking — one layer right on top of the wool blocks.
                auto woolForCarpetId = [](quint16 carpetId) -> quint16
                {
                    if (carpetId < IdCarpetBase || carpetId >= IdCarpetBase + kCarpetsCount)
                        return 0;
                    QByteArray n(kCarpets[int(carpetId) - int(IdCarpetBase)].name);
                    n.replace(QByteArrayLiteral("_carpet"), QByteArrayLiteral("_wool"));
                    for (int i = 0; i < kWoolsCount; ++i)       // wool — SPECIAL id range (like
                        if (qstrcmp(kWools[i].name, n.constData()) == 0)   // csrpol), not the palette
                            return quint16(IdWoolBase + i);
                    return 0;
                };
                RoadJob job;
                job.road = road;
                job.cid = cid;
                job.woolId = woolForCarpetId(cid);
                job.car = !ped && !bike;                  // a motor road (dirt roads too) — bus stops look for these
                const bool earthRoad = (o.code == 61310000 || o.code == 61320000 || o.code == 61330000 || o.code == 61330001
                                        || o.code == 45233333 || o.code == 45200003 || o.code == 71610000);
                {   // the roadway material: motor roads — dark concrete, pedestrian — light concrete (grass does not grow on concrete);
                    // dirt, field roads, firebreaks and ruts — PODZOL; bike path — red wool, as it was
                    if (earthRoad)
                        job.woolId = IdPodzol;
                    else if (!bike)
                        job.woolId = ped ? quint16(IdPaletteStart + paletteIndexOf("minecraft:light_gray_concrete"))
                                         : quint16(IdPaletteStart + paletteIndexOf("minecraft:gray_concrete"));
                }
                job.axisX = axisX;                                // the marking — by the cross slices of the strip
                job.core = topY;
                job.ext = (!bike && !ped && !topY.isEmpty()) ? ((!mark && tf.sx != 1.0) ? 1 : 2) : 0;
                job.fullCarpet = bike && job.woolId != 0;          // bike path: wool + carpet of the same color
                if (!o.street.isEmpty())
                {
                    const QString nk = o.streetType + QLatin1Char('\n') + o.street;
                    auto nit = roadNameIdx.find(nk);
                    if (nit == roadNameIdx.end())
                    {
                        nit = roadNameIdx.insert(nk, roadNames.size());
                        roadNames.append(qMakePair(o.streetType, o.street));
                    }
                    job.nameId = nit.value();
                }
                {   // the road ends: the first and the last point of each axis; the radius — half-width (cells/length/2) + 2
                    double totLen = 0.0;
                    for (const QVector<QPointF>& pl : o.paths)
                    {
                        if (pl.size() < 2) continue;
                        for (int k = 0; k + 1 < pl.size(); ++k)
                            totLen += std::hypot(toBlockX(tf, pl[k + 1].x()) - toBlockX(tf, pl[k].x()),
                                                 toBlockZ(tf, pl[k + 1].y()) - toBlockZ(tf, pl[k].y()));
                        job.ends << QPointF(toBlockX(tf, pl.first().x()), toBlockZ(tf, pl.first().y()))
                                 << QPointF(toBlockX(tf, pl.last().x()), toBlockZ(tf, pl.last().y()));
                    }
                    job.endR = totLen > 1.0 ? std::max(3.0, double(job.road.size()) / totLen * 0.5 + 2.0) : 0.0;
                }
                {   // the center lines (to find real intersections): block coordinates, every part of the object has a common obj
                    const int objId = roadObjCounter++;
                    for (const QVector<QPointF>& pl : o.paths)
                    {
                        RoadLine rl; rl.obj = objId; rl.nameId = job.nameId; rl.ped = (bike || ped);
                        for (const QPointF& w : pl)
                            rl.pts << QPointF(toBlockX(tf, w.x()), toBlockZ(tf, w.y()));
                        if (rl.pts.size() >= 2) { roadLines.append(rl); job.lines.append(rl.pts); }
                    }
                }
                if (mark && whiteIdx >= 0)
                    job.markId = quint16(IdCarpetBase + whiteIdx); // the center marking — WHITE carpet (the curbs along the edges — gray)
                if (earthRoad)
                    roadsEarth.append(job);                     // dirt roads — after all the others, they do not cover other people's
                else if (bike || ped)
                    roadsLow.append(job);                       // yield to the dark gray roadway
                else
                    roadsHigh.append(job);
            }
            else
            {
                // Railway along the AXIS line: one rail track 1 block wide, cells are connected, rail shapes
                // (straight, turns, slopes) by the direction of the track; double-track (two parallel tracks with
                // a gap of 2 blocks) is ONLY code 61111000. The cell height — from the mesh strip (topY).
                const double gauge = 3.0;                           // the shift of the second track: a rail + a gap of 2 blocks
                QVector<QVector<QPointF>> lines;                    // the track axes in block coordinates
                for (const QVector<QPointF>& pl : o.paths)
                {
                    QVector<QPointF> bp;
                    for (const QPointF& w : pl)
                        bp << QPointF(toBlockX(tf, w.x()), toBlockZ(tf, w.y()));
                    if (bp.size() >= 2) lines << bp;
                }
                if (o.code == 61123100)
                {   // the underground metro: no surface rails - the rails go into a tunnel dug deep
                    // under the ground (see the "subway tunnels" pass after the ground fill).
                    // Real lines in OSM are long diagonals (Moscow: 95% of the length off the axes) and the
                    // orthogonal cell grid turns them into a staircase that turns on EVERY cell. Trains do
                    // not corner per block, so the polyline is straightened first: every segment becomes
                    // a staircase of short 10-block straight steps with 20-block diagonal corrections
                    // back toward the real line (the nodes - the stations and the curve breaks - stay in
                    // place; the corrections keep parallel lines from knotting into one corridor).
                    // Station buildings (CODE 44330000) are ALWAYS turned through - by ONE line though, the
                    // nearest one (between 20 and 96 blocks - a closer line is never bent, the hall widens
                    // to it instead): its polyline gets the station point inserted as a node,
                    // so the tunnel crosses exactly under the station and its hall (dug later) connects to
                    // the corridor. Pulling EVERY line that passes nearby into the one point used to knot the
                    // station: parallel lines merged into a shared corridor and then crossed again past the
                    // hall. The rest keep their OSM geometry (parallel lines stay parallel) and only get
                    // rails in the hall if they already run under it on their own.
                    for (const StationSite& st : stations)
                    {
                        int bl = -1, bi = -1;                                 // the nearest line, its nearest segment
                        double bd = 96.0 * 96.0;
                        for (int li = 0; li < lines.size(); ++li)
                            for (int i = 0; i + 1 < lines[li].size(); ++i)
                            {   // the polyline segment nearest to the station center
                                const QPointF& p0 = lines[li][i], & p1 = lines[li][i + 1];
                                const double vx = p1.x() - p0.x(), vy = p1.y() - p0.y();
                                const double ll = vx * vx + vy * vy;
                                double t = ll > 1e-9 ? ((st.c.x() - p0.x()) * vx + (st.c.y() - p0.y()) * vy) / ll : 0.0;
                                t = std::max(0.0, std::min(1.0, t));
                                const double dx = st.c.x() - (p0.x() + vx * t), dy = st.c.y() - (p0.y() + vy * t);
                                const double d2 = dx * dx + dy * dy;
                                if (d2 < bd) { bd = d2; bl = li; bi = i; }
                            }
                        if (bl < 0) continue;
                        const bool stAxisX = st.bb.width() >= st.bb.height();
                        const int stAcross = stAxisX ? st.bb.height() : st.bb.width();
                        // a line closer than 20 blocks to the station is NEVER pulled (a user request:
                        // "if a metro line is within 20 blocks of the station - do not pull the line,
                        // widen the station itself"): the hall dig below unions the track cells it
                        // touches and stretches the hall toward them, so the line keeps its shape
                        if (bd <= 20.0 * 20.0) continue;
                        // the attraction point stands off the hall centre, on the side OPPOSITE the
                        // entrance shaft (the hall takes its side from this same 0x51DE hash): the rails
                        // then cross the hall mid-way, the entrance sits in a corner, and the shaft's
                        // ring fence can never wall up the corridor it almost stands on
                        const quint32 sSide = colHash(pairKey(st.c.x(), st.c.y()) ^ 0x51DEull);
                        const int sb = (sSide & 1u) ? 1 : -1;
                        const int stOff = std::min(8, std::max(0, stAcross / 2 - 7));
                        const QPointF sp(double(st.c.x() - (stAxisX ? 0 : sb * stOff)),
                                         double(st.c.y() - (stAxisX ? sb * stOff : 0)));
                        bool dup = false;                     // the OSM way itself may already have a node here
                        for (const QPointF& p : lines[bl])
                            if (std::abs(p.x() - sp.x()) < 2.0 && std::abs(p.y() - sp.y()) < 2.0) { dup = true; break; }
                        if (!dup) lines[bl].insert(bi + 1, sp);
                    }
                    // (the user pattern: "10 blocks straight, 20 diagonal - the lines then stand closer
                    // to their real places") every segment becomes a staircase of short 10-block straight
                    // runs alternating with 20-block diagonals back toward the real line; once the line is
                    // caught up, the remainder of the segment runs along the OSM line itself. The nodes -
                    // the stations and the curve breaks - stay in place.
                    for (const QVector<QPointF>& ln : lines)
                    {
                        QVector<QPointF> pl;
                        for (const QPointF& p : ln)
                        {
                            if (pl.isEmpty()) { pl << p; continue; }
                            QPointF c = pl.last();
                            const bool domX = std::abs(p.x() - c.x()) >= std::abs(p.y() - c.y());
                            const int sA = domX ? (p.x() >= c.x() ? 1 : -1) : (p.y() >= c.y() ? 1 : -1);
                            const int sC = domX ? (p.y() >= c.y() ? 1 : -1) : (p.x() >= c.x() ? 1 : -1);
                            const auto step = [&](double advA, double advC)
                            {   // one piece: along the dominant axis, crossed by advC, the turn is kept
                                if (domX) c.setX(c.x() + sA * advA), c.setY(c.y() + sC * advC);
                                else      c.setY(c.y() + sA * advA), c.setX(c.x() + sC * advC);
                                pl << c;
                            };
                            double da = domX ? std::abs(p.x() - c.x()) : std::abs(p.y() - c.y());
                            double dc = domX ? std::abs(p.y() - c.y()) : std::abs(p.x() - c.x());
                            // the slope of the correction: a period is 10 straight + 20 diagonal = 30
                            // blocks along the dominant axis, so 20*rc = 30 * the line's own cross rate
                            // (clamped 1:1 - steeper OSM keeps a cross residual for the landing below)
                            const double rc = std::min(1.0, 1.5 * dc / std::max(da, 1.0));
                            while (da > 1.0 && dc > 1.0)
                            {
                                const double s = std::min(10.0, da);               // drift off the line
                                step(s, 0.0); da -= s;
                                if (da <= 1.0) break;
                                const double adv = std::min(20.0, da);             // the walk back
                                const double cr = std::min(dc, adv * rc);
                                step(adv, cr); da -= adv; dc -= cr;
                            }
                            if (dc <= 1.0)
                            {   // caught up with the real line (the usual finish): repeat the original
                                // line to the node (a user request) - a plain piece, the rail pass
                                // stairs it along the true direction
                                pl << p; c = p;
                            }
                            else
                            {   // a steep line the 1:1 diagonals could not fully catch: land on the
                              // node with straight steps (rare, only on steep OSM data)
                                while (da > 1.0) { const double s = std::min(80.0, da); step(s, 0.0); da -= s; }
                                if (dc > 1.0) step(0.0, dc);
                                pl[pl.size() - 1] = p;
                            }
                        }
                        const QVector<QPoint> chain = railCellChain(pl);
                        if (chain.size() >= 2) subwayTunnels << chain;
                    }
                    ++res.objectCount;
                    continue;
                }
                if (lines.isEmpty() && !topY.isEmpty())
                {   // no axes — a straight line in the middle of the strip along its long axis
                    int rx0 = std::numeric_limits<int>::max(), rx1 = std::numeric_limits<int>::min();
                    int rz0 = rx0, rz1 = rx1;
                    for (auto t2 = topY.constBegin(); t2 != topY.constEnd(); ++t2)
                    {
                        rx0 = std::min(rx0, keyFirst(t2.key())); rx1 = std::max(rx1, keyFirst(t2.key()));
                        rz0 = std::min(rz0, keySecond(t2.key())); rz1 = std::max(rz1, keySecond(t2.key()));
                    }
                    const bool axisX = (rx1 - rx0) >= (rz1 - rz0);
                    QVector<QPointF> bp;
                    if (axisX) bp << QPointF(rx0 + 0.5, (rz0 + rz1 + 1) * 0.5) << QPointF(rx1 + 0.5, (rz0 + rz1 + 1) * 0.5);
                    else       bp << QPointF((rx0 + rx1 + 1) * 0.5, rz0 + 0.5) << QPointF((rx0 + rx1 + 1) * 0.5, rz1 + 0.5);
                    lines << bp;
                }
                if (o.code == 61111000)
                {   // the second track: shift every point along the normal to the direction of the track
                    const int nOrig = lines.size();
                    for (int li = 0; li < nOrig; ++li)
                    {
                        const QVector<QPointF>& src = lines[li];
                        QVector<QPointF> off;
                        for (int pi = 0; pi < src.size(); ++pi)
                        {
                            const QPointF a = src[std::max(0, pi - 1)], b = src[std::min(int(src.size()) - 1, pi + 1)];
                            double dx = b.x() - a.x(), dz = b.y() - a.y();
                            const double l = std::hypot(dx, dz);
                            if (l < 1e-9) { off << src[pi]; continue; }
                            dx /= l; dz /= l;
                            off << QPointF(src[pi].x() - dz * gauge, src[pi].y() + dx * gauge);
                        }
                        lines << off;
                    }
                }
                auto ribbonY = [&](int x, int z, bool& ok) -> int
                {
                    ok = true;
                    auto it = topY.constFind(pairKey(x, z));
                    if (it != topY.constEnd()) return it.value();
                    int best = 0; bool have = false;
                    for (int dz = -1; dz <= 1; ++dz)
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            auto n2 = topY.constFind(pairKey(x + dx, z + dz));
                            if (n2 != topY.constEnd() && (!have || n2.value() > best)) { best = n2.value(); have = true; }
                        }
                    ok = have;
                    return best;
                };
                const bool tram = (o.code == 61124000);               // tram tracks: brick bed under the rail line only
                const bool elec = o.railElec || tram;                 // powered rails: the ELEC column, or a tram (always)
                const quint16 bedId = quint16(IdPaletteStart + paletteIndexOf(
                                          tram ? "minecraft:stone_bricks" : "minecraft:stone"));
                                                                      // the track bed: stone along an ordinary railway
                                                                      // (a request — the rail column and BOTH sides), stone
                                                                      // bricks under the tram rails only; the powered rails
                                                                      // sit on redstone from electrifyRails (store wins)
                int nRailElectro = 0;
                for (const QVector<QPointF>& ln : lines)
                {
                    const QVector<QPoint> chain = railCellChain(ln);
                    const int n = chain.size();
                    if (n == 0) continue;
                    QVector<int> ys(n, 0);
                    int lastY = std::numeric_limits<int>::min();
                    for (int ci = 0; ci < n; ++ci)
                    {
                        bool ok = false;
                        const int y = ribbonY(chain[ci].x(), chain[ci].y(), ok);
                        if (ok) lastY = y;
                        ys[ci] = (lastY == std::numeric_limits<int>::min()) ? y : lastY;
                    }
                    // slopes no steeper than 1 block per cell (a "stepped" rail with a gap will not connect)
                    for (int ci = 1; ci < n; ++ci) ys[ci] = std::max(ys[ci - 1] - 1, std::min(ys[ci - 1] + 1, ys[ci]));
                    for (int ci = n - 2; ci >= 0; --ci) ys[ci] = std::max(ys[ci + 1] - 1, std::min(ys[ci + 1] + 1, ys[ci]));
                    // the corner cells stay FLAT (a sloped rail cannot turn), the step moves to the
                    // neighbouring straight cells - the re-clamp may push a corner back up, so a few rounds
                    for (int round = 0; round < 4; ++round)
                    {
                        bool moved = false;
                        for (int ci = 1; ci + 1 < n; ++ci)
                            if ((chain[ci] - chain[ci - 1]) != (chain[ci + 1] - chain[ci]) && ys[ci] != ys[ci - 1])
                            { ys[ci] = ys[ci - 1]; moved = true; }
                        if (!moved) break;
                        for (int ci = 1; ci < n; ++ci) ys[ci] = std::max(ys[ci - 1] - 1, std::min(ys[ci - 1] + 1, ys[ci]));
                        for (int ci = n - 2; ci >= 0; --ci) ys[ci] = std::max(ys[ci + 1] - 1, std::min(ys[ci + 1] + 1, ys[ci]));
                    }
                    for (int ci = 0; ci < n; ++ci)
                    {
                        const QPoint& c = chain[ci];
                        const QPoint* pv = ci > 0 ? &chain[ci - 1] : nullptr;
                        const QPoint* nx = ci + 1 < n ? &chain[ci + 1] : nullptr;
                        int shape = railShapeFor(c, pv, nx);
                        if (shape <= 1 && pv && nx)
                        {   // straight section: the slope toward the higher neighbor cell
                            const int up = (ys[ci + 1] == ys[ci] + 1) ? ci + 1 : (ys[ci - 1] == ys[ci] + 1 ? ci - 1 : -1);
                            if (up >= 0)
                            {
                                const QPoint& u = chain[up];
                                shape = u.x() > c.x() ? 2 : u.x() < c.x() ? 3 : u.y() < c.y() ? 4 : 5;
                            }
                        }
                        const qint64 ck = pairKey(c.x(), c.y());
                        railColsAll.insert(ck);                      // trees (3.51) avoid the tracks
                        if (bedId)
                        {
                            railBedCols.insert(ck, bedId);           // the ground under the track becomes stone/brick
                            if (!tram)
                            {   // an ordinary railway: the earth turns to stone in 3 columns — the rail and both
                                // sides (a request). The sides are perpendicular to the local direction; a corner
                                // cell takes the prev->next chord, so the stone strip follows the bend
                                const QPoint d = (pv && nx) ? (*nx - *pv) : (nx ? *nx - c : (pv ? c - *pv : QPoint(1, 0)));
                                const QPoint n(int((d.x() > 0) - (d.x() < 0)), int((d.y() > 0) - (d.y() < 0)));
                                const QPoint lat(-n.y(), n.x());
                                if (lat.x() || lat.y())
                                {
                                    railBedCols.insert(pairKey(c.x() + lat.x(), c.y() + lat.y()), bedId);
                                    railBedCols.insert(pairKey(c.x() - lat.x(), c.y() - lat.y()), bedId);
                                }
                            }
                        }
                        if (store.get(c.x(), ys[ci], c.y()) != IdAir) continue;
                        store.set(c.x(), ys[ci], c.y(), quint16(int(IdRailShapeBase) + shape));
                        auto mit = colMin.find(ck);
                        if (mit == colMin.end() || ys[ci] < mit.value())
                            colMin[ck] = ys[ci];
                    }
                    if (elec)
                        nRailElectro += electrifyRails(store, chain, ys);   // powered rails on straight cells only
                }
                if (elec && nRailElectro > 0)
                {   // a trace for the diagnostics (the same style as the subway pass)
                    char ebuf[96];
                    snprintf(ebuf, sizeof ebuf, "electrified rail line %d: %d powered rails", int(o.code), nRailElectro);
                    tmcStepLog(ebuf);
                }
            }
            ++res.objectCount;
            continue;
        }
        // Foundation: a closed body (a building) is lowered by 2 blocks — the lower logs and the floor end up
        // in the ground, the ground level around stays the same (it is computed by colAnchor). Private
        // sector: the floor slab is 1 block, so the sinking is also 1 block (user request).
        const bool privSec = o.kind == McObject::Building && o.closed
                             && (o.code == 44400002 || o.code == 44400100 || o.code == 44400110);
        if (privSec)
        {   // the center of the house bbox in block coordinates — later the village centers are chosen from the clusters
            double ax0 = 1e18, ax1 = -1e18, az0 = 1e18, az1 = -1e18;
            for (const QVector3D& v : o.vertices)
            {
                const double bx = toBlockX(tf, v.x()), bz = toBlockZ(tf, v.y());
                ax0 = std::min(ax0, bx); ax1 = std::max(ax1, bx); az0 = std::min(az0, bz); az1 = std::max(az1, bz);
            }
            if (ax1 >= ax0)
            {
                privCenters.append(QPoint(int(std::floor((ax0 + ax1) * 0.5)), int(std::floor((az0 + az1) * 0.5))));
                privBoxes.append(QRect(int(std::floor(ax0)), int(std::floor(az0)),
                                       int(std::ceil(ax1 - ax0)) + 1, int(std::ceil(az1 - az0)) + 1));
            }
        }
        const bool shedSec = o.kind == McObject::Building && o.closed && o.code == 44000000;   // shed: foundation 1 block, walls/roof 1 block
        const int sink = (o.closed && o.raised <= 0.0) ? ((privSec || shedSec) ? 1 : 2) : 0;
        // Growth (user request): a private-sector house grows exactly to the minimal frame
        // grid of 7 rows: foundation (1) + 1st floor room (4) + ceiling slab (1) + the row of the
        // 2nd floor/attic (1), i.e. the span lyHigh-lyLow >= 6. Other buildings: base +2,
        // tall (>=6 m); "low" < 6 m — +4.
        int extraBlocks = 0;
        if (sink)
            extraBlocks = privSec ? qMax(0, 6 - int(std::ceil(o.extrudeH)))
                                  : (shedSec ? 1 : (o.extrudeH >= 6.0 ? 2 : 4));   // shed: 1 instead of 2 — the foundation is a block shallower (the height above the ground is the same), and the floor/ceiling without lining give 2 blocks to the room
        if (o.kind == McObject::Building && o.lift && o.closed)
        {
            liftJobs.append(LiftJob{ &o, id, sink, extraBlocks, structId, matWood, privMode });
            continue;
        }
        {
            VoxJob* j = new VoxJob;
            j->o = &o; j->id = id; j->sink = sink; j->extraBlocks = extraBlocks;
            j->structId = structId; j->woodSingle = matWood; j->privMode = privMode;
            j->deepRole = deepRoleOf[objDone - 1];
            j->building = (o.kind == McObject::Building);       // "floors"/windows — only bldpol
            int est = 0;                                        // the floor estimate: the counter base of the next building
            for (const VoxJob* q : pending) est += q->building && q->o->closed ? std::max(1, int(q->o->extrudeH + q->extraBlocks) / 5) : 0;
            j->storyBase = storyCounter + est;
            pending.append(j);
            if (pending.size() >= 64)
                flushJobs();
            if (cancel && cancel->load()) { res.cancelled = true; res.error = QObject::tr("Export cancelled."); return res; }
        }
    }
    tmcStepLog("vox: loop over objects finished (before the last flush)");
    for (int k = 0; k < 10; ++k)
        if (cntByKind[k])
        {
            char buf[160];
        snprintf(buf, sizeof buf, "vox: kind %d: objects %lld, preparation in the loop %lld ms", k, (long long)cntByKind[k], (long long)(nsByKind[k] / 1000000));
            tmcStepLog(buf);
        }
    {
        char buf[160];
        snprintf(buf, sizeof buf, "vox: flush x%lld: parallel part %lld ms, merge %lld ms", (long long)nFlush, (long long)msFlushRun, (long long)msFlushMerge);
        tmcStepLog(buf);
    }
    flushJobs();
    // The bottom row of the walls of a raised part (the tier border, for example of a church): concrete instead of the wall material.
    {
        const quint16 baseId = quint16(IdPaletteStart + paletteIndexOf("minecraft:light_gray_concrete"));
        for (const McObject& o : objectsRaised)
        {
            if (o.kind != McObject::Building || !o.closed || o.raised <= 0.0 || o.vertices.size() < 3) continue;
            QList<qint64> cols;
            footprintColumns(o, tf, cols, cancel);
            double ay0 = 1e18;
            for (const QVector3D& v : o.vertices) ay0 = std::min(ay0, toBlockY(tf, v.z()));
            const int y0 = int(std::floor(ay0));
            // the footprint columns do not include the outermost columns of the walls: look at the ring of 1 block around them too
            QSet<qint64> ring;
            for (qint64 ck : cols)
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx)
                        ring.insert(pairKey(keyFirst(ck) + dx, keySecond(ck) + dz));
            for (qint64 ck : ring)
            {
                const int x = keyFirst(ck), z = keySecond(ck);
                const quint16 bid = store.get(x, y0, z);
                if (bid == IdAir) continue;                                               // everything but air (glass and windows too): the ring is solid
                if (store.get(x - 1, y0, z) != IdAir && store.get(x + 1, y0, z) != IdAir && store.get(x, y0, z - 1) != IdAir
                    && store.get(x, y0, z + 1) != IdAir && store.get(x, y0 - 1, z) != IdAir) continue;     // an inner block, not the facade
                store.set(x, y0, z, baseId);
            }
        }
    }
    // Access to the raised building parts: a part hanging over another building (a tower on a podium) has no stairs from its base building. Where the floor of the part
    // lies right over the room of the lower building, a shaft with a ladder is cut through the slab (a ladder on a pillar inside the plan): from the floor of the lower room
    // to the floor of the part. The rooms are looked for in the store, so it does not matter how the floors of each part were laid out.
    {
        static const int ddx[4] = { 0, 1, 0, -1 }, ddz[4] = { -1, 0, 1, 0 };           // N, E, S, W (z grows to the south)
        const quint16 ladderFor[4] = { quint16(IdLadderS), quint16(IdLadderW), quint16(IdLadderN), quint16(IdLadderE) };   // the facing is AWAY from the pillar on the side d
        int nLadders = 0;
        for (const McObject& o : objects)
        {
            if (o.kind != McObject::Building || !o.closed || o.raised <= 0.0 || o.merged || o.vertices.size() < 3) continue;
            QList<qint64> cols;
            footprintColumns(o, tf, cols, cancel);
            if (cols.size() < 6) continue;
            QSet<qint64> colSet;
            double sx = 0, sz = 0, ay0 = 1e18;
            for (qint64 ck : cols) { colSet.insert(ck); sx += keyFirst(ck); sz += keySecond(ck); }
            for (const QVector3D& v : o.vertices) ay0 = std::min(ay0, toBlockY(tf, v.z()));
            sx /= cols.size(); sz /= cols.size();
            const int by0 = int(std::floor(ay0));
            // the cell is inside a building: in all four directions a wall is met (an open terrace on the roof of the lower building is not a room)
            auto enclosed = [&](int x, int y, int z)
            {
                static const int dxs[4] = { 0, 1, 0, -1 }, dzs[4] = { -1, 0, 1, 0 };
                for (int d = 0; d < 4; ++d)
                {
                    bool hit = false;
                    for (int t = 1; t <= 30 && !hit; ++t) hit = store.get(x + dxs[d] * t, y, z + dzs[d] * t) != IdAir;
                    if (!hit) return false;
                }
                return true;
            };
            struct Cand { int x, z, d, yf, yu, s0; double dist; };
            Cand best{ 0, 0, -1, 0, 0, 0, 1e18 };
            for (qint64 ck : cols)
            {
                const int x = keyFirst(ck), z = keySecond(ck);
                int yu = std::numeric_limits<int>::min();                               // the standing level of the first floor of the part
                for (int y = by0; y <= by0 + 4; ++y)
                    if (store.get(x, y, z) == IdAir && store.get(x, y + 1, z) == IdAir) { yu = y; break; }
                if (yu == std::numeric_limits<int>::min() || store.get(x, yu - 1, z) == IdAir) continue;
                int j = yu - 1;                                                         // the slab: the solid rows under the floor of the part
                while (j > yu - 8 && store.get(x, j, z) != IdAir) --j;
                if (store.get(x, j, z) != IdAir) continue;
                const int s0 = j + 1;
                int k = j;                                                              // the room of the lower building: air down to its floor
                while (k > j - 9 && store.get(x, k, z) == IdAir) --k;
                if (store.get(x, k, z) == IdAir || j - k < 2) continue;
                const int yf = k + 1;
                const double dist = std::hypot(x - sx, z - sz);
                if (dist >= best.dist) continue;
                bool inner = true;                                                      // not at the edge of the plan: all 8 neighbors are the plan of the part too
                for (int dz = -1; dz <= 1 && inner; ++dz)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (!colSet.contains(pairKey(x + dx, z + dz))) { inner = false; break; }
                if (!inner || !enclosed(x, yf, z) || !enclosed(x, yf + 1, z) || !enclosed(x, yu, z)) continue;       // outside — no ladder at all
                for (int d = 0; d < 4; ++d)
                {
                    const int px = x + ddx[d], pz = z + ddz[d];
                    if (!colSet.contains(pairKey(px, pz))) continue;                    // the pillar stands inside the plan of the part
                    if (store.get(px, s0, pz) == IdAir) continue;                       // under the pillar there must be the slab
                    best = Cand{ x, z, d, yf, yu, s0, dist };
                    break;
                }
            }
            if (best.d < 0) continue;
            const int px = best.x + ddx[best.d], pz = best.z + ddz[best.d];
            const quint16 pillar = store.get(px, best.s0, pz);
            for (int y = best.s0; y < best.yu; ++y) store.erase(best.x, y, best.z);       // the shaft through the slab
            for (int y = best.yf; y <= best.yu; ++y) store.set(best.x, y, best.z, ladderFor[best.d]);
            for (int y = best.yf; y <= best.yu + 1; ++y)
                if (store.get(px, y, pz) == IdAir) store.set(px, y, pz, pillar);
            ++nLadders;
        }
        if (nLadders > 0) tmcStepLog(("vox: ladders to the raised parts: " + std::to_string(nLadders)).c_str());
    }
    // Monopitch roofs (OSM roof:shape=skillion): a wedge over the flat top of the body - the rise from the low edge to the high edge is roof:height, the roof falls toward roof:direction.
    {
        for (const McObject& o : objectsRaised)
        {
            if (!o.skillionRoof || o.kind != McObject::Building || !o.closed || o.vertices.size() < 3) continue;
            QList<qint64> cols;
            footprintColumns(o, tf, cols, cancel);
            if (cols.isEmpty()) continue;
            double ay1 = -1e18;
            for (const QVector3D& v : o.vertices) ay1 = std::max(ay1, toBlockY(tf, v.z()));
            const int yTop = int(std::floor(ay1));
            const double b = o.roofDir * 3.14159265358979 / 180.0;
            const double dx = std::sin(b), dz = -std::cos(b);                    // the block axes: x - east, z - south; the bearing is clockwise from the north
            double uMin = 1e18, uMax = -1e18;
            for (qint64 ck : cols)
            {
                const double u = keyFirst(ck) * dx + keySecond(ck) * dz;
                uMin = std::min(uMin, u); uMax = std::max(uMax, u);
            }
            const double span = uMax - uMin;
            if (span < 1.0) continue;
            const double rise = std::min(24.0, o.roofH * tf.zScale);
            for (qint64 ck : cols)
            {
                const int x = keyFirst(ck), z = keySecond(ck);
                int roofY = std::numeric_limits<int>::min();
                quint16 rid = 0;
                for (int y = yTop + 4; y >= yTop - 6; --y)
                {
                    const quint16 bid = store.get(x, y, z);
                    if (bid != IdAir) { roofY = y; rid = bid; break; }
                }
                if (roofY == std::numeric_limits<int>::min()) continue;
                const double t = (uMax - (x * dx + z * dz)) / span;              // 0 at the low edge, 1 at the high one
                const int h = int(std::lround(t * rise));
                for (int k = 1; k <= h; ++k)
                    if (store.get(x, roofY + k, z) == IdAir) store.set(x, roofY + k, z, rid);
            }
        }
    }
    // Domes of churches (OSM roof:shape=onion/dome): an onion dome on the top. It is built after all the buildings by the finished roof: the top of the roof is looked for in the store above the center of the plan (so the
    // changes of the heights by the export — growth, gluing complexes — do not matter). The look is chosen by roof:colour / roof:material: the colour gives the concrete of the dome (white — polished quartz),
    // and a spire is put only on the domes of yellow (gold), blue (lapis), light blue (diamond), red (redstone) and black (coal); without both a colour and a known material the dome is of plain gray concrete;
    // a glass dome is a transparent hemisphere, the ceiling under it is cut out (the upper floor is seen through it).
    {
        auto blk = [](const char* name) { return quint16(IdPaletteStart + paletteIndexOf(name)); };
        struct DomeLook { quint16 block; quint16 spire; bool glass; };
        auto lookOf = [&](const McObject& o) -> DomeLook
        {
            const QString mat = o.roofMaterial.trimmed().toLower(), col = o.roofColour.trimmed().toLower();
            if (mat.contains(QLatin1String("glass")) || mat.contains(QStringLiteral("\u0441\u0442\u0435\u043a\u043b")))
                return { quint16(IdGlass), 0, true };
            auto fromConcrete = [&](quint16 id) -> DomeLook
            {   // the spire — by the nearest concrete
                if (id == blk("minecraft:yellow_concrete"))     return { id, blk("minecraft:gold_block"), false };
                if (id == blk("minecraft:blue_concrete"))       return { id, blk("minecraft:lapis_block"), false };
                if (id == blk("minecraft:light_blue_concrete")) return { id, blk("minecraft:diamond_block"), false };
                if (id == blk("minecraft:red_concrete"))        return { id, blk("minecraft:redstone_block"), false };
                if (id == blk("minecraft:black_concrete"))      return { id, blk("minecraft:coal_block"), false };
                return { id, 0, false };
            };
            if (!col.isEmpty())
            {
                static const QHash<QString, const char*> names = {
                    { QStringLiteral("gold"), "minecraft:yellow_concrete" }, { QStringLiteral("golden"), "minecraft:yellow_concrete" }, { QStringLiteral("yellow"), "minecraft:yellow_concrete" },
                    { QStringLiteral("blue"), "minecraft:blue_concrete" }, { QStringLiteral("darkblue"), "minecraft:blue_concrete" }, { QStringLiteral("navy"), "minecraft:blue_concrete" },
                    { QStringLiteral("lightblue"), "minecraft:light_blue_concrete" }, { QStringLiteral("light_blue"), "minecraft:light_blue_concrete" }, { QStringLiteral("azure"), "minecraft:light_blue_concrete" },
                    { QStringLiteral("cyan"), "minecraft:light_blue_concrete" }, { QStringLiteral("turquoise"), "minecraft:light_blue_concrete" }, { QStringLiteral("skyblue"), "minecraft:light_blue_concrete" },
                    { QStringLiteral("red"), "minecraft:red_concrete" }, { QStringLiteral("darkred"), "minecraft:red_concrete" }, { QStringLiteral("maroon"), "minecraft:red_concrete" },
                    { QStringLiteral("black"), "minecraft:black_concrete" },
                    // white (and its OSM synonyms) — polished quartz, not white concrete: whiter and cleaner than any dye color
                    { QStringLiteral("white"), "minecraft:smooth_quartz" }, { QStringLiteral("snow"), "minecraft:smooth_quartz" },
                    { QStringLiteral("ivory"), "minecraft:smooth_quartz" }, { QStringLiteral("pearl"), "minecraft:smooth_quartz" } };
                const auto it = names.constFind(col);
                if (it != names.constEnd()) return fromConcrete(blk(it.value()));
                const QColor qc(col);                                              // other names and #rrggbb — by the nearest concrete
                if (qc.isValid())
                {   // what comes out white by the nearest match is polished quartz too (e.g. #ffffff, #f8f8ff)
                    const quint16 near = nearestBlockId(qc);
                    if (near == blk("minecraft:white_concrete")) return { blk("minecraft:smooth_quartz"), 0, false };
                    return fromConcrete(near);
                }
            }
            if (mat.contains(QLatin1String("copper"))) return { blk("minecraft:orange_concrete"), 0, false };
            if (mat.contains(QLatin1String("steel")) || mat.contains(QLatin1String("zinc")) || mat.contains(QLatin1String("tin")) || mat.contains(QLatin1String("lead"))
                || mat.contains(QLatin1String("alumin")) || mat == QLatin1String("metal") || mat.contains(QLatin1String("iron")))
                return { blk("minecraft:light_gray_concrete"), 0, false };
            return { blk("minecraft:light_gray_concrete"), 0, false };             // neither the colour nor a known material: plain concrete
        };
        for (const McObject& o : objectsRaised)
        {
            if (!o.onionRoof || o.kind != McObject::Building || !o.closed || o.vertices.size() < 3) continue;
            double ax0 = 1e18, ax1 = -1e18, az0 = 1e18, az1 = -1e18, ay1 = -1e18;
            for (const QVector3D& v : o.vertices)
            {
                const double bx = toBlockX(tf, v.x()), bz = toBlockZ(tf, v.y());
                ax0 = std::min(ax0, bx); ax1 = std::max(ax1, bx); az0 = std::min(az0, bz); az1 = std::max(az1, bz);
                ay1 = std::max(ay1, toBlockY(tf, v.z()));
            }
            if (ax1 <= ax0 || az1 <= az0) continue;
            const DomeLook look = lookOf(o);
            const int cx = int(std::floor((ax0 + ax1) * 0.5)), cz = int(std::floor((az0 + az1) * 0.5));
            const int R = std::max(2, std::min(look.glass ? 10 : 9, int(std::lround(std::min(ax1 - ax0, az1 - az0) * 0.5))));
            const int yTop = int(std::floor(ay1));
            int roofY = std::numeric_limits<int>::min();
            for (int y = yTop + 8; y >= yTop - 8; --y)
                if (store.get(cx, y, cz) != IdAir) { roofY = y; break; }
            if (roofY == std::numeric_limits<int>::min()) continue;
            if (look.glass)
            {   // a hemisphere: a shell of one block; the roof under it (the two upper rows inside the circle) is removed
                const double Rr = R + 0.5;
                for (int dz = -R; dz <= R; ++dz)
                    for (int dx = -R; dx <= R; ++dx)
                        if (dx * dx + dz * dz <= (Rr - 1.0) * (Rr - 1.0))
                            for (int y = roofY; y > roofY - 2; --y)
                                store.erase(cx + dx, y, cz + dz);
                for (int k = 0; k < R; ++k)
                    for (int dz = -R; dz <= R; ++dz)
                        for (int dx = -R; dx <= R; ++dx)
                        {
                            const double d = std::sqrt(double(dx * dx + dz * dz) + (k + 0.5) * (k + 0.5));
                            if (d <= Rr && d > Rr - 1.2 && store.get(cx + dx, roofY + 1 + k, cz + dz) == IdAir)
                                store.set(cx + dx, roofY + 1 + k, cz + dz, look.block);
                        }
                continue;
            }
            const int H = o.roofH >= 2.0 ? std::min(16, int(std::lround(o.roofH * tf.zScale))) : std::max(5, int(std::lround(R * 1.7)));
            for (int k = 0; k < H; ++k)
            {   // the profile of an onion: a narrow neck, a bulge at ~30% of the height, a taper to a point
                const double t = (k + 0.5) / H;
                const double f = t < 0.3 ? 0.8 + 0.2 * std::sin(t / 0.3 * 1.5707963) : std::pow(std::cos((t - 0.3) / 0.7 * 1.5707963), 0.75);
                const double rk = R * f;
                const int ri = int(std::ceil(rk));
                for (int dz = -ri; dz <= ri; ++dz)
                    for (int dx = -ri; dx <= ri; ++dx)
                        if (dx * dx + dz * dz <= rk * rk + 0.25 && store.get(cx + dx, roofY + 1 + k, cz + dz) == IdAir)
                            store.set(cx + dx, roofY + 1 + k, cz + dz, look.block);
            }
            if (look.spire)
                for (int k = 0; k < 3; ++k)
                    store.set(cx, roofY + 1 + H + k, cz, look.spire);
        }
    }
    for (const LiftJob& lj : liftJobs)
    {
        if (cancel && cancel->load()) { res.cancelled = true; res.error = QObject::tr("Export cancelled."); return res; }
        VoxelStore tmp;
        QHash<ColKey, int> tmpMin, tmpAnchor;
        // sites (chests, villagers, basements…) are not needed for the shaft — nullptr: no chests and no entities inside the cut-off part
        VoxelOut vo;
        vo.colMin = &tmpMin; vo.colAnchor = &tmpAnchor;
        const int added = voxelizeObject(*lj.o, tf, lj.id, tmp, vo, cancel, lj.sink, lj.extra, true,
                                         lj.structId, lj.woodSingle, lj.privMode);
        if (added == 0) { ++res.skippedCount; continue; }
        // the "host" top by columns: the highest non-empty block of the already written buildings
        QHash<ColKey, int> hostTop;
        int plane = std::numeric_limits<int>::min();
        for (auto cit = tmp.chunks.constBegin(); cit != tmp.chunks.constEnd(); ++cit)
        {
            const int bx0 = keyFirst(cit.key()) << 4, bz0 = keySecond(cit.key()) << 4;
            QSet<int> seen;
            for (auto sit = cit->constBegin(); sit != cit->constEnd(); ++sit)
            {
                const QVector<quint16>& sec = sit.value();
                for (int i = 0; i < sec.size(); ++i)
                {
                    if (!sec[i]) continue;
                    const int lc = (i & 15) | (((i >> 4) & 15) << 4);
                    if (seen.contains(lc)) continue;
                    seen.insert(lc);
                    const int x = bx0 + (lc & 15), z = bz0 + (lc >> 4);
                    for (int y = 319; y >= -64; --y)
                        if (store.get(x, y, z) != IdAir)
                        {
                            hostTop.insert(pairKey(x, z), y);
                            plane = std::max(plane, y);
                            break;
                        }
                }
            }
        }
        const bool hosted = !hostTop.isEmpty();
        QSet<ColKey> shaftCols;                                   // all the shaft columns (walls and interior) — to find the room
        for (auto cit = tmp.chunks.constBegin(); cit != tmp.chunks.constEnd(); ++cit)
        {
            const int bx0 = keyFirst(cit.key()) << 4, bz0 = keySecond(cit.key()) << 4;
            for (auto sit = cit->constBegin(); sit != cit->constEnd(); ++sit)
            {
                const int baseY = sit.key() << 4;
                const QVector<quint16>& sec = sit.value();
                for (int i = 0; i < sec.size(); ++i)
                {
                    if (!sec[i]) continue;
                    const int x = bx0 + (i & 15), z = bz0 + ((i >> 4) & 15), y = baseY + ((i >> 8) & 15);
                    if (hosted)
                    {
                        const auto ht = hostTop.constFind(pairKey(x, z));
                        const int lim = ht != hostTop.constEnd() ? ht.value() : plane;   // outside the host spot — by its highest point
                        if (y <= lim) continue;                                          // below/inside the host roof — not built
                    }
                    store.set(x, y, z, sec[i]);
                    shaftCols.insert(pairKey(x, z));
                }
            }
        }
        if (hosted && !shaftCols.isEmpty())
        {   // the shaft room above the roof: +25% chest chance (rarity — as for the treasures on building floors: 55/30/15)
            const qint64 firstCol = *std::min_element(shaftCols.constBegin(), shaftCols.constEnd());
            const quint32 hl = colHash(firstCol ^ 0x11F7ull);
            if (hl % 100u < 25u)
            {
                QVector<qint64> cand;
                for (qint64 ck : shaftCols)
                {
                    const int x = keyFirst(ck), z = keySecond(ck);
                    const auto ht = hostTop.constFind(ck);
                    const int y = (ht != hostTop.constEnd() ? ht.value() : plane) + 1;
                    if (store.get(x, y, z) != IdAir || store.get(x, y + 1, z) != IdAir || store.get(x, y - 1, z) == IdAir) continue;
                    bool inner = true;                              // a cell inside the shaft: all 8 neighboring columns are the shaft too
                    for (int dx = -1; dx <= 1 && inner; ++dx)
                        for (int dz = -1; dz <= 1; ++dz)
                            if (!shaftCols.contains(pairKey(x + dx, z + dz))) { inner = false; break; }
                    if (inner) cand.append(ck);
                }
                if (!cand.isEmpty())
                {
                    std::sort(cand.begin(), cand.end());
                    const qint64 ck = cand[int((hl >> 8) % quint32(cand.size()))];
                    const int x = keyFirst(ck), z = keySecond(ck);
                    const auto ht = hostTop.constFind(ck);
                    const int y = (ht != hostTop.constEnd() ? ht.value() : plane) + 1;
                    store.set(x, y, z, IdChest);
                    chests.append(ChestSite{ x, y, z, pickChestLoot(hl >> 12, 55, 30) });
                }
            }
        }
        if (!hosted)
        {   // a shaft without a host building — an independent structure: the ground is filled under it, as before.
            // (For a shaft on a roof colMin/colAnchor are not touched: otherwise an earth column would grow under the canopy outside the host.)
            for (auto it = tmpMin.constBegin(); it != tmpMin.constEnd(); ++it)
            {
                auto m = colMin.find(it.key());
                if (m == colMin.end() || it.value() < m.value()) colMin[it.key()] = it.value();
            }
            for (auto it = tmpAnchor.constBegin(); it != tmpAnchor.constEnd(); ++it)
            {
                auto m = colAnchor.find(it.key());
                if (m == colAnchor.end() || it.value() < m.value()) colAnchor[it.key()] = it.value();
            }
        }
        ++res.objectCount;
    }
    tmcStepLog("vox: elevator shafts ready");
    // Road drawing with priority (see the declaration of roadsHigh/roadsLow): first the dark gray
    // roadways (wool into the surface layer), on top of them the marking carpet into the air above the wool (other
    // blocks are not touched), and only then the light gray pedestrian/bike paths — first-wins in
    // colWool does not let them repaint the highway. In the old order a path that came earlier in the list
    // "ate" the highway cells together with the marking lines.
    {
        // DYNAMIC ROAD WIDTH: the standard widening (+2 / +1 blocks on each side) is kept, but on each section it is cut if it would run into a building or
        // come close to it (at least one block remains between the roadway and the wall); the strip cells lying under a building are excluded from the roadway.
        // It is done BEFORE any road is drawn: then colMin contains only objects (buildings, sites), not the already laid roadways.
        {
            auto isBld = [&](qint64 ck) { return colMin.contains(ck); };
            int nTrim = 0, nDrop = 0;
            auto rebuild = [&](RoadJob& j)
            {
                if (j.core.isEmpty()) return;
                QHash<ColKey, int> out;
                out.reserve(j.road.size());
                for (auto it = j.core.constBegin(); it != j.core.constEnd(); ++it)
                {
                    if (isBld(it.key())) { ++nDrop; continue; }                      // the strip under a building — not a road
                    out.insert(it.key(), it.value());
                    const int bx = keyFirst(it.key()), bz = keySecond(it.key());
                    for (int sgn = -1; sgn <= 1; sgn += 2)
                        for (int d = 1; d <= j.ext; ++d)
                        {
                            const qint64 c1 = j.axisX ? pairKey(bx, bz + sgn * d) : pairKey(bx + sgn * d, bz);
                            const qint64 c2 = j.axisX ? pairKey(bx, bz + sgn * (d + 1)) : pairKey(bx + sgn * (d + 1), bz);
                            if (isBld(c1) || isBld(c2)) { ++nTrim; break; }              // a building further on (or its wall close by) — this section is already
                            if (!out.contains(c1)) out.insert(c1, it.value());
                        }
                }
                j.road = out;
            };
            for (RoadJob& j : roadsHigh) rebuild(j);
            for (RoadJob& j : roadsLow) rebuild(j);
            for (RoadJob& j : roadsEarth) rebuild(j);
            if (nTrim + nDrop > 0) res.lairInfo += QObject::tr("; road width trimmed near buildings: %1 sections, cells under buildings removed: %2").arg(nTrim).arg(nDrop);
        }
        auto paintBody = [&](const RoadJob& j)
        {
            for (auto t2 = j.road.constBegin(); t2 != j.road.constEnd(); ++t2)
            {
                const int bx = keyFirst(t2.key()), bz = keySecond(t2.key());
                if (store.get(bx, t2.value(), bz) != IdAir) continue;     // a construction place — not a road
                if (j.woolId)
                {
                    // The roadway — a WOOL BLOCK instead of turf: colWool gives it to the surface layer
                    // of the ground, i.e. the road is flush with the terrain ("sunk", request). The cell
                    // above the wool remains air — the marking carpet of the main roads lands on it.
                    // First-wins: pedestrian/bike paths are drawn AFTER the roadways and do not repaint
                    // other people's wool — the priority of the main road is kept.
                    if (!colWool.contains(t2.key()))
                        colWool.insert(t2.key(), j.woolId);
                }
                else
                    store.set(bx, t2.value(), bz, j.cid);                 // no wool — a carpet, as before
                if (j.woolId && j.fullCarpet)
                    store.set(bx, t2.value(), bz, j.cid);                 // bike path: a carpet of the same color on top of the wool
                roadCols.insert(t2.key());                              // street lamps will stand along the strip edge
                if (j.car) autoRoadCols.insert(t2.key());               // bus stops turn to motor roads, not to paths
                auto mit = colMin.find(t2.key());                       // the ground exactly under the roadway
                if (mit == colMin.end() || t2.value() < mit.value())
                    colMin[t2.key()] = t2.value();
            }
        };
        for (const RoadJob& j : roadsHigh)
            paintBody(j);
        for (const RoadJob& j : roadsHigh)                               // marking — a carpet into the air above the wool
        {
            if (!j.markId) continue;
            // "ALONG the whole road" (user request): lines NOT by the bbox size — for an
            // oblique/bent strip it put the outer rows only where the strip touches the edges of the
            // size, i.e. transverse strokes at the ends. We split the cells into transverse
            // slices by the main axis; in each slice only the middle cell is marked —
            // the center line runs ALONG the road, repeating its turns (the edges are not marked, request).
            if (!j.lines.isEmpty())
            {   // the center marking follows the road axis itself (a solid 4-connected line), not the size slices: for oblique and bent roads the slices gave a dotted line "from corner to corner"
                for (const QVector<QPointF>& pl : j.lines)
                {
                    int px = std::numeric_limits<int>::min(), pz = 0;
                    auto putMark = [&](int bx, int bz)
                    {
                        const auto rit = j.road.constFind(pairKey(bx, bz));
                        if (rit == j.road.constEnd()) return;
                        const int y = rit.value();
                        const quint16 cur = store.get(bx, y, bz);
                        if (cur != IdAir && !(cur >= IdCarpetBase && cur < IdCarpetBase + kCarpetsCount)) return;
                        store.set(bx, y, bz, j.markId);
                    };
                    for (int k = 0; k + 1 < pl.size(); ++k)
                    {
                        const double len = std::hypot(pl[k + 1].x() - pl[k].x(), pl[k + 1].y() - pl[k].y());
                        const int n = std::max(1, int(std::ceil(len * 2.0)));
                        for (int i = 0; i <= n; ++i)
                        {
                            const double u = double(i) / double(n);
                            const int bx = int(std::floor(pl[k].x() + (pl[k + 1].x() - pl[k].x()) * u));
                            const int bz = int(std::floor(pl[k].y() + (pl[k + 1].y() - pl[k].y()) * u));
                            if (bx == px && bz == pz) continue;
                            if (px != std::numeric_limits<int>::min() && bx != px && bz != pz) putMark(bx, pz);   // a diagonal step — complete the corner, the line without gaps
                            putMark(bx, bz);
                            px = bx; pz = bz;
                        }
                    }
                }
                continue;
            }
            std::map<int, std::map<int, int>> slices;   // main axis -> (transverse -> Y)
            for (auto t2 = j.road.constBegin(); t2 != j.road.constEnd(); ++t2)
            {
                const int bx = keyFirst(t2.key()), bz = keySecond(t2.key());
                slices[j.axisX ? bx : bz][j.axisX ? bz : bx] = t2.value();
            }
            for (const auto& sl : slices)
            {
                const auto& across = sl.second;
                if (across.empty()) continue;
                auto mid = across.begin();
                std::advance(mid, int(across.size()) / 2);
                const int a = mid->first;                     // only the center line (request: the edges are not needed)
                const int y = across.at(a);
                const int bx = j.axisX ? sl.first : a, bz = j.axisX ? a : sl.first;
                const quint16 cur = store.get(bx, y, bz);
                if (cur != IdAir &&
                    !(cur >= IdCarpetBase && cur < IdCarpetBase + kCarpetsCount))
                    continue;                                 // other people's block (a wall) is not touched
                store.set(bx, y, bz, j.markId);
            }
        }
    tmcStepLog("vox: roads: roadways ready");
        // PEDESTRIAN CROSSINGS (zebra): strips of white carpet along the road, alternating with gaps across it. The crossing axis (across the roadway)
        // defines where and how to draw: a cell belongs to the zebra if it lies no further than 1.5 blocks from the axis ALONG the road (a strip 3 wide) and in the "white" part of the
        // alternation (the step is chosen so that the crossing starts and ends with a white strip). It lies ONLY on the cells of the motor road roadway —
        // at the height of and onto the roadway block itself, so it does not hang over the roadside/sidewalk and follows the terrain; other people's blocks (walls, lamps) are not touched.
        if (!crosswalks.isEmpty())
        {
            quint16 whiteC = 0;
            for (int i = 0; i < kCarpetsCount; ++i)
                if (qstrcmp(kCarpets[i].name, "minecraft:white_carpet") == 0) whiteC = quint16(IdCarpetBase + i);
            QHash<ColKey, int> roadAll;                                  // a motor road cell -> Y of the carpet
            for (const RoadJob& j : roadsHigh)
                for (auto t2 = j.road.constBegin(); t2 != j.road.constEnd(); ++t2)
                    if (!roadAll.contains(t2.key())) roadAll.insert(t2.key(), t2.value());
            int nOnRoad = 0, nCells = 0;
            if (whiteC)
                for (const QVector<QPointF>& pl : crosswalks)
                {
                    double total = 0.0;
                    for (int k = 0; k + 1 < pl.size(); ++k) total += std::hypot(pl[k + 1].x() - pl[k].x(), pl[k + 1].y() - pl[k].y());
                    if (total < 2.0) continue;                           // too short a crossing — not drawn
                    const int m = std::max(1, int(std::lround((total - 1.0) / 2.0)));
                    const double pitch = total / double(2 * m + 1);     // an odd number of strips: white-empty-…-white
                    constexpr double hw = 1.5;                           // half the zebra width along the road
                    int before = nCells;
                    double s0 = 0.0;
                    for (int k = 0; k + 1 < pl.size(); ++k)
                    {
                        const double ax = pl[k].x(), az = pl[k].y();
                        const double len = std::hypot(pl[k + 1].x() - ax, pl[k + 1].y() - az);
                        if (len < 1e-6) continue;
                        const double tx = (pl[k + 1].x() - ax) / len, tz = (pl[k + 1].y() - az) / len;   // along the crossing (across the road)
                        const double nx = -tz, nz = tx;                                                  // along the road
                        const int bx0 = int(std::floor(std::min(ax, pl[k + 1].x()) - hw - 1)), bx1 = int(std::floor(std::max(ax, pl[k + 1].x()) + hw + 1));
                        const int bz0 = int(std::floor(std::min(az, pl[k + 1].y()) - hw - 1)), bz1 = int(std::floor(std::max(az, pl[k + 1].y()) + hw + 1));
                        for (int bx = bx0; bx <= bx1; ++bx)
                            for (int bz = bz0; bz <= bz1; ++bz)
                            {
                                const double vx = bx + 0.5 - ax, vz = bz + 0.5 - az;
                                const double s = vx * tx + vz * tz, u = vx * nx + vz * nz;
                                if (s < 0.0 || s >= len || std::fabs(u) > hw) continue;
                                if ((int(std::floor((s0 + s) / pitch)) & 1) != 0) continue;             // an empty strip
                                const auto rit = roadAll.constFind(pairKey(bx, bz));
                                if (rit == roadAll.constEnd()) continue;                                  // not a roadway — not drawn
                                const quint16 cur = store.get(bx, rit.value(), bz);
                                if (cur != IdAir && !(cur >= IdCarpetBase && cur < IdCarpetBase + kCarpetsCount)) continue;
                                store.set(bx, rit.value(), bz, whiteC);
                                ++nCells;
                            }
                        s0 += len;
                    }
                    if (nCells > before) ++nOnRoad;
                }
            res.lairInfo += QObject::tr("; zebra on the roads: crossings %1 of %2 (cells %3)").arg(nOnRoad).arg(crosswalks.size()).arg(nCells);
        }
        for (const RoadJob& j : roadsLow)
            paintBody(j);
        for (const RoadJob& j : roadsEarth)                              // last: first-wins in colWool does not let them repaint
            paintBody(j);                                                // motor and pedestrian roads
    tmcStepLog("vox: zebras ready");
        // OUTLINE of the motor (dark) roads with light gray carpets along the roadway edge. The edges are roadway cells that have a
        // 4-neighbor outside this roadway. The outline does not cross other roads (pedestrian, dirt, other motor roads): a cell
        // is skipped if it belongs to another roadway or there is a cell of another road nearby (8 neighbors). Too narrow
        // roads (the inner cells are fewer than a third of the edge ones) are not outlined — the outline would cover them completely.
        {
            quint16 lgC = 0;
            for (int i = 0; i < kCarpetsCount; ++i)
                if (qstrcmp(kCarpets[i].name, "minecraft:light_gray_carpet") == 0) lgC = quint16(IdCarpetBase + i);
            if (lgC)
            {
                QHash<ColKey, int> ownerId;                                 // cell -> roadway number (the first one won, as in colWool)
                int idc = 0;
                auto regOwner = [&](const QVector<RoadJob>& v, QVector<int>* idsOut)
                {
                    for (const RoadJob& j : v)
                    {
                        if (idsOut) idsOut->append(idc);
                        for (auto t2 = j.road.constBegin(); t2 != j.road.constEnd(); ++t2)
                            if (!ownerId.contains(t2.key())) ownerId.insert(t2.key(), idc);
                        ++idc;
                    }
                };
                QSet<ColKey> parkAll;                                       // all the parking spots (rdcpol)
                for (const ParkSite& ps : parks) parkAll.unite(ps.cols);
                QVector<int> highIds;
                regOwner(roadsHigh, &highIds);
                regOwner(roadsLow, nullptr);
                regOwner(roadsEarth, nullptr);
                for (int ji = 0; ji < roadsHigh.size(); ++ji)
                {
                    const RoadJob& j = roadsHigh[ji];
                    const int me = highIds[ji];
                    QVector<qint64> edge;
                    int interior = 0;
                    for (auto t2 = j.road.constBegin(); t2 != j.road.constEnd(); ++t2)
                    {
                        if (ownerId.value(t2.key(), -1) != me) continue;
                        const int x = keyFirst(t2.key()), z = keySecond(t2.key());
                        const bool isEdge = !j.road.contains(pairKey(x + 1, z)) || !j.road.contains(pairKey(x - 1, z))
                                         || !j.road.contains(pairKey(x, z + 1)) || !j.road.contains(pairKey(x, z - 1));
                        if (isEdge) edge.append(t2.key()); else ++interior;
                    }
                    if (edge.isEmpty() || interior * 3 < edge.size()) continue;
                    for (qint64 ck : edge)
                    {
                        const int x = keyFirst(ck), z = keySecond(ck);
                        bool foreign = false;
                        for (int dx = -1; dx <= 1 && !foreign; ++dx)
                            for (int dz = -1; dz <= 1; ++dz)
                            {
                                const auto oit = ownerId.constFind(pairKey(x + dx, z + dz));
                                if (oit != ownerId.constEnd() && oit.value() != me) { foreign = true; break; }
                            }
                        if (foreign) continue;
                        for (int dx = -1; dx <= 1 && !foreign; ++dx)              // right next to a parking lot — no curb
                            for (int dz = -1; dz <= 1; ++dz)
                                if (parkAll.contains(pairKey(x + dx, z + dz))) { foreign = true; break; }
                        if (foreign) continue;
                        for (const QPointF& e : j.ends)                           // at the road ends — no curb
                            if (std::hypot(x + 0.5 - e.x(), z + 0.5 - e.y()) < j.endR) { foreign = true; break; }
                        if (foreign) continue;
                        const int y = j.road.value(ck);
                        if (store.get(x, y, z) == IdAir) store.set(x, y, z, lgC);
                    }
                }
            }
        }
    }
    tmcStepLog("vox: roads finished");
    if (store.chunks.isEmpty())
    {
        res.error = QObject::tr("Could not voxelize a single object (empty or too huge meshes).");
        return res;
    }

    // Diagnostics of the export step times: MCX_LOG_STEPS=1 in the environment -> "[mcx] step ms" lines to stderr
    // (needed to find heavy steps on big projects; in the normal mode it prints nothing).
    tmcStepLog("export: object voxelization finished");
    if (qEnvironmentVariableIsSet("MCX_STOP_AFTER_VOX")) { res.ok = true; return res; }   // debug: measure the voxelization without the rest of the export
    logSteps = qEnvironmentVariableIsSet("MCX_LOG_STEPS");
    QElapsedTimer stepTimer; stepTimer.start();
    // The start of the step of stage st (the stage name — in the window caption, see the tqt 3D window, updateLoadStatus): without a counter
    // (total=0) the caption just shows WHAT is being done now, not the "hanging" percent of the previous step.
    auto beginStep = [&](int st, int total = 0) { if (progress) progress(0, total, st); };
    auto lap = [&](const char* name)
    {
        if (!logSteps) { stepTimer.restart(); return; }
        const qint64 ms = stepTimer.restart();
        // MCX_LOG_STEPS=1 — to stderr; MCX_LOG_STEPS=path — append the lines to a file (a GUI application without a console)
        const QByteArray target = qgetenv("MCX_LOG_STEPS");
        if (target.size() > 1)
        {
            QFile f(QString::fromLocal8Bit(target));
            if (f.open(QIODevice::Append | QIODevice::Text)) f.write(QStringLiteral("[mcx] %1 %2 %3 ms\n").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), QString::fromUtf8(name)).arg(ms).toUtf8());
        }
        else fprintf(stderr, "[mcx] %s %lld ms\n", name, (long long)ms);
    };
    // 3. Ground: we fill ALL the columns of ALL the chunks touched by objects. Earlier the ground was
    // only a "skirt" under the objects themselves, but a chunk is written with the full status and is not generated by the game —
    // the empty columns inside it looked like drops to the abyss exactly along the chunk border grid
    // ("the ground is pushed through, a hole through the bedrock"). The flat ground level = the median of the heights under the objects
    // (it also goes to the generator so that neighboring chunks join without steps); the terrain above the
    // level is kept as a hill, below it is filled up to the level (no pits remain).
    hasTerrain = bool(heightAt);
    // FALLBACK PALE GARDEN (data without big forests and meadows, e.g. an OSM extract): if there is neither a forest nor a meadow with an allowed code
    // with an area of 2000 columns or more, we allocate an empty 15×15 chunk site nearest to the data (on the export border or in a big void
    // inside it) and declare it a forest with an allowed code — then it is processed as an ordinary Pale Garden (biome, donor, mansion).
    {
        qint64 bestEligible = 0;
        for (const VegSite& v : forests)
        {
            if (v.park || v.intra || !isPaleForestCode(v.code)) continue;
            qint64 a = 0;
            for (const ColSpan& c : v.spans) a += c.z1 - c.z0 + 1;
            bestEligible = std::max(bestEligible, a);
        }
        for (const VegSite& v : grassPale)
        {
            qint64 a = 0;
            for (const ColSpan& c : v.spans) a += c.z1 - c.z0 + 1;
            bestEligible = std::max(bestEligible, a);
        }
        if (bestEligible < 2000 && world.paleForestFallback)
        {
            constexpr int S = 15;                                       // the site side, chunks (the forest template is 14×14)
            QSet<ColKey> occ;                                           // occupied chunks (key pairKey(cx, cz))
            for (auto it = store.chunks.constBegin(); it != store.chunks.constEnd(); ++it) occ.insert(it.key());
            auto occCol = [&](qint64 ck) { occ.insert(pairKey(keyFirst(ck) >> 4, keySecond(ck) >> 4)); };
            for (auto it = colPlant.constBegin(); it != colPlant.constEnd(); ++it) occCol(it.key());
            for (auto it = colWool.constBegin(); it != colWool.constEnd(); ++it) occCol(it.key());
            for (auto it = colWater.constBegin(); it != colWater.constEnd(); ++it) occCol(it.key());
            for (const qint64 ck : roadCols) occCol(ck);
            for (const QList<qint64>& pc : pads) for (const qint64 ck : pc) occCol(ck);
            for (const ParkSite& ps : parks) for (const qint64 ck : ps.cols) occCol(ck);
            for (const VegSite& v : forests)
                for (const ColSpan& c : v.spans)
                    for (int cz = c.z0 >> 4; cz <= (c.z1 >> 4); ++cz) occ.insert(pairKey(c.x >> 4, cz));
            if (!occ.isEmpty())
            {
                int bx0 = std::numeric_limits<int>::max(), bz0 = bx0, bx1 = std::numeric_limits<int>::min(), bz1 = bx1;
                double sumX = 0, sumZ = 0;
                for (const qint64 k : occ)
                {
                    const int cx = keyFirst(k), cz = keySecond(k);
                    bx0 = std::min(bx0, cx); bx1 = std::max(bx1, cx); bz0 = std::min(bz0, cz); bz1 = std::max(bz1, cz);
                    sumX += cx; sumZ += cz;
                }
                const double midX = sumX / occ.size(), midZ = sumZ / occ.size();
                const int m = S + 2;                                    // a margin around the data
                const int gx0 = bx0 - m, gz0 = bz0 - m;
                const int W = (bx1 - bx0 + 1) + 2 * m, H = (bz1 - bz0 + 1) + 2 * m;
                const qint64 cells = qint64(W) * H;
                if (cells <= 40000000)
                {
                    auto gi = [W](int x, int z) { return z * W + x; };
                    QVector<qint16> dist(int(cells), qint16(-1));
                    QVector<int> queue;
                    queue.reserve(occ.size() * 2);
                    for (const qint64 k : occ) { const int i = gi(keyFirst(k) - gx0, keySecond(k) - gz0); dist[i] = 0; queue << i; }
                    for (int qi = 0; qi < queue.size(); ++qi)           // the distance (Chebyshev) from the occupied chunks
                    {
                        const int i = queue[qi], x = i % W, z = i / W;
                        for (int dz = -1; dz <= 1; ++dz)
                            for (int dx = -1; dx <= 1; ++dx)
                            {
                                const int nx = x + dx, nz = z + dz;
                                if (nx < 0 || nz < 0 || nx >= W || nz >= H) continue;
                                const int ni = gi(nx, nz);
                                if (dist[ni] >= 0) continue;
                                dist[ni] = qint16(std::min<int>(dist[i] + 1, 30000));
                                queue << ni;
                            }
                    }
                    QVector<int> pre(int(qint64(W + 1) * (H + 1)), 0);  // prefix sums of occupancy
                    for (int z = 0; z < H; ++z)
                        for (int x = 0; x < W; ++x)
                            pre[z * (W + 1) + x + 1 + (W + 1)] = pre[z * (W + 1) + x + 1] + pre[(z + 1) * (W + 1) + x] - pre[z * (W + 1) + x] + (dist[gi(x, z)] == 0 ? 1 : 0);
                    auto occIn = [&](int x0, int z0) { return pre[(z0 + S) * (W + 1) + x0 + S] - pre[z0 * (W + 1) + x0 + S] - pre[(z0 + S) * (W + 1) + x0] + pre[z0 * (W + 1) + x0]; };
                    int bestX = -1, bestZ = -1, bestMin = std::numeric_limits<int>::max();
                    double bestC = 1e30;
                    for (int z0 = 0; z0 + S <= H; ++z0)
                        for (int x0 = 0; x0 + S <= W; ++x0)
                        {
                            if (occIn(x0, z0) != 0) continue;
                            int mn = std::numeric_limits<int>::max();   // the site cell nearest to the data lies on its border
                            for (int t = 0; t < S && mn > 1; ++t)
                                mn = std::min({ mn, int(dist[gi(x0 + t, z0)]), int(dist[gi(x0 + t, z0 + S - 1)]), int(dist[gi(x0, z0 + t)]), int(dist[gi(x0 + S - 1, z0 + t)]) });
                            const double cx = gx0 + x0 + 0.5 * S - midX, cz = gz0 + z0 + 0.5 * S - midZ;
                            const double c = cx * cx + cz * cz;
                            if (mn < bestMin || (mn == bestMin && c < bestC)) { bestMin = mn; bestC = c; bestX = x0; bestZ = z0; }
                        }
                    if (bestX >= 0)
                    {
                        const int cx0 = gx0 + bestX, cz0 = gz0 + bestZ;
                        VegSite vs;
                        vs.treeH = 5;
                        vs.code = 71111110;
                        for (int x = cx0 * 16; x < (cx0 + S) * 16; ++x) vs.spans << ColSpan{ x, cz0 * 16, (cz0 + S) * 16 - 1 };
                        vs.minX = cx0 * 16; vs.maxX = (cx0 + S) * 16 - 1; vs.minZ = cz0 * 16; vs.maxZ = (cz0 + S) * 16 - 1;
                        forests << vs;
                        res.lairInfo += QObject::tr("%1Pale Garden: there is no suitable vegetation in the data — an empty site of %2×%2 chunks was allocated (chunks %3…%4 along X, %5…%6 along Z, gap to the data %7 chunks)")
                                            .arg(res.lairInfo.isEmpty() ? QString() : QStringLiteral("; ")).arg(S).arg(cx0).arg(cx0 + S - 1).arg(cz0).arg(cz0 + S - 1).arg(bestMin - 1);
                    }
                }
            }
        }
    }
    // We create in advance the chunks where the vegetation will land (a tree crown is ±2 blocks from the column of
    // the trunk): otherwise planting after the earth fill would create "clean" chunks with a crown over
    // the void — the same pit along the grid that we cure below.
    // Earlier every forest column made 25 accesses to the store.chunks hash (95 million columns × 25 — minutes in a single
    // thread). Now: the forest columns are sorted (X, then Z), so the chunk range ±2 blocks almost
    // does not change from column to column — we skip repeats; the other maps are first collected into a small
    // set of unique chunks, and store.chunks is created once per chunk.
    {
        QSet<ColKey> need;
        for (const VegSite& site : forests)
            for (const ColSpan& c : site.spans)
            {   // a tree crown is ±2 blocks from the trunk column: chunks by x±2 and by the whole z range of the segment ±2
                const int a0 = (c.x - 2) >> 4, a1 = (c.x + 2) >> 4, b0 = (c.z0 - 2) >> 4, b1 = (c.z1 + 2) >> 4;
                for (int cx = a0; cx <= a1; ++cx)
                    for (int cz = b0; cz <= b1; ++cz)
                        need.insert(pairKey(cx, cz));
            }
        for (auto it = colPlant.constBegin(); it != colPlant.constEnd(); ++it)
            need.insert(pairKey(keyFirst(it.key()) >> 4, keySecond(it.key()) >> 4));
        for (auto it = colWool.constBegin(); it != colWool.constEnd(); ++it)
            need.insert(pairKey(keyFirst(it.key()) >> 4, keySecond(it.key()) >> 4));
        for (auto it = colWater.constBegin(); it != colWater.constEnd(); ++it)
            need.insert(pairKey(keyFirst(it.key()) >> 4, keySecond(it.key()) >> 4));
        for (qint64 k : need)
            store.chunks[k];
    }
    yShift = 0;
                      // the lambda reads it by reference — the height survey goes before the raise, all the late
                      // calls (fill/vegetation) already get the shifted terrain.
    auto terrainTopBlock = [&](int cx, int cz) -> int
    {   // the terrain top in blocks (INT_MIN — there is no terrain at this point)
        if (!hasTerrain) return std::numeric_limits<int>::min();
        const double wx = (double(cx) + 0.5) / tf.sx + tf.cx0;       // blocks -> map units
        const double wy = tf.cy0 - (double(cz) + 0.5) / tf.sz;
        const double h = heightAt(wx, wy);
        if (h <= -9998.0) return std::numeric_limits<int>::min();
        return int(std::floor((h - tf.yBase) * tf.zScale + 1.0)) + yShift;
    };
    for (auto tcIt = store.chunks.constBegin(); tcIt != store.chunks.constEnd(); ++tcIt) touchedChunks.append(tcIt.key());   // a snapshot: later sections are added to the existing chunks
    // "Full terrain" (user request, only for the "normal world"): if the crust stands only
    // under the object chunks, the city is a small clearing with a cliff along the perimeter. We cover with our
    // ground an apron M chunks thick around the city rectangle (and the empty holes inside it);
    // beyond the apron — the game's noise terrain (the junction is known, smoothing is a separate future task).
    fillChunks = touchedChunks;
    {   // Spots without buildings (csrpol sites, parking lots, greenhouses, quarries): their chunks must get ground too, otherwise a lone site stays without a surface
        QSet<ColKey> have(fillChunks.constBegin(), fillChunks.constEnd());
        auto addCols = [&](const QList<qint64>& cs)
        {
            qint64 last = std::numeric_limits<qint64>::min();
            for (qint64 ck : cs)
            {
                const qint64 c = pairKey(keyFirst(ck) >> 4, keySecond(ck) >> 4);
                if (c == last) continue;
                last = c;
                if (!have.contains(c)) { have.insert(c); fillChunks.append(c); }
            }
        };
        for (const QList<qint64>& pc : pads) addCols(pc);
        for (const ParkSite& ps : parks)
        {
            QList<qint64> pcols;
            pcols.reserve(ps.cols.size());
            for (const ColKey& pk : ps.cols) pcols.append(pk.v);
            addCols(pcols);
        }
    }
    // The REAL export border — the rectangle of chunks with objects (+ the apron in the "normal world"). The chamfer and
    // the "flowing out" water/lava (3.59b/c) are needed only on it. The empty places INSIDE the rectangle (where there are no
    // objects and, in a flat world, no chunks with ground either) — are not an edge: the game itself will finish the same
    // flat ground there (in the "normal world" the holes inside the rectangle are filled with our ground anyway).
    rcx0 = std::numeric_limits<int>::max();
    rcz0 = std::numeric_limits<int>::max();
    rcx1 = std::numeric_limits<int>::min();
    rcz1 = std::numeric_limits<int>::min();
    for (qint64 ckKey : touchedChunks)
    {
        rcx0 = std::min(rcx0, keyFirst(ckKey)); rcz0 = std::min(rcz0, keySecond(ckKey));
        rcx1 = std::max(rcx1, keyFirst(ckKey)); rcz1 = std::max(rcz1, keySecond(ckKey));
    }
    rectM = world.normalSurroundings ? std::max(0, std::min(world.apronChunks, 64)) : 0;
    auto inExportRect = [=](int cx, int cz)
    { return cx >= rcx0 - rectM && cx <= rcx1 + rectM && cz >= rcz0 - rectM && cz <= rcz1 + rectM; };
    if (world.normalSurroundings)
    {
        const int M = std::max(0, std::min(world.apronChunks, 64)); // the apron, chunks (a setting in the dialog)
        int cx0 = std::numeric_limits<int>::max(), cz0 = std::numeric_limits<int>::max();
        int cx1 = std::numeric_limits<int>::min(), cz1 = std::numeric_limits<int>::min();
        for (qint64 ckKey : touchedChunks)
        {
            cx0 = std::min(cx0, keyFirst(ckKey)); cz0 = std::min(cz0, keySecond(ckKey));
            cx1 = std::max(cx1, keyFirst(ckKey)); cz1 = std::max(cz1, keySecond(ckKey));
        }
        const QSet<ColKey> touched(touchedChunks.constBegin(), touchedChunks.constEnd());
        for (int cx = cx0 - M; cx <= cx1 + M; ++cx)
            for (int cz = cz0 - M; cz <= cz1 + M; ++cz)
            {
                const qint64 k = pairKey(cx, cz);
                if (touched.contains(k)) continue;
                fillChunks << k;       // M=0: only the "hole" finishing of the city rectangle remains
            }
    }
    // The chunks under floating islands get ground too (an island must not be cut along the chunk border without soil); repeats are removed.
    for (const IslandSite& is : islandSites)
        if (is.kind == 2)
        for (int icz = (is.cz - is.rz - 2) >> 4; icz <= ((is.cz + is.rz + 2) >> 4); ++icz)
            for (int icx = (is.cx - is.rx - 2) >> 4; icx <= ((is.cx + is.rx + 2) >> 4); ++icx)
                fillChunks << pairKey(icx, icz);
    std::sort(fillChunks.begin(), fillChunks.end());
    fillChunks.erase(std::unique(fillChunks.begin(), fillChunks.end()), fillChunks.end());
    // FOREST TO THE CHUNK EDGE ON THE EXPORT BORDER: if a forest gets into the outermost chunk of the exported area (the chunk has no neighbor in fillChunks), this chunk is entirely
    // added to the forest (the same biome, trees by the common rules) — otherwise a 6+ block "clearing" remains between the occupied part of the chunk and the game world.
    // The occupied columns (buildings, water, roads, sites) are not included in the extension.
    {
        const QSet<ColKey> fillSet(fillChunks.constBegin(), fillChunks.constEnd());
        // the export border = the outer ring of the rectangle of data chunks (chunks with objects + forest chunks); the apron in the noise mode lies beyond it
        int dcx0 = rcx0, dcx1 = rcx1, dcz0 = rcz0, dcz1 = rcz1;
        for (const VegSite& vs : forests)
            for (const ColSpan& c : vs.spans)
            {
                dcx0 = std::min(dcx0, c.x >> 4); dcx1 = std::max(dcx1, c.x >> 4);
                dcz0 = std::min(dcz0, c.z0 >> 4); dcz1 = std::max(dcz1, c.z1 >> 4);
            }
        auto isEdge = [&](int cx, int cz)
        { return cx == dcx0 || cx == dcx1 || cz == dcz0 || cz == dcz1 || !fillSet.contains(pairKey(cx - 1, cz)) || !fillSet.contains(pairKey(cx + 1, cz)) || !fillSet.contains(pairKey(cx, cz - 1)) || !fillSet.contains(pairKey(cx, cz + 1)); };
        int nExt = 0;
        for (VegSite& vs : forests)
        {
            if (vs.park) continue;
            QSet<ColKey> edgeCh;
            for (const ColSpan& c : vs.spans)
                for (int cz = c.z0 >> 4; cz <= (c.z1 >> 4); ++cz)
                    if (fillSet.contains(pairKey(c.x >> 4, cz)) && isEdge(c.x >> 4, cz)) edgeCh.insert(pairKey(c.x >> 4, cz));
            if (edgeCh.isEmpty()) continue;
            QVector<ColSpan> add;
            for (const qint64 ek : edgeCh)
            {
                const int ecx = keyFirst(ek), ecz = keySecond(ek);
                for (int x = ecx * 16; x < ecx * 16 + 16; ++x)
                {
                    int run = -1;
                    for (int z = ecz * 16; z <= ecz * 16 + 16; ++z)
                    {
                        const qint64 ck = pairKey(x, z);
                        const bool ok = z < ecz * 16 + 16 && !colMin.contains(ck) && !colWater.contains(ck) && !colWool.contains(ck) && !roadCols.contains(ck);
                        if (ok && run < 0) run = z;
                        else if (!ok && run >= 0) { add.append(ColSpan{ x, run, z - 1 }); run = -1; }
                    }
                }
            }
            vs.spans += add;
            vs.spans = mergeSpans(vs.spans);
            vs.minX = vs.spans.first().x; vs.maxX = vs.spans.last().x;
            vs.minZ = std::numeric_limits<int>::max(); vs.maxZ = std::numeric_limits<int>::min();
            for (const ColSpan& c : vs.spans) { vs.minZ = std::min(vs.minZ, c.z0); vs.maxZ = std::max(vs.maxZ, c.z1); }
            nExt += int(edgeCh.size());
        }
        // Fields and grass (any vegetation without trees — colPlant): the outermost chunk of the area entirely gets the plant that prevails in it; the forest columns are not touched.
        {
            QVector<ColSpan> allF;
            for (const VegSite& vs : forests) allF += vs.spans;
            allF = mergeSpans(allF);
            QHash<ColKey, QHash<quint16, int>> cnt;
            for (auto pit = colPlant.constBegin(); pit != colPlant.constEnd(); ++pit)
            {
                const int cx = keyFirst(pit.key()) >> 4, cz = keySecond(pit.key()) >> 4;
                if (fillSet.contains(pairKey(cx, cz)) && isEdge(cx, cz)) ++cnt[pairKey(cx, cz)][pit.value()];
            }
            int nFld = 0;
            for (auto cit = cnt.constBegin(); cit != cnt.constEnd(); ++cit)
            {
                quint16 best = 0; int bestN = -1;
                for (auto qit = cit.value().constBegin(); qit != cit.value().constEnd(); ++qit)
                    if (qit.value() > bestN || (qit.value() == bestN && qit.key() < best)) { best = qit.key(); bestN = qit.value(); }
                const int ecx = keyFirst(cit.key()), ecz = keySecond(cit.key());
                for (int z = ecz * 16; z < ecz * 16 + 16; ++z)
                    for (int x = ecx * 16; x < ecx * 16 + 16; ++x)
                    {
                        const qint64 ck = pairKey(x, z);
                        if (colPlant.contains(ck) || colMin.contains(ck) || colWater.contains(ck) || colWool.contains(ck) || roadCols.contains(ck) || spansContain(allF, x, z)) continue;
                        colPlant.insert(ck, best);
                    }
                ++nFld;
            }
            if (nFld > 0) res.lairInfo += QObject::tr("; fields and grass extended to the chunk edge on the export border: %1 chunks").arg(nFld);
        }
        if (nExt > 0) res.lairInfo += QObject::tr("; forest extended to the chunk edge on the export border: %1 chunks").arg(nExt);
    }
    // "Ground" is the heaviest phase (every column of every filled chunk), so it is also
    // covered by progress: total = height survey (touched chunks) + fill (in the noise mode —
    // touched + the apron).
    gSurvey = int(touchedChunks.size());
    gTotal = gSurvey + int(fillChunks.size());
    // Tree planting (3.5) is part of the same stage: we count the 6×6 grid cells over the forest bbox so that the counter
    // "X of N" goes on and does not freeze after the earth fill (the scale is monotonic: ground, then planting).
    vegCells64 = 0;
    for (const VegSite& site : forests)
    {   // the same clamp as in the planting loops: a site wider than the world must not inflate the counter
        const int vgX0 = std::max(site.minX, (rcx0 - rectM) * 16), vgX1 = std::min(site.maxX, (rcx1 + rectM + 1) * 16 - 1);
        const int vgZ0 = std::max(site.minZ, (rcz0 - rectM) * 16), vgZ1 = std::min(site.maxZ, (rcz1 + rectM + 1) * 16 - 1);
        if (vgX1 < vgX0 || vgZ1 < vgZ0) continue;
        vegCells64 += qint64(vgX1 / 6 - vgX0 / 6 + 2) * qint64(vgZ1 / 6 - vgZ0 / 6 + 2);
    }
    vegCells = int(std::min<qint64>(vegCells64, 1000000000));
    progTotal = gTotal;
    planeCols = 0;
    for (qint64 ckKey : touchedChunks)
    {
        if (progress && (planeCols & 0x3F) == 0)
            progress(planeCols, progTotal, 1);                 // the "ground" phase — height survey
        ++planeCols;
        const int bx0 = keyFirst(ckKey) << 4, bz0 = keySecond(ckKey) << 4;
        // Only the MEDIAN of the heights is needed: a 4×4 sample of the 256 columns of a chunk gives the same median and is 16 times cheaper
        // (on hundreds of thousands of chunks the 512 accesses to the huge colMin/colAnchor hashes per chunk were the main cost).
        for (int lx = 1; lx < 16; lx += 4)
            for (int lz = 1; lz < 16; lz += 4)
            {
                const qint64 ck = pairKey(bx0 + lx, bz0 + lz);
                int cand = terrainTopBlock(bx0 + lx, bz0 + lz);
                const auto ait = colAnchor.constFind(ck);
                const auto oit = colMin.constFind(ck);
                if (ait != colAnchor.constEnd())
                    cand = std::max(cand, ait.value() - 1);   // the level WITHOUT the foundation: the ground at the former mark
                else if (oit != colMin.constEnd())
                    cand = std::max(cand, oit.value() - 1);
                if (cand > std::numeric_limits<int>::min() / 2)
                    planeSamples << cand;
            }
    }
    // The reference level ("plane"): under buildings without terrain — the median; with terrain — the lower quartile (20%): the terrain is now kept
    // IN FULL (valleys too), and the plane is needed only as a mark for columns without terrain data, for the junction with the generator and for
    // smoothing the apron (otherwise everything below the median was filled up to it and buildings in valleys ended up walled into the ground).
    genGroundTop = 0;
    planeMax = 0;
    if (!planeSamples.isEmpty())
    {
        const int qi = hasTerrain ? planeSamples.size() / 5 : planeSamples.size() / 2;
        std::nth_element(planeSamples.begin(), planeSamples.begin() + qi, planeSamples.end());
        genGroundTop = planeSamples[qi];
        planeMax = *std::max_element(planeSamples.constBegin(), planeSamples.constEnd());
    }
    genGroundTop = std::max(0, std::min(genGroundTop, 255));    // one height both for us and for the generator
    // "Normal seed-based world" (user request): the marks of the WDF terrain are in no way connected with the
    // absolute Minecraft Y — in the free mode the city would stand at y≈0, i.e. 60+ blocks below
    // sea level, and the noise surroundings would surround it with a "huge pit". We raise the whole export
    // (objects, ground, terrain — everything written) so that the ground plane ends up slightly
    // ABOVE the average Minecraft land height (sea level 62, a typical plain ~65-70): the city stands
    // on a ledge, not in a pit.
    if (world.normalSurroundings)
    {
        yShift = 68 - genGroundTop;                               // with terrain it can also go down: the reference level is 68, not "terrain 140 above the surroundings"
        yShift = std::min(yShift, std::max(0, 200 - genGroundTop));   // a reserve for high-rises (the terrain is cut at 255)
        yShift = std::min(yShift, 250 - planeMax);                    // the highest point is not above 250
        yShift = std::max(yShift, -std::max(0, genGroundTop - 40));  // and we do not go too low: the reference level >= 40
        if (yShift != 0)
        {
            store.translateY(yShift);                                 // blocks (the sections will be rebuilt)
            for (auto it = colMin.begin(); it != colMin.end(); ++it) it.value() += yShift;
            for (auto it = colAnchor.begin(); it != colAnchor.end(); ++it) it.value() += yShift;
            for (ChestSite& cs : chests) cs.y += yShift;              // chests — together with the houses
            for (PaintingSite& pt : paints) pt.y += yShift;           // paintings — the same coordinates
            for (MannequinSite& ms : mannequins) ms.y += yShift;      // mannequins (the entity Pos)
            for (NpcSite& ns : npcs) ns.y += yShift;                  // villagers — together with the houses
            for (FrameSite& fr : frames) fr.y += yShift;              // frames — too
            for (BasementSite& bs : basements) bs.wy += yShift;       // basements — too
            for (SignCand& sg : signCands) sg.y += yShift;            // signs — too
            for (BannerSite& bn : bannerSites) bn.y += yShift;        // banners — too
            for (LairZone& lz : lairZones) { lz.y0 += yShift; lz.y1 += yShift; }   // spawn zones — too
            for (IslandSite& is : islandSites) is.yTop += yShift;                  // islands — too
            for (SpawnerSite& sp : spawners) sp.y += yShift;          // spawners — their own BE
            genGroundTop += yShift;                                   // terrainTopBlock will add it itself (by reference)
        }
    }
    lap("g0 before the Pale Garden");
    // PALE GARDEN: the biggest forest (by the number of columns) becomes a full "Pale Garden" biome — the biome on
    // 4×4 cells along the forest outline, trees of pale oak (a 2×2 trunk, some with a "creaking heart"), moss, "eyeblossom" flowers.
    paleIdx = -1;
    {
        qint64 bestArea = 0;
        // a forest set by the user by chunk (WorldOptions::paleForestChunk): the forest nearest to the chunk center; otherwise — the biggest
        bool havePick = false;
        double pickX = 0, pickZ = 0;
        {
            static const QRegularExpression numRe3(QStringLiteral("-?\\d+"));
            QVector<int> nums;
            auto it = numRe3.globalMatch(world.paleForestChunk);
            while (it.hasNext()) nums.append(it.next().captured().toInt());
            if (nums.size() >= 2)
            {
                havePick = true;
                pickX = (nums.size() >= 3 ? nums[0] : nums[0]) * 16 + 8;
                pickZ = (nums.size() >= 3 ? nums[2] : nums[1]) * 16 + 8;
            }
        }
        double bestD = 1e18;
        for (int fi = 0; fi < forests.size(); ++fi)
        {
            if (forests[fi].park || forests[fi].intra || !isPaleForestCode(forests[fi].code)) continue;   // Pale Garden — only on the allowed vegpol codes (forest, undergrowth, shrubs, industrial crops, meadow); not parks 71126000, not lawns 71329000, not vgbpol                               // a park is not a "forest"
            qint64 a = 0;
            for (const ColSpan& c : forests[fi].spans) a += c.z1 - c.z0 + 1;
            if (havePick)
            {
                if (a < 2000) continue;
                double d = 1e18;
                for (const ColSpan& c : forests[fi].spans)
                {
                    const double dz = pickZ < c.z0 ? c.z0 - pickZ : (pickZ > c.z1 ? pickZ - c.z1 : 0.0);
                    d = std::min(d, std::hypot(c.x - pickX, dz));
                }
                if (d < bestD) { bestD = d; bestArea = a; paleIdx = fi; }
            }
            else if (a > bestArea) { bestArea = a; paleIdx = fi; }
        }
        if (havePick && paleIdx >= 0 && bestD > 200.0)
        {   // there is no forest far from the given chunk — we take the biggest one
            res.lairInfo += QObject::tr("%1Pale Garden: there is no forest near the given chunk").arg(res.lairInfo.isEmpty() ? QString() : QStringLiteral("; "));
            bestArea = 0; paleIdx = -1;
            for (int fi = 0; fi < forests.size(); ++fi)
            {
                if (forests[fi].park || forests[fi].intra || !isPaleForestCode(forests[fi].code)) continue;   // Pale Garden — only on the allowed vegpol codes (forest, undergrowth, shrubs, industrial crops, meadow); not parks 71126000, not lawns 71329000, not vgbpol
                qint64 a = 0;
                for (const ColSpan& c : forests[fi].spans) a += c.z1 - c.z0 + 1;
                if (a > bestArea) { bestArea = a; paleIdx = fi; }
            }
        }
        // Meadow/grassy vegetation without height is a candidate only if its area is MORE THAN TWICE the best forest with height; with comparable areas the forest with height wins
        if (!havePick && !grassPale.isEmpty())
        {
            int gBest = -1; qint64 gArea = 0;
            for (int gi = 0; gi < grassPale.size(); ++gi)
            {
                qint64 a = 0;
                for (const ColSpan& c : grassPale[gi].spans) a += c.z1 - c.z0 + 1;
                if (a > gArea) { gArea = a; gBest = gi; }
            }
            const qint64 treeBest = paleIdx >= 0 ? bestArea : 0;
            if (gBest >= 0 && gArea >= 2000 && (paleIdx < 0 || treeBest < 2000 || gArea > 2 * treeBest))
            {
                forests.append(grassPale[gBest]);
                paleIdx = forests.size() - 1;
                bestArea = gArea;
                res.lairInfo += QObject::tr("%1Pale Garden: meadow vegetation was chosen (%2 m², more than twice the best forest with height — %3 m²)")
                                    .arg(res.lairInfo.isEmpty() ? QString() : QStringLiteral("; ")).arg(gArea).arg(treeBest);
            }
        }
        if (paleIdx >= 0 && bestArea >= 2000)
        {
            for (const ColSpan& c : forests[paleIdx].spans)
                for (int z4 = c.z0 >> 2; z4 <= (c.z1 >> 2); ++z4)
                    paleCells.insert(pairKey(c.x >> 2, z4));
            {   // coordinates for the player (/tp): a point inside the forest closer to the center of gravity, the chunk (as in F3), the size
                const VegSite& pf = forests[paleIdx];
                double sx = 0, sz = 0; qint64 n = 0;
                for (const ColSpan& c : pf.spans) { const qint64 len = c.z1 - c.z0 + 1; sx += double(c.x) * len; sz += 0.5 * (c.z0 + c.z1) * len; n += len; }
                int px = int(std::lround(sx / double(n))), pz = int(std::lround(sz / double(n)));
                if (!spansContain(pf.spans, px, pz))
                {   // the center of gravity is outside the forest (a ring, a horseshoe) — the nearest column from the middle of the list
                    const ColSpan& mid = pf.spans[pf.spans.size() / 2];
                    px = mid.x; pz = (mid.z0 + mid.z1) / 2;
                }
                res.lairInfo += QObject::tr("%1Pale Garden: %2 m², center x=%3 z=%4 (chunk %5 %6), bounds x %7…%8, z %9…%10")
                                    .arg(res.lairInfo.isEmpty() ? QString() : QStringLiteral("; ")).arg(bestArea).arg(px).arg(pz).arg(px >> 4).arg(pz >> 4)
                                    .arg(pf.minX).arg(pf.maxX).arg(pf.minZ).arg(pf.maxZ);
            }
        }
        else
        {
            paleIdx = -1;
            res.lairInfo += QObject::tr("%1Pale Garden: no suitable forest").arg(res.lairInfo.isEmpty() ? QString() : QStringLiteral("; "));
        }
    }
    // Spruce forests (71111110, big): the taiga biome cells along the outline
    for (int fi = 0; fi < forests.size(); ++fi)
        if (forests[fi].spruce && fi != paleIdx)
            for (const ColSpan& c : forests[fi].spans)
                for (int z4 = c.z0 >> 2; z4 <= (c.z1 >> 2); ++z4)
                    taigaCells.insert(pairKey(c.x >> 2, z4));
    // The single biome map (a 4×4 cell -> 1 pale_garden, 2 taiga, 3 forest, 4 birch_forest, 5 dappled_forest); priority: pale > taiga > zones > forest.
    // Fields: tall grass (>= 50000 m²) — meadow (at the map edge the game will continue the meadow with flowers and tall grass). Wheat fields and ordinary grass — plains (the default biome, the cells are not written).
    for (auto pit = colPlant.constBegin(); pit != colPlant.constEnd(); ++pit)
        if (pit.value() == quint16(IdTallGrass))
            biomeCells.insert(pairKey(keyFirst(pit.key()) >> 2, keySecond(pit.key()) >> 2), quint8(7));
    for (int fi = 0; fi < forests.size(); ++fi)
        if (fi != paleIdx && !forests[fi].spruce)
            for (const ColSpan& c : forests[fi].spans)
                for (int z4 = c.z0 >> 2; z4 <= (c.z1 >> 2); ++z4)
                    biomeCells.insert(pairKey(c.x >> 2, z4), quint8(forests[fi].park ? 6 : 3));   // a park — flower_forest (a flowery forest), other forests — forest
    constexpr int kZoneR = 18;                                       // the average radius of a ragged zone around a school / research institute / kindergarten (blocks from the walls; up to ×1.45 in the tongues), where the biome changes and trees grow
    constexpr int kZoneMax = 27;                                     // the cell scan reach (kZoneR × 1.45 with a margin)
    for (const IslandSite& is : islandSites)
        if (is.kind != 2)
        for (int z4 = (is.cz - is.rz - kZoneMax) >> 2; z4 <= ((is.cz + is.rz + kZoneMax) >> 2); ++z4)
            for (int x4 = (is.cx - is.rx - kZoneMax) >> 2; x4 <= ((is.cx + is.rx + kZoneMax) >> 2); ++x4)
            {
                const int px = x4 * 4 + 2, pz = z4 * 4 + 2;
                const int dx = std::max(0, std::abs(px - is.cx) - is.rx), dz = std::max(0, std::abs(pz - is.cz) - is.rz);
                Q_UNUSED(dx); Q_UNUSED(dz);
                if (!zoneInside(is, px, pz, kZoneR)) continue;
                biomeCells.insert(pairKey(x4, z4), quint8(is.kind == 1 ? 4 : 5));
            }
    for (const qint64 k : taigaCells) biomeCells.insert(k, quint8(2));
    for (const qint64 k : paleCells) biomeCells.insert(k, quint8(1));
    // Swamps (mrspol, natural=wetland): the minecraft:swamp biome cells — the game tints the grass, leaves and water there (value 8; wins over a forest under the swamp: the trees there are already swamp oaks).
    for (const qint64 ck : swampCols)
        biomeCells.insert(pairKey(keyFirst(ck) >> 2, keySecond(ck) >> 2), quint8(8));
    // Cemeteries (csrpol 53510000): the minecraft:dark_forest biome cells (value 9) — the dark tint of the grass and
    // leaves (wins over a swamp under the same contour: the cemetery plants its own dark oaks).
    for (const qint64 ck : cemeteryCols)
        biomeCells.insert(pairKey(keyFirst(ck) >> 2, keySecond(ck) >> 2), quint8(9));
    // Donor: the terrain of the real Pale Garden instead of the forest terrain (smoothly at the border), trees are carried over together with it.
    QVector<PaleDonor>& patches = patchesLoaded;                   // terrain patches with structures (patch_*.bin)
    paleDonorUsed = false;
    // FLOATING ISLANDS above parks (71126000): above 70% of the suitable parks (area >= 6000 m²) and above every big one (>= 4 ha), not closer than 250 blocks to each other.
    {
        QVector<int> ord;
        for (int i = 0; i < parkCands.size(); ++i) ord << i;
        std::sort(ord.begin(), ord.end(), [&](int a, int b) { return parkCands[a].areaM2 != parkCands[b].areaM2 ? parkCands[a].areaM2 > parkCands[b].areaM2 : parkCands[a].h < parkCands[b].h; });
        QVector<QPoint> chosen;
        for (int i : ord)
        {
            const ParkCand& pc = parkCands[i];
            if (pc.areaM2 < 40000 && pc.h % 100u >= 70u) continue;                 // 70% of the suitable parks, big ones (>= 4 ha) always: the biggest park massifs were skipped whole
            bool far = true;
            for (const QPoint& q : chosen) if (std::hypot(double(q.x() - pc.cx), double(q.y() - pc.cz)) < 250.0) { far = false; break; }
            if (!far) continue;
            IslandSite is;
            is.kind = 2;
            is.cx = pc.cx; is.cz = pc.cz;
            is.rx = 18 + int((pc.h >> 8) % 7u); is.rz = 14 + int((pc.h >> 12) % 6u);        // ~ 40×30 blocks — the size of the former islands above kindergartens
            is.yTop = 0;
            is.hash = pc.h * 2654435761u ^ 0x151A9Du;
            islandSites.append(is);
            chosen.append(QPoint(pc.cx, pc.cz));
        }
    }
    // The column ground top: the terrain as is (valleys are not filled up to the plane); in the apron (beyond the object rectangle) it smoothly (smoothstep)
    // descends to the reference level — otherwise hills at the export edge would break off as a wall into the flat surroundings. No terrain data — the plane.
    // Light terrain of the "empty lands" (clearings, fields, wastelands between blocks): a dense grid of height additions over the export rectangle (filled after forests and patches).
    erX0 = 0;
    erZ0 = 0;
    erW = 0;
    erH = 0;
    auto addEmpty = [&](int v, int x, int z) -> int
    {
        if (erW == 0) return v;
        const int gx = x - erX0, gz = z - erZ0;
        if (gx < 0 || gx >= erW || gz < 0 || gz >= erH) return v;
        return std::max(0, std::min(255, v + int(emptyDelta[qsizetype(gz) * erW + gx])));
    };
    auto groundTopCol = [&](int x, int z) -> int
    {
        if (!paleCol.isEmpty())
        {
            const auto pit = paleCol.constFind(pairKey(x, z));
            if (pit != paleCol.constEnd()) return pit->top;
        }
        const int t = terrainTopBlock(x, z);
        if (t == std::numeric_limits<int>::min()) return addEmpty(genGroundTop, x, z);
        const int cxk = x >> 4, czk = z >> 4;
        const int d = std::max(std::max(rcx0 - cxk, cxk - rcx1), std::max(std::max(rcz0 - czk, czk - rcz1), 0));
        if (d <= 0 || rectM <= 0) return addEmpty(std::max(0, std::min(t, 255)), x, z);
        double u = std::min(1.0, double(d) / double(rectM));
        u = u * u * (3.0 - 2.0 * u);
        return std::max(0, std::min(255, int(std::lround(double(t) * (1.0 - u) + double(genGroundTop) * u))));
    };
    // Columns where the relief is not made: buildings and water. Roads are NOT in this list: they follow the terrain (the marking is moved onto the final relief
    // before the ground fill, see "roads follow the relief" below), otherwise the road would be a flat trench cut through the hills of a forest.
    auto reliefBlocked = [&](qint64 ck) -> bool
    {
        return (colMin.contains(ck) && !roadCols.contains(ck)) || colWater.contains(ck) || (colWool.contains(ck) && !roadCols.contains(ck));
    };
    // The distance (blocks) to the border of the exported rectangle of chunks, mapped to 0..1: the donor mountains slowly come down to the user's ground within 64 blocks of the border,
    // otherwise they are cut by a wall where the generated world of the game begins.
    efx0 = rcx0;
    efx1 = rcx1;
    efz0 = rcz0;
    efz1 = rcz1;
    for (const VegSite& vs : forests)
        for (const ColSpan& c : vs.spans)
        {
            efx0 = std::min(efx0, c.x >> 4); efx1 = std::max(efx1, c.x >> 4);
            efz0 = std::min(efz0, c.z0 >> 4); efz1 = std::max(efz1, c.z1 >> 4);
        }
    auto edgeFade = [=](int x, int z) -> double
    {
        if (efx1 < efx0) return 1.0;
        const int e = std::min(std::min(x - efx0 * 16, (efx1 + 1) * 16 - 1 - x), std::min(z - efz0 * 16, (efz1 + 1) * 16 - 1 - z));
        if (e >= 64) return 1.0;
        if (e <= 0) return 0.0;
        const double u = e / 64.0;
        return u * u * (3.0 - 2.0 * u);
    };
    if (paleIdx >= 0 && !paleDonor.empty())
    {
        const VegSite& ps = forests[paleIdx];
        const int pbx0 = ps.minX, pbz0 = ps.minZ, PW = ps.maxX - ps.minX + 1, PH = ps.maxZ - ps.minZ + 1;
        if (qint64(PW) * PH <= 60000000ll)
        {
            QVector<quint8> dist(PW * PH, 0);                          // the distance (city block) to the nearest column outside the forest, up to 255
            for (const ColSpan& c : ps.spans)
                for (int z = c.z0; z <= c.z1; ++z) dist[(z - pbz0) * PW + (c.x - pbx0)] = 255;
            for (int z = 0; z < PH; ++z)                                // forward pass
                for (int x = 0; x < PW; ++x)
                {
                    quint8& dv = dist[z * PW + x];
                    if (!dv) continue;
                    const int l = (x > 0) ? dist[z * PW + x - 1] : 0, u = (z > 0) ? dist[(z - 1) * PW + x] : 0;
                    dv = quint8(std::min<int>(dv, std::min(l, u) + 1));
                }
            for (int z = PH - 1; z >= 0; --z)                           // backward pass
                for (int x = PW - 1; x >= 0; --x)
                {
                    quint8& dv = dist[z * PW + x];
                    if (!dv) continue;
                    const int r = (x + 1 < PW) ? dist[z * PW + x + 1] : 0, dn = (z + 1 < PH) ? dist[(z + 1) * PW + x] : 0;
                    dv = quint8(std::min<int>(dv, std::min(r, dn) + 1));
                }
            constexpr double kBlendL = 28.0;                                // the width of the smooth transition at the forest border, blocks
            constexpr int kBlendB = 12;                                     // the ramp width at the seam of a replaced mansion box, blocks
            auto donorIndex = [&](int x, int z) -> int
            {
                int ux = (x - pbx0) % (2 * paleDonor.W), uz = (z - pbz0) % (2 * paleDonor.H);
                if (ux >= paleDonor.W) ux = 2 * paleDonor.W - 1 - ux;
                if (uz >= paleDonor.H) uz = 2 * paleDonor.H - 1 - uz;
                return uz * paleDonor.W + ux;
            };
            // The forest mansion in the template would be repeated over the whole big forest (the tiling is mirrored) — we keep ONE (the largest by the number of columns);
            // the other mansion columns are replaced with a reflection of the neighboring free part of the template: the same trees and terrain continue there.
            QSet<ColKey> keepMansion;
            bool mansionPatch = false;
            for (const PaleDonor& pdn : patchesLoaded) if (pdn.name.contains(QLatin1String("mansion"))) mansionPatch = true;
            if (paleDonor.mx1 >= paleDonor.mx0 && !mansionPatch)
            {
                QHash<ColKey, int> comp;                                    // a mansion column -> the component number
                QVector<int> compSize;
                for (const ColSpan& c : ps.spans)
                    for (int z = c.z0; z <= c.z1; ++z)
                        if (paleDonor.mansion[donorIndex(c.x, z)]) comp.insert(pairKey(c.x, z), -1);
                QVector<qint64> stack;
                for (auto it = comp.begin(); it != comp.end(); ++it)
                {
                    if (it.value() >= 0) continue;
                    const int id = compSize.size();
                    compSize.append(0);
                    it.value() = id;
                    stack.clear(); stack.append(it.key());
                    while (!stack.isEmpty())
                    {
                        const qint64 k = stack.takeLast();
                        ++compSize[id];
                        const int kx = keyFirst(k), kz = keySecond(k);
                        const qint64 nb[4] = { pairKey(kx + 1, kz), pairKey(kx - 1, kz), pairKey(kx, kz + 1), pairKey(kx, kz - 1) };
                        for (qint64 nk : nb)
                        {
                            auto nit = comp.find(nk);
                            if (nit != comp.end() && nit.value() < 0) { nit.value() = id; stack.append(nk); }
                        }
                    }
                }
                // We prefer a whole mansion (not cut by the forest edge) in a direct (non-mirrored) tile; otherwise the largest.
                QVector<char> compMirror(compSize.size(), 0);
                for (auto it = comp.constBegin(); it != comp.constEnd(); ++it)
                    if ((keyFirst(it.key()) - pbx0) % (2 * paleDonor.W) >= paleDonor.W || (keySecond(it.key()) - pbz0) % (2 * paleDonor.H) >= paleDonor.H) compMirror[it.value()] = 1;
                int fullCount = 0;
                for (const quint8 mv : paleDonor.mansion) if (mv) ++fullCount;
                int bestComp = -1;
                double bestScore = -1.0;
                for (int i = 0; i < compSize.size(); ++i)
                {
                    const double sc = double(compSize[i]) * (compSize[i] >= fullCount * 0.97 ? 3.0 : 1.0) * (compMirror[i] ? 0.8 : 1.0);
                    if (sc > bestScore) { bestScore = sc; bestComp = i; }
                }
                for (auto it = comp.constBegin(); it != comp.constEnd(); ++it)
                    if (it.value() == bestComp) keepMansion.insert(it.key());
                res.lairInfo += QObject::tr("; forest mansion: 1 of %1 kept").arg(compSize.size());
            }
            // TERRAIN AS IN THE ORIGINAL: the forest ground = the donor terrain + ONE integer height shift (all the columns are shifted equally, so the trees and the mansion carried
            // together with the terrain stay whole — without "hanging" crowns); to the user's ground the terrain smoothly descends at the forest border (kBlendL blocks, smoothstep).
            double hdSum = 0.0;
            for (const qint16 hv : paleDonor.hd) hdSum += hv;
            const double hdMeanL = paleDonor.hd.isEmpty() ? 0.0 : hdSum / double(paleDonor.hd.size());
            double sumT = 0.0; qint64 nT = 0;
            for (const ColSpan& c : ps.spans)
                for (int z = c.z0; z <= c.z1; z += 3) { sumT += groundTopCol(c.x, z); ++nT; }
            const double baseT = nT ? sumT / double(nT) : double(genGroundTop);
            const int shiftC = int(std::lround(baseT - hdMeanL));
            for (const ColSpan& c : ps.spans)
                for (int z = c.z0; z <= c.z1; ++z)
                {
                    const qint64 ck = pairKey(c.x, z);
                    if (reliefBlocked(ck)) continue;   // buildings and water are not touched (roads follow the terrain)
                    int di = donorIndex(c.x, z);
                    int seamDi = -1, seamD = 0;                            // a replaced mansion box: the donor column just outside its nearest edge + the distance to that edge
                    if (paleDonor.mansion[di] && !keepMansion.contains(ck))
                    {   // an extra mansion: we take the reflected free column (or the bare terrain, if there is none)
                        const int uxT = di % paleDonor.W, uzT = di / paleDonor.W;
                        const int dl = uxT - paleDonor.mx0, dr = paleDonor.mx1 - uxT, du = uzT - paleDonor.mz0, dd = paleDonor.mz1 - uzT;
                        seamD = std::min(std::min(dl, dr), std::min(du, dd));
                        if (seamD == dl && paleDonor.mx0 > 0)                    seamDi = uzT * paleDonor.W + paleDonor.mx0 - 1;
                        else if (seamD == dr && paleDonor.mx1 + 1 < paleDonor.W) seamDi = uzT * paleDonor.W + paleDonor.mx1 + 1;
                        else if (seamD == du && paleDonor.mz0 > 0)               seamDi = (paleDonor.mz0 - 1) * paleDonor.W + uxT;
                        else if (seamD == dd && paleDonor.mz1 + 1 < paleDonor.H) seamDi = (paleDonor.mz1 + 1) * paleDonor.W + uxT;
                        const int alt = paleDonor.reflectFree(uxT, uzT);
                        if (alt >= 0) di = alt;
                    }
                    double wv = std::min(1.0, double(dist[(z - pbz0) * PW + (c.x - pbx0)]) / kBlendL);
                    wv = wv * wv * (3.0 - 2.0 * wv);
                    wv *= edgeFade(c.x, z);
                    int hdHere = paleDonor.hd[di];
                    if (seamDi >= 0 && seamD < kBlendB)
                    {   // the seam of the replaced box: the terrain ramps from the donor neighbor OUTSIDE the box to the replaced
                        // terrain within kBlendB columns — otherwise the box edge is a wall where the mirror seam crosses a mountain slope
                        const double u = seamD / double(kBlendB), su = u * u * (3.0 - 2.0 * u);
                        hdHere = int(std::lround(paleDonor.hd[seamDi] * (1.0 - su) + double(hdHere) * su));
                    }
                    const int t0 = groundTopCol(c.x, z);
                    const int tn = std::max(1, std::min(250, int(std::lround(double(t0) * (1.0 - wv) + double(hdHere + shiftC) * wv))));
                    paleCol.insert(ck, PaleCol{ tn, qint16(tn - paleDonor.hd[di]), quint8(std::lround(wv * 255.0)), wv >= 0.5 ? paleDonor.surf[di] : quint16(0), qint32(di) });
                }
            paleDonorUsed = !paleCol.isEmpty();
        }
    }
    if (paleIdx >= 0 && paleDonor.empty())
        res.lairInfo += QObject::tr("; Pale Garden: pale_garden.bin was not found next to cave_templates.bin — a procedural forest without the donor terrain");
    lap("g1 Pale Garden/donor");
    // RANDOM TERRAIN IN FORESTS on top of the standard one: a low-frequency "hilliness" (a sum of sinusoids with their own phases for each forest, ±A blocks), at the border
    // the forest (28 blocks) smoothly fades out. Spruce/taiga forests are hillier (±12 by the sinusoid amplitude, in fact ±4…8), other forests (±8), the procedural pale oak without a donor (±10); big parks (from 6000 m²) are softer (±5), small parks are flat.
    {
        constexpr double kBlendR = 28.0;
        // The forests are independent of each other and only read the common maps — we compute in parallel (on big projects it is the longest
        // sequential step of the "Ground"), and into paleCol we put in the order of the forests: the earlier forest wins at intersections, as before.
        struct ReliefOut { QVector<QPair<qint64, int>> cells; qint64 n = 0; };
        QVector<ReliefOut> reliefOut(forests.size());
        QVector<int> reliefIdx;
        for (int fi = 0; fi < forests.size(); ++fi) reliefIdx.append(fi);
        QtConcurrent::blockingMap(reliefIdx, [&](int fi)
        {
            const VegSite& fs = forests[fi];
            if (fi == paleIdx) return;                                      // the Pale Garden terrain is not touched
            if (fs.loose) return;                                           // yards and taiga of the private sector (OSM): the "empty lands" terrain, see below
            if (fs.park)
            {   // big parks (from 6000 m²) get terrain too, softer than the forest one; small ones stay flat
                qint64 parkArea = 0;
                for (const ColSpan& c : fs.spans) parkArea += c.z1 - c.z0 + 1;
                if (parkArea < 6000) return;
            }
            const int fbx0 = fs.minX, fbz0 = fs.minZ, FW = fs.maxX - fs.minX + 1, FH = fs.maxZ - fs.minZ + 1;
            if (FW <= 0 || FH <= 0 || qint64(FW) * FH > 60000000ll) return;
            QVector<quint8> dist(FW * FH, 0);
            for (const ColSpan& c : fs.spans)
                for (int z = c.z0; z <= c.z1; ++z) dist[(z - fbz0) * FW + (c.x - fbx0)] = 255;
            for (int z = 0; z < FH; ++z)
                for (int x = 0; x < FW; ++x)
                {
                    quint8& dv = dist[z * FW + x];
                    if (!dv) continue;
                    const int l = (x > 0) ? dist[z * FW + x - 1] : 0, u = (z > 0) ? dist[(z - 1) * FW + x] : 0;
                    dv = quint8(std::min<int>(dv, std::min(l, u) + 1));
                }
            for (int z = FH - 1; z >= 0; --z)
                for (int x = FW - 1; x >= 0; --x)
                {
                    quint8& dv = dist[z * FW + x];
                    if (!dv) continue;
                    const int r = (x + 1 < FW) ? dist[z * FW + x + 1] : 0, dn = (z + 1 < FH) ? dist[(z + 1) * FW + x] : 0;
                    dv = quint8(std::min<int>(dv, std::min(r, dn) + 1));
                }
            const double A = fs.park ? 5.0 : (fs.spruce ? 12.0 : 8.0);
            const quint32 hs = colHash(pairKey(fs.spans.first().x, fs.spans.first().z0) ^ 0xBEEFull);
            const double ph1 = (hs & 255u) / 40.0, ph2 = ((hs >> 8) & 255u) / 40.0, ph3 = ((hs >> 16) & 255u) / 40.0, ph4 = ((hs >> 24) & 255u) / 40.0;
            ReliefOut& ro = reliefOut[fi];
            for (const ColSpan& c : fs.spans)
                for (int z = c.z0; z <= c.z1; ++z)
                {
                    const qint64 ck = pairKey(c.x, z);
                    if (reliefBlocked(ck) || paleCol.contains(ck)) continue;
                    double wv = std::min(1.0, double(dist[(z - fbz0) * FW + (c.x - fbx0)]) / kBlendR);
                    wv = wv * wv * (3.0 - 2.0 * wv);
                    const double n = 0.45 * std::sin(c.x / 17.0 + ph1) * std::cos(z / 21.0 + ph2) + 0.35 * std::sin((c.x + z) / 29.0 + ph3)
                                   + 0.25 * std::sin(c.x / 8.5 + ph4) * std::sin(z / 9.5 + ph2) + 0.15 * std::cos((c.x - z) / 13.0 + ph1);
                    const int t0 = groundTopCol(c.x, z);
                    const int tn = std::max(1, std::min(250, t0 + int(std::lround(A * n * wv))));
                    if (tn != t0) ro.cells.append(qMakePair(ck, tn));
                    ++ro.n;
                }
        });
        int nRelief = 0;
        for (int fi = 0; fi < forests.size(); ++fi)
        {
            ReliefOut& ro = reliefOut[fi];
            nRelief += int(ro.n);
            for (const auto& cl : ro.cells)
                if (!paleCol.contains(cl.first)) paleCol.insert(cl.first, PaleCol{ cl.second, 0, 0, 0 });   // w=0: only the ground mark (the donor blocks/surface are not affected)
            ro.cells.clear(); ro.cells.squeeze();
        }
        if (nRelief > 0) res.lairInfo += QObject::tr("; terrain in forests: %1 columns").arg(nRelief);
    }
    lap("g2 forest terrain");
    // BANDIT OUTPOSTS and ABANDONED CAMPS (cut out of a real world whole, together with the terrain): outposts — only in big spruce (taiga) forests
    // (an area of at least 9 outposts, 48×48 -> 20736 m²), roughly one per two such "nines"; camps — in forests from 10000 m² (a taiga/ordinary camp by the forest type).
    if (!patches.isEmpty())
    {
        QVector<int> outposts, campsTaiga, campsForest, mansionsP;
        for (int i = 0; i < patches.size(); ++i)
        {
            if (patches[i].name.contains(QLatin1String("mansion"))) mansionsP << i;
            else if (patches[i].name.contains(QLatin1String("outpost"))) outposts << i;
            else if (patches[i].name.contains(QLatin1String("camp_taiga"))) campsTaiga << i;
            else if (patches[i].name.contains(QLatin1String("wreck"))) continue;       // ships — into water bodies (below), not into forests
            else campsForest << i;
        }
        auto tryPlace = [&](int fi, int pi, quint32 salt, QVector<QPoint>* keepApart = nullptr) -> bool
        {
            const VegSite& fs = forests[fi];
            const PaleDonor& pd = patches[pi];
            const int bw = fs.maxX - fs.minX + 1 - pd.W, bh = fs.maxZ - fs.minZ + 1 - pd.H;
            if (bw <= 0 || bh <= 0) return false;
            // Mansion: from the suitable places (up to 150) we take the one where the forest terrain fits best the terrain of the structure itself, and the height
            // is chosen by the average deviation around — otherwise the mansion "sits in a pit"/on a fill that the smoothing ring cuts off.
            const bool isMansion = pd.name.contains(QLatin1String("mansion")) && pd.bbx1 >= pd.bbx0 && pd.bbz1 >= pd.bbz0;
            const int mRefX = isMansion ? (pd.bbx0 + pd.bbx1) / 2 : 0, mRefZ = isMansion ? (pd.bbz0 + pd.bbz1) / 2 : 0;
            double fitBase = 0.0;                                           // the chosen height of the mansion reference point
            bool haveFit = false;
            // A patch with a building (the mansion) may be crossed by roads and paths (in OSM forests have plenty of them) outside the building itself: they follow its terrain;
            // only the building and the strip of 6 blocks around it must be free of roads.
            const bool hasBoxP = pd.bbx1 >= pd.bbx0 && pd.bbz1 >= pd.bbz0;
            const auto placeable = [&](int x0, int z0) -> bool
            {
                for (int z = z0; z < z0 + pd.H; z += 4)                    // the whole rectangle inside the forest, without buildings, water, roads and other patches
                    for (int x = x0; x < x0 + pd.W; x += 4)
                    {
                        const qint64 ck = pairKey(x, z);
                        const int ux = x - x0, uz = z - z0;
                        const bool nearBuilding = !hasBoxP || (ux >= pd.bbx0 - 6 && ux <= pd.bbx1 + 6 && uz >= pd.bbz0 - 6 && uz <= pd.bbz1 + 6);
                        const bool road = roadCols.contains(ck);
                        if (!spansContain(fs.spans, x, z) || (colMin.contains(ck) && (nearBuilding || !road)) || colWater.contains(ck) || (colWool.contains(ck) && (nearBuilding || !road))
                            || (road && nearBuilding) || railColsAll.contains(ck) || patchCols.contains(ck) || paleCol.contains(ck) && paleCol.value(ck).pid >= 0) return false;
                    }
                return true;
            };
            int pickX = 0, pickZ = 0;
            bool havePick = false;
            if (isMansion)
            {   // iterating over places: the average deviation of the forest terrain from the structure terrain on its frame +4 blocks (inside the frame the donor site is flat, and this is the measure of "pit/fill") (after the height is chosen)
                double bestFit = 1e18, bestBase = 0.0;
                int nValid = 0;
                const int hdR = pd.hd[mRefZ * pd.W + mRefX];
                for (int t = 0; t < 400 && nValid < 150; ++t)
                {
                    const quint32 h = colHash(pairKey(fi * 977 + int(salt & 0xFF), t) ^ (qint64(salt) << 8));
                    const int x0 = fs.minX + int(h % quint32(bw)), z0 = fs.minZ + int((h >> 12) % quint32(bh));
                    if (!placeable(x0, z0)) continue;
                    ++nValid;
                    QVector<double> diffs;
                    for (int uz = std::max(0, pd.bbz0 - 4); uz <= std::min(pd.H - 1, pd.bbz1 + 4); uz += 2)
                        for (int ux = std::max(0, pd.bbx0 - 4); ux <= std::min(pd.W - 1, pd.bbx1 + 4); ux += 2)
                            diffs.append(double(groundTopCol(x0 + ux, z0 + uz)) - double(pd.hd[uz * pd.W + ux] - hdR));
                    if (diffs.isEmpty()) continue;
                    double mean = 0;
                    for (double d : diffs) mean += d;
                    mean /= diffs.size();
                    double dev = 0;
                    for (double d : diffs) dev += std::abs(d - mean);
                    dev /= diffs.size();
                    if (dev < bestFit) { bestFit = dev; bestBase = mean; pickX = x0; pickZ = z0; havePick = true; }
                }
                if (havePick)
                {
                    fitBase = bestBase; haveFit = true;
                    if (qEnvironmentVariableIsSet("MCX_LAIRDBG"))
                        std::fprintf(stderr, "[mansion] x0=%d z0=%d base=%.1f meanDev=%.2f of %d\n", pickX, pickZ, bestBase, bestFit, nValid);
                }
            }
            for (int t = 0; t < 400; ++t)
            {
                const quint32 h = colHash(pairKey(fi * 977 + int(salt & 0xFF), t) ^ (qint64(salt) << 8));
                int x0 = fs.minX + int(h % quint32(bw)), z0 = fs.minZ + int((h >> 12) % quint32(bh));
                if (havePick) { x0 = pickX; z0 = pickZ; }
                const bool ok = placeable(x0, z0);
                if (!ok && havePick) return false;
                if (!ok) continue;
                const int ccx = x0 + pd.W / 2, ccz = z0 + pd.H / 2;
                if (keepApart)
                {   // outposts of one forest — not closer than 300 blocks to each other
                    bool far = true;
                    for (const QPoint& q : *keepApart) if (std::hypot(double(q.x() - ccx), double(q.y() - ccz)) < 300.0) { far = false; break; }
                    if (!far) continue;
                }
                const bool hasBox = pd.bbx1 >= pd.bbx0 && pd.bbz1 >= pd.bbz0;
                const int refX = hasBox ? (pd.bbx0 + pd.bbx1) / 2 : pd.W / 2, refZ = hasBox ? (pd.bbz0 + pd.bbz1) / 2 : pd.H / 2;
                const double baseT = haveFit ? fitBase : double(groundTopCol(x0 + refX, z0 + refZ));
                const int hdRef = pd.hd[refZ * pd.W + refX];
                if (isMansion)
                {   // Mansion: the site is flat within the structure frame (the column blocks travel together with the ground, so at the walls the weight must be 1 —
                    // otherwise a wall is "led" up/down following the forest terrain), and beyond the frame the forest is smoothly (18 blocks) pulled to the site; the ring
                    // goes beyond the cut-out patch — there is only the ground mark.
                    constexpr int kRing = 18;
                    for (int z = z0 + pd.bbz0 - kRing; z <= z0 + pd.bbz1 + kRing; ++z)
                        for (int x = x0 + pd.bbx0 - kRing; x <= x0 + pd.bbx1 + kRing; ++x)
                        {
                            const qint64 ck = pairKey(x, z);
                            if (reliefBlocked(ck)) continue;
                            const int ux = x - x0, uz = z - z0;
                            const int ddx = std::max(0, std::max(pd.bbx0 - ux, ux - pd.bbx1)), ddz = std::max(0, std::max(pd.bbz0 - uz, uz - pd.bbz1));
                            double wb = 1.0 - std::min(1.0, std::hypot(double(ddx), double(ddz)) / double(kRing));
                            wb = wb * wb * (3.0 - 2.0 * wb);
                            if (wb <= 0.0) continue;
                            const int cux = std::max(pd.bbx0, std::min(pd.bbx1, ux)), cuz = std::max(pd.bbz0, std::min(pd.bbz1, uz));   // beyond the frame — the site edge (flat)
                            const bool inPatch = ux >= 0 && ux < pd.W && uz >= 0 && uz < pd.H;
                            const int di = (inPatch ? uz : cuz) * pd.W + (inPatch ? ux : cux);
                            const int hdHere = ddx == 0 && ddz == 0 ? pd.hd[di] : pd.hd[cuz * pd.W + cux];
                            const int t0 = groundTopCol(x, z);
                            const int tn = std::max(1, std::min(250, int(std::lround(double(t0) * (1.0 - wb) + (baseT + double(hdHere - hdRef)) * wb))));
                            if (inPatch)
                                paleCol.insert(ck, PaleCol{ tn, qint16(tn - pd.hd[di]), quint8(std::lround(wb * 255.0)), wb >= 0.5 ? pd.surf[di] : quint16(0), qint32(di), qint16(pi) });
                            else
                                paleCol.insert(ck, PaleCol{ tn, 0, 0, 0 });            // w=0: only the ground mark
                        }
                }
                else
                for (int z = z0; z < z0 + pd.H; ++z)
                    for (int x = x0; x < x0 + pd.W; ++x)
                    {
                        const qint64 ck = pairKey(x, z);
                        if (reliefBlocked(ck)) continue;
                        const int ux = x - x0, uz = z - z0, di = uz * pd.W + ux;
                        const int dEdge = std::min(std::min(ux, pd.W - 1 - ux), std::min(uz, pd.H - 1 - uz));
                        double wv = std::min(1.0, double(dEdge) / 12.0);                 // the smooth edge of the patch (12 blocks)
                        wv = wv * wv * (3.0 - 2.0 * wv);
                        const int t0 = groundTopCol(x, z);
                        double wt = wv;                                                     // the donor terrain weight: 1 right at the structure, beyond its frame it goes down to the user's ground within 10 blocks
                        if (hasBox)
                        {
                            const int ddx = std::max(0, std::max(pd.bbx0 - ux, ux - pd.bbx1)), ddz = std::max(0, std::max(pd.bbz0 - uz, uz - pd.bbz1));
                            double wb = 1.0 - std::min(1.0, std::hypot(double(ddx), double(ddz)) / (isMansion ? 18.0 : 10.0));
                            wb = wb * wb * (3.0 - 2.0 * wb);
                            wt = std::min(wv, wb);
                        }
                        const int tn = std::max(1, std::min(250, int(std::lround(double(t0) * (1.0 - wt) + (baseT + double(pd.hd[di] - hdRef)) * wt))));
                        if (hasBox && pd.name.contains(QLatin1String("mansion")) && wt <= 0.0) continue;       // beyond the smoothing ring — the former forest
                        paleCol.insert(ck, PaleCol{ tn, qint16(tn - pd.hd[di]), quint8(std::lround(wv * 255.0)), wv >= 0.5 ? pd.surf[di] : quint16(0), qint32(di), qint16(pi) });
                    }
                if (hasBox && pd.name.contains(QLatin1String("mansion")))
                {   // trees do not grow near the mansion only in the structure itself (frame +3); around — a ring with procedural trees
                    for (int z = z0 + pd.bbz0 - 3; z <= z0 + pd.bbz1 + 3; ++z)
                        for (int x = x0 + pd.bbx0 - 3; x <= x0 + pd.bbx1 + 3; ++x) patchCols.insert(pairKey(x, z));
                }
                else
                    for (int z = z0 - 3; z < z0 + pd.H + 3; ++z)
                        for (int x = x0 - 3; x < x0 + pd.W + 3; ++x) patchCols.insert(pairKey(x, z));
                patchPlaces.append(PatchPlace{ pi, x0, z0 });
                if (keepApart) keepApart->append(QPoint(ccx, ccz));
                return true;
            }
            return false;
        };
        int nOut = 0, nCamp = 0;
        if (paleIdx >= 0 && !mansionsP.isEmpty())
        {   // the forest mansion (cut out of the world whole, one per Pale Garden): we look for a place in the forest, on failure — no mansion
            bool placedM = false;
            for (int k = 0; k < 6 && !placedM; ++k) placedM = tryPlace(paleIdx, mansionsP[0], 0x3A + quint32(k) * 11u);
            if (placedM)
            {
                const PatchPlace& mp = patchPlaces.last();
                const PaleDonor& mpd = patches[mp.pi];
                const int mx = mp.x0 + (mpd.bbx0 + mpd.bbx1) / 2, mz = mp.z0 + (mpd.bbz0 + mpd.bbz1) / 2;
                res.lairInfo += QObject::tr("; forest mansion (cut out of the game world) placed: center x=%1 z=%2 (chunk %3 %4)").arg(mx).arg(mz).arg(mx >> 4).arg(mz >> 4);
            }
            else
                res.lairInfo += QObject::tr("; forest mansion: no place was found in the Pale Garden");
        }
        for (int fi = 0; fi < forests.size(); ++fi)
        {
            const VegSite& fs = forests[fi];
            if (fs.park || fs.intra || fi == paleIdx) continue;                 // intra-block vegetation (vgbpol) gets no structures
            qint64 area = 0;
            for (const ColSpan& c : fs.spans) area += c.z1 - c.z0 + 1;
            if (fs.spruce && !outposts.isEmpty() && area >= 20736)
            {
                const int n = int(std::min<qint64>(4, std::max<qint64>(1, area / 90000)));       // not more often than one per ~300×300 forest blocks
                QVector<QPoint> placedHere;
                for (int k = 0; k < n; ++k)
                    if (tryPlace(fi, outposts[(fi + k) % outposts.size()], 0x0A + quint32(k) * 7u, &placedHere)) ++nOut;
            }
            const QVector<int>& camps = fs.spruce ? campsTaiga : campsForest;
            if (!camps.isEmpty() && area >= 10000)
            {
                const int n = int(std::min<qint64>(6, area / 30000 + 1));
                for (int k = 0; k < n; ++k)
                    if (tryPlace(fi, camps[(fi * 3 + k) % camps.size()], 0xC0 + quint32(k) * 13u)) ++nCamp;
            }
        }
        res.lairInfo += QObject::tr("; bandit outposts: %1, abandoned camps: %2").arg(nOut).arg(nCamp);
    }
    {   // QUARRY (csrpol 51111000) — a pit with terrace steps; SPOIL HEAP (51123000) — a mound. The terrain is set through paleCol (the column top and the surface block).
        // The distance to the spot edge (by 4 neighbors) is computed on a dense grid of the spot frame.
        int nQuarry = 0, nHeap = 0;
        for (int pi = 0; pi < pads.size(); ++pi)
        {
            const int code = padCode.value(pi, 0);
            if (code != 51111000 && code != 51123000) continue;
            const QList<qint64>& pc = pads[pi];
            if (pc.size() < 150) continue;
            int x0 = std::numeric_limits<int>::max(), x1 = std::numeric_limits<int>::min(), z0 = x0, z1 = x1;
            for (qint64 ck : pc) { x0 = std::min(x0, keyFirst(ck)); x1 = std::max(x1, keyFirst(ck)); z0 = std::min(z0, keySecond(ck)); z1 = std::max(z1, keySecond(ck)); }
            const qint64 gw = qint64(x1) - x0 + 3, gh = qint64(z1) - z0 + 3;
            if (gw * gh > 40000000ll) continue;
            const int W = int(gw), H = int(gh);
            QVector<quint8> dist(W * H, 0);
            for (qint64 ck : pc) dist[(keySecond(ck) - z0 + 1) * W + (keyFirst(ck) - x0 + 1)] = 255;
            for (int z = 1; z < H - 1; ++z)
                for (int x = 1; x < W - 1; ++x)
                {
                    quint8& dv = dist[z * W + x];
                    if (!dv) continue;
                    dv = quint8(std::min<int>(dv, std::min(dist[z * W + x - 1], dist[(z - 1) * W + x]) + 1));
                }
            int dmax = 1;
            for (int z = H - 2; z >= 1; --z)
                for (int x = W - 2; x >= 1; --x)
                {
                    quint8& dv = dist[z * W + x];
                    if (!dv) continue;
                    dv = quint8(std::min<int>(dv, std::min(dist[z * W + x + 1], dist[(z + 1) * W + x]) + 1));
                    dmax = std::max(dmax, int(dv));
                }
            const double area = double(pc.size());
            const quint16 stoneId = IdStone;
            const quint16 gravelId = quint16(IdPaletteStart + paletteIndexOf("minecraft:gravel"));
            const quint16 andesiteId = quint16(IdPaletteStart + paletteIndexOf("minecraft:andesite"));
            const quint16 coalId = quint16(IdPaletteStart + paletteIndexOf("minecraft:coal_ore"));
            const int maxDepth = std::max(6, std::min(24, int(std::sqrt(area) / 3.0)));
            const int hHeap = std::max(5, std::min(26, 5 + int(std::sqrt(area) / 3.5)));
            for (qint64 ck : pc)
            {
                const int x = keyFirst(ck), z = keySecond(ck);
                if (colMin.contains(ck) || colWater.contains(ck) || roadCols.contains(ck) || railColsAll.contains(ck)) continue;
                const int d = int(dist[(z - z0 + 1) * W + (x - x0 + 1)]);
                const int t0 = groundTopCol(x, z);
                const quint32 h = colHash(ck ^ 0x9A331ull);
                if (code == 51111000)
                {   // terrace steps: 5 blocks wide, a 3-block drop, an uneven edge (noise up to 2 blocks by the distance); the bottom — at maxDepth
                    const int dn = d + int((colHash(pairKey(x >> 2, z >> 2) ^ 0x7A11ull) % 3u)) - 1;
                    const int T = dn <= 2 ? 0 : (dn - 2) / 5 + 1;
                    const int depth = std::min(maxDepth, T * 3);
                    const int tn = std::max(-40, t0 - depth);
                    quint16 surf = depth == 0 ? quint16(IdCoarseDirt) : (T % 3 == 1 ? gravelId : (T % 3 == 2 ? andesiteId : stoneId));
                    if (depth >= 3 && h % 100u < 6u) surf = coalId;                 // rare coal seams
                    paleCol.insert(ck, PaleCol{ tn, 0, 0, surf });
                    if (depth > 0) ++nQuarry;
                }
                else
                {   // a mound: the profile — a convex cone with noise; on the slopes stone/coal, on the crest gravel
                    const double u = std::min(1.0, double(d) / double(std::max(3, dmax)));
                    double prof = 1.0 - (1.0 - u) * (1.0 - u);
                    prof *= 0.85 + 0.15 * std::sin(x * 0.31 + z * 0.23) * std::cos(z * 0.27 - x * 0.19);
                    const int hh = int(std::lround(hHeap * std::max(0.0, std::min(1.0, prof))));
                    const int tn = std::min(250, t0 + hh);
                    const quint32 r = h % 100u;
                    quint16 surf = r < 55u ? stoneId : (r < 78u ? coalId : (r < 90u ? gravelId : quint16(IdCoarseDirt)));
                    paleCol.insert(ck, PaleCol{ tn, 0, 0, surf });
                    if (hh > 0) ++nHeap;
                }
            }
        }
        if (nQuarry > 0) res.lairInfo += QObject::tr("; quarries: %1 columns of the stepped excavation").arg(nQuarry);
        if (nHeap > 0) res.lairInfo += QObject::tr("; spoil heaps: %1 columns of the mound").arg(nHeap);
    }
    {   // LIGHT TERRAIN OF THE EMPTY LANDS: big free places (where single oaks/birches are planted, fields, wastelands) get soft hills (±4), like parks and forests.
        // The distance to the occupied (buildings, roads, rails, water, sites, forests and patches) is computed on a dense grid; near the occupied and the export edge the addition smoothly fades out (24 blocks).
        const int ex0 = rcx0 * 16, ez0 = rcz0 * 16;
        const qint64 eW = qint64(rcx1 - rcx0 + 1) * 16, eH = qint64(rcz1 - rcz0 + 1) * 16;
        if (eW > 0 && eH > 0 && eW * eH <= 400000000ll)
        {
            const int W3 = int(eW), H3 = int(eH);
            QVector<quint8> ed(qsizetype(W3) * H3, 255);                       // 0 — occupied / outside the export, otherwise the distance
            auto mark = [&](qint64 k)
            {
                const int gx = keyFirst(k) - ex0, gz = keySecond(k) - ez0;
                if (gx >= 0 && gx < W3 && gz >= 0 && gz < H3) ed[qsizetype(gz) * W3 + gx] = 0;
            };
            for (auto it = colMin.constBegin(); it != colMin.constEnd(); ++it) mark(it.key());
            for (auto it = colWool.constBegin(); it != colWool.constEnd(); ++it) mark(it.key());
            for (auto it = colWater.constBegin(); it != colWater.constEnd(); ++it) mark(it.key());
            for (const qint64 k : roadCols) mark(k);
            for (const qint64 k : railColsAll) mark(k);
            for (auto it = paleCol.constBegin(); it != paleCol.constEnd(); ++it) mark(it.key());
            for (const VegSite& vs : forests)
                if (!vs.loose)                                                  // yards and taiga of the private sector (OSM) are not counted as "occupied": hills under their trees too
                    for (const ColSpan& c : vs.spans)
                        for (int z = c.z0; z <= c.z1; ++z) mark(pairKey(c.x, z));
            for (int x = 0; x < W3; ++x) { ed[x] = 0; ed[qsizetype(H3 - 1) * W3 + x] = 0; }
            for (int z = 0; z < H3; ++z) { ed[qsizetype(z) * W3] = 0; ed[qsizetype(z) * W3 + W3 - 1] = 0; }
            for (int z = 1; z < H3; ++z)                                         // forward pass (Manhattan)
                for (int x = 1; x < W3; ++x)
                {
                    quint8& dv = ed[qsizetype(z) * W3 + x];
                    if (!dv) continue;
                    dv = quint8(std::min<int>(dv, std::min(ed[qsizetype(z) * W3 + x - 1], ed[qsizetype(z - 1) * W3 + x]) + 1));
                }
            for (int z = H3 - 2; z >= 0; --z)                                   // backward
                for (int x = W3 - 2; x >= 0; --x)
                {
                    quint8& dv = ed[qsizetype(z) * W3 + x];
                    if (!dv) continue;
                    dv = quint8(std::min<int>(dv, std::min(ed[qsizetype(z) * W3 + x + 1], ed[qsizetype(z + 1) * W3 + x]) + 1));
                }
            QVector<qint8> delta(qsizetype(W3) * H3, 0);
            const double A = hasTerrain ? 2.5 : 4.5;                              // with a ready terrain (DTM) — more careful
            constexpr double kBlendE = 24.0;
            QVector<int> rows;
            for (int z = 0; z < H3; ++z) rows.append(z);
            QtConcurrent::blockingMap(rows, [&](int gz)
            {
                const int z = gz + ez0;
                for (int gx = 0; gx < W3; ++gx)
                {
                    const quint8 dv = ed[qsizetype(gz) * W3 + gx];
                    if (dv < 3) continue;
                    const int x = gx + ex0;
                    double wv = std::min(1.0, double(dv) / kBlendE);
                    wv = wv * wv * (3.0 - 2.0 * wv);
                    const double n = 0.45 * std::sin(x / 19.0 + 1.3) * std::cos(z / 23.0 + 2.1) + 0.35 * std::sin((x + z) / 31.0 + 0.7)
                                   + 0.25 * std::sin(x / 9.5 + 4.2) * std::sin(z / 10.5 + 1.9) + 0.15 * std::cos((x - z) / 14.0 + 3.3);
                    delta[qsizetype(gz) * W3 + gx] = qint8(std::lround(A * n * wv));
                }
            });
            qint64 nEmpty = 0;
            for (qsizetype i = 0; i < delta.size(); ++i) if (delta[i]) ++nEmpty;
            erX0 = ex0; erZ0 = ez0; erW = W3; erH = H3;
            emptyDelta = std::move(delta);
            if (nEmpty > 0) res.lairInfo += QObject::tr("; terrain of the empty lands: %1 columns").arg(nEmpty);
        }
    }
    g_sandId = quint16(IdPaletteStart + paletteIndexOf("minecraft:sand"));
    g_clayId = quint16(IdPaletteStart + paletteIndexOf("minecraft:clay"));
    // CLIMATE ZONE (export/climate.h): the chunk biomes, the surface and the tree species of the export area (the world around — as before)
    g_zone = std::max(0, std::min(int(Climate::Count) - 1, world.climateZone));
    {
        // cell values: 0 other, 1 Pale Garden, 2 taiga, 3 forest, 4 birch forest, 5 dappled forest, 6 flower forest, 7 meadow (8 and 9 are fixed: swamp, dark forest)
        static const char* const kPlains = "minecraft:plains";
        const char* b[8] = { "minecraft:plains", "minecraft:pale_garden", "minecraft:taiga", "minecraft:forest", "minecraft:birch_forest",
                             "minecraft:dappled_forest", "minecraft:flower_forest", "minecraft:meadow" };
        switch (g_zone)
        {
        case Climate::Boreal:        b[0] = "minecraft:taiga"; b[3] = "minecraft:taiga"; b[5] = "minecraft:taiga"; b[6] = "minecraft:taiga"; b[7] = "minecraft:taiga"; break;
        case Climate::Tundra:        b[0] = b[2] = b[3] = b[4] = b[5] = b[6] = b[7] = "minecraft:snowy_plains"; b[2] = b[3] = b[4] = b[5] = b[6] = "minecraft:snowy_taiga"; break;
        case Climate::Steppe:        b[0] = b[7] = kPlains; break;
        case Climate::Mediterranean: b[0] = b[7] = "minecraft:savanna"; b[2] = b[3] = b[4] = b[5] = b[6] = "minecraft:savanna"; break;
        case Climate::Desert:        b[0] = b[2] = b[3] = b[4] = b[5] = b[6] = b[7] = "minecraft:desert"; break;
        case Climate::Savanna:       b[0] = b[2] = b[3] = b[4] = b[5] = b[6] = b[7] = "minecraft:savanna"; break;
        case Climate::Tropical:      b[0] = b[7] = "minecraft:jungle"; b[2] = b[3] = b[4] = b[5] = b[6] = "minecraft:jungle"; break;
        case Climate::Monsoon:       b[4] = kPlains; break;   // oaks and palms around the schools — not a birch forest
        case Climate::SemiArid:      b[0] = b[2] = b[3] = b[4] = b[5] = b[6] = b[7] = "minecraft:savanna"; break;   // sand surface, the savanna's green tint of the leaves
        case Climate::Rainforest:    b[0] = "minecraft:forest"; break;   // the open lands of a temperate rainforest are forest too
        case Climate::DarkForest:    b[0] = b[2] = b[3] = b[4] = b[5] = b[6] = b[7] = "minecraft:dark_forest"; break;
        default: break;
        }
        for (int i = 0; i < 8; ++i) g_biomeName[i] = b[i];
        g_zoneSurface = (g_zone == Climate::Desert || g_zone == Climate::SemiArid) ? g_sandId : (g_zone == Climate::Tundra ? quint16(IdPaletteStart + paletteIndexOf("minecraft:snow_block")) : quint16(IdGrass));
        res.lairInfo += QObject::tr("; climate zone: %1").arg(Climate::name(g_zone));
    }
    // SMOOTH SLOPE OF THE BOTTOM: the depth of a water body column grows from the shore to the center by 1 block every ~2.5 blocks (the distance to the shore by 8 neighbors), but not more than the depth of the water body;
    // near the shore — a 1-block shallow, not a sheer pit.
    if (!colWater.isEmpty())
    {   // The distance to the shore — on a dense grid (forward/backward passes by 8 neighbors until convergence): a hash walk over tens of millions of columns of a big reservoir
        // took tens of minutes. Non-water and artificial columns do not pass the distance on (as before).
        int wx0 = std::numeric_limits<int>::max(), wz0 = std::numeric_limits<int>::max(), wx1 = std::numeric_limits<int>::min(), wz1 = std::numeric_limits<int>::min();
        for (auto it = colWater.constBegin(); it != colWater.constEnd(); ++it)
        {
            const int x = keyFirst(it.key()), z = keySecond(it.key());
            wx0 = std::min(wx0, x); wx1 = std::max(wx1, x); wz0 = std::min(wz0, z); wz1 = std::max(wz1, z);
        }
        const qint64 WW = qint64(wx1) - wx0 + 3, WH = qint64(wz1) - wz0 + 3;       // a frame with a 1-cell margin: at the grid border there are enough "land" neighbors
        if (WW * WH <= 400000000ll)
        {
            const int W2 = int(WW), H2 = int(WH);
            QVector<quint8> wd(int(WW * WH), 0);                                   // 0 — not water or artificial; 255 — water, the distance is not yet known
            QVector<char> isWater(int(WW * WH), 0);
            for (auto it = colWater.constBegin(); it != colWater.constEnd(); ++it)
            {
                const int idx = (keySecond(it.key()) - wz0 + 1) * W2 + (keyFirst(it.key()) - wx0 + 1);
                isWater[idx] = 1;
                if (!colWaterArt.contains(it.key())) wd[idx] = 255;
            }
            for (int z = 1; z < H2 - 1; ++z)                                       // the initial cells: a non-artificial water has a side neighbor without water
                for (int x = 1; x < W2 - 1; ++x)
                {
                    const int idx = z * W2 + x;
                    if (!wd[idx]) continue;
                    if (!isWater[idx - 1] || !isWater[idx + 1] || !isWater[idx - W2] || !isWater[idx + W2]) wd[idx] = 1;
                }
            for (int pass = 0; pass < 64; ++pass)
            {
                bool changed = false;
                for (int z = 1; z < H2 - 1; ++z)
                    for (int x = 1; x < W2 - 1; ++x)
                    {
                        const int idx = z * W2 + x;
                        if (!wd[idx] || wd[idx] == 1) continue;
                        int m = wd[idx];
                        const int nb[4] = { idx - 1, idx - W2 - 1, idx - W2, idx - W2 + 1 };
                        for (int k = 0; k < 4; ++k) if (wd[nb[k]] && wd[nb[k]] + 1 < m) m = wd[nb[k]] + 1;
                        if (m < wd[idx]) { wd[idx] = quint8(m); changed = true; }
                    }
                for (int z = H2 - 2; z >= 1; --z)
                    for (int x = W2 - 2; x >= 1; --x)
                    {
                        const int idx = z * W2 + x;
                        if (!wd[idx] || wd[idx] == 1) continue;
                        int m = wd[idx];
                        const int nb[4] = { idx + 1, idx + W2 + 1, idx + W2, idx + W2 - 1 };
                        for (int k = 0; k < 4; ++k) if (wd[nb[k]] && wd[nb[k]] + 1 < m) m = wd[nb[k]] + 1;
                        if (m < wd[idx]) { wd[idx] = quint8(m); changed = true; }
                    }
                if (!changed) break;
            }
            for (auto it = colWater.begin(); it != colWater.end(); ++it)
            {
                if (colWaterArt.contains(it.key())) continue;
                const int dv = wd[(keySecond(it.key()) - wz0 + 1) * W2 + (keyFirst(it.key()) - wx0 + 1)];
                const int dd = dv == 0 ? 1 : dv;                               // 255 — very far from the shore (the distance is saturated)
                const int slope = 1 + int((dd - 1) / 2.5);
                it.value() = std::max(1, std::min(it.value(), slope));
            }
        }
        else
        {   // an unrealistically big water frame (separated lakes tens of kilometers apart) — the former hash walk
            QHash<ColKey, int> wdist;
            wdist.reserve(colWater.size());
            QVector<qint64> cur;
            for (auto it = colWater.constBegin(); it != colWater.constEnd(); ++it)
            {
                if (colWaterArt.contains(it.key())) continue;                 // artificial water bodies — a flat bottom, the former depth
                const int x = keyFirst(it.key()), z = keySecond(it.key());
                if (!colWater.contains(pairKey(x + 1, z)) || !colWater.contains(pairKey(x - 1, z)) || !colWater.contains(pairKey(x, z + 1)) || !colWater.contains(pairKey(x, z - 1)))
                { wdist.insert(it.key(), 1); cur.append(it.key()); }
            }
            for (int step = 2; !cur.isEmpty(); ++step)
            {
                QVector<qint64> nxt;
                for (const qint64 k : cur)
                {
                    const int x = keyFirst(k), z = keySecond(k);
                    for (int dz = -1; dz <= 1; ++dz)
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            const qint64 nk = pairKey(x + dx, z + dz);
                            if (!colWater.contains(nk) || colWaterArt.contains(nk) || wdist.contains(nk)) continue;
                            wdist.insert(nk, step);
                            nxt.append(nk);
                        }
                }
                cur = nxt;
            }
            for (auto it = colWater.begin(); it != colWater.end(); ++it)
            {
                if (colWaterArt.contains(it.key())) continue;
                const int dd = wdist.value(it.key(), 1);
                const int slope = 1 + int((dd - 1) / 2.5);
                it.value() = std::max(1, std::min(it.value(), slope));
            }
        }
    }
    // The fill is ONLY the column metadata (GroundChunk): the ground is not written into the store at all, the chunk
    // sections are assembled by buildChunkNbt (deep stone — with a unary palette without materialization,
    // we pack only the transition layers). Earlier there was a loop y=-64..top here with store.set for every block
    // (~130 per column, ~170 million calls per noise export with an apron) — the main time cost.
    // Sandy shores: the distance (1..2, Chebyshev) from a dry column to the nearest non-artificial water — on a dense grid over the water frame (+2): on big reservoirs
    // 25 hash queries for every land column were the longest step of the fill.
    snX0 = 0;
    snZ0 = 0;
    snW = 0;
    snH = 0;
    sandGrid = false;
    if (!colWater.isEmpty() && g_sandId)
    {
        int ax0 = std::numeric_limits<int>::max(), az0 = std::numeric_limits<int>::max(), ax1 = std::numeric_limits<int>::min(), az1 = std::numeric_limits<int>::min();
        for (auto it = colWater.constBegin(); it != colWater.constEnd(); ++it)
        {
            const int x = keyFirst(it.key()), z = keySecond(it.key());
            ax0 = std::min(ax0, x); ax1 = std::max(ax1, x); az0 = std::min(az0, z); az1 = std::max(az1, z);
        }
        snX0 = ax0 - 3; snZ0 = az0 - 3;
        const qint64 gw = qint64(ax1) - ax0 + 7, gh = qint64(az1) - az0 + 7;
        if (gw * gh <= 400000000ll)
        {
            snW = int(gw); snH = int(gh);
            QVector<char> natW(snW * snH, 0);
            for (auto it = colWater.constBegin(); it != colWater.constEnd(); ++it)
                if (!colWaterArt.contains(it.key())) natW[(keySecond(it.key()) - snZ0) * snW + (keyFirst(it.key()) - snX0)] = 1;
            sandNear.fill(0, snW * snH);
            for (int z = 3; z < snH - 3; ++z)
                for (int x = 3; x < snW - 3; ++x)
                {
                    const int idx = z * snW + x;
                    if (!natW[idx]) continue;
                    if (natW[idx - 1] && natW[idx + 1] && natW[idx - snW] && natW[idx + snW]
                        && natW[idx - snW - 1] && natW[idx - snW + 1] && natW[idx + snW - 1] && natW[idx + snW + 1]) continue;   // inside the water — the edges are not touched
                    for (int dz = -2; dz <= 2; ++dz)
                        for (int dx = -2; dx <= 2; ++dx)
                        {
                            const int d = std::max(std::abs(dx), std::abs(dz));
                            if (!d) continue;
                            quint8& sv = sandNear[idx + dz * snW + dx];
                            if (!sv || d < sv) sv = quint8(d);
                        }
                }
            sandGrid = true;
        }
    }
    // The chunks that have any building / wool / water columns at all: the ground fill looks into these hashes for each of tens of millions of columns (cache misses are
    // the longest part of the step on big projects), and the overwhelming majority of chunks (forests, fields) are not touched by them.
    {
        auto addChunks = [](QSet<ColKey>& dst, const auto& m)
        {
            qint64 last = std::numeric_limits<qint64>::min();
            for (auto it = m.constBegin(); it != m.constEnd(); ++it)
            {
                const qint64 c = pairKey(keyFirst(it.key()) >> 4, keySecond(it.key()) >> 4);
                if (c != last) { dst.insert(c); last = c; }
            }
        };
        addChunks(chunksMin, colMin);
        addChunks(chunksWool, colWool);
        addChunks(chunksWater, colWater);
        addChunks(chunksBed, railBedCols);
    }
    ground.reserve(int(fillChunks.size()));
    auto makeGround = [&](qint64 ckKey)
    {
        GroundChunk g;
        g.top.resize(256); g.surf.resize(256); g.wd.resize(256); g.art.fill(0, 256);
        int stoneFloor = std::numeric_limits<int>::max();
        const int bx0 = keyFirst(ckKey) << 4, bz0 = keySecond(ckKey) << 4;
        const bool hasMin = chunksMin.contains(ckKey), hasWool = chunksWool.contains(ckKey), hasWat = chunksWater.contains(ckKey),
                   hasBed = chunksBed.contains(ckKey);
        for (int lz = 0; lz < 16; ++lz)
            for (int lx = 0; lx < 16; ++lx)
            {
                const int cx = bx0 + lx, cz = bz0 + lz;
                const qint64 ck = pairKey(cx, cz);
                int top = groundTopCol(cx, cz);
                if (hasMin)
                {
                    const auto oit = colMin.constFind(ck);
                    if (oit != colMin.constEnd() && oit.value() - 1 <= top + 3)       // a body hanging higher than 3 blocks over the ground does not raise it
                        top = std::max(top, oit.value() - 1);
                }
                if (top > 255) top = 255;
                quint16 surface = g_zoneSurface;              // the top block of the column (turf or csrpol wool; sand in the desert, snow in the tundra)
                const auto wit = hasWool ? colWool.constFind(ck) : colWool.constEnd();
                if (wit != colWool.constEnd())
                    surface = wit.value();
                else if (!paleCol.isEmpty())
                {
                    const auto pit = paleCol.constFind(ck);
                    if (pit != paleCol.constEnd() && pit->surf) surface = pit->surf;
                }
                int wd = 0;                                   // the depth of the water pit (0 — land)
                const auto witW = hasWat ? colWater.constFind(ck) : colWater.constEnd();
                if (witW != colWater.constEnd())
                    wd = witW.value();                        // top>=0 always (median clamp), the pit does not go below -64
                else if (surface == IdGrass && !colWater.isEmpty() && g_sandId)
                {   // sandy shores (partially): a dry column near water (up to 2 blocks) — sand with a chance that falls with the distance
                    int nearD = 99;
                    if (sandGrid)
                    {
                        const int gx = cx - snX0, gz = cz - snZ0;
                        if (gx >= 0 && gx < snW && gz >= 0 && gz < snH && sandNear[gz * snW + gx]) nearD = sandNear[gz * snW + gx];
                    }
                    else
                        for (int dz = -2; dz <= 2 && nearD > 1; ++dz)
                            for (int dx = -2; dx <= 2; ++dx)
                                if (colWater.contains(pairKey(cx + dx, cz + dz)) && !colWaterArt.contains(pairKey(cx + dx, cz + dz))) nearD = std::min(nearD, std::max(std::abs(dx), std::abs(dz)));
                    if (nearD <= 2)
                    {
                        quint32 h = quint32(cx) * 374761393u ^ quint32(cz) * 668265263u ^ 0x5A4Du;
                        h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
                        if (h % 100u < (nearD == 1 ? 70u : 35u)) surface = g_sandId;
                    }
                }
                const auto bit = hasBed ? railBedCols.constFind(ck) : railBedCols.constEnd();
                if (bit != railBedCols.constEnd() && wit == colWool.constEnd() && witW == colWater.constEnd())
                    surface = bit.value();      // the track bed: the rails lie on stone/brick, not on turf
                                                // (a road crossing or a water body wins over the bed)
                const int col = lz * 16 + lx;
                g.top[col] = qint16(top);
                g.surf[col] = surface;
                g.wd[col] = quint16(wd);
                if (wd > 0 && colWaterArt.contains(ck)) g.art[col] = 1;
                // below (inclusive) in all the columns there is guaranteed stone: water at wd>3
                // reaches top-3 and deeper, the ground occupies top-2..top-1 (see groundBlockAt)
                stoneFloor = std::min(stoneFloor, wd > 0 ? top - wd - 3 : top - 3);    // under the water body bottom — up to 3 blocks of sand, then stone (for artificial ones also with a margin)
            }
        g.stoneFloor = stoneFloor;
        return g;
    };
    lap("g3 outposts/camps+makeGround setup");
    {   // ROADS FOLLOW THE RELIEF: the roadway marking (carpets) was laid at the level of the plane/terrain before the forest relief existed. Where the relief changed the ground
        // of a road column, the carpet is moved onto the new ground (top+1) and colMin follows it, so the road is not a trench cut through a hill and the ground is not
        // raised back to the old level.
        qint64 nMoved = 0;
        for (const qint64 ck : roadCols)
        {
            const auto pit = paleCol.constFind(ck);
            if (pit == paleCol.constEnd()) continue;
            auto mit = colMin.find(ck);
            if (mit == colMin.end()) continue;
            const int x = keyFirst(ck), z = keySecond(ck);
            const int oldY = mit.value(), newY = int(pit->top) + 1;
            if (newY == oldY) continue;
            const quint16 b = store.get(x, oldY, z);
            if (b != IdAir && store.get(x, newY, z) == IdAir)
            {
                store.erase(x, oldY, z);
                store.set(x, newY, z, b);
            }
            mit.value() = newY;
            ++nMoved;
        }
        if (nMoved > 0) res.lairInfo += QObject::tr("; road columns moved onto the relief: %1").arg(nMoved);
    }
    // Caves: a chunk is deterministically assigned a template index (different chunks — different caves,
    // a repeat is allowed). Without the file (caveSet is empty) caveTpl stays -1 — the underground is solid rock.
    auto assignCave = [&](GroundChunk& g, qint64 ckKey)
    {
        if (!caveSet.empty())
            g.caveTpl = 0;          // the chunk underground is taken from the cave world by the chunk coordinates (CaveSet::get)
    };
    {   // Ground fill: makeGround only reads the common maps, the chunks are independent — we compute in blocks
        // by threads (our own pool), then put into ground in order. Progress/cancel — from the main thread.
        const QVector<qint64> fl(fillChunks.constBegin(), fillChunks.constEnd());
        const int nF = fl.size();
        QVector<GroundChunk> built(nF);
        QThreadPool gpool;
        gpool.setMaxThreadCount(std::max(1, QThread::idealThreadCount()));
        constexpr int kBlock = 64;
        QVector<QFuture<void>> gf;
        gf.reserve((nF + kBlock - 1) / kBlock);
        for (int b0 = 0; b0 < nF; b0 += kBlock)
            gf << QtConcurrent::run(&gpool, [&, b0]()
            {
                const int b1 = std::min(nF, b0 + kBlock);
                for (int i2 = b0; i2 < b1; ++i2)
                {
                    if (cancel && cancel->load()) return;
                    built[i2] = makeGround(fl[i2]);
                    assignCave(built[i2], fl[i2]);
                }
            });
        for (int bi = 0; bi < gf.size(); ++bi)
        {
            gf[bi].waitForFinished();
            if (progress && (bi & 0x7) == 0)
                progress(std::min(gSurvey + (bi + 1) * kBlock, gTotal), progTotal, 1);  // the "ground" phase — fill
            if (cancel && cancel->load())
            {
                for (QFuture<void>& f : gf) f.waitForFinished();
                res.cancelled = true;
                res.error = QObject::tr("Export cancelled.");
                return res;
            }
        }
        for (int i2 = 0; i2 < nF; ++i2)
            ground.insert(fl[i2], built[i2]);
    }

    lap("g4 ground fill");
    {   // BRIDGES: a road or a railway that runs over water keeps its level (the plane of the ground around) and gets an artificial bridge: a deck flush with the road level
        // (the roadway wool, for a railway — stone bricks), a stone brick girder under it and piers every 12 blocks down to the bottom. The water stays under the bridge.
        const quint16 stoneBr = quint16(IdPaletteStart + paletteIndexOf("minecraft:stone_bricks"));
        qint64 nBridge = 0;
        auto bridgeAt = [&](qint64 ck, bool rail)
        {
            const int x = keyFirst(ck), z = keySecond(ck);
            const auto git = ground.constFind(pairKey(x >> 4, z >> 4));
            if (git == ground.constEnd()) return;
            const int col = (z & 15) * 16 + (x & 15);
            const int wd = git->wd[col], top = git->top[col];
            if (wd <= 0) return;
            const auto wit = colWool.constFind(ck);
            store.set(x, top, z, (!rail && wit != colWool.constEnd()) ? wit.value() : stoneBr);       // the deck (replaces the top row of water)
            store.set(x, top - 1, z, stoneBr);                                                         // the girder under the deck
            if (x % 12 == 0 && z % 12 == 0)
                for (int y = top - 2; y >= top - wd; --y) store.set(x, y, z, stoneBr);                 // a pier down to the bottom
            ++nBridge;
        };
        if (!colWater.isEmpty())
        {
            for (const qint64 ck : roadCols) if (colWater.contains(ck)) bridgeAt(ck, false);
            for (const qint64 ck : railColsAll) if (colWater.contains(ck) && !roadCols.contains(ck)) bridgeAt(ck, true);
        }
        if (nBridge > 0) res.lairInfo += QObject::tr("; bridge columns over water (roads and railways): %1").arg(nBridge);
    }
    if (!patchPlaces.isEmpty())
    {   // outposts and camps: the structure blocks (columns with weight >= 0.5), chests, the raider spawn zone
        qint64 nBl = 0;
        for (const PatchPlace& pp : patchPlaces)
        {
            const PaleDonor& pd = patches[pp.pi];
            for (int uz = 0; uz < pd.H; ++uz)
                for (int ux = 0; ux < pd.W; ++ux)
                {
                    const int x = pp.x0 + ux, z = pp.z0 + uz, di = uz * pd.W + ux;
                    const auto pit = paleCol.constFind(pairKey(x, z));
                    if (pit == paleCol.constEnd() || pit->pid != pp.pi || pit->w < 128) continue;
                    if (roadCols.contains(pairKey(x, z))) continue;                // a road crosses the patch: it follows the terrain, without the donor trees on it
                    for (quint32 k = pd.off[di]; k < pd.off[di + 1]; ++k)
                    {
                        const int y = pit->top + int(pd.ady[int(k)]);
                        if (y > 319 || y < 1) continue;
                        const quint16 id = pd.aid[int(k)];
                        if (pd.name.contains(QLatin1String("mansion")) && (id == IdLava || blockName(id) == QLatin1String("minecraft:lava"))) continue;   // there is no lava in the mansion
                        if (store.get(x, y, z) != IdAir) continue;
                        store.set(x, y, z, id);
                        ++nBl;
                    }
                }
            for (const PaleDonor::PatchBe& b : pd.bes)
            {
                const int x = pp.x0 + b.x, z = pp.z0 + b.z, di = b.z * pd.W + b.x;
                const auto pit = paleCol.constFind(pairKey(x, z));
                if (pit == paleCol.constEnd() || pit->pid != pp.pi) continue;
                const int y = pit->top + b.dy;
                if (store.get(x, y, z) == IdAir) continue;                          // there is no block (cut off) — no record needed
                CaveBe cb;
                cb.lx = x & 15; cb.lz = z & 15; cb.y = y; cb.kind = b.kind; cb.str = b.str;
                extraBesByChunk[pairKey(x >> 4, z >> 4)].append(cb);
                Q_UNUSED(di);
            }
            if (pd.name.contains(QLatin1String("mansion")) && pd.bbx1 >= pd.bbx0)
            {   // the mansion inhabitants (in the game they are created by the structure generator, the template has only blocks): vindicators, evokers, allays — on the free floor cells inside the frame
                struct Cell { int x, y, z; quint32 h; };
                QVector<Cell> cand;
                for (int uz = pd.bbz0 + 2; uz <= pd.bbz1 - 2; uz += 2)
                    for (int ux = pd.bbx0 + 2; ux <= pd.bbx1 - 2; ux += 2)
                    {
                        const int x = pp.x0 + ux, z = pp.z0 + uz;
                        const auto pit = paleCol.constFind(pairKey(x, z));
                        if (pit == paleCol.constEnd() || pit->pid != pp.pi) continue;
                        for (int y = pit->top + 1; y < pit->top + 34; ++y)
                            if (store.get(x, y, z) == IdAir && store.get(x, y - 1, z) != IdAir && store.get(x, y + 1, z) == IdAir && store.get(x, y + 2, z) == IdAir
                                && store.get(x + 1, y, z) == IdAir && store.get(x - 1, y, z) == IdAir && store.get(x, y, z + 1) == IdAir && store.get(x, y, z - 1) == IdAir)
                                cand.append(Cell{ x, y, z, colHash(pairKey(x * 7 + y, z) ^ 0x3A71ull) });
                    }
                std::sort(cand.begin(), cand.end(), [](const Cell& a, const Cell& b) { return a.h < b.h; });
                QVector<Cell> taken;
                for (const Cell& c : cand)
                {
                    if (taken.size() >= 18) break;
                    bool far = true;
                    for (const Cell& t : taken)
                        if (std::abs(t.x - c.x) + std::abs(t.z - c.z) + std::abs(t.y - c.y) * 2 < 7) { far = false; break; }
                    if (far) taken.append(c);
                }
                for (int i = 0; i < taken.size(); ++i)
                {
                    NpcSite m;
                    m.kind = (i % 6 == 5) ? 5 : 4;                                     // every sixth — an allay, the rest — raiders
                    if (m.kind == 5) m.animal = QStringLiteral("minecraft:allay");
                    else m.variant = (i % 6 == 2) ? 6 : 5;                             // an evoker / a vindicator
                    m.x = taken[i].x; m.y = taken[i].y; m.z = taken[i].z;
                    m.yaw = int((taken[i].h >> 9) % 4u) * 90;
                    npcs.append(m);
                }
                res.lairInfo += QObject::tr("; mansion inhabitants: %1").arg(taken.size());
            }
            if (pd.name.contains(QLatin1String("outpost")) && pd.bbx1 >= pd.bbx0)
            {   // the raider spawn zone = the structure frame (like the "false outpost" of the logs): the game spawns pillagers inside
                const auto cit = paleCol.constFind(pairKey(pp.x0 + (pd.bbx0 + pd.bbx1) / 2, pp.z0 + (pd.bbz0 + pd.bbz1) / 2));
                if (cit != paleCol.constEnd())
                {
                    LairZone z;
                    z.x0 = pp.x0 + pd.bbx0; z.x1 = pp.x0 + pd.bbx1; z.z0 = pp.z0 + pd.bbz0; z.z1 = pp.z0 + pd.bbz1;
                    z.y0 = cit->top; z.y1 = cit->top + 30;
                    z.floorsY.append(cit->top + 1);
                    lairZones.append(z);
                }
            }
        }
        res.lairInfo += QObject::tr("; outpost and camp blocks: %1").arg(nBl);
    }
    if (!colWater.isEmpty())
    {   // SHIPS AND SEAWEED of big water bodies. A ship is a piece of a real world (patch_wreck*.bin): it either lies on the bottom (50%) or drifts on the water with the keel 2 blocks below the waterline
        // (without waterlogged in the blocks above the water). Only in big deep water bodies: the whole site (the patch with a 6-block margin) must be non-artificial water with a depth
        // of 5 (bottom) / 4 (drift) or more, with a bottom drop up to 4, without bridges; ships — not closer than 90 blocks to each other.
        QVector<int> wr;
        for (int i = 0; i < patches.size(); ++i) if (patches[i].name.contains(QLatin1String("wreck")) && patches[i].W > 0) wr << i;
        auto groundCol = [&](int x, int z, int& top, int& wdp, bool& art) -> bool
        {
            const auto git = ground.constFind(pairKey(x >> 4, z >> 4));
            if (git == ground.constEnd()) return false;
            const int col = (z & 15) * 16 + (x & 15);
            top = git->top[col]; wdp = git->wd[col]; art = !git->art.isEmpty() && git->art[col] != 0;
            return true;
        };
        int nWreckSunk = 0, nWreckFloat = 0;
        if (!wr.isEmpty())
        {
            int wx0 = std::numeric_limits<int>::max(), wz0 = std::numeric_limits<int>::max(), wx1 = std::numeric_limits<int>::min(), wz1 = std::numeric_limits<int>::min();
            for (auto it = colWater.constBegin(); it != colWater.constEnd(); ++it)
            {
                const int x = keyFirst(it.key()), z = keySecond(it.key());
                wx0 = std::min(wx0, x); wx1 = std::max(wx1, x); wz0 = std::min(wz0, z); wz1 = std::max(wz1, z);
            }
            struct WCand { int x, z; quint32 h; };
            QVector<WCand> cands;
            constexpr int kStep = 28;
            for (int gz = wz0 / kStep - 1; gz <= wz1 / kStep + 1; ++gz)
                for (int gx = wx0 / kStep - 1; gx <= wx1 / kStep + 1; ++gx)
                {
                    const quint32 h = colHash(pairKey(gx, gz) ^ 0x57EC4ull);
                    if (h % 100u >= 40u) continue;
                    const int cx = gx * kStep + int((h >> 8) % quint32(kStep)), cz = gz * kStep + int((h >> 16) % quint32(kStep));
                    int t = 0, d = 0; bool a = false;
                    if (!groundCol(cx, cz, t, d, a) || d < 5 || a) continue;                // the center — deep natural water
                    cands.append(WCand{ cx, cz, h });
                }
            std::sort(cands.begin(), cands.end(), [](const WCand& a, const WCand& b) { return a.h != b.h ? a.h < b.h : a.x < b.x; });
            QVector<QPoint> placedW;
            for (const WCand& c : cands)
            {
                bool far = true;
                for (const QPoint& q : placedW) if (std::hypot(double(q.x() - c.x), double(q.y() - c.z)) < 90.0) { far = false; break; }
                if (!far) continue;
                const PaleDonor& pd = patches[wr[int((c.h >> 4) % quint32(wr.size()))]];
                const bool floating = ((c.h >> 12) & 1u) != 0u;
                const int need = floating ? 4 : 5;
                const int x0 = c.x - pd.W / 2, z0 = c.z - pd.H / 2;
                int minF = 1000, maxF = -1000, topW = -1000;
                bool ok = true;
                for (int z = z0; z < z0 + pd.H && ok; z += 2)
                    for (int x = x0; x < x0 + pd.W; x += 2)
                    {
                        int t = 0, d = 0; bool a = false;
                        const qint64 ck = pairKey(x, z);
                        if (!groundCol(x, z, t, d, a) || d < need || a || colMin.contains(ck) || roadCols.contains(ck) || railColsAll.contains(ck) || colWool.contains(ck)) { ok = false; break; }
                        minF = std::min(minF, t - d); maxF = std::max(maxF, t - d); topW = std::max(topW, t);
                    }
                if (!ok || maxF - minF > 4) continue;
                // the keel line of the donor: the lowest block of the ship (hd + dy)
                int keel = std::numeric_limits<int>::max();
                for (int di = 0; di < pd.W * pd.H; ++di)
                    for (quint32 k = pd.off[di]; k < pd.off[di + 1]; ++k) keel = std::min(keel, int(pd.hd[di]) + int(pd.ady[int(k)]));
                if (keel == std::numeric_limits<int>::max()) continue;
                const int keelY = floating ? topW - 2 : maxF - 1;
                const QVector<quint16>& ids = floating ? pd.aidDry : pd.aid;
                for (int uz = 0; uz < pd.H; ++uz)
                    for (int ux = 0; ux < pd.W; ++ux)
                    {
                        const int di = uz * pd.W + ux;
                        const int x = x0 + ux, z = z0 + uz;
                        for (quint32 k = pd.off[di]; k < pd.off[di + 1]; ++k)
                        {
                            const int y = keelY + int(pd.hd[di]) + int(pd.ady[int(k)]) - keel;
                            if (y < -63 || y > 318) continue;
                            store.set(x, y, z, ids[int(k)]);
                        }
                    }
                for (const PaleDonor::PatchBe& b : pd.bes)
                {
                    const int x = x0 + b.x, z = z0 + b.z;
                    const int y = keelY + int(pd.hd[b.z * pd.W + b.x]) + b.dy - keel;
                    if (store.get(x, y, z) == IdAir) continue;
                    CaveBe cb;
                    cb.lx = x & 15; cb.lz = z & 15; cb.y = y; cb.kind = b.kind; cb.str = b.str;
                    extraBesByChunk[pairKey(x >> 4, z >> 4)].append(cb);
                }
                placedW.append(QPoint(c.x, c.z));
                (floating ? nWreckFloat : nWreckSunk)++;
            }
            if (nWreckSunk + nWreckFloat > 0)
                res.lairInfo += QObject::tr("; shipwrecks in big water bodies: %1 (on the bottom %2, on the water %3)").arg(nWreckSunk + nWreckFloat).arg(nWreckSunk).arg(nWreckFloat);
        }
        // seaweed: kelp (the kelp_plant stem + the kelp top) and seagrass in patches; only non-artificial water with a depth of 2 or more, an untouched bottom
        const quint16 kelpTop = quint16(IdPaletteStart + paletteIndexOf("minecraft:kelp"));
        const quint16 kelpStem = quint16(IdPaletteStart + paletteIndexOf("minecraft:kelp_plant"));
        const quint16 seagr = quint16(IdPaletteStart + paletteIndexOf("minecraft:seagrass"));
        qint64 nKelp = 0, nGrass = 0;
        for (auto it = colWater.constBegin(); it != colWater.constEnd(); ++it)
        {
            const int x = keyFirst(it.key()), z = keySecond(it.key());
            int t = 0, d = 0; bool a = false;
            if (!groundCol(x, z, t, d, a) || a || d < 2) continue;
            const quint32 pt = colHash(pairKey(x >> 3, z >> 3) ^ 0x5EA6ull) % 100u;          // an 8×8 patch: dense / sparse
            const quint32 h = colHash(it.key() ^ 0x5EA7ull);
            const int floorY = t - d;
            if (store.get(x, floorY + 1, z) != IdAir) continue;
            if (d >= 4 && h % 100u < (pt < 40u ? 13u : 2u))
            {
                const int top = std::min(d - 1, 3 + int((h >> 8) % 8u));
                for (int i = 1; i < top; ++i) store.set(x, floorY + i, z, kelpStem);
                store.set(x, floorY + top, z, kelpTop);
                ++nKelp;
            }
            else if ((h >> 12) % 100u < (pt < 50u ? 18u : 4u))
            {
                store.set(x, floorY + 1, z, seagr);
                ++nGrass;
            }
        }
        if (nKelp + nGrass > 0) res.lairInfo += QObject::tr("; seaweed: kelp %1, seagrass %2 columns").arg(nKelp).arg(nGrass);
    }
    if (paleDonorUsed)
    {   // donor trees and litter: columns with a weight >= 0.65 (right at the forest border tree pieces are not cut)
        qint64 nBl = 0;
        QSet<ColKey> lavaCols;
        for (auto pit = paleCol.constBegin(); pit != paleCol.constEnd(); ++pit)
        {
            if (pit->pid >= 0 || pit->w < 235 || pit->surf == 0) continue;   // donor trees — where the terrain is almost entirely donor (the fringe at the border — procedural trees)
            if (roadCols.contains(pit.key())) continue;                       // a road lies on the donor terrain: no trees and no underground voids on it
            const int x = keyFirst(pit.key()), z = keySecond(pit.key());
            const int di = pit->di;                                     // the template column (for extra mansions — the reflected free one)
            for (quint32 k = paleDonor.off[di]; k < paleDonor.off[di + 1]; ++k)
            {
                const int y = pit->top + int(paleDonor.ady[int(k)]);
                if (y > 319) break;
                if (store.get(x, y, z) != IdAir) continue;
                quint16 id = paleDonor.aid[int(k)];
                if (id == IdLava || blockName(id) == QLatin1String("minecraft:lava")) continue;          // there is no lava in the Pale Garden
                // there are few real hearts in the donor trees (in the game they are rare): some trunks get a heart — in the game there are enough of them too, so that the forest "lives"
                if (id == paleDonor.logId && paleDonor.heartId && paleDonor.ady[int(k)] == 4 && ((x | z) & 1) == 0
                    && colHash(pairKey(x >> 2, z >> 2) ^ 0x4EA7ull) % 100u < 22u)
                    id = paleDonor.heartId;
                store.set(x, y, z, id);
                ++nBl;
                if (id == IdLava) lavaCols.insert(pit.key());
                if (paleDonor.heartIds.contains(id)) heartsByChunk[pairKey(x >> 4, z >> 4)].append(QPoint3{ x, y, z });
            }
            if (pit->surf == quint16(IdLava)) lavaCols.insert(pit.key());
            if (!paleDonor.uoff.isEmpty() && !colMin.contains(pit.key()) && !colWater.contains(pit.key()))
            {   // under the donor terrain (the top 28 blocks) lie its real pits and voids — we carry them as rigidly as the trees
                QVector<QPair<qint16, quint16>> ul;
                for (quint32 k = paleDonor.uoff[di]; k < paleDonor.uoff[di + 1]; ++k)
                {
                    const quint16 uidv = paleDonor.uid[int(k)];
                    if (uidv == IdLava || blockName(uidv) == QLatin1String("minecraft:lava")) continue;
                    ul.append(qMakePair(qint16(pit->top - int(paleDonor.udy[int(k)])), uidv));
                }
                if (!ul.isEmpty())
                {
                    int lowY = 1000;
                    for (const auto& ue : ul) lowY = std::min(lowY, int(ue.first));
                    const auto gu = ground.find(pairKey(x >> 4, z >> 4));
                    if (gu != ground.end()) gu->stoneFloor = std::min(gu->stoneFloor, lowY - 1);
                    paleUnder.insert(pit.key(), ul);
                }
            }
        }
        // Lava puddles: along the outline (8 neighbors) stone instead of moss/grass and no plants on top, otherwise the lava sets the litter on fire.
        for (const qint64 lk : lavaCols)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int x = keyFirst(lk) + dx, z = keySecond(lk) + dz;
                    const qint64 ck = pairKey(x, z);
                    if (lavaCols.contains(ck)) continue;
                    const auto git = ground.find(pairKey(x >> 4, z >> 4));
                    const auto pc = paleCol.constFind(ck);
                    if (git == ground.end() || pc == paleCol.constEnd()) continue;
                    const int col = (z & 15) * 16 + (x & 15);
                    if (git->wd[col] > 0) continue;
                    git->surf[col] = quint16(IdStone);
                    for (int yy = pc->top + 1; yy <= pc->top + 2; ++yy)
                    {
                        const quint16 b = store.get(x, yy, z);
                        if (b != IdAir && b != IdLava && b != paleDonor.logId && !paleDonor.heartIds.contains(b)) store.erase(x, yy, z);
                    }
                }
        res.lairInfo += QObject::tr("; terrain and trees of the real Pale Garden: %1 blocks").arg(nBl);
    }
    // A station hall under a station building (CODE 44330000, building=train_station): a big decorated hall
    // (platform strips, two rows of pylons, benches, a statue, a wall mosaic, hanged lamps and an entrance
    // shaft with a staircase to the surface, its mouth inside the station building - or, when the hut is
    // too small to hold it, on the plaza where the swept pavilion stood). When a subway line passes
    // under the station, the hall floor is the tunnel
    // floor (the line was bent to the station by the straightener) and the hall sits OVER the already built
    // corridor: its entry cells are not walled up. The dig is "ground-down" (restore the rock above the ceiling
    // and lower the column top under the floor) as in the tunnels, so the hall also survives inside a cave.
    auto digStationHall = [&](const QPoint& center, int fy, const QRect& hall, const QRect& bld,
                              const QSet<qint64>& trackCols, const QSet<qint64>& openCols, bool lineStation,
                              bool canopy = false) -> bool
    {
        const auto gcen = ground.find(pairKey(center.x() >> 4, center.y() >> 4));
        if (gcen == ground.end()) return false;
        const int gc0 = (center.y() & 15) * 16 + (center.x() & 15);
        // a line station sits exactly on the tunnel floor: the corridor has already lowered the column tops
        // there, so the surface guard only makes sense for the lone dig
        if (!lineStation && (gcen->wd[gc0] > 0 || gcen->top[gc0] < fy + 12)) return false;   // under water / no earth above the ceiling
        const int x0 = hall.x(), z0 = hall.y();
        const int x1 = hall.x() + hall.width() - 1, z1 = hall.y() + hall.height() - 1;
        const bool axisX = hall.width() >= hall.height();
        const int A0 = axisX ? x0 : z0, A1 = axisX ? x1 : z1;               // along the hall / across it
        const int B0 = axisX ? z0 : x0, B1 = axisX ? z1 : x1;
        const int ac = (A0 + A1) / 2, bc = (B0 + B1) / 2;
        auto cell = [&](int a, int b) { return axisX ? pairKey(a, b) : pairKey(b, a); };
        auto put = [&](int a, int b, int y, quint16 id) { if (axisX) store.set(a, y, b, id); else store.set(b, y, a, id); };
        auto eraseV = [&](int a, int b, int y) { if (axisX) store.erase(a, y, b); else store.erase(b, y, a); };
        auto airAt = [&](int a, int b, int y) { return axisX ? store.get(a, y, b) == IdAir : store.get(b, y, a) == IdAir; };
        const auto stBlk = [](const char* n) { return quint16(IdPaletteStart + paletteIndexOf(n)); };
        static const char* const kMasonry[8] = { "minecraft:stone_bricks", "minecraft:stone_bricks",
            "minecraft:cracked_stone_bricks", "minecraft:mossy_stone_bricks", "minecraft:cobblestone",
            "minecraft:stone", "minecraft:andesite", "minecraft:polished_andesite" };
        static const char* const kFloor[8] = {
            "minecraft:stone_bricks", "minecraft:polished_andesite", "minecraft:polished_diorite", "minecraft:polished_granite",
            "minecraft:smooth_stone", "minecraft:stone", "minecraft:andesite", "minecraft:smooth_stone" };
        quint16 masId[8], floorId[8];
        for (int i = 0; i < 8; ++i) { masId[i] = stBlk(kMasonry[i]); floorId[i] = stBlk(kFloor[i]); }
        const quint16 idWhite = stBlk("minecraft:white_concrete"), idYellow = stBlk("minecraft:yellow_concrete");
        const quint16 idRed = stBlk("minecraft:red_concrete"), idLBlue = stBlk("minecraft:light_blue_concrete");
        const quint16 idLGray = stBlk("minecraft:light_gray_concrete"), idChis = stBlk("minecraft:chiseled_stone_bricks");
        const quint16 idAnd = stBlk("minecraft:polished_andesite"), idDi = stBlk("minecraft:polished_diorite");
        const quint16 idQuartz = stBlk("minecraft:smooth_quartz");         // the light portal posts

        // ---- the interior: the hall rectangle plus the boundary cells the tunnel corridor already hollowed
        QSet<qint64> inner;
        QHash<qint64, int> origTops;                                       // the original ground top (the shaft needs it)
        for (int x = x0; x <= x1; ++x)
            for (int z = z0; z <= z1; ++z) inner.insert(pairKey(x, z));
        for (int x = x0; x <= x1; ++x)
            for (int z : { z0 - 1, z1 + 1 })
                if (openCols.contains(pairKey(x, z))) inner.insert(pairKey(x, z));
        for (int z = z0; z <= z1; ++z)
            for (int x : { x0 - 1, x1 + 1 })
                if (openCols.contains(pairKey(x, z))) inner.insert(pairKey(x, z));
        for (const qint64 ck : inner)
        {
            const int x = int(keyFirst(ck)), z = int(keySecond(ck));
            railColsAll.insert(ck);                                        // lone trees (3.51) do not stand over the station
            const auto git = ground.find(pairKey(x >> 4, z >> 4));
            if (git == ground.end()) continue;                             // a column over nowhere - solid rock stays
            const int gc = (z & 15) * 16 + (x & 15);
            GroundChunk& g = git.value();
            const int orig = std::max(int(g.top[gc]), fy + 15);    // a column the tunnel has already lowered: the original surface is fy+15 (floor = surface-15 there)
            origTops.insert(ck, orig);
            const quint32 hh = colHash(ck ^ 0x515A77ull);
            for (int y = fy + 9; y <= orig; ++y)
            {   // the rock above the ceiling is restored as store blocks (building blocks are not overwritten)
                const quint16 id = (y == orig) ? g.surf[gc] : (y >= orig - 2 ? quint16(IdDirt) : quint16(IdStone));
                if (store.get(x, y, z) == IdAir) store.set(x, y, z, id);
            }
            if (orig > fy - 1) g.top[gc] = qint16(fy - 1);
            if (store.get(x, fy, z) == IdAir) store.set(x, fy, z, floorId[hh % 8u]);       // the floor, two layers
            if (store.get(x, fy - 1, z) == IdAir) store.set(x, fy - 1, z, floorId[(hh >> 8) % 8u]);
            for (int y = fy + 1; y <= fy + 8; ++y) store.erase(x, y, z);                   // the interior is 8 rows high
        }
        // ---- the lining walls around the hall (the entry of a corridor is left open)
        for (int x = x0 - 1; x <= x1 + 1; ++x)
            for (int z = z0 - 1; z <= z1 + 1; ++z)
            {
                if (x > x0 - 1 && x < x1 + 1 && z > z0 - 1 && z < z1 + 1) continue;
                const qint64 ck = pairKey(x, z);
                if (inner.contains(ck)) continue;
                railColsAll.insert(ck);
                for (int y = fy; y <= fy + 9; ++y)
                    store.set(x, y, z, masId[colHash(ck ^ (qint64(y) * 31 + 0x2A17ull)) % 8u]);
            }

        // ---- the entrance shaft up to the surface: a 5x5 pit standing INSIDE the station building (or,
        // for a swept street pavilion, on the plaza over the hall - the same construction either way).
        // The whole structure - the pit, the wall ring around it and the door passage to the lobby - is
        // checked against the building walls and never cuts them: the ring stands a cell off the facade;
        // a building too narrow for the ring loses the ring (a bare pit, still a cell off the walls), and
        // a building where even the pit with its passage does not fit gets NO shaft at all - no entrance
        // is better than a door carved through a facade. The stairs are "multi-storey" style:
        // 2-block-wide flights with full-width landings.
        const int aB0 = axisX ? bld.left() : bld.top(), aB1 = axisX ? bld.right() : bld.bottom();
        const int bB0 = axisX ? bld.top() : bld.left(), bB1 = axisX ? bld.bottom() : bld.right();
        const quint32 hSel = colHash(cell(A0, B0) ^ 0xA11CEull);
        const quint32 sSide = colHash(pairKey(center.x(), center.y()) ^ 0x51DEull);   // the same hash the
        const bool lowEnd = (sSide & 2u) == 0;                              // straightener used for the rails
        // the a window is common to both shaft variants: the three-cell door passage to the lobby must also
        // stay inside the building (the ring / door columns stand a cell off the facade walls)
        int wA0 = std::max(A0 + 1, aB0 + 2), wA1 = std::min(A1 - 5, aB1 - 6);
        int wR0 = std::max(B0 + 3, bB0 + 4), wR1 = std::min(B1 - 3, bB1 - 4);   // the ring variant owns
        int wP0 = std::max(B0 + 3, bB0 + 3), wP1 = std::min(B1 - 3, bB1 - 3);   // one more cell across
        if (canopy)
        {   // the entrance hut too small for a stairwell is REPLACED by the standard street entrance -
            // the same shaft, ring and portal a lobby building gets, only without the turnstile lane
            // (a user request: "make the entrance identical to the one used in buildings; without the
            // turnstiles it is square and compact enough to fit anywhere"). The hut is swept away, so
            // there is no facade to guard - the window is the hall itself, the hut's centre orders
            // the candidates, and a bare pit with no ring on an open plaza would just be a hazard
            wA0 = A0; wA1 = A1 - 4;
            wR0 = B0 + 3; wR1 = B1 - 3;
            wP0 = B1 + 1; wP1 = B0;                                       // the bare-pit level is disabled
        }
        int sa0 = A0, sa1 = A0 + 4, bS = bc;                               // the pit rectangle in (a, b)
        int ringLv = 0;                                     // 2 = the pit with the ring fence, 1 = the pit only
        auto shaftFree = [&](int a0, int a1, int b0, int b1)
        {   // neither the shaft nor its ring may touch a live corridor column or a rail
            for (int a = a0; a <= a1; ++a)
                for (int b = b0; b <= b1; ++b)
                    if (openCols.contains(cell(a, b)) || trackCols.contains(cell(a, b)))
                        return false;
            return true;
        };
        if (wA0 <= wA1 && wR0 <= wR1)
        {   // a big building keeps the staircase right in the middle of the hall (a user request:
            // "if the building is big enough by area - the stairway must be INSIDE it, literally in
            // the centre of the hall"). Only the line rule stands above it: the shaft may not cross
            // the corridor, so a hall pierced through its middle falls back to a corner shaft below.
            // A canopy station aims at its own swept hut's centre, not at the middle of the hall
            const int aCt = canopy ? (aB0 + aB1 - 4) / 2 : ac - 2;
            const int bCt = canopy ? (bB0 + bB1) / 2 : bc;
            const int saC = std::min(std::max(aCt, wA0), wA1);
            const int sbC = std::min(std::max(bCt, wR0), wR1);
            // a canopy hut stands on a plaza dense with tunnels: the fence may cross a live corridor
            // (the ring pass simply omits those wall columns), so only the pit and the portal's jamb
            // column at ga must stand on clear ground
            const int a0c = canopy ? (lowEnd ? saC - 1 : saC) : saC - 1;
            const int a1c = canopy ? (lowEnd ? saC + 4 : saC + 5) : saC + 5;
            const int bp = canopy ? 2 : 3;
            if (std::abs(saC - aCt) <= 1 && std::abs(sbC - bCt) <= 1 &&
                shaftFree(a0c, a1c, sbC - bp, sbC + bp))
            { sa0 = saC; sa1 = sa0 + 4; bS = sbC; ringLv = 2; }
        }
        if (wA0 <= wA1 && !ringLv)
        {   // the shaft corner inside the building: the hashed corner first, then the opposite one - a
            // live corridor crosses the hall at odd places and a shaft at any free corner still beats
            // no entrance at all (the old three fixed candidates almost always collided with one).
            // Within a corner: the ring variant everywhere before the bare pit; the candidates run
            // hash side, opposite side, centre, then the whole b window from its low edge.
            const int saCs[2] = { canopy ? std::min(std::max((aB0 + aB1 - 4) / 2, wA0), wA1)   // the shaft
                                         : (lowEnd ? wA0 : wA1),                                // stands on
                                          lowEnd ? wA1 : wA0 };                                // the hut itself
            const int bCen = canopy ? (bB0 + bB1) / 2 : bc;      // the hut's own centre, not the hall's
            for (int ci = 0; ci < 2 && !ringLv; ++ci)
                for (int lv = 2; lv >= 1 && !ringLv; --lv)
                {
                    const int wb0 = lv == 2 ? wR0 : wP0, wb1 = lv == 2 ? wR1 : wP1;
                    if (wb0 > wb1) continue;                            // even the pit edge would cut a wall
                    const bool sgnHi = (sSide & 1u) != 0;               // the side the rails were pulled away from
                    const int sp[3] = { sgnHi ? wb1 : wb0, sgnHi ? wb0 : wb1,
                                        std::min(std::max(bCen, wb0), wb1) };
                    const int ord[3] = { canopy ? sp[2] : sp[0], canopy ? sp[0] : sp[1],
                                         canopy ? sp[1] : sp[2] };      // the hut's own centre first for a canopy
                    QSet<int> tried;
                    for (int pass = 0; pass < 2 && !ringLv; ++pass)
                    {
                        // the full-window sweep: a canopy searches outward from the hut's middle (the
                        // pit should cover the hut's centre - the entrance looks right), the ordinary
                        // shaft simply walks the window from its low edge
                        QVector<int> sweep;
                        if (pass == 1)
                        {
                            for (int b = wb0; b <= wb1; ++b) sweep << b;
                            if (canopy)
                                std::stable_sort(sweep.begin(), sweep.end(), [&](int l, int r)
                                                 { return std::abs(l - bCen) < std::abs(r - bCen); });
                        }
                        const int hi = pass == 0 ? 2 : sweep.size() - 1;
                        for (int i = 0; i <= hi && !ringLv; ++i)
                        {
                            const int cand = pass == 0 ? ord[i] : sweep[i];
                            if (tried.contains(cand)) continue;
                            tried.insert(cand);
                            // a canopy hut usually stands right where the tunnels converge (that is what
                            // a metro entrance is): an indoor shaft keeps its WHOLE ring clear of a live
                            // corridor, the canopy keeps the pit and the portal's jamb column at ga - the
                            // fence on a plaza may cross a corridor, the ring pass omits those wall
                            // columns there (a round-4 request: the street entrance fits anywhere)
                            const int a0c = canopy ? (lowEnd ? saCs[ci] - 1 : saCs[ci]) : saCs[ci] - 1;
                            const int a1c = canopy ? (lowEnd ? saCs[ci] + 4 : saCs[ci] + 5) : saCs[ci] + 5;
                            const int bp = canopy ? 2 : (lv == 2 ? 3 : 2);
                            if (!shaftFree(a0c, a1c, cand - bp, cand + bp))
                                continue;
                            sa0 = saCs[ci]; sa1 = sa0 + 4; bS = cand; ringLv = lv; break;
                        }
                    }
                }
        }
        auto inShaft = [&](int a, int b)
        { return ringLv && a >= sa0 && a <= sa1 && b >= bS - 2 && b <= bS + 2; };
        auto dropTop = [&](int a, int b, int newTop)                        // the ground writer must not refill the pit
        {
            const qint64 ck = cell(a, b);
            const auto git = ground.find(pairKey(int(keyFirst(ck)) >> 4, int(keySecond(ck)) >> 4));
            if (git == ground.end()) return;
            qint16& t = git.value().top[(int(keySecond(ck)) & 15) * 16 + (int(keyFirst(ck)) & 15)];
            if (t > newTop) t = qint16(newTop);
        };
        // the pit top one over the highest natural surface over it: the top landing block takes the surface
        // block's own row, so the plaza walks straight onto it. The old fy-relative init stood two rows over
        // the ground - every entrance turned into a step the player had to jump up on (a user complaint)
        int topS = fy + 2;
        for (int a = sa0; a <= sa1; ++a)
            for (int b = bS - 2; b <= bS + 2; ++b)
                topS = std::max(topS, origTops.value(cell(a, b), fy + 1) + 1);
        const int ga = lowEnd ? sa0 - 1 : sa1 + 1;                          // the doorway row in the wall ring
        if (canopy && ringLv)
        {   // the hut too small for a stairwell is REPLACED by the compact square entrance (a user
            // request: "the arch instead of the building only when the stair and the lobby do not fit
            // inside; sweep the structure entirely so no ruined walls remain"). Done AFTER the shaft
            // placement: a hut over a corridor where even the pit cannot be placed standing free of
            // the rails keeps its building and simply gets no entrance. The hut was built as usual by
            // the voxelize pass (it cannot know about the hall decision made later), so the station
            // pass sweeps everything it put above the surface and leaves an open plaza
            for (int x = bld.left() - 1; x <= bld.right() + 1; ++x)
                for (int z = bld.top() - 1; z <= bld.bottom() + 1; ++z)
                {
                    const qint64 ck = pairKey(x, z);
                    int top = origTops.value(ck, -1);
                    if (top < 0)
                    {   // a long hut reaches past the clamped hall rectangle and origTops only knows the
                        // hall columns - read the real surface height off the ground chunk, floored so the
                        // sweep never bites into the hall lining under the building
                        const auto g0 = ground.find(pairKey(x >> 4, z >> 4));
                        top = g0 == ground.end()
                                  ? fy + 17
                                  : std::max(int(g0->top[(z & 15) * 16 + (x & 15)]), fy + 14);
                    }
                    for (int y = top + 1; y <= top + 14; ++y)
                        store.erase(x, y, z);
                    const auto git = ground.find(pairKey(x >> 4, z >> 4));   // a point object that stood on
                    if (git != ground.end() && top >= fy + 14)               // the hut left its base in the
                    {                                                        // turf - put the turf back
                        const int gcx = (z & 15) * 16 + (x & 15);
                        const int gt = std::max(int(git->top[gcx]), top);
                        if (gt >= 0 && store.get(x, gt, z) != git->surf[gcx])
                            store.set(x, gt, z, git->surf[gcx]);
                    }
                }
        }
        // dig the whole pit, not only what the hall had already hollowed: the old design left a solid column
        // standing in the pit whenever the shaft touched rock the hall had not opened - that was the wall
        // hiding the way out. The placement loop above guarantees the pit stands clear of the corridor.
        if (ringLv)
        {
            for (int a = sa0; a <= sa1; ++a)
                for (int b = bS - 2; b <= bS + 2; ++b)
                {
                    const qint64 ck = cell(a, b);
                    railColsAll.insert(ck);                                 // lone trees do not stand in the shaft
                    for (int y = fy + 1; y <= origTops.value(ck, fy + 17) + 2; ++y) eraseV(a, b, y);
                    dropTop(a, b, fy - 1);
                }
        }
        // ---- the "multi-storey" staircase: 2-block-wide flights of stone brick steps, one block of rise per
        // step, climbing along the long sides of the pit turn about turn; a full-width landing (solid masonry)
        // ends every flight - those landings are the slabs that divide the "floors" of a stairwell in an
        // apartment block, and every flight hangs over the empty half of the pit, so the headroom is honest.
        // The last flight is shortened so that its landing walks flush with the surface over the pit. The
        // flight parity is picked so that the top landing ends exactly on the doorway row (the hall door,
        // the portal and the passage all stand in one line on that side of the pit). A deeper pit is simply
        // more flights - the old ladder fallback is gone.
        int exa = ga, exb = bS, oda = 0, odb = 0;                           // the stair door cell and its out direction
        int topRow = topS;                                                  // the passage floor hangs on it
        if (ringLv)
        {
            const int nFl = (topS - fy + 2) / 4;                            // flights, the last one shortened
            const bool invert = (((nFl - 1) & 1) != 0) != lowEnd;   // the parity that lands the top
                                                                    // flight right on the ga doorway
            int rAbs = fy + 1;                                              // the row of the next step
            auto placeFlight = [&](int f, int steps)
            {
                const bool back = (((f & 1) != 0) != invert);               // a return flight runs toward sa0
                const int q0 = back ? 3 : 0;                                // its strip in the pit rows (0..4)
                const quint16 sid = quint16(axisX ? (back ? IdBrickStairsW : IdBrickStairsE)
                                                  : (back ? IdBrickStairsN : IdBrickStairsS));
                for (int j = 0; j < steps; ++j)
                {
                    const int p = back ? (sa1 - 1 - j) : (sa0 + j);         // a cell short of its landing
                    for (int q = q0; q < q0 + 2; ++q)
                        put(p, bS - 2 + q, rAbs + j, sid);
                }
                // a 2x1 ledge in the middle of the flight (a user request): the full-height web cut
                // through the flights, while the open middle row was a free fall to the bottom. Two
                // masonry cells at the mid-step row break every fall down the well; a torch on the
                // ledge lights the stairwell the way an apartment-block one is lit
                if (steps >= 3)
                {
                    const int pM = back ? (sa1 - 3) : (sa0 + 1);            // the two middle step cells
                    for (int j = 0; j <= 1; ++j)
                    {
                        const qint64 ck = cell(pM + j, bS);
                        put(pM + j, bS, rAbs + 1,
                            masId[colHash(ck ^ (qint64(rAbs + 1) * 31 + 0x2E17ull)) % 8u]);
                    }
                    put(pM + 1, bS, rAbs + 2, IdTorch);                     // standing on the ledge
                }
                const int pL = back ? sa0 : sa1;                            // the landing across the pit end
                const int rL = rAbs + steps - 1;                            // solid at the top step's row
                for (int q = 0; q < 5; ++q)
                {
                    const int b = bS - 2 + q;
                    const qint64 ck = cell(pL, b);
                    put(pL, b, rL, masId[colHash(ck ^ (qint64(rL) * 31 + 0x2E17ull)) % 8u]);
                }
                rAbs += steps;
                topRow = rL + 1;                                            // the passage floor a row under
                exa = back ? sa0 - 1 : sa1 + 1;                             // the door over the top landing's
                exb = bS; oda = back ? -1 : 1; odb = 0;                     // end, through the ring wall
            };
            for (int f = 0; f + 1 < nFl; ++f) placeFlight(f, 4);
            placeFlight(nFl - 1, topS - rAbs);                  // the short last flight lands flush with the surface
        }
        // ---- the wall ring around the pit and the fence over it: high on the hall side (no fall into the
        // pit from the grass), low on the other sides (an emergency exit); open at the hall doorway and at
        // the stair door. The bare-pit variant of a narrow building has no ring (it would cut the facade)
        for (int a = sa0 - 1; ringLv == 2 && a <= sa1 + 1; ++a)
            for (int b = bS - 3; b <= bS + 3; ++b)
            {
                const bool border = (a == sa0 - 1 || a == sa1 + 1 || b == bS - 3 || b == bS + 3);
                if (!border || inShaft(a, b) || (a == exa && b == exb)) continue;
                if (a == ga && b >= bS - 1 && b <= bS + 1)                  // the doorway into the hall stays open,
                {   // only the rock above it is restored (the ring column may stand outside the hall interior)
                    const qint64 ckd = cell(a, b);
                    for (int y = fy + 9; y <= std::max(origTops.value(ckd, fy + 17), fy + 15) + 2; ++y)
                        if (airAt(a, b, y)) put(a, b, y, masId[colHash(ckd ^ (qint64(y) * 31 + 0x2A17ull)) % 8u]);
                    continue;
                }
                const qint64 ck = cell(a, b);
                if (openCols.contains(ck) || trackCols.contains(ck)) continue;   // a fallback shaft crossing the
                const auto git = ground.find(pairKey(int(keyFirst(ck)) >> 4, int(keySecond(ck)) >> 4));   // corridor: the ring walls
                const int top = git == ground.end() ? fy + 15
                                                    : std::max(int(git->top[(int(keySecond(ck)) & 15) * 16 + (int(keyFirst(ck)) & 15)]), fy + 15);
                for (int y = fy; y <= top + 1; ++y)
                {
                    const quint16 id = masId[colHash(ck ^ (qint64(y) * 31 + 0x9B1Dull)) % 8u];
                    if (axisX) store.set(a, y, b, id); else store.set(b, y, a, id);
                }
                const int fh = (a == ga) ? 2 : 1;
                for (int k = 1; k <= fh; ++k) put(a, b, top + 1 + k, IdStoneBrickWall);
            }
        // ---- the stair door: a short passage through the ring wall out to the lobby floor (or, for a
        // swept street pavilion, straight onto the plaza), its floor a row below the top landing (a
        // threshold step out of the shaft); it punches up through the ground surface even when the
        // stair ran a little higher than the surface over the pit
        for (int e = 0; e <= 2 && ringLv; ++e)
        {
            const int xa = exa + e * oda, xb = exb + e * odb;
            const qint64 ck = cell(xa, xb);
            railColsAll.insert(ck);
            for (int y = topRow; y <= std::max(origTops.value(ck, topRow + 2), topRow + 4) + 2; ++y)
                eraseV(xa, xb, y);                                          // the passage body
            put(xa, xb, topRow - 1, idAnd);                                 // its floor
            dropTop(xa, xb, topRow - 1);
        }
        if (ringLv)
        {   // the entrance portal: a plain light frame (a user request: "the turnstiles looked bad -
            // just a frame and an entrance", and "the arch lighter - iron blocks and something light,
            // quartz for example"). Smooth quartz posts with iron bases and an iron beam over the
            // gate; the same frame stands at the hall door below and at the surface mouth above,
            // indoors and on the street alike (seen from any corner of the lobby or the plaza)
            const quint16 idIron = quint16(IdIronBlock);
            for (int db : { -2, 2 })
                for (int y = fy; y <= fy + 8; ++y) put(ga, bS + db, y, y == fy ? idIron : idQuartz);
            put(ga, bS, fy + 8, IdLanternHung);                             // a lamp above the doorway
            const qint64 ck = cell(ga, bS);
            const auto git = ground.find(pairKey(int(keyFirst(ck)) >> 4, int(keySecond(ck)) >> 4));
            const int top = git == ground.end() ? fy + 15
                            : std::max(int(git->top[(int(keySecond(ck)) & 15) * 16 + (int(keyFirst(ck)) & 15)]), fy + 15);
            for (int db : { -2, 2 })                                        // the jamb posts over the floor
                for (int y = top + 2; y <= top + 4; ++y) put(ga, bS + db, y, y == top + 2 ? idIron : idQuartz);
            for (int db : { -1, 0, 1 }) put(ga, bS + db, top + 5, idIron);  // the beam across the doorway
            put(ga, bS, top + 4, IdLanternHung);                            // the lamp glowing under the beam
        }

        // ---- the floor: polished checker tiles, the platform edge white by the rails, yellow behind it;
        // the rails lie ON it (a user request: no trench under the tracks inside a station - the floor is
        // flush with them, less authentic but playable)
        QSet<qint64> whiteSet;
        for (int a = A0; a <= A1; ++a)
            for (int b = B0; b <= B1; ++b)
            {
                const qint64 ck = cell(a, b);
                if (!inner.contains(ck) || trackCols.contains(ck) || inShaft(a, b)) continue;
                put(a, b, fy, (((a >> 1) + (b >> 1)) & 1) ? idDi : idAnd);
                if (trackCols.contains(cell(a - 1, b)) || trackCols.contains(cell(a + 1, b))
                    || trackCols.contains(cell(a, b - 1)) || trackCols.contains(cell(a, b + 1)))
                { put(a, b, fy, idWhite); whiteSet.insert(ck); }
            }
        for (int a = A0; a <= A1; ++a)
            for (int b = B0; b <= B1; ++b)
            {
                const qint64 ck = cell(a, b);
                if (!inner.contains(ck) || trackCols.contains(ck) || whiteSet.contains(ck) || inShaft(a, b)) continue;
                if (whiteSet.contains(cell(a - 1, b)) || whiteSet.contains(cell(a + 1, b))
                    || whiteSet.contains(cell(a, b - 1)) || whiteSet.contains(cell(a, b + 1)))
                    put(a, b, fy, idYellow);
            }
        // ---- pylons: two rows of 2x2 columns every 8 cells (chiseled caps, polished granite shafts)
        const int pBase[2] = { bc - 6, bc + 4 };
        for (int side = 0; side < 2; ++side)
            for (int t = A0 + 7; t + 1 <= A1 - 7; t += 8)
            {
                bool ok = true;
                for (int da = 0; da <= 1 && ok; ++da)
                    for (int db = 0; db <= 1 && ok; ++db)
                    {
                        const int a = t + da, b = pBase[side] + db;
                        const qint64 ck = cell(a, b);
                        if (!inner.contains(ck) || inShaft(a, b) || trackCols.contains(ck)
                            || trackCols.contains(cell(a - 1, b)) || trackCols.contains(cell(a + 1, b))
                            || trackCols.contains(cell(a, b - 1)) || trackCols.contains(cell(a, b + 1)))
                            ok = false;
                    }
                if (!ok) continue;
                for (int da = 0; da <= 1; ++da)
                    for (int db = 0; db <= 1; ++db)
                    {
                        const int a = t + da, b = pBase[side] + db;
                        put(a, b, fy + 1, idChis);
                        for (int y = fy + 2; y <= fy + 6; ++y) put(a, b, y, idAnd);
                        put(a, b, fy + 7, idChis);
                        put(a, b, fy + 8, idChis);
                    }
            }
        // ---- benches: three steps along the long walls between the pylons; the tall (backrest) side of a
        // stair faces its wall (facing = uphill), so a person sitting looks at the tracks, not at the wall
        const int bRow[2] = { B0 + 1, B1 - 1 };
        const quint16 bId[2] = { quint16(axisX ? IdStairsN : IdStairsW), quint16(axisX ? IdStairsS : IdStairsE) };
        for (int r = 0; r < 2; ++r)
            for (int t = A0 + 10; t + 2 <= A1 - 7; t += 8)
            {
                bool ok = true;
                for (int k = 0; k < 3 && ok; ++k)
                    ok = inner.contains(cell(t + k, bRow[r])) && !inShaft(t + k, bRow[r])
                         && !trackCols.contains(cell(t + k, bRow[r])) && airAt(t + k, bRow[r], fy + 1);
                if (!ok) continue;
                for (int k = 0; k < 3; ++k) put(t + k, bRow[r], fy + 1, bId[r]);
            }
        // ---- the statue at the end opposite the shaft (a pedestal, a wall torso, a chiseled head, a lamp above)
        {
            const int fa = lowEnd ? A1 - 2 : A0 + 2;
            int fb = bc;
            if (trackCols.contains(cell(fa, fb))) fb = trackCols.contains(cell(fa, bc - 3)) ? bc + 3 : bc - 3;
            if (inner.contains(cell(fa, fb)) && !inShaft(fa, fb) && !trackCols.contains(cell(fa, fb)) && airAt(fa, fb, fy + 1))
            {
                put(fa, fb, fy + 1, masId[0]); put(fa, fb, fy + 2, masId[0]);
                put(fa, fb, fy + 3, IdStoneBrickWall); put(fa, fb, fy + 4, IdStoneBrickWall);
                put(fa, fb, fy + 5, idChis);
                put(fa, fb, fy + 8, idChis); put(fa, fb, fy + 7, IdLanternHung);
            }
            // ---- the wall mosaic: a concentric diamond on one of the long walls (a hash picks the side)
            const int bM = (hSel & 2u) ? B1 + 1 : B0 - 1;
            for (int a = ac - 5; a <= ac + 5; ++a)
            {
                const qint64 ck = cell(a, bM);
                if (inner.contains(ck) || openCols.contains(ck)) continue;          // a corridor entry - keep it open
                for (int dy = 0; dy < 5; ++dy)
                {
                    const int d = std::abs(a - ac) + std::abs(dy - 2);
                    const quint16 id = d == 0 ? idYellow : d == 1 ? idRed : d == 2 ? idLBlue : d == 3 ? idWhite : idLGray;
                    put(a, bM, fy + 2 + dy, id);
                }
            }
        }
        // ---- hanged lamps along the two platform rows
        for (int t = A0 + 4; t <= A1 - 4; t += 6)
            for (int r = 0; r < 2; ++r)
            {
                const int b = r ? B1 - 2 : B0 + 2;
                if (inner.contains(cell(t, b)) && airAt(t, b, fy + 8)) put(t, b, fy + 8, IdLanternHung);
            }
        return true;
    };
    // The underground metro (code 61123100, railway=subway + tunnel=yes): the rails do NOT lie on the surface
    // but in a tunnel dug ~15 blocks deep. The floor is 2 blocks, the interior is 5 blocks high, the walls are a
    // 1-block lining; the section is a little rounded (the top corners of the interior are solid stone).
    // The cavity is dug "ground-down" like the church dungeons (restore the rock above the ceiling into the store
    // and lower the column top under the floor): the cave cast never applies to a lowered column, so the tunnel
    // cuts through any cave it touches, and the lining (store blocks) wins over the cast as well.
    // The walls and the floor are of a mixed stone masonry (a deterministic hash per cell), the lamps hang from
    // the ceiling every 6th chain cell; every 10th rail is electrified, but a powered rail cannot be curved, so
    // a hit that falls on a turn carries to the next straight cell and is laid there as a row of 2-5 powered rails.
    if (!subwayTunnels.isEmpty())
    {
        static const char* const kWallNames[16] = {
            "minecraft:stone_bricks", "minecraft:stone_bricks", "minecraft:stone_bricks", "minecraft:cracked_stone_bricks",
            "minecraft:mossy_stone_bricks", "minecraft:cobblestone", "minecraft:mossy_cobblestone", "minecraft:stone",
            "minecraft:stone", "minecraft:andesite", "minecraft:polished_andesite", "minecraft:polished_andesite",
            "minecraft:polished_granite", "minecraft:polished_diorite", "minecraft:granite", "minecraft:diorite" };
        static const char* const kFloorNames[8] = {
            "minecraft:stone_bricks", "minecraft:polished_andesite", "minecraft:polished_diorite", "minecraft:polished_granite",
            "minecraft:smooth_stone", "minecraft:stone", "minecraft:andesite", "minecraft:smooth_stone" };
        static const char* const kCeilNames[4] = {
            "minecraft:stone", "minecraft:cobblestone", "minecraft:andesite", "minecraft:mossy_cobblestone" };
        quint16 wallId[16], floorId[8], ceilId[4];
        for (int i = 0; i < 16; ++i) wallId[i] = quint16(IdPaletteStart + paletteIndexOf(kWallNames[i]));
        for (int i = 0; i < 8; ++i) floorId[i] = quint16(IdPaletteStart + paletteIndexOf(kFloorNames[i]));
        for (int i = 0; i < 4; ++i) ceilId[i] = quint16(IdPaletteStart + paletteIndexOf(kCeilNames[i]));
        int nTun = 0, nRails = 0, nElectro = 0, nLamps = 0;
        // The union of the sections of ALL chains is built first, the shell is applied once after it.
        // Chains that touch or cross (the two tracks of one line, a corner of one chain running along
        // another one) would otherwise paint each other's cells: the wall column of one chain landing
        // inside the interior of another became a solid "column" in the middle of the corridor. An
        // interior column always wins over a wall column; a shared interior follows its deepest floor,
        // the lining of a shared wall spans every floor level it passes through.
        struct TunCol { int floorY; int maxFloor; int minU; };    // deepest and highest floor claiming the column
        QHash<qint64, TunCol> tInt;
        QHash<qint64, int> tWallLo, tWallHi;                   // the wall rows [lo, hi] over all the passes
        QVector<QVector<QPoint>> tunChains;                    // chains with a floor (the rest has no ground)
        QVector<QVector<int>> tunFy;                           // the final floor levels, per that chain
        for (const QVector<QPoint>& chain : subwayTunnels)
        {
            const int n = chain.size();
            if (n < 2) continue;
            ++nTun;
            // The floor level of every chain cell: 15 blocks under the surface (5 deeper than the first
            // design - a user request; deeper under water bodies so that the ceiling stays under the bed),
            // slopes no steeper than 1 block per cell (the rails must connect).
            QVector<int> fy(n, 0);
            int last = std::numeric_limits<int>::min();
            for (int ci = 0; ci < n; ++ci)
            {
                const QPoint& c = chain[ci];
                const auto git = ground.find(pairKey(c.x() >> 4, c.y() >> 4));
                if (git != ground.end())
                {
                    const int gc = (c.y() & 15) * 16 + (c.x() & 15);
                    const int wd = git->wd[gc];
                    last = git->top[gc] - (wd > 0 ? std::max(15, wd + 11) : 15);
                }
                fy[ci] = last;
            }
            if (last == std::numeric_limits<int>::min()) continue;      // the tunnel lies over nowhere (no ground)
            // The cells hanging over "nowhere" (no ground chunk at the chain end) carry INT_MIN: clamping must
            // not do arithmetic on it (INT_MIN±1 overflows and poisons the whole chain - the wall pass then
            // spins on a -10..2e9 row loop). Such cells take the neighbor's level instead.
            const int MN = std::numeric_limits<int>::min();
            for (int ci = 1; ci < n; ++ci)
            {
                if (fy[ci] == MN) fy[ci] = fy[ci - 1];
                else if (fy[ci - 1] != MN) fy[ci] = std::max(fy[ci - 1] - 1, std::min(fy[ci - 1] + 1, fy[ci]));
            }
            for (int ci = n - 2; ci >= 0; --ci)
            {
                if (fy[ci] == MN) fy[ci] = fy[ci + 1];
                else if (fy[ci + 1] != MN) fy[ci] = std::max(fy[ci + 1] - 1, std::min(fy[ci + 1] + 1, fy[ci]));
            }
            // the corner cells stay FLAT (a sloped rail cannot turn): the step moves to the neighbouring
            // straight cells, the re-clamp may push a corner back up, so a few rounds (as on the surface)
            for (int round = 0; round < 4; ++round)
            {
                bool moved = false;
                for (int ci = 1; ci + 1 < n; ++ci)
                    if ((chain[ci] - chain[ci - 1]) != (chain[ci + 1] - chain[ci]) && fy[ci] != fy[ci - 1])
                    { fy[ci] = fy[ci - 1]; moved = true; }
                if (!moved) break;
                for (int ci = 1; ci < n; ++ci) fy[ci] = std::max(fy[ci - 1] - 1, std::min(fy[ci - 1] + 1, fy[ci]));
                for (int ci = n - 2; ci >= 0; --ci) fy[ci] = std::max(fy[ci + 1] - 1, std::min(fy[ci + 1] + 1, fy[ci]));
            }
            // ---- the sections of this chain into the union maps (the interior of 5 columns, the wall
            // columns on both sides, 7 across in all); nothing is written to the world at this stage
            for (int ci = 0; ci < n; ++ci)
            {
                const QPoint& c = chain[ci];
                QPoint dIn(0, 0), dOut(0, 0);                     // the path directions (the chain is orthogonal)
                if (ci + 1 < n && chain[ci + 1] != c) dOut = chain[ci + 1] - c;
                if (ci > 0 && c != chain[ci - 1]) dIn = c - chain[ci - 1];
                QPoint d = (dOut.x() || dOut.y()) ? dOut : dIn;
                if (!d.x() && !d.y()) d = QPoint(1, 0);           // a single-cell chain
                const int floorY = fy[ci];
                auto claim = [&](const QPoint& dir)
                {
                    const QPoint px(-dir.y(), dir.x());                 // the cross-section perpendicular to the path
                    for (int u = -3; u <= 3; ++u)
                    {
                        const int x = c.x() + px.x() * u, z = c.y() + px.y() * u;
                        const qint64 ck = pairKey(x, z);
                        railColsAll.insert(ck);                      // lone trees (3.51) do not stand over the tunnel
                        if (!ground.contains(pairKey(x >> 4, z >> 4))) continue;   // a column over nowhere is not built
                        const int au = std::abs(u);
                        if (au <= 2)
                        {   // the interior wins over any wall pass; the deepest floor sets the column, minU
                            // tells whether the top corner of the interior is rounded off here at all. When a
                            // higher chain also takes the column (a crossing of two levels), the interior of
                            // the lower one must stay open up to the ceiling of the upper one (maxFloor),
                            // otherwise the rock restored over the lower ceiling plugs the upper corridor.
                            tWallLo.remove(ck);
                            tWallHi.remove(ck);
                            auto it = tInt.find(ck);
                            if (it == tInt.end()) tInt.insert(ck, TunCol{ floorY, floorY, au });
                            else
                            {
                                TunCol& v = it.value();
                                if (floorY > v.maxFloor) { v.maxFloor = floorY; v.minU = au; }
                                else if (floorY == v.maxFloor && au < v.minU) v.minU = au;
                                if (floorY < v.floorY) v.floorY = floorY;
                            }
                        }
                        else if (!tInt.contains(ck))
                        {   // the wall column: the lining spans from the lowest floor to the highest ceiling
                            auto it = tWallLo.find(ck);
                            if (it == tWallLo.end()) { tWallLo.insert(ck, floorY); tWallHi.insert(ck, floorY + 6); }
                            else
                            {
                                if (floorY < it.value()) it.value() = floorY;
                                int& hi = tWallHi[ck];
                                if (floorY + 6 > hi) hi = floorY + 6;
                            }
                        }
                    }
                };
                claim(d);                                          // the exit axis (or the entry one at a chain end)
                // a turn cell claims BOTH cross-sections: the corridor comes in along one axis and the
                // section of the turn cell alone (perpendicular to the exit) would leave the three columns
                // behind the corner solid - a rock stub inside the corridor on every bend
                if ((dIn.x() || dIn.y()) && (dOut.x() || dOut.y()) && dIn != dOut) claim(dIn);
            }
            tunChains << chain;
            tunFy << fy;
        }
        for (auto it = tWallLo.constBegin(); it != tWallLo.constEnd(); ++it)
        {   // the wall columns: a mixed-masonry lining from the floor to the ceiling row - store blocks
            // win over the cave cast, so the tunnel keeps its shape even inside a cave it crossed
            const int x = int(keyFirst(it.key())), z = int(keySecond(it.key()));
            for (int y = it.value(); y <= tWallHi.value(it.key()); ++y)
                store.set(x, y, z, wallId[colHash(it.key() ^ (qint64(y) * 31 + 0x7EED1ull)) % 16u]);
        }
        for (auto it = tInt.constBegin(); it != tInt.constEnd(); ++it)
        {   // the interior columns: the rock above the ceiling is restored as store blocks (the surface
            // stays intact), the column top drops under the floor - above it both the ground and the
            // cave cast read air
            const int x = int(keyFirst(it.key())), z = int(keySecond(it.key()));
            const auto git = ground.find(pairKey(x >> 4, z >> 4));
            if (git == ground.end()) continue;
            const int gc = (z & 15) * 16 + (x & 15);
            GroundChunk& g = git.value();
            const int orig = g.top[gc];
            const int floorY = it.value().floorY;
            const int topF = it.value().maxFloor;                 // the highest floor sharing this column
            const quint32 hh = colHash(it.key() ^ 0x5AB51ull);
            for (int y = topF + 6; y <= orig; ++y)
            {
                const quint16 id = (y == orig) ? g.surf[gc] : (y >= orig - 2 ? quint16(IdDirt) : quint16(IdStone));
                if (store.get(x, y, z) == IdAir) store.set(x, y, z, id);   // building blocks are not overwritten
            }
            if (orig > floorY - 1) g.top[gc] = qint16(floorY - 1);
            store.set(x, floorY, z, floorId[hh % 8u]);                      // the floor, two layers
            if (store.get(x, floorY - 1, z) == IdAir)
                store.set(x, floorY - 1, z, floorId[(hh >> 8) % 8u]);
            if (topF - floorY >= 5)
            {   // two separate levels cross here: the lower interior keeps its height, a floor slab of
              // the upper tunnel divides them, the upper interior is dug above that slab
                for (int y = floorY + 1; y <= floorY + 5; ++y) store.erase(x, y, z);
                store.set(x, topF, z, floorId[(hh >> 4) % 8u]);
                for (int y = topF + 1; y <= topF + 5; ++y) store.erase(x, y, z);
            }
            else
            {   // the levels touch (a shallow crossing): one merged interior from the deepest floor
                for (int y = floorY + 1; y <= topF + 5; ++y) store.erase(x, y, z);
            }
            if (it.value().minU == 2)
            {   // the rounded top corners; and the sidewalk (a user request: "the 1st and the 5th
                // floor cells, the edge ones by the walls, stand 1 block over the rails") - the outer
                // columns of the 5-wide interior get one more floor row, the rails then run in the
                // 3-cell trench between the sidewalks
                store.set(x, topF + 5, z, ceilId[(hh >> 4) % 4u]);
                if (store.get(x, topF + 1, z) == IdAir)
                    store.set(x, topF + 1, z, floorId[(hh >> 12) % 8u]);
            }
        }
        for (int ti = 0; ti < tunChains.size(); ++ti)
        {   // ---- the rails and the lamps: after ALL the shells, so that a crossing section of a turn
            // (the same cell seen from two chains or two directions) cannot chip an already laid rail off
            const QVector<QPoint>& chain = tunChains[ti];
            const QVector<int>& fy = tunFy[ti];
            const int n = chain.size();
            for (int ci = 0; ci < n; ++ci)
            {
                const QPoint& c = chain[ci];
                const QPoint* pv = ci > 0 ? &chain[ci - 1] : nullptr;
                const QPoint* nx = ci + 1 < n ? &chain[ci + 1] : nullptr;
                int shape = railShapeFor(c, pv, nx);
                if (shape <= 1 && pv && nx)
                {   // a straight section: the slope toward the higher neighbor cell (as on the surface rails)
                    const int up = (fy[ci + 1] == fy[ci] + 1) ? ci + 1 : (fy[ci - 1] == fy[ci] + 1 ? ci - 1 : -1);
                    if (up >= 0)
                    {
                        const QPoint& u = chain[up];
                        shape = u.x() > c.x() ? 2 : u.x() < c.x() ? 3 : u.y() < c.y() ? 4 : 5;
                    }
                }
                const int ry = fy[ci] + 1;                               // the rails lie on the floor
                if (store.get(c.x(), ry, c.y()) == IdAir)
                {   // ordinary rails; the electrification pass comes after the whole line is laid
                    if (store.get(c.x(), ry - 1, c.y()) == IdAir)        // the column floor ended lower (someone
                        store.set(c.x(), ry - 1, c.y(),                  // else's corridor passes under it):
                                  floorId[colHash(pairKey(c.x(), c.y()) ^ 0x5AB51ull) % 8u]);   // prop the rails up
                    store.set(c.x(), ry, c.y(), quint16(int(IdRailShapeBase) + shape));
                    ++nRails;
                }
                if (ci % 6 == 3 && store.get(c.x(), fy[ci] + 5, c.y()) == IdAir)
                {   // the lamps hang from the ceiling between the rails (the rails lie a row lower)
                    store.set(c.x(), fy[ci] + 5, c.y(), IdLanternHung);
                    ++nLamps;
                }
            }
            QVector<int> railYs(n);
            for (int ci = 0; ci < n; ++ci)
                railYs[ci] = fy[ci] + 1;
            nElectro += electrifyRails(store, chain, railYs);            // every 20th, straight cells only
        }
        // Station halls under the station buildings the line passes (the straightener bent the line to them):
        // the hall floor is the tunnel floor. openCols - the tunnel interior columns: the boundary cells of the
        // hall touched by a corridor are not walled up. The dig erases the rails and the lamps inside the hall:
        // they are laid again and the electrification of the line is applied anew.
        QSet<qint64> openCols;
        for (auto it = tInt.constBegin(); it != tInt.constEnd(); ++it) openCols.insert(it.key());
        int nSt = 0;
        for (int si = 0; si < stations.size(); ++si)
        {
            const StationSite& st = stations[si];
            QSet<int> hit;
            QSet<qint64> track;
            int tx0 = std::numeric_limits<int>::max(), tx1 = std::numeric_limits<int>::min();
            int tz0 = tx0, tz1 = tx1, fy = tx1;
            for (int ti = 0; ti < tunChains.size(); ++ti)
                for (int ci = 0; ci < tunChains[ti].size(); ++ci)
                {
                    const QPoint& c = tunChains[ti][ci];
                    if (qAbs(c.x() - st.c.x()) > 30 || qAbs(c.y() - st.c.y()) > 30) continue;
                    hit.insert(ti);
                    track.insert(pairKey(c.x(), c.y()));
                    tx0 = std::min(tx0, c.x()); tx1 = std::max(tx1, c.x());
                    tz0 = std::min(tz0, c.y()); tz1 = std::max(tz1, c.y());
                    fy = std::max(fy, tunFy[ti][ci]);                      // the shallower track sets the floor level
                }
            if (hit.isEmpty()) continue;                                   // no line under the station - the lone dig below
            // the hall = the building outline together with the touched cells, stretched along the long axis
            // to the length of a real platform
            int hx0 = std::min(tx0, st.bb.left()), hx1 = std::max(tx1, st.bb.right());
            int hz0 = std::min(tz0, st.bb.top()), hz1 = std::max(tz1, st.bb.bottom());
            if (hx1 - hx0 >= hz1 - hz0)
            {
                if (hx1 - hx0 < 40) { const int d = (40 - (hx1 - hx0) + 1) / 2; hx0 -= d; hx1 += d; }
                else if (hx1 - hx0 > 82) { const int d = (hx1 - hx0 - 82) / 2; hx0 += d; hx1 -= d; }
                if (hz1 - hz0 < 16) { const int d = (16 - (hz1 - hz0) + 1) / 2; hz0 -= d; hz1 += d; }
                else if (hz1 - hz0 > 20) { const int d = (hz1 - hz0 - 20) / 2; hz0 += d; hz1 -= d; }
            }
            else
            {
                if (hz1 - hz0 < 40) { const int d = (40 - (hz1 - hz0) + 1) / 2; hz0 -= d; hz1 += d; }
                else if (hz1 - hz0 > 82) { const int d = (hz1 - hz0 - 82) / 2; hz0 += d; hz1 -= d; }
                if (hx1 - hx0 < 16) { const int d = (16 - (hx1 - hx0) + 1) / 2; hx0 -= d; hx1 += d; }
                else if (hx1 - hx0 > 20) { const int d = (hx1 - hx0 - 20) / 2; hx0 += d; hx1 -= d; }
            }
            const QRect hrgn(hx0, hz0, hx1 - hx0 + 1, hz1 - hz0 + 1);
            if (!digStationHall(st.c, fy, hrgn, st.bb, track, openCols, true, st.canopy != 0)) continue;
            stationBuilt[si] = 1;
            ++nSt;
            for (const int ti : hit)
            {   // rails and lamps inside the hall are laid again (the dig erased them), then the line electrifies
                const QVector<QPoint>& chain = tunChains[ti];
                const QVector<int>& fyv = tunFy[ti];
                const int n = chain.size();
                for (int ci = 0; ci < n; ++ci)
                {
                    const QPoint& c = chain[ci];
                    if (!hrgn.contains(c) || fyv[ci] != fy) continue;
                    const QPoint* pv = ci > 0 ? &chain[ci - 1] : nullptr;
                    const QPoint* nx = ci + 1 < n ? &chain[ci + 1] : nullptr;
                    int shape = railShapeFor(c, pv, nx);
                    if (shape <= 1 && pv && nx)
                    {   // the same slope rule as in the tunnel pass
                        const int up = (fyv[ci + 1] == fyv[ci] + 1) ? ci + 1 : (fyv[ci - 1] == fyv[ci] + 1 ? ci - 1 : -1);
                        if (up >= 0)
                        {
                            const QPoint& u = chain[up];
                            shape = u.x() > c.x() ? 2 : u.x() < c.x() ? 3 : u.y() < c.y() ? 4 : u.y() > c.y() ? 5 : 0;
                        }
                    }
                    const int ry = fyv[ci] + 1;
                    if (store.get(c.x(), ry, c.y()) == IdAir)
                        store.set(c.x(), ry, c.y(), quint16(int(IdRailShapeBase) + shape));
                    if (ci % 6 == 3 && store.get(c.x(), fyv[ci] + 8, c.y()) == IdAir)
                        store.set(c.x(), fyv[ci] + 8, c.y(), IdLanternHung);    // the hall ceiling is 3 rows over the tunnel's
                }
                QVector<int> railYs(n);
                for (int ci = 0; ci < n; ++ci) railYs[ci] = fyv[ci] + 1;
                electrifyRails(store, chain, railYs);
            }
        }
        if (nSt > 0) tmcStepLog(("subway: station halls at the lines: " + std::to_string(nSt)).c_str());
        if (nTun > 0)
            res.lairInfo += QObject::tr("; subway: %1 tunnels, %2 rails (%3 electrified), %4 lamps")
                               .arg(nTun).arg(nRails).arg(nElectro).arg(nLamps);
        char buf[160];
        snprintf(buf, sizeof buf, "subway tunnels: %d, rails %d/%d electro, lamps %d", nTun, nElectro, nRails, nLamps);
        tmcStepLog(buf);
        // ---- secret loot rooms: hash-picked tunnel cells far from any station (>=96 blocks from a station
        // centre) open a narrow 3-high passage into a small rock chamber with a double chest of expensive
        // loot guarded by mobs (zombies, skeletons, spiders, witches). The room is dug "ground-down" exactly
        // like the tunnel shell: the dug columns drop their ground top, the rock above the ceiling is
        // restored into the store (store blocks win over the ground and the cave cast).
        int nSecret = 0, nSecretMobs = 0;
        QSet<qint64> secretCols;                                     // claimed columns: no two rooms share a wall
        for (int ti = 0; ti < tunChains.size() && nSecret < 32; ++ti)
        {
            const QVector<QPoint>& chain = tunChains[ti];
            const QVector<int>& fyv = tunFy[ti];
            const int n = chain.size();
            for (int ci = 4; ci + 4 < n && nSecret < 32; ++ci)
            {
                const QPoint& c = chain[ci];
                const quint32 hs = colHash(pairKey(c.x(), c.y()) ^ 0x5EC37ull);
                if (hs % 1000u >= 5u) continue;                      // roughly every 200th cell of a chain
                const int fy = fyv[ci];
                if (fy <= 5) continue;                               // the floor would fall into the void
                if (store.get(c.x(), fy + 2, c.y()) != IdAir) continue;   // the tunnel interior is not here
                                                                            // (row fy+1 is where the rails lie)
                bool far = true;
                for (const StationSite& st : stations)
                    if (qAbs(c.x() - st.c.x()) <= 96 && qAbs(c.y() - st.c.y()) <= 96) { far = false; break; }
                if (!far) continue;                                  // no secrets under or near a station
                // the local direction of the line. The corridor is 5 columns wide (interior at +-2 of the
                // chain axis, the lining at +-3) and a line may have a second tube alongside, so the hash
                // side is tried first and the opposite side next; whichever footprint is free wins
                const QPoint& pv = chain[ci - 2];
                const QPoint& nx = chain[ci + 2];
                const bool alongX = qAbs(nx.x() - pv.x()) >= qAbs(nx.y() - pv.y());
                QPoint dirv[2];
                dirv[0] = alongX ? QPoint(0, (hs & 4u) ? 1 : -1) : QPoint((hs & 4u) ? 1 : -1, 0);
                dirv[1] = QPoint(-dirv[0].x(), -dirv[0].y());
                bool ok = false;
                QPoint chosen(0, 0);
                QVector<QPoint> digCols;
                for (int s2 = 0; s2 < 2 && !ok; ++s2)
                {
                    const QPoint& s = dirv[s2];
                    // a door bored through the lining column at +-3, a 3-cell passage at 4..6, the 7x7
                    // chamber centered at 10 (its near wall at 7 adjoins the passage)
                    const QPoint e4(c.x() + s.x() * 3, c.y() + s.y() * 3);
                    const qint64 ckE = pairKey(e4.x(), e4.y());
                    if (!tWallLo.contains(ckE) || tInt.contains(ckE) || secretCols.contains(ckE)) continue;
                    if (ground.find(pairKey(e4.x() >> 4, e4.y() >> 4)) == ground.end()) continue;
                    QVector<QPoint> dc;
                    for (int k = 5; k <= 7; ++k) dc << QPoint(c.x() + s.x() * k, c.y() + s.y() * k);
                    const QRect room(c.x() + s.x() * 10 - 3, c.y() + s.y() * 10 - 3, 7, 7);
                    for (int x = room.left(); x <= room.right(); ++x)
                        for (int z = room.top(); z <= room.bottom(); ++z) dc << QPoint(x, z);
                    bool bad = false;
                    for (const QPoint& q : dc)
                    {
                        const qint64 ck = pairKey(q.x(), q.y());
                        if (tInt.contains(ck) || secretCols.contains(ck) || railColsAll.contains(ck)) { bad = true; break; }
                        if (ground.find(pairKey(q.x() >> 4, q.y() >> 4)) == ground.end()) { bad = true; break; }
                    }
                    if (bad) continue;
                    dc.prepend(e4);                                   // the lining column checked on its own
                    ok = true; chosen = s; digCols = dc;
                }
                if (!ok) continue;
                const int rcx = c.x() + chosen.x() * 10, rcz = c.y() + chosen.y() * 10;
                const QRect room(rcx - 3, rcz - 3, 7, 7);
                for (const QPoint& q : digCols)
                {
                    const qint64 ck = pairKey(q.x(), q.y());
                    secretCols.insert(ck);
                    const auto git = ground.find(pairKey(q.x() >> 4, q.y() >> 4));
                    const int gc = (q.y() & 15) * 16 + (q.x() & 15);
                    GroundChunk& g = git.value();
                    const int orig = g.top[gc];
                    const quint32 hh = colHash(ck ^ 0x5EC77ull);
                    for (int y = (room.contains(q) ? fy + 6 : fy + 4); y <= orig; ++y)
                    {   // the rock over the ceiling goes back into the store (the surface stays intact)
                        const quint16 id = (y == orig) ? g.surf[gc] : (y >= orig - 2 ? quint16(IdDirt) : quint16(IdStone));
                        if (store.get(q.x(), y, q.y()) == IdAir) store.set(q.x(), y, q.y(), id);
                    }
                    if (orig > fy - 1) g.top[gc] = qint16(fy - 1);
                    store.set(q.x(), fy, q.y(), floorId[hh % 8u]);                   // the floor, two layers
                    if (store.get(q.x(), fy - 1, q.y()) == IdAir)
                        store.set(q.x(), fy - 1, q.y(), floorId[(hh >> 8) % 8u]);
                    const int topIn = room.contains(q) ? fy + 5 : fy + 3;           // the chamber is taller than the passage
                    for (int y = fy + 1; y <= topIn; ++y) store.erase(q.x(), y, q.y());
                }
                // the lamps hang in the four corners of the chamber
                const QPoint kLamp[4] = { QPoint(room.left() + 1, room.top() + 1), QPoint(room.right() - 1, room.top() + 1),
                                          QPoint(room.left() + 1, room.bottom() - 1), QPoint(room.right() - 1, room.bottom() - 1) };
                for (const QPoint& lp : kLamp)
                    if (store.get(lp.x(), fy + 5, lp.y()) == IdAir) store.set(lp.x(), fy + 5, lp.y(), IdLanternHung);
                // the double chest in the middle: the expensive loot tier only
                const quint32 hc = colHash(pairKey(rcx, rcz) ^ 0x5E177ull);
                const int tbl = pickChestLoot(hc >> 4, 0, 0);
                store.set(rcx - 1, fy + 1, rcz, IdChestL);
                store.set(rcx, fy + 1, rcz, IdChestR);
                chests << ChestSite{ rcx - 1, fy + 1, rcz, tbl } << ChestSite{ rcx, fy + 1, rcz, tbl };
                // the guardians: 2-3 of zombie / skeleton / spider / witch on free cells of the chamber
                QSet<qint64> mobAt;
                const int nMob = 2 + int((hc >> 12) % 2u);
                for (int m = 0; m < nMob; ++m)
                {
                    const quint32 hm = colHash(pairKey(rcx + m * 7, rcz - m * 5) ^ 0x5EB17ull);
                    const int mx = room.left() + 1 + int((hm >> 16) % 5u), mz = room.top() + 1 + int((hm >> 20) % 5u);
                    const qint64 mk = pairKey(mx, mz);
                    if (mobAt.contains(mk) || store.get(mx, fy + 1, mz) != IdAir) continue;
                    mobAt.insert(mk);
                    NpcSite ms;
                    ms.kind = 4; ms.variant = int((hm >> 8) % 4u);                   // 0 zombie, 1 skeleton, 2 spider, 3 witch
                    ms.x = mx; ms.y = fy + 1; ms.z = mz; ms.yaw = int((hm >> 4) % 4u) * 90;
                    npcs.append(ms);
                    ++nSecretMobs;
                }
                ++nSecret;
            }
        }
        if (nSecret > 0)
        {
            snprintf(buf, sizeof buf, "secret loot rooms: %d, guardians %d", nSecret, nSecretMobs);
            tmcStepLog(buf);
            res.lairInfo += QObject::tr("; secret rooms in the metro: %1").arg(nSecret);
        }
    }
    // Station halls under the station buildings no line passes (digStationHall above): dug on their own, the
    // floor 12 blocks under the surface (the tunnel floor sits 10 under it), the platform without rails -
    // a concert hall/anticipatory station. The decorated interior is the same as at the line stations.
    nStations = 0;
    for (int si = 0; si < stations.size(); ++si)
    {
        if (stationBuilt[si]) { ++nStations; continue; }
        const StationSite& st = stations[si];
        const auto git = ground.find(pairKey(st.c.x() >> 4, st.c.y() >> 4));
        if (git == ground.end()) continue;
        const int gc = (st.c.y() & 15) * 16 + (st.c.x() & 15);
        if (git->wd[gc] > 0) continue;                                    // under water - no station
        int hx0 = st.bb.left() - 4, hx1 = st.bb.right() + 4;
        int hz0 = st.bb.top() - 4, hz1 = st.bb.bottom() + 4;
        if (hx1 - hx0 >= hz1 - hz0)
        {
            if (hx1 - hx0 < 30) { const int d = (30 - (hx1 - hx0) + 1) / 2; hx0 -= d; hx1 += d; }
            else if (hx1 - hx0 > 56) { const int d = (hx1 - hx0 - 56) / 2; hx0 += d; hx1 -= d; }
            if (hz1 - hz0 < 15) { const int d = (15 - (hz1 - hz0) + 1) / 2; hz0 -= d; hz1 += d; }
            else if (hz1 - hz0 > 20) { const int d = (hz1 - hz0 - 20) / 2; hz0 += d; hz1 -= d; }
        }
        else
        {
            if (hz1 - hz0 < 30) { const int d = (30 - (hz1 - hz0) + 1) / 2; hz0 -= d; hz1 += d; }
            else if (hz1 - hz0 > 56) { const int d = (hz1 - hz0 - 56) / 2; hz0 += d; hz1 -= d; }
            if (hx1 - hx0 < 15) { const int d = (15 - (hx1 - hx0) + 1) / 2; hx0 -= d; hx1 += d; }
            else if (hx1 - hx0 > 20) { const int d = (hx1 - hx0 - 20) / 2; hx0 += d; hx1 -= d; }
        }
        if (digStationHall(st.c, git->top[gc] - 17, QRect(hx0, hz0, hx1 - hx0 + 1, hz1 - hz0 + 1),
                           st.bb, QSet<qint64>(), QSet<qint64>(), false, st.canopy != 0))
            ++nStations;
    }
    if (nStations > 0)
    {
        res.lairInfo += QObject::tr("; station halls: %1").arg(nStations);
        tmcStepLog(("station halls: " + std::to_string(nStations)).c_str());
    }
    lap("before 3.5");
    beginStep(4, vegCells);
    // 3.5 Vegetation — after the ground (the surface height is needed). Trees — roughly every 25th
    // column of the "spot" (a deterministic hash — a forest without clearings and without rows), the grass is denser.
    // Nothing is planted on the occupied columns (structures).
    auto surfaceTop = [&](qint64 ck) -> int
    {
        int top = groundTopCol(keyFirst(ck), keySecond(ck));
        const auto oit = colMin.constFind(ck);
        if (oit != colMin.constEnd() && oit.value() - 1 <= top + 3)
            top = std::max(top, oit.value() - 1);
        return std::min(top, 255);
    };
    // Spruce: a spruce_log trunk (2×2 for big ones), bare at the bottom, then needle tiers from top to bottom with a radius from 0 (the top) to R; even tiers are narrower than
    // odd ones — like a real spruce. The needles cover the trunk up to the very top. The crown must not go beyond the chunks with ground.
    sprFill = QSet<ColKey>(fillChunks.constBegin(), fillChunks.constEnd());
    // Tree species by the climate zone: "oak" and "birch" in the planting code are replaced with the trunk+leaves pairs of the zone, the spruce cone — with a big broadleaf tree
    // where there are no conifers; density — the share of plantings that remain (in the desert and tundra trees are rare)
    birchLogRaw = quint16(IdPaletteStart + paletteIndexOf("minecraft:birch_log"));
    ZoneTrees zt{ IdOakLog, IdOakLeaves, birchLogRaw, IdBirchLeaves, 100, true };
    {
        const quint16 aca = quint16(IdPaletteStart + paletteIndexOf("minecraft:acacia_log"));
        const quint16 jng = quint16(IdPaletteStart + paletteIndexOf("minecraft:jungle_log"));
        switch (g_zone)
        {
        case Climate::Boreal:        zt.oakLog = birchLogRaw; zt.oakLeaf = IdBirchLeaves; break;
        case Climate::Tundra:        zt.oakLog = birchLogRaw; zt.oakLeaf = IdBirchLeaves; zt.density = 30; break;
        case Climate::Steppe:        zt.density = 35; zt.cone = false; break;
        case Climate::Mediterranean: zt.birchLog = IdOakLog; zt.birchLeaf = IdOakLeaves; zt.density = 60; zt.cone = false; break;
        case Climate::Desert:        zt = ZoneTrees{ aca, IdAcaciaLeaves, aca, IdAcaciaLeaves, 8, false }; break;
        case Climate::Savanna:       zt = ZoneTrees{ aca, IdAcaciaLeaves, aca, IdAcaciaLeaves, 35, false }; break;
        case Climate::Tropical:      zt = ZoneTrees{ jng, IdJungleLeaves, jng, IdJungleLeaves, 100, false, true }; break;
        case Climate::Monsoon:       zt.birchLog = IdOakLog; zt.birchLeaf = IdOakLeaves; zt.density = 60; zt.cone = false; zt.palmShare = 80; break;   // oaks and palms, no conifers
        case Climate::SemiArid:      zt = ZoneTrees{ IdOakLog, IdOakLeaves, IdOakLog, IdOakLeaves, 30, false, false, 50 }; break;   // the Gulf coast: sparse oaks and palms on sand, cacti on the open lands
        case Climate::Rainforest:    zt.bigOakShare = 35; break;   // the Pacific Northwest: every third tree in broadleaf forests is a big oak with branches
        case Climate::DarkForest:    { const quint16 dko = quint16(IdPaletteStart + paletteIndexOf("minecraft:dark_oak_log"));
                                       zt = ZoneTrees{ dko, IdDarkOakLeaves, dko, IdDarkOakLeaves, 100, false }; } break;   // Hokkaido and Colchis: every tree is a dark oak, the spruce sites grow big broadleaf ones instead of cones
        default: break;
        }
    }
    auto zoneSkip = [&](int bx, int bz) { return zt.density < 100 && colHash(pairKey(bx, bz) ^ 0xD3E5ull) % 100u >= quint32(zt.density); };
    auto zoneRemap = [&](quint16& log, quint16& leaf)
    {
        if (log == quint16(IdOakLog)) { log = zt.oakLog; leaf = zt.oakLeaf; }
        else if (log == birchLogRaw) { log = zt.birchLog; leaf = zt.birchLeaf; }
    };
    auto plantSpruceCone = [&](int bx, int bz, int y0, int h, int R) -> bool
    {
        const bool wide = R >= 5;                                              // big spruces — a 2×2 trunk
        const int x1 = bx + (wide ? 1 : 0), z1 = bz + (wide ? 1 : 0);
        for (int cxk = (bx - R) >> 4; cxk <= ((x1 + R) >> 4); ++cxk)
            for (int czk = (bz - R) >> 4; czk <= ((z1 + R) >> 4); ++czk)
                if (!sprFill.contains(pairKey(cxk, czk))) return false;
        const quint16 sprLog = quint16(IdPaletteStart + paletteIndexOf("minecraft:spruce_log"));
        const int bare = std::max(2, h / 5);                                   // the bare trunk at the bottom
        for (int dy = 0; dy < h; ++dy)
            for (int x = bx; x <= x1; ++x)
                for (int z = bz; z <= z1; ++z)
                    if (store.get(x, y0 + dy, z) == IdAir) store.set(x, y0 + dy, z, sprLog);
        const int n = h - bare + 1;                                            // needle tiers (the top one — above the trunk)
        const int topY = y0 + h;                                               // the top: a block above the trunk
        for (int k = 0; k < n; ++k)
        {
            int r = (k == 0) ? 0 : std::max(1, (R * k + n - 2) / std::max(1, n - 1));
            if (k >= 2 && (k & 1)) r = std::max(1, r - 1);                     // odd tiers are narrower — "skirts"
            if (k == 1) r = 1;
            const int y = topY - k;
            const double lim = (r + (wide ? 0.75 : 0.25)) * (r + (wide ? 0.75 : 0.25));
            for (int dx = -r; dx <= r + (wide ? 1 : 0); ++dx)
                for (int dz = -r; dz <= r + (wide ? 1 : 0); ++dz)
                {
                    const double fx = wide ? dx - 0.5 : double(dx), fz = wide ? dz - 0.5 : double(dz);      // the crown center — the trunk center
                    if (fx * fx + fz * fz > lim) continue;
                    const int x = bx + dx, z = bz + dz;
                    if (store.get(x, y, z) == IdAir) store.set(x, y, z, IdSpruceLeaves);
                }
        }
        return true;
    };
    // Vines (like the game's jungle ones): a vine can stand in the crown itself (replacing a leaf),
    // hang a thread of 1..5 blocks under a crown column, poke above the crown and grow along the trunk.
    // The block is saved WITHOUT properties (as in a real world: the game connects the faces itself)
    auto vineColumn = [&](int x, int z, int yBottom, int win, quint16 leafId = quint16(IdJungleLeaves))
    {   // one crown column: the crown fills [yBottom, yBottom+win], decorate the column by its hash
        const quint32 hr = colHash(pairKey(x, z) ^ 0x2ADEull);
        int yTopC = -9999;
        for (int y = yBottom + win; y >= yBottom; --y)
            if (store.get(x, y, z) == leafId) { yTopC = y; break; }
        if (yTopC < yBottom) return;                                   // the column has no crown (a trunk, an air gap)
        const int roll = int(hr % 100u);
        if (roll < 62) return;                                         // ~3 of 5 columns stay plain leaves
        if (roll >= 84 && yTopC > yBottom && store.get(x, yTopC + 1, z) == IdAir)
            store.set(x, yTopC + 1, z, IdVine);                        // a vine pokes above the crown
        if (roll >= 76 && store.get(x, yBottom, z) == leafId)
            store.set(x, yBottom, z, IdVine);                          // the vine stands IN the crown (a replaced leaf)
        const int len = roll < 70 ? 1 + int((hr >> 8) % 2u) : 1 + int((hr >> 8) % 5u);
        for (int k = 0; k < len; ++k)
        {
            const int y = yBottom - 1 - k;
            if (store.get(x, y, z) != IdAir || store.get(x, y + 1, z) == IdAir) break;
            store.set(x, y, z, IdVine);                                // the hanging thread under the column
        }
    };
    auto jungleCrownVines = [&](int bx, int bz, int yBottom, int R, quint16 leafId = quint16(IdJungleLeaves))
    {   // the crown of a 1×1 trunk (the far corners cut): every column of the lower layer is a candidate
        for (int dx = -R; dx <= R; ++dx)
            for (int dz = -R; dz <= R; ++dz)
            {
                if (std::abs(dx) == R && std::abs(dz) == R) continue;
                if (!dx && !dz) continue;                              // the trunk column (vines near it — below)
                vineColumn(bx + dx, bz + dz, yBottom, R + 2, leafId);
            }
        static const int kT[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (const auto& t : kT)                                       // a vine against the trunk, inside the crown
            if (colHash(pairKey(bx + t[0], bz + t[1]) ^ 0x51DEull) % 100u < 18u && store.get(bx + t[0], yBottom, bz + t[1]) == leafId)
                store.set(bx + t[0], yBottom, bz + t[1], IdVine);
    };
    // Swamp oak (ground under a source with the "swamp" effect, e.g. mrspol wetland): a 1×1 oak trunk of 5..8, a wide flat crown of oak leaves with VINES
    // hanging from it, a couple of gnarled branches. The species is independent of the climate zone — a swamp is a swamp.
    auto plantSwampOak = [&](int bx, int bz, int y0, int h, quint32 hh) -> bool
    {
        for (int sx2 = -3; sx2 <= 3; sx2 += 6)
            for (int sz2 = -3; sz2 <= 3; sz2 += 6)
                if (!sprFill.contains(pairKey((bx + sx2) >> 4, (bz + sz2) >> 4))) return false;
        for (int dy = 0; dy < h; ++dy)
            if (store.get(bx, y0 + dy, bz) == IdAir) store.set(bx, y0 + dy, bz, IdOakLog);
        const int topTree = y0 + h - 1;
        static const int kR[4] = { 1, 2, 3, 2 };                       // crown layers topTree+1 .. topTree-2: a flat bottom
        for (int li = 0; li < 4; ++li)
        {
            const int dy = 1 - li, r = kR[li];
            for (int dx = -r; dx <= r; ++dx)
                for (int dz = -r; dz <= r; ++dz)
                {
                    if (dy == 1 && std::abs(dx) + std::abs(dz) > 1) continue;
                    if (dy == -1 && std::abs(dx) == 3 && std::abs(dz) == 3) continue;   // no far corners at the bottom
                    if (store.get(bx + dx, topTree + dy, bz + dz) == IdAir)
                        store.set(bx + dx, topTree + dy, bz + dz, IdOakLeaves);
                }
        }
        if (hh % 3u == 0u)
        {   // a gnarled branch — a stump sticking out sideways
            static const int kA[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            const auto& a = kA[(hh >> 8) % 4u];
            const int by = y0 + h - 3 + int((hh >> 10) % 2u);
            if (store.get(bx + a[0], by, bz + a[1]) == IdAir) store.set(bx + a[0], by, bz + a[1], IdOakLog);
        }
        jungleCrownVines(bx, bz, topTree - 2, 2, IdOakLeaves);         // the vines hang from the lowest layer of the crown
        return true;
    };
    // A broadleaf tree (birch etc.): a 1×1 trunk of the given block, a "diamond" crown of radius 2 of the given leaves; the crown does not go beyond the chunks with ground
    auto plantBroad = [&](int bx, int bz, int y0, int h, quint16 logId, quint16 leafId) -> bool
    {
        if (zoneSkip(bx, bz)) return true;
        zoneRemap(logId, leafId);
        for (int sx2 = -2; sx2 <= 2; sx2 += 4)
            for (int sz2 = -2; sz2 <= 2; sz2 += 4)
                if (!sprFill.contains(pairKey((bx + sx2) >> 4, (bz + sz2) >> 4))) return false;
        for (int dy = 0; dy < h; ++dy)
            if (store.get(bx, y0 + dy, bz) == IdAir) store.set(bx, y0 + dy, bz, logId);
        const int topTree = y0 + h - 1;
        for (int dy = -2; dy <= 1; ++dy)
        {
            const int r = (dy == 1) ? 1 : 2;
            for (int dx = -r; dx <= r; ++dx)
                for (int dz = -r; dz <= r; ++dz)
                {
                    if (dy == 1 && std::abs(dx) + std::abs(dz) > 1) continue;
                    if (dy != 1 && std::abs(dx) == 2 && std::abs(dz) == 2) continue;
                    if (store.get(bx + dx, topTree + dy, bz + dz) == IdAir) store.set(bx + dx, topTree + dy, bz + dz, leafId);
                }
        }
        if (zt.jungle) jungleCrownVines(bx, bz, topTree - 2, 2);       // a jungle tree — a small one with vines (the game's look)
        return true;
    };
    // A dark oak (a cemetery, a request): a trunk and a dense wide flat-topped crown, as the game's dark forests.
    auto plantDarkOak = [&](int bx, int bz, int y0, int h, quint32 hh) -> bool
    {
        for (int sx2 = -3; sx2 <= 3; sx2 += 6)
            for (int sz2 = -3; sz2 <= 3; sz2 += 6)
                if (!sprFill.contains(pairKey((bx + sx2) >> 4, (bz + sz2) >> 4))) return false;
        const quint16 dLog = quint16(IdPaletteStart + paletteIndexOf("minecraft:dark_oak_log"));
        for (int dy = 0; dy < h; ++dy)
            if (store.get(bx, y0 + dy, bz) == IdAir) store.set(bx, y0 + dy, bz, dLog);
        const int topTree = y0 + h - 1;
        static const int kR[4] = { 2, 3, 3, 2 };                     // the crown layers topTree+1 .. topTree-2
        for (int li = 0; li < 4; ++li)
        {
            const int dy = 1 - li, r = kR[li];
            for (int dx = -r; dx <= r; ++dx)
                for (int dz = -r; dz <= r; ++dz)
                {
                    if (dy == 1 && std::abs(dx) + std::abs(dz) > 2) continue;            // a flat broad top
                    if (dy == -2 && std::abs(dx) == r && std::abs(dz) == r) continue;    // no far corners at the bottom
                    if (store.get(bx + dx, topTree + dy, bz + dz) == IdAir)
                        store.set(bx + dx, topTree + dy, bz + dz, IdDarkOakLeaves);
                }
        }
        return true;
    };
    // A tree of arbitrary size: a trunk of height h, a spherical crown of radius R (1..3) with cut corners — for intra-block greenery of different sizes
    auto plantSized = [&](int bx, int bz, int y0, int h, int R, quint16 logId, quint16 leafId) -> bool
    {
        if (zoneSkip(bx, bz)) return true;
        zoneRemap(logId, leafId);
        for (int sx2 = -R; sx2 <= R; sx2 += 2 * R)
            for (int sz2 = -R; sz2 <= R; sz2 += 2 * R)
                if (!sprFill.contains(pairKey((bx + sx2) >> 4, (bz + sz2) >> 4))) return false;
        for (int dy = 0; dy < h; ++dy)
            if (store.get(bx, y0 + dy, bz) == IdAir) store.set(bx, y0 + dy, bz, logId);
        const int topTree = y0 + h - 1;
        for (int dy = -R; dy <= 1; ++dy)
        {
            const int r = (dy == 1) ? 1 : (dy == 0 ? std::max(1, R - 1) : R);
            for (int dx = -r; dx <= r; ++dx)
                for (int dz = -r; dz <= r; ++dz)
                {
                    if (dy == 1 && std::abs(dx) + std::abs(dz) > 1) continue;
                    if (dy != 1 && r >= 2 && std::abs(dx) == r && std::abs(dz) == r) continue;
                    if (store.get(bx + dx, topTree + dy, bz + dz) == IdAir) store.set(bx + dx, topTree + dy, bz + dz, leafId);
                }
        }
        if (zt.jungle && R >= 2) jungleCrownVines(bx, bz, topTree - R, R);
        return true;
    };
    // Spruce: in zones with conifers — a cone, in the others (steppe, desert, tropics…) — a big broadleaf tree of the zone instead of it
    auto plantSpruce = [&](int bx, int bz, int y0, int h, int R) -> bool
    {
        if (zt.cone) return zoneSkip(bx, bz) ? true : plantSpruceCone(bx, bz, y0, h, R);
        return plantSized(bx, bz, y0, std::max(4, std::min(h / 2 + 2, 9)), R >= 5 ? 3 : 2, zt.oakLog, zt.oakLeaf);   // the zone's own broadleaf (a dark forest grows dark oaks here)
    };
    // A big jungle tree (like the game's one): a 2×2 trunk 10..16, a wide crown over the trunk top
    // and 2..3 side branches with leaf caps at 60% of the height; vines hang under the crown rims
    auto plantJungleBig = [&](int bx, int bz, int y0, quint32 hh) -> bool
    {
        const int h = 10 + int(hh % 17u);                               // the trunk 10..26 — the tree up to ~32 blocks tall (as in the game)
        if (y0 + h + 6 > 255) return false;
        for (int cxk = (bx - 5) >> 4; cxk <= ((bx + 6) >> 4); ++cxk)   // the crown and the branches entirely inside the fill
            for (int czk = (bz - 5) >> 4; czk <= ((bz + 6) >> 4); ++czk)
                if (!sprFill.contains(pairKey(cxk, czk))) return false;
        const quint16 jng = zt.oakLog, leaf = zt.oakLeaf;
        for (int dy = 0; dy < h; ++dy)                                 // the trunk is written over decorative grass too
            for (int dx = 0; dx <= 1; ++dx)
                for (int dz = 0; dz <= 1; ++dz)
                    store.set(bx + dx, y0 + dy, bz + dz, jng);
        const int topY = y0 + h;                                       // the crown around/over the trunk top (the trunk ends at topY - 1)
        static const int kR[3] = { 5, 4, 3 };                          // a flat crown: 12 wide below, every layer 2 narrower (as in the game)
        for (int li = 0; li < 3; ++li)
        {
            const int r = kR[li], lim = 4 * r - 4, y = topY - 1 + li;
            for (int dx = -r; dx <= r + 1; ++dx)
                for (int dz = -r; dz <= r + 1; ++dz)
                {
                    if (std::abs(2 * dx - 1) + std::abs(2 * dz - 1) > lim) continue;   // rounding the corners (from the 2×2 center)
                    if (store.get(bx + dx, y, bz + dz) == IdAir)
                        store.set(bx + dx, y, bz + dz, leaf);
                }
        }
        for (int dx = -5; dx <= 6; ++dx)                               // vines along the whole skirt (as in the game)
            for (int dz = -5; dz <= 6; ++dz)
            {
                if (std::abs(2 * dx - 1) + std::abs(2 * dz - 1) > 16) continue;
                vineColumn(bx + dx, bz + dz, topY - 1, 3);
            }
        const int nBr = 2 + int((hh >> 8) % 2u);                       // 2..3 branches at ~60% of the height
        static const int kDir[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };   // N E S W
        for (int b = 0; b < nBr; ++b)
        {
            const int d = (int(hh >> 12) + b * 2 + (b == 2 ? 1 : 0)) % 4;
            const int ax = kDir[d][0], az = kDir[d][1];
            const int pb = int((hh >> (16 + b * 3)) % 2u);             // which of the two trunk columns the branch grows from
            const int yB = y0 + int(h * 3 / 5) + int((hh >> (22 + b * 2)) % 3u) - 1;
            int sxp, szp;                                              // the first branch block next to the trunk
            if (ax) { sxp = ax > 0 ? bx + 2 : bx - 1; szp = bz + pb; }
            else    { szp = az > 0 ? bz + 2 : bz - 1; sxp = bx + pb; }
            for (int i = 0; i < 2; ++i)                                // the branch's own trunk (2 blocks)
            {
                const int x = sxp + ax * i, z = szp + az * i;
                if (store.get(x, yB, z) == IdAir) store.set(x, yB, z, jng);
            }
            const int tx = sxp + ax, tz = szp + az;                    // the tip: a 3×3 cap and its dome
            for (int ex = -1; ex <= 1; ++ex)
                for (int ez = -1; ez <= 1; ++ez)
                {
                    if (store.get(tx + ex, yB, tz + ez) == IdAir)
                        store.set(tx + ex, yB, tz + ez, leaf);
                    if ((ex == 1 || ex == -1) && (ez == 1 || ez == -1)) continue;
                    if (store.get(tx + ex, yB + 1, tz + ez) == IdAir)
                        store.set(tx + ex, yB + 1, tz + ez, leaf);
                }
            for (int ex = -1; ex <= 1; ++ex)                            // vines on the cap columns
                for (int ez = -1; ez <= 1; ++ez)
                    if (ex || ez) vineColumn(tx + ex, tz + ez, yB, 1);
        }
        return true;
    };
    // A big oak (temperate rainforest): a 2×2 trunk 8..12 tall, a wide round crown (the ordinary oak's
    // "diamond" scaled up) and 2..3 bare branches under its edge — an ordinary oak is shorter and has none
    auto plantBigOak = [&](int bx, int bz, int y0, quint32 hh) -> bool
    {
        const int h = 8 + int(hh % 5u);                                        // trunk 8..12 (an ordinary oak is 5..7)
        if (y0 + h + 4 > 255) return false;
        for (int cxk = (bx - 4) >> 4; cxk <= ((bx + 5) >> 4); ++cxk)           // the crown and the branches entirely inside the fill
            for (int czk = (bz - 4) >> 4; czk <= ((bz + 5) >> 4); ++czk)
                if (!sprFill.contains(pairKey(cxk, czk))) return false;
        const quint16 log = zt.oakLog, leaf = zt.oakLeaf;
        for (int dy = 0; dy < h; ++dy)
            for (int dx = 0; dx <= 1; ++dx)
                for (int dz = 0; dz <= 1; ++dz)
                    store.set(bx + dx, y0 + dy, bz + dz, log);
        const int topTree = y0 + h - 1;
        static const int kR[4] = { 4, 4, 3, 1 };                               // the crown layers from the bottom skirt up
        for (int li = 0; li < 4; ++li)
        {
            const int r = kR[li], y = topTree + li - 2;
            for (int dx = -r; dx <= r + 1; ++dx)
                for (int dz = -r; dz <= r + 1; ++dz)
                {
                    const double fx = dx - 0.5, fz = dz - 0.5;
                    if (fx * fx + fz * fz > (r + 0.6) * (r + 0.6)) continue;   // rounding the corners (from the 2×2 center)
                    if (store.get(bx + dx, y, bz + dz) == IdAir)
                        store.set(bx + dx, y, bz + dz, leaf);
                }
        }
        const int nBr = 2 + int((hh >> 8) % 2u);                               // 2..3 bare branches under the crown skirt
        static const int kDir[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };   // N E S W
        for (int b = 0; b < nBr; ++b)
        {
            const int d = (int(hh >> 12) + b * 2 + (b == 2 ? 1 : 0)) % 4;
            const int ax = kDir[d][0], az = kDir[d][1];
            const int pb = int((hh >> (16 + b * 3)) % 2u);                     // which of the two trunk columns the branch grows from
            const int yB = y0 + h - 5 + int((hh >> (22 + b * 2)) % 2u);
            int sxp, szp;
            if (ax) { sxp = ax > 0 ? bx + 2 : bx - 1; szp = bz + pb; }
            else    { szp = az > 0 ? bz + 2 : bz - 1; sxp = bx + pb; }
            for (int i = 0; i < 2; ++i)                                        // the branch itself (2 blocks)
            {
                const int x = sxp + ax * i, z = szp + az * i;
                if (store.get(x, yB, z) == IdAir) store.set(x, yB, z, log);
            }
            const int tx = sxp + ax * 2, tz = szp + az * 2;                    // the tip: a leaf cap tucked under the crown
            for (int ex = -1; ex <= 1; ++ex)
                for (int ez = -1; ez <= 1; ++ez)
                    if ((ex || ez) && store.get(tx + ex, yB + 1, tz + ez) == IdAir)
                        store.set(tx + ex, yB + 1, tz + ez, leaf);
        }
        return true;
    };
    // A palm (monsoon zones): a slim bare trunk of jungle logs, a crown of arching diagonal
    // "arms" with hanging tips and a small cap on the top (like adventure-map palms in the game)
    auto plantPalm = [&](int bx, int bz, int y0, quint32 hh) -> bool
    {
        const int h = 7 + int(hh % 10u);                                  // trunk 7..16 (the tallest ones are 1.5 times taller than the shortest)
        if (y0 + h + 2 > 255) return false;
        for (int sx = -4; sx <= 4; sx += 8)                               // the crown reach (±4) entirely inside the fill
            for (int sz = -4; sz <= 4; sz += 8)
                if (!sprFill.contains(pairKey((bx + sx) >> 4, (bz + sz) >> 4))) return false;
        const quint16 pLog = quint16(IdPaletteStart + paletteIndexOf("minecraft:jungle_log"));
        for (int dy = 0; dy < h; ++dy)                                    // the trunk over decorative grass too
            store.set(bx, y0 + dy, bz, pLog);
        auto leaf = [&](int x, int y, int z)
        { if (store.get(x, y, z) == IdAir) store.set(x, y, z, IdJungleLeaves); };
        const int topY = y0 + h - 1;
        const int kLen = 2 + int((hh >> 8) % 2u);                         // the arms 2..3 blocks long
        static const int kAx[4] = { 1, 1, -1, -1 }, kAz[4] = { 1, -1, 1, -1 };
        for (int a = 0; a < 4; ++a)
        {
            for (int k = 1; k <= kLen; ++k)
                leaf(bx + kAx[a] * k, topY, bz + kAz[a] * k);
            leaf(bx + kAx[a] * (kLen + 1), topY - 1, bz + kAz[a] * (kLen + 1));   // the tip hangs down
        }
        static const int kSx[4] = { 1, -1, 0, 0 }, kSz[4] = { 0, 0, 1, -1 };
        for (int a = 0; a < 4; ++a)                                       // short side leafs along the axes
            leaf(bx + kSx[a], topY, bz + kSz[a]);
        leaf(bx, topY + 1, bz);                                           // a small cap over the trunk top
        return true;
    };
    // Pale oak: a 2×2 trunk of height h, a "cap" crown of radius 3, moss hanging under the crown edge, a "creaking heart" in the trunk
    auto plantPaleTree = [&](int bx, int bz, int y0, int h, quint32 hh)
    {
        for (int cxk = (bx - 4) >> 4; cxk <= ((bx + 5) >> 4); ++cxk)             // the crown (up to 4 blocks from the 2×2 trunk) entirely inside the fill
            for (int czk = (bz - 4) >> 4; czk <= ((bz + 5) >> 4); ++czk)
                if (!sprFill.contains(pairKey(cxk, czk))) return;
        bool trunkOk = true;                                                      // the trunk entirely of pale oak (the neighbor's leaves do not interfere)
        for (int dy = 0; dy < h; ++dy)
            for (int dx = 0; dx <= 1; ++dx)
                for (int dz = 0; dz <= 1; ++dz)
                {
                    const quint16 old = store.get(bx + dx, y0 + dy, bz + dz);
                    if (old == IdAir || old == IdPaleLeaves || old == IdOakLeaves || old == IdPaleHangingMoss || old == IdPaleMossCarpet)
                        store.set(bx + dx, y0 + dy, bz + dz, IdPaleLog);
                    else if (old != IdPaleLog) trunkOk = false;
                }
        if (trunkOk && hh % 100u < 16u && h >= 4)
        {
            store.set(bx, y0 + 2, bz, IdCreakingHeart);                          // the "heart": around it a trunk of pale oak
            heartsByChunk[pairKey(bx >> 4, bz >> 4)].append(QPoint3{ bx, y0 + 2, bz });
        }
        const int topTree = y0 + h - 1;
        static const int kR[4] = { 3, 3, 2, 1 };                                 // the crown layers from top to bottom from topTree-2 to topTree+1
        for (int li = 0; li < 4; ++li)
        {
            const int dy = li - 2, r = kR[li];
            for (int dx = -r; dx <= r + 1; ++dx)
                for (int dz = -r; dz <= r + 1; ++dz)
                {
                    const double fx = dx - 0.5, fz = dz - 0.5;
                    if (fx * fx + fz * fz > (r + 0.7) * (r + 0.7)) continue;
                    const int x = bx + dx, y = topTree + dy, z = bz + dz;
                    if (store.get(x, y, z) != IdAir) continue;
                    store.set(x, y, z, IdPaleLeaves);
                    if (li == 0 && ((x * 7 + z * 13 + int(hh)) & 3) == 0 && store.get(x, y - 1, z) == IdAir)
                        store.set(x, y - 1, z, IdPaleHangingMoss);               // moss hangs from the lowest layer of the crown
                }
        }
    };
    vegCols = 0;
    for (int fidx = 0; fidx < forests.size(); ++fidx)
    {
        const VegSite& site = forests[fidx];
        const bool pale = (fidx == paleIdx);
        // A 6×6 grid (~2.8% of the columns, like the former ~2.5%): in every cell one hash-shifted place, inside
        // the forest — a tree. The work is of the order of the number of bbox cells, not the number of forest columns (tens of millions).
        const int G = site.dense ? 8 : 6;                // yards (OSM): an 8×8 grid (it was 4×4: the crowns of neighboring trees merged into a solid canopy over the whole yard)
        // The grid is clamped to the export rectangle: an OSM forest polygon can be many times wider
        // than the world (a park strip with a bbox of 11 km used to cost the whole grid over its bbox).
        const int vgX0 = std::max(site.minX, (rcx0 - rectM) * 16), vgX1 = std::min(site.maxX, (rcx1 + rectM + 1) * 16 - 1);
        const int vgZ0 = std::max(site.minZ, (rcz0 - rectM) * 16), vgZ1 = std::min(site.maxZ, (rcz1 + rectM + 1) * 16 - 1);
        const int gx0 = int(std::floor(double(vgX0) / G)), gx1 = int(std::floor(double(vgX1) / G));
        const int gz0 = int(std::floor(double(vgZ0) / G)), gz1 = int(std::floor(double(vgZ1) / G));
        for (int gx = gx0; gx <= gx1; ++gx)
        for (int gz = gz0; gz <= gz1; ++gz)
        {
            if ((++vegCols & 0x3FF) == 0 && cancel && cancel->load())
            {
                res.cancelled = true;
                res.error = QObject::tr("Export cancelled.");
                return res;
            }
            if (progress && (vegCols & 0xFFFF) == 0)
                progress(std::min(vegCols, vegCells), vegCells, 4);                  // the "trees in forests" stage
            const quint32 hc = colHash(pairKey(gx, gz) ^ 0x7EEDull);
            if (site.dense && (hc >> 20) % 10u == 0u) continue;                          // yards: every tenth place stays empty (so the trees stand in groups with clearings)
            const int tx = gx * G + int(hc % G), tz = gz * G + int((hc >> 8) % G);
            if (tx < site.minX || tx > site.maxX || tz < site.minZ || tz > site.maxZ) continue;
            if (!spansContain(site.spans, tx, tz)) continue;
            const qint64 ck = pairKey(tx, tz);
            if (colWater.contains(ck)) continue;               // we do not grow on water
            if (noTreePadCols.contains(ck)) continue;          // the reserved spots (apiaries, farms, objects from points)
            if (colMin.contains(ck)) continue;                 // a structure column — the forest does not climb into the house
                                                               // (after the floor was sunk by 1 block the slab
                                                               // stopped being an obstacle for y0)
            const int bx = keyFirst(ck), bz = keySecond(ck);
            const int y0 = surfaceTop(ck) + 1;
            if (y0 + site.treeH + 2 > 255) continue;
            if (store.get(bx, y0, bz) != IdAir) continue;
            if (pale && paleDonorUsed)
            {   // trees were carried together with the donor terrain; in the fringe at the forest border and in the ring around the mansion (there the terrain is mixed with the ground) we plant procedural ones
                const auto pcit = paleCol.constFind(ck);
                if (pcit != paleCol.constEnd() && pcit->pid < 0 && pcit->w >= 235 && pcit->surf != 0) continue;
            }
            if (paleDonorUsed && !pale && spansContain(forests[paleIdx].spans, tx, tz)) continue;
            if (!patchCols.isEmpty() && patchCols.contains(pairKey(tx, tz))) continue;      // outposts/camps: trees do not climb into the structures   // other (overlapping) forests do not plant extra trees into the Pale Garden
            if (pale)
            {   // pale oak: taller than usual (6..9), 2×2 — free neighboring columns are needed
                const quint32 hp = colHash(ck ^ 0x9A1Eull);
                const int hh = 6 + int(hp % 4u);
                if (y0 + hh + 4 > 255) continue;
                if (colMin.contains(pairKey(bx + 1, bz)) || colMin.contains(pairKey(bx, bz + 1)) || colMin.contains(pairKey(bx + 1, bz + 1))) continue;
                // the 2x2 trunk must not stand half in a drawn pond (the anchor was checked above)
                if (colWater.contains(pairKey(bx + 1, bz)) || colWater.contains(pairKey(bx, bz + 1)) || colWater.contains(pairKey(bx + 1, bz + 1))) continue;
                if (!spansContain(site.spans, bx + 1, bz + 1)) continue;
                plantPaleTree(bx, bz, y0, hh, hp >> 4);
                continue;
            }
            if (cemeteryCols.contains(ck))
            {   // a cemetery (csrpol 53510000, landuse=cemetery): whatever forest was planned here becomes a dense
                // grove of dark oaks (a user request) — the dark forest look, independent of the climate zone
                const quint32 hc = colHash(ck ^ 0x0CE51ull);
                if (hc % 100u >= 85u) continue;                                // ~85% of the grid cells are planted
                const int h = 7 + int(hc % 4u);                                // trunks 7..10 (a request: 2 blocks taller, the crown over the paths)
                if (y0 + h + 4 > 255) continue;
                plantDarkOak(bx, bz, y0, h, hc);
                continue;
            }
            if (swampCols.contains(ck))
            {   // the swamp biome (a layer with the "swamp" effect lies over this spot): whatever forest grows
                // here — a park, a yard or an ordinary one — becomes a grove of swamp oaks with vines, independent
                // of the climate zone; rarer than an ordinary forest, with clearings between the trees
                const quint32 hs = colHash(ck ^ 0x2F8C3ull);
                if (hs % 100u >= 50u) continue;                                // ~50% of the grid cells are planted
                const int h = 5 + int(hs % 4u);                                // trunks 5..8
                if (y0 + h + 4 > 255) continue;
                plantSwampOak(bx, bz, y0, h, hs);
                continue;
            }
            if (site.park)
            {   // a park: mixed trees (oak 40%, birch 35%, spruce 25%) and not frequent — about 24% of the cells of the 6×6 grid (≈ 1 tree per 135 m²)
                const quint32 hr = colHash(ck ^ 0x9A4Bull);
                if (hr % 100u >= 24u) continue;                                    // 24% of the cells (was 16%): in parks there are 1.5 times more trees
                if (zt.palmShare)
                {   // a palm park (monsoon and semi-arid): the zone's share is palms, the rest are oaks (no birches and no spruces)
                    if (colHash(ck ^ 0x3C77ull) % 100u < quint32(zt.palmShare) && plantPalm(bx, bz, y0, colHash(ck ^ 0x5F13ull))) continue;
                    const int h = 5 + int((hr >> 16) % 3u);
                    if (y0 + h + 4 > 255) continue;
                    plantBroad(bx, bz, y0, h, IdOakLog, IdOakLeaves);
                    continue;
                }
                const quint32 cls = (hr >> 8) % 100u;
                if (cls < 40)
                {   // oak — as in ordinary forests
                    const int h = 5 + int((hr >> 16) % 3u);
                    if (y0 + h + 4 > 255) continue;
                    plantBroad(bx, bz, y0, h, IdOakLog, IdOakLeaves);
                }
                else if (cls < 75)
                {   // birch: thinner and taller than the oak
                    const int h = 6 + int((hr >> 16) % 4u);
                    if (y0 + h + 4 > 255) continue;
                    plantBroad(bx, bz, y0, h, quint16(IdPaletteStart + paletteIndexOf("minecraft:birch_log")), IdBirchLeaves);
                }
                else
                {   // a medium-sized spruce
                    const int hs = 9 + int((hr >> 16) % 5u);
                    if (y0 + hs + 3 > 255) continue;
                    plantSpruce(bx, bz, y0, hs, 3);
                }
                continue;
            }
            if (site.intra && !site.dense && colHash(ck ^ 0x1A7Bull) % 3u == 0u) continue;       // intra-block vegetation (vgbpol): 1.5 times rarer (OSM yards — without thinning)
            if (site.intra)
            {   // trees of different sizes: small (trunk 3–4, crown 1), medium (5–6, crown 2), big (7–9, crown 3); oak and birch mixed
                const quint32 hz = colHash(ck ^ 0x51ABEull);
                const int roll = int(hz % 100u);
                const int R = roll < 30 ? 1 : (roll < 75 ? 2 : 3);
                const int h = R == 1 ? 3 + int((hz >> 8) % 2u) : (R == 2 ? 5 + int((hz >> 8) % 2u) : 7 + int((hz >> 8) % 3u));
                if (y0 + h + 4 > 255) continue;
                const bool birch = ((hz >> 16) % 100u) < 35u;
                plantSized(bx, bz, y0, h, R, birch ? quint16(IdPaletteStart + paletteIndexOf("minecraft:birch_log")) : quint16(IdOakLog), birch ? quint16(IdBirchLeaves) : quint16(IdOakLeaves));
                continue;
            }
            if (zt.palmShare)                                                 // a palm forest (monsoon and semi-arid): the zone's share is palms, the rest are oaks (no spruces and no birches there)
            {
                if (zoneSkip(bx, bz)) continue;
                if (colHash(ck ^ 0x8D27ull) % 100u < quint32(zt.palmShare) && plantPalm(bx, bz, y0, colHash(ck ^ 0x60C5ull))) continue;
            }
            if (site.spruce && !zt.jungle && !zt.palmShare)
            {   // spruces of different sizes, MUCH rarer than oaks: some cells of the grid are empty, big and giant ones are a rarity
                // (a tropical forest is a jungle: dense, every fifth tree big — it must not be thinned as a spruce one)
                const quint32 hr = colHash(ck ^ 0x5B0Eull);
                if (hr % 100u >= 38u) continue;                                      // ~38% of the cells of the 6×6 grid
                const quint32 cls = (hr >> 8) % 100u;
                int hs, R;
                if (cls < 50)      { hs = 9 + int((hr >> 16) % 5u);   R = 3; }       // ordinary 9..13
                else if (cls < 75) { hs = 14 + int((hr >> 16) % 6u);  R = 4; }       // taller 14..19
                else if (cls < 93) { hs = 20 + int((hr >> 16) % 8u);  R = 5; }       // big 20..27, a 2×2 trunk
                else               { hs = 30 + int((hr >> 16) % 11u); R = 7; }       // a giant 30..40, a 2×2 trunk
                if (y0 + hs + 3 > 255) continue;
                if (R >= 5 && (colMin.contains(pairKey(bx + 1, bz)) || colMin.contains(pairKey(bx, bz + 1)) || colMin.contains(pairKey(bx + 1, bz + 1))
                               || !spansContain(site.spans, bx + 1, bz) || !spansContain(site.spans, bx, bz + 1) || !spansContain(site.spans, bx + 1, bz + 1))) continue;
                plantSpruce(bx, bz, y0, hs, R);
                continue;
            }
            if (zoneSkip(bx, bz)) continue;
            if (zt.jungle)
            {   // a jungle forest (like the game's one): one in five — a big 2×2 tree with branches
                // (where the 2×2 is free), the rest are thin trunks taller than the ordinary oak
                const quint32 hj = colHash(ck ^ 0x3ADE11ull);
                if (hj % 5u == 0u
                    && !colMin.contains(pairKey(bx + 1, bz)) && !colMin.contains(pairKey(bx, bz + 1)) && !colMin.contains(pairKey(bx + 1, bz + 1))
                    && !colWater.contains(pairKey(bx + 1, bz)) && !colWater.contains(pairKey(bx, bz + 1)) && !colWater.contains(pairKey(bx + 1, bz + 1))
                    && spansContain(site.spans, bx + 1, bz) && spansContain(site.spans, bx, bz + 1) && spansContain(site.spans, bx + 1, bz + 1)
                    && plantJungleBig(bx, bz, y0, hj))
                    continue;
                if (y0 + site.treeH + 6 > 255) continue;
                plantBroad(bx, bz, y0, site.treeH + int(hj % 4u), IdOakLog, IdOakLeaves);   // the zone remap turns it into a jungle one with vines
                continue;
            }
            if (zt.bigOakShare && colHash(ck ^ 0x2A17Cull) % 100u < quint32(zt.bigOakShare)
                && !colMin.contains(pairKey(bx + 1, bz)) && !colMin.contains(pairKey(bx, bz + 1)) && !colMin.contains(pairKey(bx + 1, bz + 1))
                && !colWater.contains(pairKey(bx + 1, bz)) && !colWater.contains(pairKey(bx, bz + 1)) && !colWater.contains(pairKey(bx + 1, bz + 1))
                && spansContain(site.spans, bx + 1, bz) && spansContain(site.spans, bx, bz + 1) && spansContain(site.spans, bx + 1, bz + 1)
                && plantBigOak(bx, bz, y0, colHash(ck ^ 0x73F91ull)))
                continue;                                                     // a temperate rainforest: every third broadleaf tree is a big oak (where the 2×2 is free)
            const int h = site.treeH - int(colHash(ck ^ 0x5EEDull) % 2u);   // trunks of different heights
            for (int dy = 0; dy < h; ++dy)
                if (store.get(bx, y0 + dy, bz) == IdAir)
                    store.set(bx, y0 + dy, bz, zt.oakLog);
            const int topTree = y0 + h - 1;                   // the crown — a "diamond" of oak, only through the air
            for (int dy = -2; dy <= 1; ++dy)
            {
                const int r = (dy == 1) ? 1 : 2;
                for (int dx = -r; dx <= r; ++dx)
                    for (int dz = -r; dz <= r; ++dz)
                    {
                        if (dy == 1 && std::abs(dx) + std::abs(dz) > 1) continue;   // the top — a cross
                        if (dy != 1 && std::abs(dx) == 2 && std::abs(dz) == 2) continue;   // without the far corners
                        const int x = bx + dx, y = topTree + dy, z = bz + dz;
                        if (store.get(x, y, z) == IdAir)
                            store.set(x, y, z, zt.oakLeaf);
                    }
            }
        }
    }
    if (paleIdx >= 0 && paleDonorUsed && !heartsByChunk.isEmpty())
    {   // CREAKING HEARTS — rare and spaced apart (horror, not a "wild run"): the extra ones (closer than 32 blocks to an already kept one) become a log again,
        // and where there is no heart at all within a radius of 50 blocks, a new one is added into a suitable trunk (not closer than 50 to the others).
        constexpr double kMinD = 32.0, kGapD = 50.0;
        QVector<QPoint3> all;
        for (auto it = heartsByChunk.constBegin(); it != heartsByChunk.constEnd(); ++it) all += it.value();
        std::sort(all.begin(), all.end(), [&](const QPoint3& a, const QPoint3& b)
        { const quint32 ha = colHash(pairKey(a.x, a.z) ^ 0x4E1ull), hb = colHash(pairKey(b.x, b.z) ^ 0x4E1ull); return ha != hb ? ha < hb : (a.x != b.x ? a.x < b.x : a.z < b.z); });
        QVector<QPoint3> kept;
        auto nearKept = [&](int x, int z, double d)
        { for (const QPoint3& q : kept) if (std::hypot(double(q.x - x), double(q.z - z)) < d) return true; return false; };
        int nDemoted = 0;
        QHash<ColKey, QVector<QPoint3>> newHearts;
        for (const QPoint3& h : all)
        {
            if (!nearKept(h.x, h.z, kMinD)) { kept.append(h); newHearts[pairKey(h.x >> 4, h.z >> 4)].append(h); continue; }
            const quint16 cur = store.get(h.x, h.y, h.z);
            store.set(h.x, h.y, h.z, cur == quint16(IdCreakingHeart) ? quint16(IdPaleLog) : paleDonor.logId);
            ++nDemoted;
        }
        // adding: pale oak trunks at a height of 4 above the ground (donor) — candidates on a 6×6 grid, in the "hash" order
        int nAdded = 0;
        if (paleDonor.heartId)
        {
            QVector<QPoint3> cands;
            for (auto pit = paleCol.constBegin(); pit != paleCol.constEnd(); ++pit)
            {
                const int x = keyFirst(pit.key()), z = keySecond(pit.key());
                if (((x * 7 + z * 13) % 5) != 0) continue;
                const auto cc = pit;
                if (cc->pid >= 0 || cc->w < 235) continue;
                const int y = cc->top + 4;
                if (store.get(x, y, z) == paleDonor.logId) cands.append(QPoint3{ x, y, z });
            }
            std::sort(cands.begin(), cands.end(), [&](const QPoint3& a, const QPoint3& b)
            { const quint32 ha = colHash(pairKey(a.x, a.z) ^ 0x4E2ull), hb = colHash(pairKey(b.x, b.z) ^ 0x4E2ull); return ha != hb ? ha < hb : (a.x != b.x ? a.x < b.x : a.z < b.z); });
            for (const QPoint3& c : cands)
            {
                if (nearKept(c.x, c.z, kGapD)) continue;
                store.set(c.x, c.y, c.z, paleDonor.heartId);
                kept.append(c);
                newHearts[pairKey(c.x >> 4, c.z >> 4)].append(c);
                ++nAdded;
            }
        }
        heartsByChunk = newHearts;
        res.lairInfo += QObject::tr("; creaking hearts: %1 (extra removed %2, added %3)").arg(kept.size()).arg(nDemoted).arg(nAdded);
    }
    if (paleIdx >= 0 && !paleDonorUsed)
    {   // the Pale Garden litter: moss blocks instead of turf, a carpet of pale moss, rare eyeblossoms
        const VegSite& ps = forests[paleIdx];
        for (const ColSpan& c : ps.spans)
            for (int z = c.z0; z <= c.z1; ++z)
            {
                const qint64 ck = pairKey(c.x, z);
                if (colWater.contains(ck) || colMin.contains(ck) || colWool.contains(ck)) continue;
                const quint32 hq = colHash(ck ^ 0xB1EDull);
                const auto git = ground.find(pairKey(c.x >> 4, z >> 4));
                if (git == ground.end()) continue;
                const int col = (z & 15) * 16 + (c.x & 15);
                if (git->wd[col] > 0) continue;
                if (hq % 100u < 14u) git->surf[col] = quint16(IdPaleMossBlock);
                const int top = git->top[col];
                if (store.get(c.x, top + 1, z) != IdAir) continue;
                const quint32 r2 = (hq >> 8) % 1000u;
                if (r2 < 330) store.set(c.x, top + 1, z, IdPaleMossCarpet);
                else if (r2 < 345) store.set(c.x, top + 1, z, (hq >> 20) & 1u ? quint16(IdEyeOpen) : quint16(IdEyeClosed));
            }
    }
    // FLOATING ISLANDS above kindergartens: a "cloud" rounded lower part (calcite/white concrete), turf on top, a lake flush with the ground,
    // slime at the shore, tropics (jungle trees, bamboo, reeds, ferns, lily pads), slimes. There are no rims — slimes sometimes fall.
    for (const IslandSite& is : islandSites)
    {
        if (is.kind != 2) continue;
        int y0 = std::min(surfaceTop(pairKey(is.cx, is.cz)), 255) + 70 + int(is.hash % 16u);   // high above the park: 70–85 blocks above the ground
        if ((y0 & 15) == 15) ++y0;                                         // the lake and lily pads (a block above the water) are not cut by a section border
        const int D = std::max(5, std::min(11, std::min(is.rx, is.rz) / 3));   // the "bowl" depth: thin, does not reach the buildings
        if (y0 + 14 > 300) continue;
        const quint16 calcite = quint16(IdPaletteStart + paletteIndexOf("minecraft:calcite"));
        const quint16 white = quint16(IdPaletteStart + paletteIndexOf("minecraft:diorite"));
        const quint16 sandB = quint16(IdPaletteStart + paletteIndexOf("minecraft:sand"));
        const quint16 slime = quint16(IdPaletteStart + paletteIndexOf("minecraft:slime_block"));
        const quint16 jungleLog = quint16(IdPaletteStart + paletteIndexOf("minecraft:jungle_log"));
        const quint16 bamboo = quint16(IdPaletteStart + paletteIndexOf("minecraft:bamboo"));
        const quint16 cane = quint16(IdPaletteStart + paletteIndexOf("minecraft:sugar_cane"));
        const quint16 fern = quint16(IdPaletteStart + paletteIndexOf("minecraft:fern"));
        const quint16 lily = quint16(IdPaletteStart + paletteIndexOf("minecraft:lily_pad"));
        const double ph = double(is.hash % 628u) / 100.0;
        // the lake: an ellipse offset from the center
        const double lcx = std::sin(ph) * is.rx * 0.18, lcz = std::cos(ph) * is.rz * 0.18;
        const double lrx = std::max(4.0, is.rx * 0.30), lrz = std::max(4.0, is.rz * 0.26);
        auto lakeD = [&](int dx, int dz) { const double a = (dx - lcx) / lrx, b = (dz - lcz) / lrz; return a * a + b * b; };   // <=1 inside
        auto colGround = [&](int x, int z) { return ground.contains(pairKey(x >> 4, z >> 4)); };
        auto hashAt = [&](int x, int z, quint32 salt) { return colHash(pairKey(x, z) ^ (qint64(is.hash) << 8) ^ salt); };
        QVector<QPoint> land;                                              // land cells (for trees/slimes)
        QHash<ColKey, int> topAt;
        for (int dz = -is.rz; dz <= is.rz; ++dz)
            for (int dx = -is.rx; dx <= is.rx; ++dx)
            {
                const double u = double(dx) / is.rx, v = double(dz) / is.rz;
                const double edge = 0.12 * std::sin(dx * 0.31 + ph) + 0.10 * std::cos(dz * 0.27) + 0.06 * std::sin((dx + dz) * 0.19);
                const double r = std::sqrt(u * u + v * v) + edge;
                if (r > 1.0) continue;
                const int x = is.cx + dx, z = is.cz + dz;
                if (!colGround(x, z)) continue;
                const bool nearLake = lakeD(dx, dz) <= 3.2;
                int top = y0;
                if (!nearLake && r > 0.15)                                  // soft mounds far from the lake
                    top = y0 + int(std::lround(1.1 * std::sin(dx * 0.23 + ph) * std::cos(dz * 0.19 - ph)));
                const int depth = 2 + int(D * std::sqrt(std::max(0.0, 1.0 - r * r)));    // a rounded "bowl"
                for (int y = top - depth; y <= top; ++y)
                {
                    const int d = top - y;
                    quint16 id;
                    if (d == 0) id = IdGrass;
                    else if (d <= 2) id = IdDirt;
                    else id = (((dx * 7 + dz * 13 + y * 5) & 7) == 0) ? white : calcite;
                    store.set(x, y, z, id);
                }
                topAt.insert(pairKey(x, z), top);
                // the lake is flush: water on the three top layers, the bottom — sand
                if (lakeD(dx, dz) <= 1.0 && r < 0.8)
                {
                    store.set(x, top, z, IdWater);
                    store.set(x, top - 1, z, IdWater);
                    store.set(x, top - 2, z, IdWater);
                    store.set(x, top - 3, z, sandB);
                    if (hashAt(x, z, 0x11A1u) % 100u < 9u) store.set(x, top + 1, z, lily);
                }
                else
                    land.append(QPoint(x, z));
            }
        // slime at the shore, reeds near the water, ferns/flowers
        for (const QPoint& c : land)
        {
            const int top = topAt.value(pairKey(c.x(), c.y()));
            const int dx = c.x() - is.cx, dz = c.y() - is.cz;
            const double ld = lakeD(dx, dz);
            const quint32 hq = hashAt(c.x(), c.y(), 0x5A1Eu);
            if (ld <= 1.7 && hq % 100u < 38u) { store.set(c.x(), top, c.y(), slime); continue; }        // the shore is partly slimy
            if (ld <= 1.7 && hq % 100u < 60u && store.get(c.x(), top, c.y()) == IdGrass)
            {   // reeds near the water: 2–3 cells
                const int h = 2 + int((hq >> 8) % 2u);
                for (int k = 1; k <= h; ++k) store.set(c.x(), top + k, c.y(), cane);
                continue;
            }
            if (ld > 1.7 && hq % 100u < 14u) store.set(c.x(), top + 1, c.y(), fern);
            else if (ld > 1.7 && hq % 100u < 17u) store.set(c.x(), top + 1, c.y(), quint16(IdFlowerBase + int((hq >> 10) % quint32(kFlowersCount))));
        }
        // slime clumps on land (2×2)
        for (int k = 0; k < 4; ++k)
        {
            const quint32 hk = is.hash * 2654435761u + quint32(k) * 40503u;
            if (land.isEmpty()) break;
            const QPoint c = land[int(hk % quint32(land.size()))];
            for (int dz = 0; dz <= 1; ++dz)
                for (int dx = 0; dx <= 1; ++dx)
                {
                    const qint64 key = pairKey(c.x() + dx, c.y() + dz);
                    if (topAt.contains(key) && store.get(c.x() + dx, topAt.value(key), c.y() + dz) == IdGrass)
                        store.set(c.x() + dx, topAt.value(key), c.y() + dz, slime);
                }
        }
        // jungle trees and bamboo
        const int nTrees = std::max(5, std::min(22, int(land.size()) / 90));
        for (int t = 0; t < nTrees && !land.isEmpty(); ++t)
        {
            const quint32 ht = is.hash * 1103515245u + quint32(t) * 12345u + 7u;
            const QPoint c = land[int((ht >> 4) % quint32(land.size()))];
            const int dx = c.x() - is.cx, dz = c.y() - is.cz;
            if (lakeD(dx, dz) <= 2.4) continue;
            const int top = topAt.value(pairKey(c.x(), c.y()));
            if (store.get(c.x(), top, c.y()) != IdGrass || store.get(c.x(), top + 1, c.y()) != IdAir) continue;
            const int h = 5 + int((ht >> 12) % 4u);
            for (int k = 1; k <= h; ++k) store.set(c.x(), top + k, c.y(), jungleLog);
            for (int dy = -2; dy <= 1; ++dy)
            {
                const int rr = (dy == 1) ? 1 : (dy == 0 ? 3 : 2);
                for (int ex = -rr; ex <= rr; ++ex)
                    for (int ez = -rr; ez <= rr; ++ez)
                    {
                        if (std::abs(ex) == rr && std::abs(ez) == rr && rr > 1) continue;
                        if (store.get(c.x() + ex, top + h + dy, c.y() + ez) == IdAir)
                            store.set(c.x() + ex, top + h + dy, c.y() + ez, IdJungleLeaves);
                    }
            }
            jungleCrownVines(c.x(), c.y(), top + h - 2, 3);             // vines over the whole crown (the game's jungle look)
        }
        for (int g = 0; g < 7 && !land.isEmpty(); ++g)
        {   // bamboo groves
            const quint32 hg = is.hash * 2246822519u + quint32(g) * 3266489917u + 3u;
            const QPoint c = land[int((hg >> 3) % quint32(land.size()))];
            if (lakeD(c.x() - is.cx, c.y() - is.cz) <= 2.0) continue;
            const int n = 3 + int((hg >> 20) % 4u);
            for (int q = 0; q < n; ++q)
            {
                const int bx = c.x() + int((hg >> (q * 3)) % 5u) - 2, bz = c.y() + int((hg >> (q * 3 + 1)) % 5u) - 2;
                const qint64 key = pairKey(bx, bz);
                if (!topAt.contains(key)) continue;
                const int top = topAt.value(key);
                if (store.get(bx, top, bz) != IdGrass || store.get(bx, top + 1, bz) != IdAir) continue;
                const int h = 4 + int((hg >> (q + 8)) % 6u);
                for (int k = 1; k <= h; ++k) store.set(bx, top + k, bz, bamboo);
            }
        }
        // slimes: up to 8 of different sizes on land
        for (int sl = 0; sl < 8 && !land.isEmpty(); ++sl)
        {
            const quint32 hs = is.hash * 668265263u + quint32(sl) * 374761393u + 11u;
            const QPoint c = land[int((hs >> 3) % quint32(land.size()))];
            const int top = topAt.value(pairKey(c.x(), c.y()));
            if (store.get(c.x(), top + 1, c.y()) != IdAir) continue;
            NpcSite m; m.kind = 4; m.variant = 8; m.level = int((hs >> 10) % 3u);
            m.x = c.x(); m.z = c.y(); m.y = top + 1; m.yaw = int((hs >> 14) % 4u) * 90;
            npcs.append(m);
        }
        // Lamps on the ground under the island (it casts a shadow on the park): two oval rings of glowing tiles flush with the ground
        // (glowstone instead of turf), on every third one — a low lamp post; in the center — a tall stand with a lantern. The rings are slightly wider than the island.
        {
            const quint16 glow = quint16(IdPaletteStart + paletteIndexOf("minecraft:glowstone"));
            auto groundCol = [&](int x, int z, int& top) -> GroundChunk*
            {
                const qint64 ck = pairKey(x, z);
                if (colWater.contains(ck) || colMin.contains(ck) || colWool.contains(ck) || roadCols.contains(ck)) return nullptr;
                auto git = ground.find(pairKey(x >> 4, z >> 4));
                if (git == ground.end()) return nullptr;
                const int col = (z & 15) * 16 + (x & 15);
                if (git->wd[col] > 0) return nullptr;
                top = git->top[col];
                return &git.value();
            };
            auto lampPost = [&](int x, int z, int top, int fences)
            {
                bool free = true;
                for (int dy = 1; dy <= fences + 2 && free; ++dy) if (store.get(x, top + dy, z) != IdAir) free = false;
                if (!free) return;
                store.set(x, top + 1, z, IdCobbleWall);
                for (int dy = 2; dy <= fences + 1; ++dy) store.set(x, top + dy, z, IdFence);
                store.set(x, top + fences + 2, z, IdLanternStanding);
            };
            const double ringK[2] = { 1.12, 0.62 };
            for (int ri = 0; ri < 2; ++ri)
            {
                const double rxr = is.rx * ringK[ri], rzr = is.rz * ringK[ri];
                const int nSeg = std::max(12, int(std::lround(2.0 * 3.14159265 * 0.5 * (rxr + rzr) / 2.0)));        // a tile every ~2 blocks
                QSet<ColKey> done;
                for (int t = 0; t < nSeg; ++t)
                {
                    const double a = 2.0 * 3.14159265 * t / nSeg;
                    const int x = is.cx + int(std::lround(std::cos(a) * rxr)), z = is.cz + int(std::lround(std::sin(a) * rzr));
                    if (done.contains(pairKey(x, z))) continue;
                    done.insert(pairKey(x, z));
                    int top = 0;
                    GroundChunk* g = groundCol(x, z, top);
                    if (!g) continue;
                    g->surf[(z & 15) * 16 + (x & 15)] = glow;
                    if (t % 3 == 0) lampPost(x + (std::cos(a) >= 0 ? 1 : -1), z, top, 3);                   // a post next to the tile, outside the ring
                }
            }
            int topC = 0;
            if (GroundChunk* gc = groundCol(is.cx, is.cz, topC))
            {
                Q_UNUSED(gc);
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        int tp = 0;
                        if (GroundChunk* g2 = groundCol(is.cx + dx, is.cz + dz, tp)) g2->surf[((is.cz + dz) & 15) * 16 + ((is.cx + dx) & 15)] = glow;
                    }
                lampPost(is.cx, is.cz, topC, 5);                                                              // the central tall stand
            }
        }
    }
    // Swamp biome (sources with the "swamp" effect, e.g. mrspol): what the biome does BY ITSELF. Forests under
    // the contour already became swamp-oak groves in the forest loop; here the OPEN swamp ground gets rare oaks
    // (sparser than forest), and mushrooms/ferns/dead branches as the ground flora. Water, roads and built-up
    // columns under the contour are left alone — the biome is an overlay, not an owner of the land.
    if (!swampCols.isEmpty())
    {
        const quint16 qFern = quint16(IdPaletteStart + paletteIndexOf("minecraft:fern")),
                    qDb = quint16(IdPaletteStart + paletteIndexOf("minecraft:dead_bush")),
                    qMushB = quint16(IdPaletteStart + paletteIndexOf("minecraft:brown_mushroom")),
                    qMushR = quint16(IdPaletteStart + paletteIndexOf("minecraft:red_mushroom"));
        auto floor6 = [](int v) { return v >= 0 ? v / 6 : -((-v + 5) / 6); };   // cell of the same 6×6 grid as the forest loop
        QSet<ColKey> cells;
        for (ColKey ck : swampCols)
            cells.insert(pairKey(floor6(keyFirst(ck)), floor6(keySecond(ck))));
        int nSwampTrees = 0;
        for (ColKey cell : cells)
        {
            const quint32 hs = colHash(cell ^ 0x2F8C3ull);
            if (hs % 100u >= 40u) continue;                                    // open swamp — rare trees (~40% of cells)
            const int tx = keyFirst(cell) * 6 + int(hs % 6u), tz = keySecond(cell) * 6 + int((hs >> 8) % 6u);
            const ColKey ck = pairKey(tx, tz);
            if (!swampCols.contains(ck)) continue;                             // the tree spot fell outside the contour
            if (colWater.contains(ck) || roadCols.contains(ck) || colMin.contains(ck) || noTreePadCols.contains(ck)) continue;
            const int y0 = surfaceTop(ck) + 1;
            if (store.get(tx, y0, tz) != IdAir) continue;                      // something already stands here
            const int h = 5 + int(hs % 4u);
            if (y0 + h + 4 > 255) continue;
            plantSwampOak(tx, tz, y0, h, hs);
            ++nSwampTrees;
        }
        for (ColKey ck : swampCols)
        {
            const quint32 hd = colHash(ck ^ 0x77AC11ull);
            if (hd % 100u >= 7u) continue;                                     // flora on ~7% of the columns
            if (colWater.contains(ck) || roadCols.contains(ck) || colMin.contains(ck)) continue;
            const int bx = keyFirst(ck), bz = keySecond(ck);
            const int y0 = surfaceTop(ck) + 1;
            if (store.get(bx, y0, bz) != IdAir) continue;                      // not on a trunk or an object
            const quint32 r = (hd >> 8) % 100u;
            store.set(bx, y0, bz, r < 40u ? qDb : (r < 58u ? qMushB : (r < 76u ? qMushR : qFern)));
        }
        tmcStepLog(QString("3.5 swamp biome: %1 columns, %2 open-ground oaks").arg(swampCols.size()).arg(nSwampTrees).toUtf8().constData());
    }
    // Cemeteries (csrpol 53510000, a user request): the dark-forest plot of the dead. Forests under the contour
    // already became dark-oak groves in the forest loop; the OPEN cemetery ground gets dense dark oaks (a 5x5 grid),
    // stone crosses between them (a 3x3 grid), a lit jack o'lantern on a fence post by some crosses, rare carved
    // pumpkins, wandering monsters (zombies, skeletons, spiders, witches) and rare loot chests (middling often,
    // expensive rarely). Water, roads and built-up columns stay alone — an overlay, not an owner of the land.
    if (!cemeteryCols.isEmpty())
    {
        const quint16 qPumpkin = quint16(IdPaletteStart + paletteIndexOf("minecraft:carved_pumpkin")),
                    qStoneC = quint16(IdPaletteStart + paletteIndexOf("minecraft:stone"));
        auto floorC5 = [](int v) { return v >= 0 ? v / 5 : -((-v + 4) / 5); };
        auto floorC3 = [](int v) { return v >= 0 ? v / 3 : -((-v + 2) / 3); };
        auto floorC6 = [](int v) { return v >= 0 ? v / 6 : -((-v + 5) / 6); };
        auto floorC9 = [](int v) { return v >= 0 ? v / 9 : -((-v + 8) / 9); };
        int nCemTrees = 0, nCross = 0, nJack = 0, nPumpkin = 0, nCemMob = 0, nCemChest = 0;
        QSet<ColKey> cells5;
        for (ColKey ck : cemeteryCols)
            cells5.insert(pairKey(floorC5(keyFirst(ck)), floorC5(keySecond(ck))));
        for (ColKey cell : cells5)
        {   // dark oaks on the open ground — quite dense: ~70% of the 5x5 cells
            const quint32 hs = colHash(cell ^ 0x0CE117ull);
            if (hs % 100u >= 70u) continue;
            const int tx = keyFirst(cell) * 5 + int(hs % 5u), tz = keySecond(cell) * 5 + int((hs >> 8) % 5u);
            const ColKey ck = pairKey(tx, tz);
            if (!cemeteryCols.contains(ck)) continue;                            // the tree spot fell outside the contour
            if (colWater.contains(ck) || roadCols.contains(ck) || colMin.contains(ck) || noTreePadCols.contains(ck)
                || colWool.contains(ck)) continue;                     // paved squares and roadways are no place for a grave tree (a request)
            const int y0 = surfaceTop(ck) + 1;
            if (store.get(tx, y0, tz) != IdAir) continue;                        // something already stands here
            const int h = 7 + int(hs % 4u);                                   // trunks 7..10 (a request: 2 blocks taller, the crown over the paths)
            if (y0 + h + 4 > 255) continue;
            plantDarkOak(tx, tz, y0, h, hs);
            ++nCemTrees;
        }
        QSet<ColKey> cells3;
        for (ColKey ck : cemeteryCols)
            cells3.insert(pairKey(floorC3(keyFirst(ck)), floorC3(keySecond(ck))));
        for (ColKey cell : cells3)
        {   // stone crosses — ~17% of the 3x3 cells (a request: ~1.5x fewer, so the paths between the graves are wider);
            // the whole cross must stand in the air
            const quint32 hc = colHash(cell ^ 0xC83C11ull);
            if (hc % 100u >= 17u) continue;
            const int tx = keyFirst(cell) * 3 + int(hc % 3u), tz = keySecond(cell) * 3 + int((hc >> 8) % 3u);
            const ColKey ck = pairKey(tx, tz);
            if (!cemeteryCols.contains(ck)) continue;
            if (colWater.contains(ck) || roadCols.contains(ck) || colMin.contains(ck) || railColsAll.contains(ck)
                || colWool.contains(ck)) continue;                                 // no crosses on roadways and paved squares (a request)
            const int y0 = surfaceTop(ck) + 1;
            if (store.get(tx, y0, tz) != IdAir || store.get(tx, y0 + 1, tz) != IdAir || store.get(tx, y0 + 2, tz) != IdAir
                || store.get(tx - 1, y0 + 2, tz) != IdAir || store.get(tx + 1, y0 + 2, tz) != IdAir) continue;
            if (roadCols.contains(pairKey(tx - 1, tz)) || roadCols.contains(pairKey(tx + 1, tz))
                || colWool.contains(pairKey(tx - 1, tz)) || colWool.contains(pairKey(tx + 1, tz))) continue;   // nor may the crossbar hang over one (a request)
            store.set(tx, y0, tz, qStoneC);                          // a stone base
            store.set(tx, y0 + 1, tz, IdCobbleWall);                 // the upright
            store.set(tx, y0 + 2, tz, IdCobbleWall);
            store.set(tx - 1, y0 + 2, tz, IdCobbleWall);             // the crossbar
            store.set(tx + 1, y0 + 2, tz, IdCobbleWall);
            ++nCross;
            if (hc % 100u >= 6u) continue;                            // a jack o'lantern by ~6% of the cells (kept proportional to the rarer crosses)
            const int side = ((hc >> 10) & 1u) ? 1 : -1;
            const int px = tx + side * 2;
            const ColKey pk = pairKey(px, tz);
            if (colWater.contains(pk) || roadCols.contains(pk) || colMin.contains(pk) || railColsAll.contains(pk)
                || colWool.contains(pk)
                || store.get(px, y0, tz) != IdAir || store.get(px, y0 + 1, tz) != IdAir) continue;
            store.set(px, y0, tz, IdFence);                          // a fence post (no connections)
            store.set(px, y0 + 1, tz, quint16(IdJackLanternBase + int((hc >> 12) % 4u)));   // the lit face looks by hash
            ++nJack;
        }
        for (ColKey ck : cemeteryCols)
        {   // carved pumpkins dot the grass (~0.75% of the columns — a request: four times rarer)
            const quint32 hp = colHash(ck ^ 0x9AB1CEull);
            if (hp % 400u >= 3u) continue;
            if (colWater.contains(ck) || roadCols.contains(ck) || colMin.contains(ck) || colWool.contains(ck)) continue;
            const int bx = keyFirst(ck), bz = keySecond(ck);
            const int y0 = surfaceTop(ck) + 1;
            if (store.get(bx, y0, bz) != IdAir) continue;
            store.set(bx, y0, bz, qPumpkin);
            ++nPumpkin;
        }
        QSet<ColKey> cells6;
        for (ColKey ck : cemeteryCols)
            cells6.insert(pairKey(floorC6(keyFirst(ck)), floorC6(keySecond(ck))));
        for (ColKey cell : cells6)
        {   // the monsters of the graveyard — a 6x6 grid, ~45% of the cells: a zombie, a skeleton, a spider or a witch
            const quint32 hm = colHash(cell ^ 0x4045E7ull);
            if (hm % 100u >= 45u) continue;
            const int mx = keyFirst(cell) * 6 + int(hm % 6u), mz = keySecond(cell) * 6 + int((hm >> 8) % 6u);
            const ColKey ck = pairKey(mx, mz);
            if (!cemeteryCols.contains(ck)) continue;
            if (colWater.contains(ck) || roadCols.contains(ck) || colMin.contains(ck) || railColsAll.contains(ck)
                || colWool.contains(ck)) continue;
            const int y0 = surfaceTop(ck) + 1;
            if (store.get(mx, y0, mz) != IdAir || store.get(mx, y0 + 1, mz) != IdAir) continue;   // the body needs two rows
            NpcSite ms;
            ms.kind = 4; ms.variant = int((hm >> 16) % 4u);          // 0 zombie, 1 skeleton, 2 spider, 3 witch
            ms.x = mx; ms.y = y0; ms.z = mz; ms.yaw = int((hm >> 4) % 4u) * 90;
            npcs.append(ms);
            ++nCemMob;
        }
        QSet<ColKey> cells9;
        for (ColKey ck : cemeteryCols)
            cells9.insert(pairKey(floorC9(keyFirst(ck)), floorC9(keySecond(ck))));
        for (ColKey cell : cells9)
        {   // the loot of the old graves — a 9x9 grid; buried graves twice as often as the ground ones (a request):
            // a chest standing on the ground OR buried under a grave (the chest lies in the dug row of the old
            // surface under a turf cover with a headstone — the lid is blocked, the treasure must be dug out).
            // Buried: 20% expensive, 80% middling; ground chests: 80% cheap, 20% middling (a request).
            const quint32 hl = colHash(cell ^ 0x51E9Cull);
            const bool wantBuried = (hl & 1u) != 0;                 // half of the candidates ask for a buried grave
            if (hl % 1000u >= (wantBuried ? 130u : 65u)) continue;  // buried ones twice as often (a request), the ground ones as before
            const int tx = keyFirst(cell) * 9 + int(hl % 9u), tz = keySecond(cell) * 9 + int((hl >> 8) % 9u);
            const ColKey ck = pairKey(tx, tz);
            if (!cemeteryCols.contains(ck)) continue;
            if (colWater.contains(ck) || roadCols.contains(ck) || colMin.contains(ck) || railColsAll.contains(ck)
                || colWool.contains(ck)) continue;
            const int y0 = surfaceTop(ck) + 1;
            if (store.get(tx, y0, tz) != IdAir || store.get(tx, y0 + 1, tz) != IdAir) continue;
            bool buried = false;
            if (wantBuried)
            {   // a buried grave: dig one row of the column (the writer builds it from `top`), the chest at the
                // old surface level, a turf block covers it from above, a cobblestone wall marks the grave
                const auto git = ground.find(pairKey(tx >> 4, tz >> 4));
                const int T = y0 - 1;
                if (git != ground.end())
                {
                    const int col = (tz & 15) * 16 + (tx & 15);
                    if (int(git.value().top[col]) == T && store.get(tx, T, tz) == IdAir)
                    {
                        git.value().top[col] = qint16(T - 1);
                        store.set(tx, T, tz, IdChest);
                        chests << ChestSite{ tx, T, tz, pickChestLoot(hl >> 4, 0, 80) };   // 80% middling, 20% expensive
                        store.set(tx, T + 1, tz, IdGrass);
                        store.set(tx, T + 2, tz, IdCobbleWall);
                        buried = true;
                    }
                }
            }
            if (!buried)
            {
                store.set(tx, y0, tz, IdChest);
                chests << ChestSite{ tx, y0, tz, pickChestLoot(hl >> 4, 80, 20) };   // 80% cheap, 20% middling (a request)
            }
            ++nCemChest;
        }
        char cbuf[192];
        snprintf(cbuf, sizeof cbuf, "3.5 cemeteries: %d columns, %d dark oaks, %d crosses, %d jack o'lanterns, %d pumpkins, %d monsters, %d chests",
                 int(cemeteryCols.size()), nCemTrees, nCross, nJack, nPumpkin, nCemMob, nCemChest);
        tmcStepLog(cbuf);
    }
    lap("before 3.51");
    beginStep(5);
    // 3.51 Tree planting by the user's rules (three rules):
    //   (1) columns inside the fill WITHOUT objects and vector coverage (just terrain) —
    //       single trees: a 32×32 grid, in a cell a point center + hash jitter ±1 — neighbors
    //       by cells do not come closer than 30 blocks (32−1−1); the hash gate lets 3 cells
    //       out of 4 pass — a sparse forest, not rows;
    //   (2) giant fields (ripe wheat, >100000 m²) — GROUPS of 2-3 trees nearby,
    //       a 44×44 grid: the group spread ≤5 + jitter ≤1 ⇒ between groups also ≥30;
    //   (3) "tall grass" (tall_grass, ≥50000 m²) — single trees, as in rule (1).
    // A buffer of ±3 columns from structures/roads/tracks/sites/water/forest spots (including lamps —
    // they are 1 column from the road): trees do not climb onto anything; a trunk is written only when ALL
    // its cells are air (unlike a forest spot, where a skipped bottom one is allowed), the crown —
    // only through the air. Before the grass is scattered: the grass looks at the store air and will not climb into the trunk.
    {
        // ATTENTION: fillChunks are the keys of CHUNKS (store.chunks), not columns!
        const QSet<ColKey> fillChunkSet(fillChunks.constBegin(), fillChunks.constEnd());
        auto inFill = [&](qint64 ck)
        { return fillChunkSet.contains(pairKey(keyFirst(ck) >> 4, keySecond(ck) >> 4)); };
        auto flat5 = [&](qint64 ck)
        {   // the center and the 4 neighbors — one surface height (we do not put a tree on a slope)
            const int y = surfaceTop(ck);
            return std::abs(y - surfaceTop(pairKey(keyFirst(ck) + 1, keySecond(ck)))) <= 1
                && std::abs(y - surfaceTop(pairKey(keyFirst(ck) - 1, keySecond(ck)))) <= 1
                && std::abs(y - surfaceTop(pairKey(keyFirst(ck), keySecond(ck) + 1))) <= 1
                && std::abs(y - surfaceTop(pairKey(keyFirst(ck), keySecond(ck) - 1))) <= 1;
        };
        const quint16 birchLogS = quint16(IdPaletteStart + paletteIndexOf("minecraft:birch_log"));
        auto plantTree = [&](qint64 ck, int h, bool birch = false) -> bool
        {
            const int bx = keyFirst(ck), bz = keySecond(ck);
            // The crown (±2) must not stick out of the fill: otherwise the chunk will be written without ground
            // ("GROUND FAIL" in verify, holes along the chunk grid in the game).
            for (int sx = -2; sx <= 2; sx += 4)
                for (int sz = -2; sz <= 2; sz += 4)
                    if (!fillChunkSet.contains(pairKey((bx + sx) >> 4, (bz + sz) >> 4)))
                        return false;
            const int y0 = surfaceTop(ck) + 1;
            if (y0 + h + 3 > 255) return false;
            for (int dy = 0; dy < h; ++dy)
                if (store.get(bx, y0 + dy, bz) != IdAir) return false;    // not a single foreign stump in the trunk
            if ((g_zone == Climate::Desert || g_zone == Climate::SemiArid) && colHash(ck ^ 0x7C31ull) % 100u < (g_zone == Climate::Desert ? 45u : 30u))   // cacti dot the desert and semi-arid open lands (the trees there stay rare)
            {
                const int hc = 1 + int(colHash(ck ^ 0x40A5ull) % 3u);               // 1..3 blocks tall (as in the game)
                if (y0 + hc <= 254)
                    for (int dy = 0; dy < hc; ++dy)
                        store.set(bx, y0 + dy, bz, quint16(IdPaletteStart + paletteIndexOf("minecraft:cactus")));
                return true;
            }
            if ((g_zone == Climate::Desert || g_zone == Climate::SemiArid) && colHash(ck ^ 0x9E77ull) % 100u < 20u)   // dry bushes speckle the same open lands between the cacti
            {
                store.set(bx, y0, bz, quint16(IdPaletteStart + paletteIndexOf("minecraft:dead_bush")));
                return true;
            }
            if (zoneSkip(bx, bz)) return true;
            if (zt.jungle)
            {   // big jungle trees grow on the empty lands too: every fifth tree (busyNear already
                // cleared a 7×7 buffer around the trunk, plantJungleBig checks the crown chunks itself)
                const quint32 hj = colHash(ck ^ 0x3ADE11ull);
                if (hj % 5u == 0u && plantJungleBig(bx, bz, y0, hj)) return true;
            }
            if (g_zone == Climate::Tundra && colHash(ck ^ 0x51A7ull) % 100u < 50u)   // the tundra: half of the empty-land trees are spruces, the rest are birches
            {
                const int hs = 6 + int(colHash(ck ^ 0x2B19ull) % 6u);                // 6..11 — hardy short firs
                if (plantSpruce(bx, bz, y0, hs, hs >= 8 ? 3 : 2)) return true;       // no room for the cone — a birch grows then
            }
            if (zt.palmShare && colHash(ck ^ 0x49F1ull) % 100u < quint32(zt.palmShare))   // palm open lands (monsoon and semi-arid): the zone's share is palms, the rest are oaks
            {
                if (plantPalm(bx, bz, y0, colHash(ck ^ 0x27A9ull))) return true;
            }
            const quint16 tLog = birch ? zt.birchLog : zt.oakLog, tLeaf = birch ? zt.birchLeaf : zt.oakLeaf;
            for (int dy = 0; dy < h; ++dy)
                store.set(bx, y0 + dy, bz, tLog);
            const int topTree = y0 + h - 1;                              // the "diamond" crown, only air
            for (int dy = -2; dy <= 1; ++dy)
            {
                const int r = (dy == 1) ? 1 : 2;
                for (int dx = -r; dx <= r; ++dx)
                    for (int dz = -r; dz <= r; ++dz)
                    {
                        if (dy == 1 && std::abs(dx) + std::abs(dz) > 1) continue;
                        if (dy != 1 && std::abs(dx) == 2 && std::abs(dz) == 2) continue;
                        if (store.get(bx + dx, topTree + dy, bz + dz) == IdAir)
                            store.set(bx + dx, topTree + dy, bz + dz, tLeaf);
                    }
            }
            if (zt.jungle) jungleCrownVines(bx, bz, topTree - 2, 2);   // the jungle trees of the fields/meadows — with vines too
            return true;
        };
        QVector<ColSpan> allForest;                                      // the whole forest — merged segments
        for (const VegSite& s : forests) allForest += s.spans;
        allForest = mergeSpans(allForest);
        auto busyNear = [&](qint64 ck, bool plantsToo)                   // a "built-up/covered" 7×7 square?
        {
            const int bx = keyFirst(ck), bz = keySecond(ck);
            for (int dz = -3; dz <= 3; ++dz)
                for (int dx = -3; dx <= 3; ++dx)
                {
                    const qint64 nk = pairKey(bx + dx, bz + dz);
                    if (colMin.contains(nk) || roadCols.contains(nk) || railColsAll.contains(nk)
                        || colWool.contains(nk) || colWater.contains(nk) || noTreePadCols.contains(nk) || spansContain(allForest, bx + dx, bz + dz)
                        || (plantsToo && colPlant.contains(nk)))
                        return true;
                }
            return false;
        };
        // — (1)+(3): a 32×32 grid over the whole fill (the noise world apron is also an "empty place").
        // The grid cells are derived from the chunk coordinates (a chunk covers a 32 grid in no more than 2×2 cells).
        // A 14×14 grid (was 24×24: three times more trees), ~48% of the cells are occupied, the point in a cell is random over the whole cell (a margin of 2): the density is 1.5 times higher than the former one (32×32, 3 of 4),
        // and the rows disappear; ~35% of the trees are birches.
        constexpr int kGrid = 14;
        auto fdivG = [](int v, int d) { return v >= 0 ? v / d : -((-v + d - 1) / d); };
        QSet<ColKey> cells32;
        cells32.reserve(qsizetype(fillChunkSet.size()) * 4);
        for (qint64 ck : fillChunks)
        {
            const int cx = keyFirst(ck), cz = keySecond(ck);
            const int x0 = fdivG(cx << 4, kGrid), x1 = fdivG((cx << 4) + 15, kGrid);   // the cells by X for this chunk
            const int z0 = fdivG(cz << 4, kGrid), z1 = fdivG((cz << 4) + 15, kGrid);
            cells32.insert(pairKey(x0, z0));
            cells32.insert(pairKey(x1, z0));
            cells32.insert(pairKey(x0, z1));
            cells32.insert(pairKey(x1, z1));
        }
        for (qint64 cj : cells32)
        {
            const quint32 hh = colHash(cj ^ 0x7EE5ull);
            if (hh % 100u >= 26u) continue;                                    // ~26% of the cells (was 63%, 48%, 36%: the single trees on empty lands were too frequent)
            const quint32 hp = colHash(cj ^ 0x51D7ull);
            const qint64 ck = pairKey(int(keyFirst(cj)) * kGrid + 2 + int(hp % quint32(kGrid - 4)),
                                      int(keySecond(cj)) * kGrid + 2 + int((hp >> 10) % quint32(kGrid - 4)));
            if (!inFill(ck) || !flat5(ck)) continue;
            const int h = 5 + int(colHash(ck ^ 0x116ull) % 3u);              // trunks 5..7
            const bool birch = (hp >> 20) % 100u < 35u;
            if (colPlant.value(ck, 0) == quint16(IdTallGrass))               // (3) tall grass
            {
                if (!busyNear(ck, false)) plantTree(ck, h, birch);
            }
            else if (!busyNear(ck, true)) plantTree(ck, h, birch);           // (1) clean terrain
        }
        // — (2): wheat fields, groups of 2-3 trees per 44×44 cell.
        auto fdiv = [](int v, int d) { return v >= 0 ? v / d : -((-v + d - 1) / d); };
        QSet<ColKey> cells44;
        for (auto it = colPlant.constBegin(); it != colPlant.constEnd(); ++it)
            if (it.value() == quint16(IdWheat))
                cells44.insert(pairKey(fdiv(keyFirst(it.key()), 44), fdiv(keySecond(it.key()), 44)));
        static const int kGroupOff[2][2] = { { 3, 2 }, { -3, 4 } };
        for (qint64 cj : cells44)
        {
            const quint32 hh = colHash(cj ^ 0x94D3ull);
            if (hh % 4u == 0u) continue;                                     // glades without a group
            const qint64 ck = pairKey(keyFirst(cj) * 44 + 22 + int(hh % 3u) - 1,
                                      keySecond(cj) * 44 + 22 + int((hh >> 2) % 3u) - 1);
            if (colPlant.value(ck, 0) != quint16(IdWheat) || !flat5(ck)
                || busyNear(ck, false))
                continue;
            const int n = 2 + int(hh >> 4) % 2;                              // 2-3 trees in a group
            int got = plantTree(ck, 5 + int(colHash(ck ^ 0x116ull) % 3u)) ? 1 : 0;
            for (int g = 0; g < 2 && got < n; ++g)
            {
                const qint64 mk = pairKey(keyFirst(ck) + kGroupOff[g][0], keySecond(ck) + kGroupOff[g][1]);
                if (colPlant.value(mk, 0) != quint16(IdWheat) || !flat5(mk)
                    || busyNear(mk, false))
                    continue;
                if (plantTree(mk, 5 + int(colHash(mk ^ 0x116ull) % 3u))) ++got;
            }
        }
    }

    // — trees around schools (birches, the birch_forest biome) and kindergartens (multicolored poplars, the dappled_forest biome): a 9×9 grid, ~35% of the cells (crowns do not form a solid canopy),
    // within kZoneR of the walls, not on structures/roads/water/forests/lawns (a buffer of ±3, like the other trees).
    {
        const QSet<ColKey> zoneFill(fillChunks.constBegin(), fillChunks.constEnd());
        QVector<ColSpan> forestAll;
        for (const VegSite& vs2 : forests) forestAll += vs2.spans;
        forestAll = mergeSpans(forestAll);
        auto busy = [&](qint64 ck)
        {
            const int bx = keyFirst(ck), bz = keySecond(ck);
            for (int dz = -3; dz <= 3; ++dz)
                for (int dx = -3; dx <= 3; ++dx)
                {
                    const qint64 nk = pairKey(bx + dx, bz + dz);
                    if (colMin.contains(nk) || roadCols.contains(nk) || railColsAll.contains(nk) || colWool.contains(nk) || colWater.contains(nk)
                        || colPlant.contains(nk) || spansContain(forestAll, bx + dx, bz + dz))
                        return true;
                }
            return false;
        };
        const quint16 birchLog = zt.birchLog, birchLeaf = zt.birchLeaf;   // the monsoon zone remaps the school birches into oaks
        const quint16 poplarLog = quint16(IdPaletteStart + paletteIndexOf("minecraft:poplar_log"));
        for (const IslandSite& is : islandSites)
        {
            if (is.kind == 2) continue;
            const int x0 = is.cx - is.rx - kZoneMax, x1 = is.cx + is.rx + kZoneMax, z0 = is.cz - is.rz - kZoneMax, z1 = is.cz + is.rz + kZoneMax;
            for (int gz = z0 / 9 - 1; gz <= z1 / 9 + 1; ++gz)
                for (int gx = x0 / 9 - 1; gx <= x1 / 9 + 1; ++gx)
                {
                    const quint32 hh = colHash(pairKey(gx, gz) ^ (is.kind ? 0xB1C4ull : 0xD4F9ull));
                    if (hh % 100u >= 35u) continue;
                    const int bx = gx * 9 + int((hh >> 8) % 7u), bz = gz * 9 + int((hh >> 14) % 7u);
                    const int dxo = std::max(0, std::abs(bx - is.cx) - is.rx), dzo = std::max(0, std::abs(bz - is.cz) - is.rz);
                    Q_UNUSED(dxo); Q_UNUSED(dzo);
                    if (!zoneInside(is, bx, bz, kZoneR)) continue;
                    const qint64 ck = pairKey(bx, bz);
                    if (busy(ck)) continue;
                    if (!zoneFill.contains(pairKey(bx >> 4, bz >> 4))) continue;
                    bool crownOk = true;
                    for (int sx2 = -3; sx2 <= 3 && crownOk; sx2 += 6)
                        for (int sz2 = -3; sz2 <= 3; sz2 += 6)
                            if (!zoneFill.contains(pairKey((bx + sx2) >> 4, (bz + sz2) >> 4))) { crownOk = false; break; }
                    if (!crownOk) continue;
                    const int y0 = surfaceTop(ck) + 1;
                    const int h = 5 + int(colHash(ck ^ 0x1B1Eull) % 5u);                       // trunk 5..9
                    if (y0 + h + 4 > 255) continue;
                    bool free = true;
                    for (int dy = 0; dy < h && free; ++dy) if (store.get(bx, y0 + dy, bz) != IdAir) free = false;
                    if (!free) continue;
                    const quint16 log = is.kind == 1 ? birchLog : poplarLog;
                    quint16 leaf = birchLeaf;
                    if (is.kind == 0)
                    {
                        static const quint16 kPop[3] = { IdRedPoplarLeaves, IdOrangePoplarLeaves, IdYellowPoplarLeaves };
                        leaf = kPop[(hh >> 20) % 3u];                                          // every tree has its own color
                    }
                    for (int dy = 0; dy < h; ++dy) store.set(bx, y0 + dy, bz, log);
                    const int topTree = y0 + h - 1;
                    for (int dy = -2; dy <= 1; ++dy)
                    {
                    const int r = (dy == 1) ? 1 : (is.kind == 0 && dy <= -1 ? 3 : 2);     // poplar: a wider crown
                        for (int dx = -r; dx <= r; ++dx)
                            for (int dz = -r; dz <= r; ++dz)
                            {
                                if (dy == 1 && std::abs(dx) + std::abs(dz) > 1) continue;
                                if (dy != 1 && std::abs(dx) == r && std::abs(dz) == r && r >= 2) continue;
                                if (store.get(bx + dx, topTree + dy, bz + dz) == IdAir)
                                    store.set(bx + dx, topTree + dy, bz + dz, leaf);
                            }
                    }
                }
        }
    }

    // FLORA AND FAUNA OF FOREST BIOMES (as in the game): by 4×4 biome cells — ferns, flowers, berry bushes (taiga), leaf litter (dappled forest),
    // flower forest — dense flowers; animals in groups (wolves/foxes/rabbits in the taiga, sheep/pigs/chickens/cows in the others, bees among flowers).
    // Not on roads/sites/water/lawns and only where it is free above the ground.
    {
        const quint16 fern = quint16(IdPaletteStart + paletteIndexOf("minecraft:fern"));
        const quint16 berry = quint16(IdPaletteStart + paletteIndexOf("minecraft:sweet_berry_bush"));
        const quint16 brownM = quint16(IdPaletteStart + paletteIndexOf("minecraft:brown_mushroom"));
        const quint16 redM = quint16(IdPaletteStart + paletteIndexOf("minecraft:red_mushroom"));
        static const char* const kAnimTaiga[] = { "wolf", "wolf", "wolf", "fox", "fox", "rabbit", "rabbit", "sheep", "pig", "chicken" };
        static const char* const kAnimForest[] = { "sheep", "sheep", "pig", "pig", "chicken", "chicken", "cow", "cow", "rabbit", "fox", "wolf" };
        static const char* const kAnimDappled[] = { "fox", "fox", "sheep", "pig", "chicken", "cow", "rabbit", "rabbit" };
        static const char* const kAnimFlower[] = { "rabbit", "rabbit", "rabbit", "sheep", "pig", "cow", "chicken", "bee", "bee" };
        int nAnimals = 0;
        QVector<qint64> cellKeys;
        cellKeys.reserve(biomeCells.size());
        for (auto bit = biomeCells.constBegin(); bit != biomeCells.constEnd(); ++bit)
            if (bit.value() != 1 && bit.value() != 7) cellKeys.append(bit.key());   // the Pale Garden (1) is already carried over from the donor; the meadow (7) — without animals
        if (g_zone == Climate::Steppe || g_zone == Climate::Mediterranean || g_zone == Climate::Desert || g_zone == Climate::Savanna || g_zone == Climate::SemiArid || g_zone == Climate::Tundra)
            cellKeys.clear();                                                       // ferns, berry bushes and "forest" animals — not for dry and cold zones
        std::sort(cellKeys.begin(), cellKeys.end());
        for (qint64 ckc : cellKeys)
        {
            const int v = biomeCells.value(ckc);
            const int x4 = keyFirst(ckc), z4 = keySecond(ckc);
            for (int lz = 0; lz < 4; ++lz)
                for (int lx = 0; lx < 4; ++lx)
                {
                    const int x = x4 * 4 + lx, z = z4 * 4 + lz;
                    const qint64 ck = pairKey(x, z);
                    const quint32 hq = colHash(ck ^ 0xF10A5ull);
                    const int r = int(hq % 1000u);
                    quint16 put = 0;
                    if (v == 2)                                                   // taiga: ferns, berry bushes, mushrooms
                    {
                        if (r < 80) put = fern; else if (r < 98) put = berry; else if (r < 104) put = (hq >> 12) & 1u ? brownM : redM;
                        else if (r < 108) put = quint16(IdFlowerBase + int((hq >> 14) % 2u ? 1 : 0));
                    }
                    else if (v == 3)                                              // forest: ferns, flowers at the edges, mushrooms
                    {
                        if (r < 40) put = fern; else if (r < 55) put = quint16(IdFlowerBase + int((hq >> 14) % 2u)); else if (r < 59) put = (hq >> 12) & 1u ? brownM : redM;
                    }
                    else if (v == 4)                                              // birch forest: rare flowers, ferns
                    {
                        if (r < 25) put = fern; else if (r < 55) put = quint16(IdFlowerBase + int((hq >> 14) % 4u));
                    }
                    else if (v == 5)                                              // dappled forest: leaf litter, mushrooms, ferns
                    {
                        if (r < 220) put = IdLeafLitter; else if (r < 240) put = fern; else if (r < 246) put = (hq >> 12) & 1u ? brownM : redM;
                    }
                    else if (v == 6)                                              // flower forest: dense flowers of all kinds
                    {
                        if (r < 300) put = quint16(IdFlowerBase + int((hq >> 14) % quint32(kFlowersCount))); else if (r < 320) put = fern;
                    }
                    if (!put) continue;                                           // a plant appears rarely — the expensive occupancy checks are made only for them
                    if (colMin.contains(ck) || colWool.contains(ck) || colWater.contains(ck) || roadCols.contains(ck) || railColsAll.contains(ck) || patchCols.contains(ck)) continue;
                    const auto git = ground.constFind(pairKey(x >> 4, z >> 4));
                    if (git == ground.constEnd()) continue;
                    const int col = (z & 15) * 16 + (x & 15);
                    if (git->wd[col] > 0 || git->surf[col] != quint16(IdGrass)) continue;
                    const int top = git->top[col];
                    if (store.get(x, top + 1, z) != IdAir) continue;
                    store.set(x, top + 1, z, put);
                }
            // animals: per 4×4 cell ~2% chance of a group (on average one brood per ~800 m²); the cap per export — 700 animals
            const quint32 hc = colHash(ckc ^ 0xA21E5ull);
            if (nAnimals >= 700 || hc % 1000u >= 22u) continue;
            const char* sp = v == 2 ? kAnimTaiga[(hc >> 10) % 10u] : v == 5 ? kAnimDappled[(hc >> 10) % 8u] : v == 6 ? kAnimFlower[(hc >> 10) % 9u] : kAnimForest[(hc >> 10) % 11u];
            const int grp = (QLatin1String(sp) == QLatin1String("wolf")) ? 3 : (QLatin1String(sp) == QLatin1String("bee") ? 2 : 1 + int((hc >> 20) % 3u));
            for (int g = 0; g < grp; ++g)
            {
                const int x = x4 * 4 + int((hc >> (4 * g)) % 4u), z = z4 * 4 + int((hc >> (4 * g + 2)) % 4u);
                const qint64 ck = pairKey(x, z);
                if (colMin.contains(ck) || colWool.contains(ck) || colWater.contains(ck) || roadCols.contains(ck)) continue;
                const auto git = ground.constFind(pairKey(x >> 4, z >> 4));
                if (git == ground.constEnd() || git->wd[(z & 15) * 16 + (x & 15)] > 0) continue;
                const int top = git->top[(z & 15) * 16 + (x & 15)];
                if (store.get(x, top + 1, z) != IdAir || store.get(x, top + 2, z) != IdAir) continue;
                NpcSite a;
                a.kind = 5; a.animal = QStringLiteral("minecraft:") + QLatin1String(sp);
                a.x = x; a.z = z; a.y = top + 1 + (QLatin1String(sp) == QLatin1String("bee") ? 2 : 0); a.yaw = int((hc >> 24) % 4u) * 90;
                npcs.append(a);
                ++nAnimals;
            }
        }
        if (nAnimals > 0) res.lairInfo += QObject::tr("; animals in forests: %1").arg(nAnimals);
    }

    // 3.586 AQUATIC FAUNA: fish live in the water as REAL entities (like the animals in the forest), they are not spawner-born.
    // Lakes and areal rivers (hdrlin lines, pools, canals and ditches are not collected) hold fish only where the body really
    // has spots deeper than 3 blocks after the smoothing — a puddle stays empty. Species by the climate zone; squid add to the
    // largest bodies (from ~0.3 km²), axolotls add to bodies next to parks. The count grows with the area, capped per body.
    if (!waterBodies.isEmpty())
    {
        // the park columns with a 64-block margin (chunk cells) — a lake near a park gets axolotls
        QSet<ColKey> parkCells;
        for (const auto& fs : forests)
            if (fs.park)
                for (int zc = (fs.minZ >> 4) - 4; zc <= ((fs.maxZ >> 4) + 4); ++zc)
                    for (int xc = (fs.minX >> 4) - 4; xc <= ((fs.maxX >> 4) + 4); ++xc)
                        parkCells.insert(pairKey(xc, zc));
        const bool coldZone = (g_zone == Climate::Tundra || g_zone == Climate::Boreal);
        const bool warmZone = (g_zone == Climate::Desert || g_zone == Climate::Savanna || g_zone == Climate::Tropical
                               || g_zone == Climate::Monsoon || g_zone == Climate::SemiArid);
        int nFauna = 0;
        for (const WaterBodyRec& wb : waterBodies)
        {
            if (wb.d <= 3 || nFauna >= 600) continue;                     // shallower than 3 blocks by the contour — a puddle
            // the samples that stayed deeper than 3 blocks after the smoothing, and the park proximity
            QVector<qint64> deep; deep.reserve(64);
            bool parkNear = false;
            for (const qint64 ck : wb.samples)
            {
                const int x = keyFirst(ck), z = keySecond(ck);
                if (!parkNear && parkCells.contains(pairKey(x >> 4, z >> 4))) parkNear = true;
                if (deep.size() >= 64) continue;
                const auto git = ground.constFind(pairKey(x >> 4, z >> 4));
                if (git != ground.constEnd() && git->wd[(z & 15) * 16 + (x & 15)] > 3) deep.append(ck);
            }
            if (deep.isEmpty()) continue;                                 // the smoothing made the whole body shallow — a puddle after all
            const quint32 h0 = colHash(wb.seed);
            const char* sp; int fishVariant = 0;
            if (coldZone) sp = "minecraft:salmon";                                            // cold waters — salmon
            else if (warmZone && (h0 % 100u) >= 15u) { sp = "minecraft:tropical_fish"; fishVariant = int(h0 & 0xffff); }
            else if (warmZone) sp = "minecraft:pufferfish";                                   // warm waters — a tropical fish school, a pufferfish is rare
            else sp = "minecraft:cod";                                                        // the rest — cod
            // ~1 fish per 2000 m², at least two in a body, at most 12 (a cap so that a reservoir does not explode the entities file)
            const int nFish = std::min(12, std::max(2, int(wb.nCols / 2000) + 1));
            for (int k = 0; k < nFish && nFauna < 600; ++k)
            {
                const quint32 h = colHash(qint64(quint64(wb.seed) ^ (quint64(k) * 0x9E3779B97F4A7C15ull)));
                const qint64 ck = deep[h % quint32(deep.size())];
                const int x = keyFirst(ck), z = keySecond(ck);
                const auto git = ground.constFind(pairKey(x >> 4, z >> 4));
                if (git == ground.constEnd()) continue;
                const int wd = git->wd[(z & 15) * 16 + (x & 15)];
                if (wd <= 3) continue;
                NpcSite f; f.kind = 5; f.animal = QLatin1String(sp); f.variant = fishVariant;
                f.x = x; f.z = z; f.y = git->top[(z & 15) * 16 + (x & 15)] - wd;              // the bottom of the water body
                f.yoff = std::min(3.0, double(wd) - 1.0);                                     // in the water, closer to the bottom
                f.yaw = int((h >> 20) % 4u) * 90;
                npcs.append(f); ++nFauna;
            }
            // squid — only in quite large lakes and big rivers
            const int nSquid = wb.nCols >= 1500000 ? 4 : (wb.nCols >= 300000 ? 2 : 0);
            for (int k = 0; k < nSquid && nFauna < 600; ++k)
            {
                const quint32 h = colHash(qint64(quint64(wb.seed) ^ (quint64(1000 + k) * 0x9E3779B97F4A7C15ull)));
                const qint64 ck = deep[h % quint32(deep.size())];
                const int x = keyFirst(ck), z = keySecond(ck);
                const auto git = ground.constFind(pairKey(x >> 4, z >> 4));
                if (git == ground.constEnd()) continue;
                const int wd = git->wd[(z & 15) * 16 + (x & 15)];
                if (wd <= 3) continue;
                NpcSite f; f.kind = 5; f.animal = QLatin1String("minecraft:squid");
                f.x = x; f.z = z; f.y = git->top[(z & 15) * 16 + (x & 15)] - wd;
                f.yoff = std::min(3.0, double(wd) - 1.0);
                npcs.append(f); ++nFauna;
            }
            // axolotls — a lake near a park (they fight with fish in the game — the balance is the game's own business)
            if (parkNear) for (int k = 0; k < 2 && nFauna < 600; ++k)
            {
                const quint32 h = colHash(qint64(quint64(wb.seed) ^ (quint64(2000 + k) * 0x9E3779B97F4A7C15ull)));
                const qint64 ck = deep[h % quint32(deep.size())];
                const int x = keyFirst(ck), z = keySecond(ck);
                const auto git = ground.constFind(pairKey(x >> 4, z >> 4));
                if (git == ground.constEnd()) continue;
                const int wd = git->wd[(z & 15) * 16 + (x & 15)];
                if (wd <= 3) continue;
                NpcSite f; f.kind = 5; f.animal = QLatin1String("minecraft:axolotl"); f.variant = int(h >> 8);
                f.x = x; f.z = z; f.y = git->top[(z & 15) * 16 + (x & 15)] - wd;
                f.yoff = std::min(3.0, double(wd) - 1.0);
                npcs.append(f); ++nFauna;
            }
        }
        if (nFauna > 0) res.lairInfo += QObject::tr("; aquatic fauna: %1").arg(nFauna);
    }

    for (auto pit = colPlant.constBegin(); pit != colPlant.constEnd(); ++pit)
    {
        const qint64 ck = pit.key();
        const quint16 pid = pit.value();
        // A wheat field — a dense planting in every column (so that it reads as a field, not as sparse
        // bushes); the other grass — a dense lawn, but not solid.
        if (pid != quint16(IdWheat) && colHash(ck) % 2u != 0u) continue;
        if (colWater.contains(ck)) continue;                   // we do not grow on water
        if (colWool.contains(ck) || roadCols.contains(ck)) continue;   // on roads, parking lots and sites (concrete/wool) grass and flowers do not grow
        const int bx = keyFirst(ck), bz = keySecond(ck);
        {   // grass does not grow on a sandy shore
            const auto gps = ground.constFind(pairKey(bx >> 4, bz >> 4));
            if (gps != ground.constEnd() && g_sandId && gps->surf[(bz & 15) * 16 + (bx & 15)] == g_sandId) continue;
        }
        const int y = surfaceTop(ck) + 1;
        if (y > 254) continue;
        // Flowers on grass polygons "without height" (user request): about 1 column
        // out of 24 instead of grass, a random kind. Wheat fields — skipped (there is a crop already).
        if (pid == quint16(IdGrassPlant) || pid == quint16(IdTallGrass))
        {
            const quint32 hf = colHash(ck ^ 0xBEEF1ull);
            if (hf % 24u == 0u)
            {
                if (store.get(bx, y, bz) == IdAir)
                    store.set(bx, y, bz,
                              quint16(IdFlowerBase + int((hf >> 8) % quint32(kFlowersCount))));
                continue;
            }
        }
        if (pid == quint16(IdTallGrass))
        {   // the grown grass is two blocks tall: without the top half the game will break the bottom one on update
            if (store.get(bx, y, bz) == IdAir && store.get(bx, y + 1, bz) == IdAir)
            {
                store.set(bx, y, bz, IdTallGrass);
                store.set(bx, y + 1, bz, IdTallGrassUpper);
            }
        }
        else if (store.get(bx, y, bz) == IdAir)
        {
            if (pid == quint16(IdWheat))
            {   // wheat grows on plowed soil, not on turf; on special tiles (the island lamps) we do not plant
                const auto gp2 = ground.find(pairKey(bx >> 4, bz >> 4));
                if (gp2 != ground.end())
                {
                    quint16& sf = gp2->surf[(bz & 15) * 16 + (bx & 15)];
                    if (sf != g_zoneSurface) continue;
                    sf = IdFarmland;
                }
            }
            store.set(bx, y, bz, pid);
        }
    }

    lap("before 3.55");
    beginStep(6);
    // 3.55 "Forest treasures" (user requests): in random places of forest spots (~1/384 of the columns)
    // a SINGLE chest stands on the ground (double ones — only urban), NOT CLOSER THAN 30 BLOCKS to each
    // other inside a polygon (the radius by the Euclidean metric). We place them BEFORE the grass is scattered — the grass
    // will not climb into the chest.
    {
        int fsCols = 0;
        for (int fiC = 0; fiC < forests.size(); ++fiC)
        {
            const VegSite& site = forests[fiC];
            const bool paleF = (fiC == paleIdx);                // Pale Garden: about 2.5 times more chests (a grid of 12, radius 18) and often expensive
            const int tRad = paleF ? 18 : 30;                   // the mutual distance of the treasures inside one polygon
            QHash<qint64, QVector<QPoint>> near;                // the treasures of THIS polygon bucketed by
            auto clash = [&](int bx, int bz)                    //   the radius: the old linear scan of ALL
            {   // 3x3 buckets around the candidate, the exact metric inside them (a forest of tens of
                //   thousands of treasures made that scan the slowest loop of the whole export)
                for (int ax = -1; ax <= 1; ++ax)
                    for (int az = -1; az <= 1; ++az)
                    {
                        const auto it = near.constFind(pairKey(bx / tRad + ax, bz / tRad + az));
                        if (it == near.constEnd()) continue;
                        for (const QPoint& p : *it)
                        {
                            const long long dx = p.x() - bx, dz = p.y() - bz;
                            if (dx * dx + dz * dz < tRad * tRad) return true;
                        }
                    }
                return false;
            };
            // a 20×20 grid (~1/400 of the columns, like the former 1/384): one hash-shifted place per cell
            const int G = paleF ? 12 : 20;
            // the grid is clamped to the export rectangle (a forest polygon can be many times wider than the world)
            const int vgX0 = std::max(site.minX, (rcx0 - rectM) * 16), vgX1 = std::min(site.maxX, (rcx1 + rectM + 1) * 16 - 1);
            const int vgZ0 = std::max(site.minZ, (rcz0 - rectM) * 16), vgZ1 = std::min(site.maxZ, (rcz1 + rectM + 1) * 16 - 1);
            const int gx0 = int(std::floor(double(vgX0) / G)), gx1 = int(std::floor(double(vgX1) / G));
            const int gz0 = int(std::floor(double(vgZ0) / G)), gz1 = int(std::floor(double(vgZ1) / G));
            for (int gx = gx0; gx <= gx1; ++gx)
            for (int gz = gz0; gz <= gz1; ++gz)
            {
                if ((++fsCols & 0x3FF) == 0 && cancel && cancel->load())
                {
                    res.cancelled = true;
                    res.error = QObject::tr("Export cancelled.");
                    return res;
                }
                const quint32 hc = colHash(pairKey(gx, gz) ^ 0xC0FFEEull);
                const int tx = gx * G + int(hc % G), tz = gz * G + int((hc >> 8) % G);
                if (tx < site.minX || tx > site.maxX || tz < site.minZ || tz > site.maxZ) continue;
                if (!spansContain(site.spans, tx, tz)) continue;
                if (!ground.contains(pairKey(tx >> 4, tz >> 4))) continue;    // outside the written ground — there is no surface to stand a treasure on
                const qint64 ck = pairKey(tx, tz);
                if (colWater.contains(ck) || colMin.contains(ck)) continue;   // on water/under buildings — skipped
                const int bx = keyFirst(ck), bz = keySecond(ck);
                if (clash(bx, bz)) continue;                                  // a treasure stands closer than the radius
                const int y = surfaceTop(ck) + 1;
                if (y > 254) continue;
                if (paleF && patchCols.contains(ck)) continue;                // not inside the mansion / a patch
                {
                    const quint16 here = store.get(bx, y, bz);
                    if (paleF && (here == IdPaleMossCarpet || here == IdLeafLitter || here == IdGrassPlant || here == IdTallGrass))
                        store.erase(bx, y, bz);                                   // the chest displaces the forest litter
                    else if (here != IdAir) continue;                             // the place is already taken by a tree/planting
                }
                store.set(bx, y, bz, IdChest);
                if (paleF)
                {   // Pale Garden: 35% expensive (mansion/pyramid/outpost/sunken treasure), 25% medium, the rest everyday
                    const quint32 hq = colHash(ck ^ 0x51ull);
                    const int rich = kChestLootCount - 3;                         // woodland_mansion
                    int tbl;
                    if ((hq >> 8) % 100u < 12u) tbl = rich;                       // every eighth treasure — as in the mansion
                    else tbl = pickChestLoot(hq, 40, 25);
                    chests.append(ChestSite{ bx, y, bz, tbl });
                }
                else
                    chests.append(ChestSite{ bx, y, bz, pickChestLoot(colHash(ck ^ 0x51ull), 85, 15) });   // treasures in forests — without the expensive ones
                near[pairKey(bx / tRad, bz / tRad)].append(QPoint(bx, bz));
            }
        }
    }

    lap("before 3.57");
    beginStep(7);
    // 3.57 Street furniture (user requests): wooden fences around csrpol sites (with no
    // fewer than two gates and solid outer corners) and lamp posts along roads —
    // a village composition "plinth + post + lantern", but taller than the game analogs. Everything stands on the
    // ground by surfaceTop (already with the noise lift taken into account).
    {
        auto gateIdFor = [](qint64 ck, const QSet<ColKey>& pad) -> quint16
        {   // the gate faces "away from the spot" (outward)
            const int x2 = keyFirst(ck), z2 = keySecond(ck);
            if (pad.contains(pairKey(x2 - 1, z2))) return IdGateE;
            if (pad.contains(pairKey(x2 + 1, z2))) return IdGateW;
            if (pad.contains(pairKey(x2, z2 - 1))) return IdGateS;
            return IdGateN;
        };
        static const int dxs[4] = { 1, -1, 0, 0 };
        static const int dzs[4] = { 0, 0, 1, -1 };
        // — site fences: a ring of free columns right around the spot, two gates roughly
        //   opposite each other (deterministically by the hash). The ring is built by FLARING over 8 neighbors:
        //   along the edges the result is the same row, and on the OUTER CORNERS diagonal cells appear —
        //   without them the fence diverged at the corners and the posts did not connect.
        int nSportEq = 0;
        // columns covered by a court of ANY pad (markings, goals and a 2-block margin). Sports plots often
        // lie inside a bigger csrpol plot (a stadium with its own code): without the global set the outer
        // plot filled its free area with benches right through the pitch lines of the inner one
        QSet<ColKey> courtCols;
        // the smallest plots go first: a pitch with its own court markings must be painted before the bigger
        // plot that contains it fills the free area with benches (the court zone is only known after its build)
        QVector<int> padOrder(pads.size());
        for (int i = 0; i < pads.size(); ++i) padOrder[i] = i;
        std::stable_sort(padOrder.begin(), padOrder.end(),
                         [this](int a, int b) { return pads[a].size() < pads[b].size(); });
        for (int padIdx : padOrder)
        {
            const QList<qint64>& padCols = pads[padIdx];
            const QSet<ColKey> pad(padCols.constBegin(), padCols.constEnd());
            if (padCode.value(padIdx, 0) == 71323000 && !padIsLake.value(padIdx, 0))
            {   // GREENHOUSE (hothouse, orangery): a plank frame with glass (walls 4 blocks, a flat glass roof with beams), inside real beds — rows of beds (farmland) with
                // plantings (wheat, carrots, potatoes, beetroot), between pairs of rows irrigation ditches with water, aisles, lamps under the roof; the entrance — an opening of 3 cells
                int gx0 = std::numeric_limits<int>::max(), gx1 = std::numeric_limits<int>::min(), gz0 = gx0, gz1 = gx1;
                for (qint64 ck : padCols)
                {
                    gx0 = std::min(gx0, keyFirst(ck)); gx1 = std::max(gx1, keyFirst(ck));
                    gz0 = std::min(gz0, keySecond(ck)); gz1 = std::max(gz1, keySecond(ck));
                }
                const bool rowsAlongX = (gx1 - gx0) >= (gz1 - gz0);                   // the bed rows run along the long axis
                auto isBoundary = [&](int x, int z)
                {
                    return !pad.contains(pairKey(x + 1, z)) || !pad.contains(pairKey(x - 1, z)) || !pad.contains(pairKey(x, z + 1)) || !pad.contains(pairKey(x, z - 1));
                };
                QVector<qint64> bnd;
                for (qint64 ck : padCols) if (isBoundary(keyFirst(ck), keySecond(ck))) bnd << ck;
                std::sort(bnd.begin(), bnd.end());
                QPoint gate(1 << 28, 1 << 28);
                if (!bnd.isEmpty())
                {
                    const qint64 gk = bnd[int(colHash(bnd.first() ^ 0x6A7Eull) % quint32(bnd.size()))];
                    gate = QPoint(keyFirst(gk), keySecond(gk));
                }
                const quint16 crops[4] = { IdWheat, IdCarrots, IdPotatoes, IdBeetroots };
                int nBed = 0, dbgNoGround = 0, dbgInterior = 0;
                for (qint64 ck : padCols)
                {
                    const int x = keyFirst(ck), z = keySecond(ck);
                    if (colMin.contains(ck) || colWater.contains(ck)) continue;
                    const int y0 = surfaceTop(ck) + 1;
                    if (y0 + 6 > 250) continue;
                    const bool boundary = isBoundary(x, z);
                    if (boundary)
                    {
                        const bool inGate = std::abs(x - gate.x()) <= 1 && std::abs(z - gate.y()) <= 1;
                        int free4 = 0;
                        if (!pad.contains(pairKey(x + 1, z))) ++free4;
                        if (!pad.contains(pairKey(x - 1, z))) ++free4;
                        if (!pad.contains(pairKey(x, z + 1))) ++free4;
                        if (!pad.contains(pairKey(x, z - 1))) ++free4;
                        const bool post = free4 >= 2 || ((x + z) & 3) == 0;
                        for (int dy = 0; dy < 4; ++dy)
                        {
                            if (inGate && dy < 3) continue;                              // the entrance opening 3 high
                            if (store.get(x, y0 + dy, z) != IdAir) continue;
                            store.set(x, y0 + dy, z, (post || dy == 0 || inGate) ? IdOakPlanks : IdGlass);
                        }
                        if (store.get(x, y0 + 4, z) == IdAir) store.set(x, y0 + 4, z, IdOakPlanks);
                        continue;
                    }
                    // the roof
                    if (store.get(x, y0 + 4, z) == IdAir) store.set(x, y0 + 4, z, (x & 3) == 0 ? IdOakPlanks : IdGlass);
                    // beds: a chain of rows along the axis, period 6: 0 — aisle, 1-2 — bed, 3 — a ditch with water, 4-5 — bed
                    const int r = rowsAlongX ? (z - gz0) : (x - gx0);
                    const int c = rowsAlongX ? (x - gx0) : (z - gz0);
                    const int ph = ((r % 6) + 6) % 6;
                    const bool nearWall = !pad.contains(pairKey(x + 1, z)) || !pad.contains(pairKey(x - 1, z)) || !pad.contains(pairKey(x, z + 1)) || !pad.contains(pairKey(x, z - 1))
                                          || !pad.contains(pairKey(x + 1, z + 1)) || !pad.contains(pairKey(x - 1, z - 1)) || !pad.contains(pairKey(x + 1, z - 1)) || !pad.contains(pairKey(x - 1, z + 1));
                    auto git = ground.find(pairKey(x >> 4, z >> 4));
                    if (git == ground.end()) { ++dbgNoGround; continue; }
                    ++dbgInterior;
                    const int col = (z & 15) * 16 + (x & 15);
                    const bool crossAisle = (c % 14) == 0 || (std::abs(x - gate.x()) <= 1 && std::abs(z - gate.y()) <= 3 && false);
                    if (nearWall || ph == 0 || crossAisle)
                    {   // aisle: trampled earth, under the roof a lamp every 8 cells
                        if (git->wd[col] == 0) git->surf[col] = IdDirtPath;
                        if (ph == 0 && (c % 8) == 4 && store.get(x, y0 + 3, z) == IdAir) store.set(x, y0 + 3, z, IdLanternHung);
                        continue;
                    }
                    if (ph == 3)
                    {   // irrigation ditch: water flush with the beds
                        if (git->wd[col] == 0)
                        {
                            git->wd[col] = 1;
                            if (git->art.isEmpty()) git->art.fill(0, 256);
                            git->art[col] = 1;
                        }
                        continue;
                    }
                    git->surf[col] = IdFarmland;
                    const quint16 crop = crops[colHash(pairKey(r / 3, c / 18) ^ padCols.first() ^ 0x6C0Full) % 4u];
                    if (store.get(x, y0, z) == IdAir) { store.set(x, y0, z, crop); ++nBed; }
                }
                nSportEq += nBed > 0 ? 1 : 0;
                if (qEnvironmentVariableIsSet("MCX_LAIRDBG")) std::fprintf(stderr, "[greenhouse] cols=%d beds=%d interior=%d noGround=%d bbox x %d..%d z %d..%d\n", int(padCols.size()), nBed, dbgInterior, dbgNoGround, gx0, gx1, gz0, gz1);
                continue;
            }
            QVector<qint64> ring;
            for (qint64 ck : padCols)
            {
                const int bx = keyFirst(ck), bz = keySecond(ck);
                for (int ddy = -1; ddy <= 1; ++ddy)
                    for (int ddx = -1; ddx <= 1; ++ddx)
                    {
                        if (!ddx && !ddy) continue;
                        const qint64 nk = pairKey(bx + ddx, bz + ddy);
                        if (pad.contains(nk) || colMin.contains(nk) || colWater.contains(nk))
                            continue;                       // the fence does not climb onto a road/structure/water
                        ring << nk;
                    }
            }
            std::sort(ring.begin(), ring.end());
            ring.erase(std::unique(ring.begin(), ring.end()), ring.end());
            if (ring.size() < 8) continue;              // a degenerate spot — not fenced
            auto freeAt = [&](int ri)
            {
                const qint64 ck = ring.at(ri);
                const int y = surfaceTop(ck) + 1;
                return y <= 254 && store.get(keyFirst(ck), y, keySecond(ck)) == IdAir;
            };
            auto findFree = [&](int start)
            {
                for (int s = 0; s < 8; ++s)
                {
                    const int ri = (start + s) % int(ring.size());
                    if (freeAt(ri)) return ri;
                }
                return -1;
            };
            const quint32 h0 = colHash(ring.first() ^ 0xFECEull);
            const int gateA = findFree(int(h0 % quint32(ring.size())));
            const int gateB = findFree((gateA >= 0 ? gateA : 0) + int(ring.size()) / 2
                                       + int(h0 >> 8) % 3);
            const bool militaryPad = (padCode.value(padIdx, 0) == 53210000);
            const quint16 milBrick = quint16(IdPaletteStart + paletteIndexOf("minecraft:stone_bricks"));
            const quint16 milMossy = quint16(IdPaletteStart + paletteIndexOf("minecraft:mossy_stone_bricks"));
            for (int ri = 0; ri < int(ring.size()); ++ri)
            {
                if (!freeAt(ri)) continue;              // occupied (a fence of a neighboring spot/a post/a planting)
                const qint64 ck = ring.at(ri);
                const int x = keyFirst(ck), z = keySecond(ck);
                const int y = surfaceTop(ck) + 1;
                if (militaryPad)
                {   // a military ground (landuse=military): a stone boundary wall two rows high with
                    // brick pillars every few cells and two open gateways; guards patrol inside
                    if (ri == gateA || ri == gateB) continue;                    // the gateway is left open
                    const quint32 hm = colHash(ck ^ 0x4C177ull);
                    if (hm % 5u == 0u && store.get(x, y + 1, z) == IdAir && store.get(x, y + 2, z) == IdAir)
                    {   // a pillar: a brick column taller than the wall
                        store.set(x, y, z, milBrick);
                        store.set(x, y + 1, z, (hm >> 8) % 4u == 0u ? milMossy : milBrick);
                        store.set(x, y + 2, z, milBrick);
                    }
                    else
                    {
                        store.set(x, y, z, IdStoneBrickWall);
                        if (store.get(x, y + 1, z) == IdAir) store.set(x, y + 1, z, IdStoneBrickWall);
                    }
                    continue;
                }
                store.set(x, y, z, (ri == gateA || ri == gateB) ? gateIdFor(ck, pad) : IdFence);
            }
            // Iron golems: on a fenced site (except lakes) a 25% chance is counted TWICE (deterministically
            // by the spot hash) — a site has 0, 1 or 2 guards. They stand on free columns inside the spot closer to
            // the center of its bbox; the second one — not closer than 3 blocks to the first. A CHILDREN'S
            // playground gets baby villagers playing there instead of a guard (the same hash choice as the
            // equipment stage below makes).
            if (!padIsLake.value(padIdx, 0))
            {
                int gx0 = std::numeric_limits<int>::max(), gx1 = std::numeric_limits<int>::min();
                int gz0 = gx0, gz1 = gx1;
                for (qint64 ck : padCols)
                {
                    gx0 = std::min(gx0, keyFirst(ck)); gx1 = std::max(gx1, keyFirst(ck));
                    gz0 = std::min(gz0, keySecond(ck)); gz1 = std::max(gz1, keySecond(ck));
                }
                const double mx = 0.5 * (gx0 + gx1), mz = 0.5 * (gz0 + gz1);
                QVector<QPoint> golemAt;
                // the same deterministic choice the equipment stage below makes: CODE 53710000 is a
                // children's playground; a small mixed 53700000 site may become one by the pad hash
                const quint32 hpNow = colHash(ring.first() ^ 0x5B027ull);
                const bool kidsPad = padCode.value(padIdx, 0) == 53710000 ||
                    (padCode.value(padIdx, 0) == 53700000 && padCols.size() < 700 && hpNow % 100u < 30u);
                // a military ground is guarded for real: 2 golems up to 10 for a huge base, standing in a ring
                // around the center; other sites keep the old deterministic 25% chance of 0, 1 or 2 golems;
                // a children's playground gets a full gang of 4-6 kids instead, always (they came to play)
                const int wantKids = kidsPad ? 4 + int((hpNow >> 8) % 3u) : 0;
                const int wantGolems = militaryPad ? std::min(10, 2 + int(padCols.size()) / 100000)
                                                   : (kidsPad ? wantKids : 2);
                for (int roll = 0; roll < wantGolems; ++roll)
                {
                    if (!militaryPad && !kidsPad &&
                        colHash(ring.first() ^ (0x601E3ull + quint64(roll) * 0x9E37ull)) % 100u >= 25u)
                        continue;
                    const double fa = 6.283185307179586 * roll / std::max(1, wantGolems);
                    const bool spread = militaryPad || kidsPad;           // kids roam the whole plot too
                    const double fx = spread ? mx + 0.3 * (gx1 - gx0) * std::cos(fa) : mx;
                    const double fz = spread ? mz + 0.3 * (gz1 - gz0) * std::sin(fa) : mz;
                    qint64 best = 0; double bestD = 1e18; bool have = false;
                    for (qint64 ck : padCols)
                    {
                        const int y = surfaceTop(ck) + 1;
                        if (y > 253 || store.get(keyFirst(ck), y, keySecond(ck)) != IdAir) continue;
                        bool nearOther = false;
                        for (const QPoint& q : golemAt)
                            if (std::max(std::abs(q.x() - keyFirst(ck)), std::abs(q.y() - keySecond(ck))) < 3) nearOther = true;
                        if (nearOther) continue;
                        const double d = (keyFirst(ck) - fx) * (keyFirst(ck) - fx) + (keySecond(ck) - fz) * (keySecond(ck) - fz);
                        if (d < bestD) { bestD = d; best = ck; have = true; }
                    }
                    if (!have) continue;
                    NpcSite g;
                    if (kidsPad)
                    {   // children play on the playground instead of a guard (baby villagers)
                        g.kind = 0; g.baby = true; g.prof = PfNone; g.level = 1;
                    }
                    else
                        g.kind = 3;                                      // minecraft:iron_golem
                    g.x = keyFirst(best); g.z = keySecond(best);
                    g.y = surfaceTop(best) + 1;                          // surfaceTop already includes the yShift lift
                    g.yaw = int(colHash(best ^ 0x601Eull) % 4u) * 90;
                    npcs.append(g);
                    golemAt.append(QPoint(g.x, g.z));
                }
            }
            if (militaryPad) continue;      // the wall and the guards are enough; no courts, lawns or benches
            // SPORTS EQUIPMENT on sites (CODE 53700000 — sports, 53710000 — children's): by the size and shape of the site — a football pitch (goals, the center
            // line and circle in white wool), volleyball/tennis (a net on posts), basketball (two backboards with cauldron hoops), workout (pull-up bars, parallel bars, benches) or a children's
            // playground (swings, a sandbox, a carousel, benches). We place only on free columns inside the spot not closer than 1 block to the fence and with air above them.
            if (!padIsLake.value(padIdx, 0) && padCols.size() >= 90)
            {
                int bx0 = std::numeric_limits<int>::max(), bx1 = std::numeric_limits<int>::min(), bz0 = bx0, bz1 = bx1;
                for (qint64 ck : padCols)
                {
                    bx0 = std::min(bx0, keyFirst(ck)); bx1 = std::max(bx1, keyFirst(ck));
                    bz0 = std::min(bz0, keySecond(ck)); bz1 = std::max(bz1, keySecond(ck));
                }
                const int Lx = bx1 - bx0 + 1, Lz = bz1 - bz0 + 1;
                const bool alongX = Lx >= Lz;                                 // the long axis
                const int L = alongX ? Lx : Lz, Wd = alongX ? Lz : Lx;        // the length / width of the site
                const double fillRatio = double(padCols.size()) / double(std::max(1, Lx * Lz));   // 1 — a rectangle, less — a ragged outline
                const quint32 hp = colHash(ring.first() ^ 0x5B027ull);
                // the "u-axis" along the length, the "v-axis" across; to(u, v) -> (x, z)
                auto toXZ = [&](int u, int v) { return alongX ? QPoint(bx0 + u, bz0 + v) : QPoint(bx0 + v, bz0 + u); };
                auto inside = [&](int u, int v, int margin)
                {
                    const QPoint q = toXZ(u, v);
                    for (int dz2 = -margin; dz2 <= margin; ++dz2)
                        for (int dx2 = -margin; dx2 <= margin; ++dx2)
                            if (!pad.contains(pairKey(q.x() + dx2, q.y() + dz2))) return false;
                    return true;
                };
                int resX0 = 1, resX1 = 0, resZ0 = 1, resZ1 = 0;                  // the area occupied by the court (x, z): ordinary items are not placed there
                auto freeCol = [&](int u, int v, int h) -> bool
                {   // the column is inside (a 1-block margin) and there is air h blocks above the ground
                    if (!inside(u, v, 1)) return false;
                    const QPoint q = toXZ(u, v);
                    if (q.x() >= resX0 && q.x() <= resX1 && q.y() >= resZ0 && q.y() <= resZ1) return false;
                    if (courtCols.contains(pairKey(q.x(), q.y()))) return false;   // somebody else's court
                    const int y = surfaceTop(pairKey(q.x(), q.y())) + 1;
                    if (y + h > 253) return false;
                    for (int dy = 0; dy < h; ++dy) if (store.get(q.x(), y + dy, q.y()) != IdAir) return false;
                    return true;
                };
                auto put1 = [&](int u, int v, int dy, quint16 id)
                {
                    const QPoint q = toXZ(u, v);
                    store.set(q.x(), surfaceTop(pairKey(q.x(), q.y())) + 1 + dy, q.y(), id);
                };
                // a post (fence) of height h; false — did not fit
                auto post = [&](int u, int v, int h) -> bool
                {
                    if (!freeCol(u, v, h)) return false;
                    for (int dy = 0; dy < h; ++dy) put1(u, v, dy, IdFence);
                    return true;
                };
                int whiteWool = -1;
                for (int i = 0; i < kWoolsCount; ++i) if (qstrcmp(kWools[i].name, "minecraft:white_wool") == 0) { whiteWool = i; break; }
                auto paint = [&](int u, int v)
                {   // white marking: we change the top ground block in the column to white wool
                    if (whiteWool < 0 || !inside(u, v, 0)) return;
                    const QPoint q = toXZ(u, v);
                    auto git = ground.find(pairKey(q.x() >> 4, q.y() >> 4));
                    if (git != ground.end() && git->wd[(q.y() & 15) * 16 + (q.x() & 15)] == 0) git->surf[(q.y() & 15) * 16 + (q.x() & 15)] = quint16(IdWoolBase + whiteWool);
                };
                int nEq = 0;
                const int vc = Wd / 2;
                const int padKind = padCode.value(padIdx, 0);
                const bool sportPad = (padKind == 53700000 || padKind == 53710000);
                auto flowerAt = [&](int u, int v)
                {   // a flower on the grass (only in a free column, air above the ground)
                    if (!freeCol(u, v, 1)) return false;
                    const QPoint q = toXZ(u, v);
                    put1(u, v, 0, quint16(IdFlowerBase + int(colHash(pairKey(q.x(), q.y()) ^ 0xF10Eull) % quint32(kFlowersCount))));
                    return true;
                };
                if (!sportPad)
                {   // ——— other csrpol by codes ———
                    quint16 hayBale = IdHayBale;
                    switch (padKind)
                    {
                    case 53510000:
                    {   // CEMETERY: rows of graves every 3 blocks along and 4 across: a gravestone (a stone wall 2 blocks) or a cross (a fence), in front of it a mound of coarse dirt; rare flowers
                        const quint16 paveA = quint16(IdPaletteStart + paletteIndexOf("minecraft:gray_concrete"));
                        const quint16 paveB = quint16(IdPaletteStart + paletteIndexOf("minecraft:light_gray_concrete"));
                        auto pavedC = [&](int u, int v) -> bool
                        {   // a roadway, a paved square or water under the grave spot (a request: roads cross the cemetery);
                            // the surface row is checked too: paving can get there without leaving a mark in the maps
                            const QPoint q = toXZ(u, v);
                            const ColKey ck = pairKey(q.x(), q.y());
                            if (roadCols.contains(ck) || colWool.contains(ck) || colWater.contains(ck)) return true;
                            const auto git = ground.find(pairKey(q.x() >> 4, q.y() >> 4));
                            if (git != ground.end())
                            {
                                const quint16 s = git->surf[(q.y() & 15) * 16 + (q.x() & 15)];
                                if (s >= IdWoolBase && s < quint16(IdWoolBase + kWoolsCount)) return true;
                                if (s == paveA || s == paveB) return true;
                            }
                            return false;
                        };
                        for (int v = 2; v < Wd - 2; v += 4)
                            for (int u = 2; u < L - 2; u += 3)
                            {
                                const quint32 hq = colHash(pairKey(u * 31, v * 17) ^ ring.first() ^ 0xC3E7ull);
                                if (hq % 100u >= 70u) continue;
                                if (!(freeCol(u, v, 3) && freeCol(u, v + 1, 1) && freeCol(u, v + 2, 1))) continue;
                                if (pavedC(u, v) || pavedC(u, v + 1) || pavedC(u, v + 2)) continue;   // no grave across a road (a request)
                                if ((hq >> 8) % 100u < 55u)
                                {   // a gravestone
                                    put1(u, v, 0, IdStoneBrickWall);
                                    put1(u, v, 1, IdStoneBrickWall);
                                }
                                else
                                {   // a cross
                                    put1(u, v, 0, IdFence); put1(u, v, 1, IdFence); put1(u, v, 2, IdFence);
                                    if (freeCol(u - 1, v, 2) && freeCol(u + 1, v, 2)
                                        && !pavedC(u - 1, v) && !pavedC(u + 1, v)) { put1(u - 1, v, 1, IdFence); put1(u + 1, v, 1, IdFence); }
                                }
                                put1(u, v + 1, 0, IdCoarseDirt);                          // a grave mound
                                put1(u, v + 2, 0, IdCoarseDirt);
                                if ((hq >> 16) % 100u < 25u && !pavedC(u + 1, v + 1)) flowerAt(u + 1, v + 1);
                                ++nEq;
                            }
                        // benches at the edge
                        for (int k = 0; k < 2; ++k)
                        {
                            const int u0 = 1 + k * std::max(4, L - 5);
                            bool ok = true;
                            for (int u = u0; u < u0 + 3 && ok; ++u) ok = freeCol(u, 1, 1) && !pavedC(u, 1);
                            if (!ok) continue;
                            for (int u = u0; u < u0 + 3; ++u) put1(u, 1, 0, alongX ? IdStairsS : IdStairsE);
                            ++nEq;
                        }
                        break;
                    }
                    case 53530000:
                    {   // MONUMENT, MEMORIAL: a stone site, in the center an obelisk (a 6-block wall on a 3×3 stone brick base) with an eternal flame (a lantern), flowers and benches around
                        const int uc = L / 2, vm = Wd / 2;
                        bool ok = true;
                        for (int du = -1; du <= 1 && ok; ++du) for (int dv = -1; dv <= 1 && ok; ++dv) ok = freeCol(uc + du, vm + dv, 8);
                        if (ok)
                        {
                            const quint16 sb = quint16(IdPaletteStart + paletteIndexOf("minecraft:stone_bricks"));
                            for (int du = -1; du <= 1; ++du) for (int dv = -1; dv <= 1; ++dv) put1(uc + du, vm + dv, 0, sb);
                            for (int dy = 1; dy <= 6; ++dy) put1(uc, vm, dy, IdStoneBrickWall);
                            put1(uc, vm, 7, IdLanternStanding);
                            ++nEq;
                            for (int a = 0; a < 4; ++a)
                            {
                                const int du = (a & 1) ? 3 : -3, dv = (a & 2) ? 3 : -3;
                                if (freeCol(uc + du, vm + dv, 1)) { put1(uc + du, vm + dv, 0, quint16(IdFlowerPotBase + int(colHash(pairKey(a, uc) ^ 0x3E3ull) % quint32(kFlowersCount)))); ++nEq; }
                            }
                            for (int sd = -1; sd <= 1; sd += 2)
                            {
                                bool b2 = true;
                                for (int dv = -1; dv <= 1 && b2; ++dv) b2 = freeCol(uc + 5 * sd, vm + dv, 1);
                                if (!b2) continue;
                                for (int dv = -1; dv <= 1; ++dv) put1(uc + 5 * sd, vm + dv, 0, sd < 0 ? (alongX ? IdStairsE : IdStairsS) : (alongX ? IdStairsW : IdStairsN));
                                ++nEq;
                            }
                        }
                        break;
                    }
                    case 51220000:
                    {   // GAS STATION: a canopy on four supports (a 9×5 slab roof), under it two pumps (a blast furnace) with an indicator lantern; barrels (fuel) nearby
                        const int uc = L / 2, vm = Wd / 2;
                        const int du = std::min(4, std::max(2, L / 2 - 2)), dv = std::min(2, std::max(1, Wd / 2 - 2));
                        bool ok = true;
                        for (int a = -du; a <= du && ok; ++a) for (int b2 = -dv; b2 <= dv && ok; ++b2) ok = freeCol(uc + a, vm + b2, 5);
                        if (ok)
                        {
                            for (int a : { -du, du }) for (int b2 : { -dv, dv }) for (int dy = 0; dy < 4; ++dy) put1(uc + a, vm + b2, dy, IdFence);
                            for (int a = -du; a <= du; ++a) for (int b2 = -dv; b2 <= dv; ++b2) put1(uc + a, vm + b2, 4, IdSlabBottom);
                            for (int a : { -du / 2, du / 2 })
                            {
                                put1(uc + a, vm, 0, quint16(int(IdBlastFurnaceBase) + (alongX ? 0 : 1)));
                                put1(uc + a, vm, 1, IdLanternStanding);
                            }
                            ++nEq;
                            for (int a = -1; a <= 1; a += 2)
                                if (freeCol(uc + a, Wd - 2, 1)) { put1(uc + a, Wd - 2, 0, IdBarrel); ++nEq; }
                        }
                        break;
                    }
                    case 52400000:
                    {   // APIARY: hives on posts in a grid every 4 blocks, flowers around, bees near the hives
                        int nHive = 0;
                        for (int v = 2; v < Wd - 2; v += 4)
                            for (int u = 2; u < L - 2; u += 4)
                            {
                                const quint32 hq = colHash(pairKey(u * 29, v * 13) ^ ring.first() ^ 0xA9E7ull);
                                if (hq % 100u >= 65u || !freeCol(u, v, 2)) continue;
                                put1(u, v, 0, IdFence);
                                put1(u, v, 1, quint16(IdPaletteStart + paletteIndexOf("minecraft:beehive")));
                                ++nHive; ++nEq;
                                if (nHive <= 8)
                                {
                                    const QPoint q = toXZ(u, v);
                                    NpcSite bee;
                                    bee.kind = 5; bee.animal = QStringLiteral("minecraft:bee");
                                    bee.x = q.x(); bee.z = q.y(); bee.y = surfaceTop(pairKey(q.x(), q.y())) + 3;
                                    bee.yaw = int((hq >> 10) % 4u) * 90;
                                    npcs.append(bee);
                                }
                            }
                        for (int v = 1; v < Wd - 1; ++v)
                            for (int u = 1; u < L - 1; ++u)
                                if (colHash(pairKey(u * 7 + 1, v * 11 + 3) ^ ring.first() ^ 0xF1A7ull) % 100u < 22u) flowerAt(u, v);
                        break;
                    }
                    case 51460000:
                    {   // ELECTRIC SUBSTATION: 2×2×2 transformers of iron blocks with insulators (fences) on the top corners, towers (a fence of 8 with a crossbar) along the edges
                        for (int v = 2; v < Wd - 3; v += 6)
                            for (int u = 2; u < L - 3; u += 6)
                            {
                                const quint32 hq = colHash(pairKey(u * 23, v * 19) ^ ring.first() ^ 0xE1ECull);
                                if (hq % 100u >= 75u) continue;
                                bool ok = true;
                                for (int du = 0; du < 2 && ok; ++du) for (int dv = 0; dv < 2 && ok; ++dv) ok = freeCol(u + du, v + dv, 4);
                                if (!ok) continue;
                                for (int du = 0; du < 2; ++du) for (int dv = 0; dv < 2; ++dv)
                                {
                                    put1(u + du, v + dv, 0, IdIronBlock); put1(u + du, v + dv, 1, IdIronBlock);
                                    if (du == dv || (hq & 1u)) { put1(u + du, v + dv, 2, IdFence); put1(u + du, v + dv, 3, IdFence); }
                                }
                                ++nEq;
                            }
                        for (int k = 0; k < 2; ++k)
                        {
                            const int u0 = 1 + k * std::max(3, L - 4), v0 = Wd / 2;
                            bool ok = true;
                            for (int dv = -1; dv <= 1 && ok; ++dv) ok = freeCol(u0, v0 + dv, 9);
                            if (!ok) continue;
                            for (int dy = 0; dy < 9; ++dy) put1(u0, v0, dy, IdFence);
                            for (int dv = -1; dv <= 1; ++dv) put1(u0, v0 + dv, 7, IdFence);
                            put1(u0, v0 - 1, 6, IdFence); put1(u0, v0 + 1, 6, IdFence);
                            ++nEq;
                        }
                        break;
                    }
                    case 52100000:
                    {   // AGRICULTURAL OBJECTS (territory): a 7×5 pen with animals, hay bales, barrels, drinkers (cauldrons)
                        if (L >= 12 && Wd >= 9)
                        {
                            const int u0 = 2, v0 = 2;
                            bool ok = true;
                            for (int u = u0; u < u0 + 7 && ok; ++u) for (int v = v0; v < v0 + 5 && ok; ++v) ok = freeCol(u, v, 1);
                            if (ok)
                            {
                                for (int u = u0; u < u0 + 7; ++u) { put1(u, v0, 0, IdFence); put1(u, v0 + 4, 0, IdFence); }
                                for (int v = v0 + 1; v < v0 + 4; ++v) { put1(u0, v, 0, IdFence); if (v != v0 + 2) put1(u0 + 6, v, 0, IdFence); }
                                put1(u0 + 6, v0 + 2, 0, alongX ? IdGateE : IdGateS);
                                put1(u0 + 1, v0 + 3, 0, IdCauldron);                         // a drinker
                                static const char* const kFarm[] = { "minecraft:cow", "minecraft:pig", "minecraft:sheep", "minecraft:chicken" };
                                const int na = 2 + int(ring.first() % 3u);
                                for (int a = 0; a < na; ++a)
                                {
                                    const QPoint q = toXZ(u0 + 2 + a, v0 + 1 + (a % 3));
                                    NpcSite an;
                                    an.kind = 5; an.animal = QString::fromLatin1(kFarm[(ring.first() >> (a * 3)) % 4u]);
                                    an.x = q.x(); an.z = q.y(); an.y = surfaceTop(pairKey(q.x(), q.y())) + 1;
                                    an.yaw = a * 90;
                                    npcs.append(an);
                                }
                                ++nEq;
                            }
                        }
                        for (int v = 2; v < Wd - 2; v += 5)
                            for (int u = 11; u < L - 2; u += 5)
                            {
                                const quint32 hq = colHash(pairKey(u * 17, v * 31) ^ ring.first() ^ 0x5E7Dull);
                                if (hq % 100u >= 60u) continue;
                                if (!freeCol(u, v, 2)) continue;
                                if ((hq >> 8) % 3u == 0u) { put1(u, v, 0, IdBarrel); if (freeCol(u + 1, v, 1)) put1(u + 1, v, 0, IdBarrel); }
                                else { put1(u, v, 0, hayBale); if ((hq >> 12) & 1u) put1(u, v, 1, hayBale); if (freeCol(u + 1, v, 1)) put1(u + 1, v, 0, hayBale); }
                                ++nEq;
                            }
                        break;
                    }
                    case 51130000:
                    {   // INDUSTRIAL ENTERPRISE (territory): stacks of containers (multicolored wool 4×2×2), barrels, pipes (fences)
                        for (int v = 2; v < Wd - 3; v += 6)
                            for (int u = 2; u < L - 5; u += 7)
                            {
                                const quint32 hq = colHash(pairKey(u * 37, v * 41) ^ ring.first() ^ 0x1D05ull);
                                if (hq % 100u >= 65u) continue;
                                if ((hq >> 8) % 4u == 0u)
                                {   // barrels
                                    if (!freeCol(u, v, 2)) continue;
                                    put1(u, v, 0, IdBarrel); put1(u, v, 1, IdBarrel);
                                    if (freeCol(u + 1, v, 1)) put1(u + 1, v, 0, IdBarrel);
                                    ++nEq;
                                    continue;
                                }
                                bool ok = true;
                                for (int du = 0; du < 4 && ok; ++du) for (int dv = 0; dv < 2 && ok; ++dv) ok = freeCol(u + du, v + dv, 2);
                                if (!ok) continue;
                                static const int kContainerWools[] = { 1, 5, 11, 14, 3, 4 };
                                const int wc = std::min(kWoolsCount - 1, kContainerWools[(hq >> 12) % 6u]);
                                for (int du = 0; du < 4; ++du) for (int dv = 0; dv < 2; ++dv)
                                {
                                    put1(u + du, v + dv, 0, quint16(IdWoolBase + wc));
                                    if (((hq >> 20) & 1u) || du < 2) put1(u + du, v + dv, 1, quint16(IdWoolBase + wc));
                                }
                                ++nEq;
                            }
                        break;
                    }
                    case 53110000:
                    {   // AIRPORT: white runway marking — center dashes 5 every 4, side lines, threshold stripes at the ends, lights along the edges
                        const int vA = 2, vB = Wd - 3;
                        for (int u = 1; u < L - 1; ++u)
                        {
                            paint(u, vA); paint(u, vB);                                      // the edges
                            if ((u / 5) % 2 == 0) paint(u, vc);                              // the center dashes
                        }
                        for (int end = 0; end < 2; ++end)
                            for (int v = vA + 2; v <= vB - 2; v += 2)
                                for (int k = 0; k < 4; ++k) paint(end == 0 ? 2 + k : L - 3 - k, v);
                        for (int u = 4; u < L - 3; u += 8)
                            for (int v : { 1, Wd - 2 }) if (freeCol(u, v, 1)) { put1(u, v, 0, IdLanternStanding); ++nEq; }
                        break;
                    }
                    case 53600000:
                    case 53400000:
                    {   // TERRITORIES OF SOCIAL AND RELIGIOUS OBJECTS (and others): a lawn with benches along the perimeter, flower beds and flowers; without wool
                        for (int k = 0; k < 4; ++k)
                        {
                            const int u0 = 2 + (k & 1) * std::max(4, L - 7);
                            const int v0 = (k & 2) ? std::max(2, Wd - 3) : 2;
                            bool ok = true;
                            for (int u = u0; u < u0 + 3 && ok; ++u) ok = freeCol(u, v0, 1);
                            if (!ok) continue;
                            for (int u = u0; u < u0 + 3; ++u) put1(u, v0, 0, alongX ? ((k & 2) ? IdStairsN : IdStairsS) : ((k & 2) ? IdStairsW : IdStairsE));
                            ++nEq;
                        }
                        for (int v = 3; v < Wd - 3; v += 6)
                            for (int u = 3; u < L - 3; u += 6)
                            {
                                const quint32 hq = colHash(pairKey(u * 13, v * 7) ^ ring.first() ^ 0xC1A5ull);
                                if (hq % 100u >= 55u) continue;
                                int made = 0;
                                for (int du = 0; du < 2; ++du) for (int dv = 0; dv < 2; ++dv) if (flowerAt(u + du, v + dv)) ++made;
                                if (made) ++nEq;
                            }
                        break;
                    }
                    default:
                        break;                                                  // other codes (pipes, towers, tanks, etc.) — without furnishing
                    }
                    nSportEq += nEq;
                    if (qEnvironmentVariableIsSet("MCX_LAIRDBG")) std::fprintf(stderr, "[csr] code=%d bbox x %d..%d z %d..%d cols=%d eq=%d\n", padKind, bx0, bx1, bz0, bz1, int(padCols.size()), nEq);
                    continue;
                }
                QVector<QPoint> barAt;                                           // pull-up bars of the site: no more than two, not closer than 12 blocks to each other
                auto canBar = [&](int u, int v)
                {
                    if (barAt.size() >= 2) return false;
                    for (const QPoint& q : barAt) if (std::max(std::abs(q.x() - u), std::abs(q.y() - v)) < 12) return false;
                    return true;
                };
                // COURTS AND FIELDS. We take the largest axis-aligned rectangle lying entirely inside the spot (with a 1-block margin from the edge) — so both rotated and oval spots are found. By its size and
                // the spot area a kind is chosen: football / mini-football / basketball / hockey / tennis / volleyball. The court stands in the center of the rectangle, marked with white wool on the ground.
                bool courtBuilt = false;
                auto buildCourt = [&]() -> bool
                {
                    if (padKind != 53700000) return false;
                    const int Wg = Lx, Hg = Lz;
                    QVector<int> hgt(Wg, 0);
                    int bestA = 0, bL = 0, bB = 0, bW = 0, bH = 0;
                    for (int zi = 0; zi < Hg; ++zi)
                    {
                        for (int xi = 0; xi < Wg; ++xi)
                        {
                            const int x = bx0 + xi, z = bz0 + zi;
                            bool ok = true;
                            for (int dz2 = -1; dz2 <= 1 && ok; ++dz2) for (int dx2 = -1; dx2 <= 1 && ok; ++dx2) ok = pad.contains(pairKey(x + dx2, z + dz2));
                            hgt[xi] = ok ? hgt[xi] + 1 : 0;
                        }
                        QVector<QPair<int, int>> st;                                      // (start, height)
                        for (int xi = 0; xi <= Wg; ++xi)
                        {
                            const int cur = xi < Wg ? hgt[xi] : 0;
                            int start = xi;
                            while (!st.isEmpty() && st.last().second >= cur)
                            {
                                const int hh = st.last().second, s0 = st.last().first;
                                st.removeLast();
                                if (hh * (xi - s0) > bestA) { bestA = hh * (xi - s0); bL = s0; bB = zi; bW = xi - s0; bH = hh; }
                                start = s0;
                            }
                            st.append(qMakePair(start, cur));
                        }
                    }
                    if (bestA < 100) return false;
                    const int rx0 = bx0 + bL, rz0 = bz0 + bB - bH + 1;                      // the rectangle [rx0, rx0+bW) × [rz0, rz0+bH)
                    const bool longX = bW >= bH;
                    const int a = longX ? bW : bH, b = longX ? bH : bW;
                    const int area = int(padCols.size());
                    struct Kind { int id; int w; };
                    QVector<Kind> opts;
                    if (a >= 36 && b >= 20 && area >= 1500) opts.append({ 0, area >= 2800 ? 8 : 3 });   // a football pitch
                    if (a >= 26 && b >= 14 && area <= 3200) opts.append({ 1, 3 });                                        // mini-football / handball
                    if (a >= 22 && b >= 11 && area <= 2200) opts.append({ 2, 4 });                                        // basketball
                    if (a >= 26 && b >= 14 && area >= 900 && area <= 4500) opts.append({ 3, area >= 2800 ? 3 : 2 });      // a hockey box
                    if (a >= 24 && b >= 10 && area <= 1400) opts.append({ 4, 2 });                                        // tennis
                    if (a >= 16 && b >= 8 && area <= 1000) opts.append({ 5, 3 });                                         // volleyball
                    if (opts.isEmpty()) return false;
                    int wsum = 0;
                    for (const Kind& k : opts) wsum += k.w;
                    int pickW = int(hp % quint32(wsum)), kindSel = opts.first().id;
                    for (const Kind& k : opts) { if (pickW < k.w) { kindSel = k.id; break; } pickW -= k.w; }
                    int Lc, Wc;
                    switch (kindSel)
                    {
                    case 0:  Lc = std::min(a - 2, 60); Wc = std::min(b - 0, 36); break;
                    case 1:  Lc = std::min(a - 2, 34); Wc = std::min(b, 18); break;
                    case 2:  Lc = std::min(a - 2, 28); Wc = std::min(b, 15); break;
                    case 3:  Lc = std::min(a - 2, 30); Wc = std::min(b - 2, 15); break;
                    case 4:  Lc = std::min(a - 2, 24); Wc = std::min(b - 2, 11); break;
                    default: Lc = std::min(a - 2, 18); Wc = std::min(b - 2, 9); break;
                    }
                    if (Lc < 12 || Wc < 7) return false;
                    const int ou = 1 + (a - 2 - Lc) / 2, ov = (b - Wc) / 2;                    // the court offset in the rectangle (along the length a margin is left for the goals)
                    const int ox = longX ? rx0 + ou : rx0 + ov, oz = longX ? rz0 + ov : rz0 + ou;
                    auto cxz = [&](int cu, int cv) { return longX ? QPoint(ox + cu, oz + cv) : QPoint(ox + cv, oz + cu); };
                    auto cPaint = [&](int cu, int cv)
                    {
                        if (whiteWool < 0) return;
                        const QPoint q = cxz(cu, cv);
                        if (!pad.contains(pairKey(q.x(), q.y()))) return;
                        auto git = ground.find(pairKey(q.x() >> 4, q.y() >> 4));
                        if (git != ground.end() && git->wd[(q.y() & 15) * 16 + (q.x() & 15)] == 0) git->surf[(q.y() & 15) * 16 + (q.x() & 15)] = quint16(IdWoolBase + whiteWool);
                    };
                    auto cPut = [&](int cu, int cv, int dy, quint16 id)
                    {
                        const QPoint q = cxz(cu, cv);
                        if (!pad.contains(pairKey(q.x(), q.y()))) return;
                        const int y = surfaceTop(pairKey(q.x(), q.y())) + 1 + dy;
                        if (y < 254 && store.get(q.x(), y, q.y()) == IdAir) store.set(q.x(), y, q.y(), id);
                    };
                    auto cFence = [&](int cu, int cv, int h) { for (int dy = 0; dy < h; ++dy) cPut(cu, cv, dy, IdFence); };
                    const int vc2 = Wc / 2, uc2 = Lc / 2;
                    auto outline = [&]()
                    {
                        for (int cu = 0; cu < Lc; ++cu) { cPaint(cu, 0); cPaint(cu, Wc - 1); }
                        for (int cv = 0; cv < Wc; ++cv) { cPaint(0, cv); cPaint(Lc - 1, cv); }
                    };
                    auto circle = [&](int cu0, int cv0, int rad)
                    {
                        for (int ang = 0; ang < 96; ++ang)
                        {
                            const double t = ang * 3.14159265358979 / 48.0;
                            cPaint(cu0 + int(std::lround(rad * std::cos(t))), cv0 + int(std::lround(rad * std::sin(t))));
                        }
                    };
                    auto goal = [&](int cu, int halfW, int h)
                    {   // a goal in the plane cu: two posts and a crossbar 2*halfW+1 wide
                        cFence(cu, vc2 - halfW, h); cFence(cu, vc2 + halfW, h);
                        for (int cv = vc2 - halfW + 1; cv < vc2 + halfW; ++cv) cPut(cu, cv, h - 1, IdFence);
                    };
                    // is it free under the court: nothing stands on the marking itself
                    for (int cu = -1; cu <= Lc; ++cu)
                        for (int cv = -1; cv <= Wc; ++cv)
                        {
                            const QPoint q = cxz(cu, cv);
                            if (!pad.contains(pairKey(q.x(), q.y()))) continue;
                            if (courtCols.contains(pairKey(q.x(), q.y()))) return false;   // no courts on top of each other
                            if (store.get(q.x(), surfaceTop(pairKey(q.x(), q.y())) + 1, q.y()) != IdAir) return false;
                        }
                    switch (kindSel)
                    {
                    case 0:
                    {   // a football pitch: the outline, the center line and circle, penalty areas 8×18 (from a length of 44), goals 7 wide and 3 high behind the lines
                        outline();
                        for (int cv = 0; cv < Wc; ++cv) cPaint(uc2, cv);
                        circle(uc2, vc2, std::max(3, std::min(5, Wc / 6)));
                        cPaint(uc2, vc2);
                        if (Lc >= 44 && Wc >= 24)
                            for (int end = 0; end < 2; ++end)
                            {
                                const int base = end == 0 ? 0 : Lc - 1, step = end == 0 ? 1 : -1;
                                for (int d = 0; d <= 8; ++d) { cPaint(base + step * d, vc2 - 9); cPaint(base + step * d, vc2 + 9); }
                                for (int cv = vc2 - 9; cv <= vc2 + 9; ++cv) cPaint(base + step * 8, cv);
                            }
                        goal(-1, 3, 3); goal(Lc, 3, 3);
                        break;
                    }
                    case 1:
                    {   // mini-football / handball: the outline, the center, goal areas in an arc, goals 5 wide
                        outline();
                        for (int cv = 0; cv < Wc; ++cv) cPaint(uc2, cv);
                        circle(uc2, vc2, 3);
                        for (int end = 0; end < 2; ++end)
                        {
                            const int base = end == 0 ? 0 : Lc - 1, step = end == 0 ? 1 : -1;
                            for (int cv = vc2 - 4; cv <= vc2 + 4; ++cv) cPaint(base + step * 5, cv);
                            for (int d = 0; d <= 5; ++d) { cPaint(base + step * d, vc2 - 4); cPaint(base + step * d, vc2 + 4); }
                        }
                        goal(-1, 2, 3); goal(Lc, 2, 3);
                        break;
                    }
                    case 2:
                    {   // basketball: the outline, the center, the circle, "keys" 5×5 at the hoops; backboards on posts behind the end lines
                        outline();
                        for (int cv = 0; cv < Wc; ++cv) cPaint(uc2, cv);
                        circle(uc2, vc2, 2);
                        for (int end = 0; end < 2; ++end)
                        {
                            const int base = end == 0 ? 0 : Lc - 1, step = end == 0 ? 1 : -1;
                            for (int d = 0; d <= 5; ++d) { cPaint(base + step * d, vc2 - 2); cPaint(base + step * d, vc2 + 2); }
                            for (int cv = vc2 - 2; cv <= vc2 + 2; ++cv) cPaint(base + step * 5, cv);
                            const int pu = end == 0 ? -1 : Lc, du = end == 0 ? 1 : -1;
                            cFence(pu, vc2, 3);
                            for (int cv = vc2 - 1; cv <= vc2 + 1; ++cv) cPut(pu, cv, 3, quint16(IdWoolBase + std::max(0, whiteWool)));
                            cPut(pu + du, vc2, 2, IdCauldron);
                        }
                        break;
                    }
                    case 3:
                    {   // a hockey box: boards of fences along the outline with 3-wide openings in the middle of the long sides, the center and blue lines, goals 3 wide
                        for (int cu = -1; cu <= Lc; ++cu) for (int cv : { -1, Wc })
                            if (std::abs(cu - uc2) > 1) cPut(cu, cv, 0, IdFence);
                        for (int cv = 0; cv < Wc; ++cv) for (int cu : { -1, Lc }) cPut(cu, cv, 0, IdFence);
                        for (int cv = 0; cv < Wc; ++cv) { cPaint(uc2, cv); cPaint(uc2 - 6, cv); cPaint(uc2 + 6, cv); }
                        circle(uc2, vc2, 3);
                        goal(2, 1, 2); goal(Lc - 3, 1, 2);
                        break;
                    }
                    case 4:
                    {   // tennis: the outline, a net in the center on posts behind the side lines, service lines ±6, the center service line
                        outline();
                        for (int cv = 1; cv < Wc - 1; ++cv) { cPaint(uc2 - 6, cv); cPaint(uc2 + 6, cv); }
                        for (int cu = uc2 - 6; cu <= uc2 + 6; ++cu) cPaint(cu, vc2);
                        cFence(uc2, -1, 3); cFence(uc2, Wc, 3);
                        for (int cv = 0; cv < Wc; ++cv) cPut(uc2, cv, 1, IdFence);
                        break;
                    }
                    default:
                    {   // volleyball: the outline, a net in the center on posts, attack lines 3 blocks from the net
                        outline();
                        for (int cv = 0; cv < Wc; ++cv) { cPaint(uc2 - 3, cv); cPaint(uc2 + 3, cv); }
                        cFence(uc2, -1, 3); cFence(uc2, Wc, 3);
                        for (int cv = 0; cv < Wc; ++cv) cPut(uc2, cv, 1, IdFence);
                        break;
                    }
                    }
                    // the court zone (with a 1-block margin) is unavailable to ordinary items; for hockey the margin for the boards is already included
                    const QPoint pA = cxz(-2, -2), pB = cxz(Lc + 1, Wc + 1);
                    resX0 = std::min(pA.x(), pB.x()); resX1 = std::max(pA.x(), pB.x());
                    resZ0 = std::min(pA.y(), pB.y()); resZ1 = std::max(pA.y(), pB.y());
                    for (int qx = resX0; qx <= resX1; ++qx)
                        for (int qz = resZ0; qz <= resZ1; ++qz)
                            courtCols.insert(pairKey(qx, qz));                     // and to the items of the other pads too
                    if (qEnvironmentVariableIsSet("MCX_LAIRDBG")) std::fprintf(stderr, "[court] kind=%d rect %dx%d court %dx%d area=%d\n", kindSel, a, b, Lc, Wc, area);
                    return true;
                };
                bool kidsPad = (padKind == 53710000);
                if (!kidsPad)
                {
                    courtBuilt = buildCourt();
                    if (courtBuilt) ++nEq;
                    else if (padCols.size() < 700 && hp % 100u < 30u) kidsPad = true;       // a small sports/children's playground (the code 53700000 unites both kinds)
                }
                if (kidsPad)
                {   // CHILDREN'S PLAYGROUND
                    // swings: a frame of two posts 4 high with a crossbar and two seats
                    for (int k = 0; k < 2; ++k)
                    {
                        const int u0 = 2 + k * 6, v0 = std::max(1, vc - 3);
                        if (u0 + 1 >= L - 1 || v0 + 5 >= Wd) break;
                        bool ok = true;
                        for (int v = v0; v <= v0 + 4 && ok; ++v) ok = freeCol(u0, v, 4);
                        if (!ok) continue;
                        post(u0, v0, 4); post(u0, v0 + 4, 4);
                        for (int v = v0 + 1; v <= v0 + 3; ++v) put1(u0, v, 3, IdFence);
                        for (int sv : { v0 + 1, v0 + 3 })
                        {
                            put1(u0, sv, 2, IdFence);                         // ropes
                            put1(u0, sv, 1, IdSlabTop);                       // the seat
                        }
                        ++nEq;
                    }
                    // a 3×3 sandbox with a slab rim
                    {
                        const int u0 = std::max(2, L - 6), v0 = std::max(1, vc - 1);
                        bool ok = u0 + 3 < L - 1 && v0 + 3 < Wd - 1;
                        for (int u = u0; u <= u0 + 2 && ok; ++u) for (int v = v0; v <= v0 + 2 && ok; ++v) ok = freeCol(u, v, 1);
                        if (ok)
                        {
                            for (int u = u0 - 1; u <= u0 + 3; ++u) for (int v = v0 - 1; v <= v0 + 3; ++v)
                            {
                                const bool edge = (u == u0 - 1 || u == u0 + 3 || v == v0 - 1 || v == v0 + 3);
                                if (!freeCol(u, v, 1)) continue;
                                if (edge) put1(u, v, 0, IdSlabBottom);
                                else if (g_sandId) { const QPoint q = toXZ(u, v); store.set(q.x(), surfaceTop(pairKey(q.x(), q.y())), q.y(), g_sandId); }
                            }
                            ++nEq;
                        }
                    }
                    // a carousel: a cauldron on a post with four fence spokes
                    {
                        const int u0 = L / 2, v0 = std::max(2, Wd - 4);
                        if (freeCol(u0, v0, 2) && freeCol(u0 - 1, v0, 1) && freeCol(u0 + 1, v0, 1) && freeCol(u0, v0 - 1, 1) && freeCol(u0, v0 + 1, 1))
                        {
                            put1(u0, v0, 0, IdFence); put1(u0, v0, 1, IdCauldron);
                            put1(u0 - 1, v0, 0, IdFence); put1(u0 + 1, v0, 0, IdFence); put1(u0, v0 - 1, 0, IdFence); put1(u0, v0 + 1, 0, IdFence);
                            ++nEq;
                        }
                    }
                    // a bench at the edge
                    for (int k = 0; k < 2; ++k)
                    {
                        const int u0 = 2 + k * (std::max(4, L - 6)), v0 = 1;
                        bool ok = true;
                        for (int u = u0; u < u0 + 3 && ok; ++u) ok = freeCol(u, v0, 1);
                        if (!ok) continue;
                        const quint16 st = alongX ? IdStairsS : IdStairsE;    // the back toward the fence
                        for (int u = u0; u < u0 + 3; ++u) put1(u, v0, 0, st);
                        ++nEq;
                    }
                }
                else if (courtBuilt)
                {
                    // the court is already built (see buildCourt)
                }
                else
                {   // WORKOUT: pull-up bars (a frame 4 wide), parallel bars, benches
                    int k = 0;
                    for (int i = 0; i < 2; ++i)
                    {
                        const int u0 = 2 + i * std::max(12, L / 2), v0 = 2 + (i % 2) * std::max(0, Wd - 6);
                        if (u0 + 3 >= L - 1 || v0 + 1 >= Wd || !canBar(u0, v0)) continue;
                        bool ok = true;
                        for (int u = u0; u <= u0 + 3 && ok; ++u) ok = freeCol(u, v0, 4);
                        if (!ok) continue;
                        post(u0, v0, 4); post(u0 + 3, v0, 4);
                        put1(u0 + 1, v0, 3, IdFence); put1(u0 + 2, v0, 3, IdFence);           // the crossbar
                        barAt.append(QPoint(u0, v0));
                        ++k;
                    }
                    for (int i = 0; i < 1; ++i)
                    {   // parallel bars: two parallel poles on posts (one set per site)
                        const int u0 = 3 + i * 7, v0 = std::max(2, vc - 1);
                        bool ok = u0 + 4 < L - 1 && v0 + 2 < Wd - 1;
                        for (int u = u0; u <= u0 + 3 && ok; ++u) ok = freeCol(u, v0, 2) && freeCol(u, v0 + 2, 2);
                        if (!ok) continue;
                        for (int vv : { v0, v0 + 2 })
                        {
                            put1(u0, vv, 0, IdFence); put1(u0 + 3, vv, 0, IdFence);
                            for (int u = u0; u <= u0 + 3; ++u) put1(u, vv, 1, IdFence);
                        }
                        ++k;
                    }
                    for (int i = 0; i < 2; ++i)
                    {   // a bench-press bench
                        const int u0 = 2 + i * std::max(4, L - 7), v0 = std::max(1, Wd - 2);
                        bool ok = true;
                        for (int u = u0; u < u0 + 3 && ok; ++u) ok = freeCol(u, v0, 1);
                        if (!ok) continue;
                        for (int u = u0; u < u0 + 3; ++u) put1(u, v0, 0, IdSlabBottom);
                        ++k;
                    }
                    nEq = k;
                }
                // EVEN FILLING: the site is divided into windows of ~9×9 blocks; in each window with free space (at least 25 inner columns) where there is nothing yet, we try to put
                // one item (a bench, parallel bars, a pull-up bar, a balance log; on children's ones — swings, a sandbox, a carousel, a bench, a rocker). We do not put much more often: one window — one item.
                {
                    const bool kids = kidsPad;
                    int nBars = 0;                                                   // parallel bars: no more than two per site
                    auto occupiedAt = [&](int u, int v)
                    {
                        const QPoint q = toXZ(u, v);
                        return store.get(q.x(), surfaceTop(pairKey(q.x(), q.y())) + 1, q.y()) != IdAir;
                    };
                    auto tryPiece = [&](int kind, int u, int v) -> bool
                    {
                        if (kids)
                        {
                            if (kind == 0)
                            {   // swings
                                bool ok = true;
                                for (int vv = v; vv <= v + 4 && ok; ++vv) ok = freeCol(u, vv, 4);
                                if (!ok) return false;
                                post(u, v, 4); post(u, v + 4, 4);
                                for (int vv = v + 1; vv <= v + 3; ++vv) put1(u, vv, 3, IdFence);
                                for (int sv : { v + 1, v + 3 }) { put1(u, sv, 2, IdFence); put1(u, sv, 1, IdSlabTop); }
                                return true;
                            }
                            if (kind == 1)
                            {   // a bench
                                bool ok = true;
                                for (int uu = u; uu < u + 3 && ok; ++uu) ok = freeCol(uu, v, 1);
                                if (!ok) return false;
                                for (int uu = u; uu < u + 3; ++uu) put1(uu, v, 0, alongX ? IdStairsS : IdStairsE);
                                return true;
                            }
                            if (kind == 2)
                            {   // a 3×3 sandbox with a rim
                                bool ok = true;
                                for (int uu = u - 1; uu <= u + 3 && ok; ++uu) for (int vv = v - 1; vv <= v + 3 && ok; ++vv) ok = freeCol(uu, vv, 1);
                                if (!ok) return false;
                                for (int uu = u - 1; uu <= u + 3; ++uu) for (int vv = v - 1; vv <= v + 3; ++vv)
                                {
                                    const bool edge = (uu == u - 1 || uu == u + 3 || vv == v - 1 || vv == v + 3);
                                    if (edge) put1(uu, vv, 0, IdSlabBottom);
                                    else if (g_sandId) { const QPoint q = toXZ(uu, vv); store.set(q.x(), surfaceTop(pairKey(q.x(), q.y())), q.y(), g_sandId); }
                                }
                                return true;
                            }
                            if (kind == 3)
                            {   // a carousel
                                if (!(freeCol(u, v, 2) && freeCol(u - 1, v, 1) && freeCol(u + 1, v, 1) && freeCol(u, v - 1, 1) && freeCol(u, v + 1, 1))) return false;
                                put1(u, v, 0, IdFence); put1(u, v, 1, IdCauldron);
                                put1(u - 1, v, 0, IdFence); put1(u + 1, v, 0, IdFence); put1(u, v - 1, 0, IdFence); put1(u, v + 1, 0, IdFence);
                                return true;
                            }
                            {   // a rocker: a slab on a post, length 5
                                bool ok = true;
                                for (int uu = u; uu < u + 5 && ok; ++uu) ok = freeCol(uu, v, 2);
                                if (!ok) return false;
                                put1(u + 2, v, 0, IdFence);
                                put1(u, v, 1, IdSlabBottom); put1(u + 1, v, 1, IdSlabBottom); put1(u + 3, v, 1, IdSlabBottom); put1(u + 4, v, 1, IdSlabBottom);
                                put1(u + 2, v, 1, IdSlabTop);
                                return true;
                            }
                        }
                        if (kind == 0)
                        {   // a bench
                            bool ok = true;
                            for (int uu = u; uu < u + 3 && ok; ++uu) ok = freeCol(uu, v, 1);
                            if (!ok) return false;
                            for (int uu = u; uu < u + 3; ++uu) put1(uu, v, 0, IdSlabBottom);
                            return true;
                        }
                        if (kind == 1)
                        {   // parallel bars (no more than two per site)
                            if (nBars >= 2) return false;
                            bool ok = true;
                            for (int uu = u; uu <= u + 3 && ok; ++uu) ok = freeCol(uu, v, 2) && freeCol(uu, v + 2, 2);
                            if (!ok) return false;
                            for (int vv : { v, v + 2 })
                            {
                                put1(u, vv, 0, IdFence); put1(u + 3, vv, 0, IdFence);
                                for (int uu = u; uu <= u + 3; ++uu) put1(uu, vv, 1, IdFence);
                            }
                            ++nBars;
                            return true;
                        }
                        if (kind == 2)
                        {   // a pull-up bar (no more than two per site, not closer than 12 blocks to each other)
                            if (!canBar(u, v)) return false;
                            bool ok = true;
                            for (int uu = u; uu <= u + 3 && ok; ++uu) ok = freeCol(uu, v, 4);
                            if (!ok) return false;
                            post(u, v, 4); post(u + 3, v, 4);
                            put1(u + 1, v, 3, IdFence); put1(u + 2, v, 3, IdFence);
                            barAt.append(QPoint(u, v));
                            return true;
                        }
                        {   // a balance log: a row of slabs on low supports
                            bool ok = true;
                            for (int uu = u; uu < u + 5 && ok; ++uu) ok = freeCol(uu, v, 2);
                            if (!ok) return false;
                            for (int uu = u; uu < u + 5; ++uu) put1(uu, v, 0, IdSlabTop);
                            return true;
                        }
                    };
                    constexpr int kWin = 8;
                    const int nKinds = kids ? 5 : 4;
                    for (int v0 = 1; v0 < Wd - 1; v0 += kWin)
                        for (int u0 = 1; u0 < L - 1; u0 += kWin)
                        {
                            int interior = 0;
                            bool has = false;
                            for (int vv = v0; vv < std::min(v0 + kWin, Wd - 1) && !has; ++vv)
                                for (int uu = u0; uu < std::min(u0 + kWin, L - 1); ++uu)
                                {
                                    if (!inside(uu, vv, 1)) continue;
                                    ++interior;
                                    if (occupiedAt(uu, vv)) { has = true; break; }
                                }
                            if (has || interior < 20) continue;
                            for (int t = 0; t < 48; ++t)
                            {
                                const quint32 hq = colHash(pairKey(u0 * 31 + t, v0 * 17) ^ ring.first() ^ 0x5CA9ull);
                                const int u = u0 + int(hq % quint32(kWin)), v = v0 + int((hq >> 8) % quint32(kWin));
                                static const int kSportKinds[8] = { 0, 3, 0, 2, 3, 0, 1, 3 };       // a bench, a balance beam, a pull-up bar, parallel bars (rare)
                                const int pickK = kids ? int(((hq >> 16) + quint32(t)) % quint32(nKinds)) : kSportKinds[((hq >> 16) + quint32(t)) % 8u];
                                if (tryPiece(pickK, u, v)) { ++nEq; break; }
                            }
                        }
                }
                nSportEq += nEq;
                if (qEnvironmentVariableIsSet("MCX_LAIRDBG")) std::fprintf(stderr, "[sport] code=%d bbox x %d..%d z %d..%d cols=%d L=%d W=%d eq=%d\n", padCode.value(padIdx, 0), bx0, bx1, bz0, bz1, int(padCols.size()), L, Wd, nEq);
            }
        }
        if (nSportEq > 0) res.lairInfo += QObject::tr("; equipment on sports grounds: %1 items").arg(nSportEq);
        // — lamp posts: at a road column by hash ~1/24 we look for a free neighboring NON-road
        //   column and put a post (not closer than 10 blocks to the others); distance + hash = "along the road,
        //   not a picket fence". The composition: a plinth of a cobblestone wall, 6 fence posts, at the top toward the
        //   road a wing of one post goes out, under it a lantern hangs (hanging=true).
        QVector<QPoint> poles;
        QVector<qint64> rc(roadCols.constBegin(), roadCols.constEnd());
        std::sort(rc.begin(), rc.end());
        for (qint64 ck : rc)
        {
            if (colHash(ck ^ 0xFA11ull) % 24u != 0u) continue;
            const int bx = keyFirst(ck), bz = keySecond(ck);
            for (int d = 0; d < 4; ++d)
            {
                const int px = bx + dxs[d], pz = bz + dzs[d];
                const qint64 pk = pairKey(px, pz);
                if (roadCols.contains(pk) || colMin.contains(pk) || colWater.contains(pk)) continue;
                const int y0 = surfaceTop(pk) + 1;
                if (y0 + 6 > 254) continue;
                bool free = true;
                for (int dy = 0; dy < 7 && free; ++dy)       // the plinth + 6 posts of the stand
                    if (store.get(px, y0 + dy, pz) != IdAir) free = false;
                if (free && store.get(bx, y0 + 6, bz) != IdAir) free = false;    // the wing above the road
                if (free && store.get(bx, y0 + 5, bz) != IdAir) free = false;    // the place for the lantern
                bool near = false;
                for (const QPoint& p : poles)
                    if (std::max(std::abs(p.x() - px), std::abs(p.y() - pz)) < 10) { near = true; break; }
                if (!free || near) continue;
                store.set(px, y0, pz, IdCobbleWall);
                for (int dy = 1; dy <= 6; ++dy) store.set(px, y0 + dy, pz, IdFence);
                store.set(bx, y0 + 6, bz, IdFence);          // the wing: a post sideways, toward the road
                store.set(bx, y0 + 5, bz, IdLanternHung);    // the lantern hangs under the wing
                poles.append(QPoint(px, pz));
                break;
            }
        }
        // — PARK PATHS (CODE 71126000) are well lit: at road columns inside a park there are low lamp posts (a plinth of a cobblestone wall, 3 fence posts,
        //   a lantern on top) no less often than every ~6 blocks; street posts are taller (the same distance for them).
        {
            QVector<ColSpan> parkSpansAll;
            for (const VegSite& vs3 : forests) if (vs3.park) parkSpansAll += vs3.spans;
            if (!parkSpansAll.isEmpty())
            {
                parkSpansAll = mergeSpans(parkSpansAll);
                for (qint64 ck : rc)
                {
                    const int bx = keyFirst(ck), bz = keySecond(ck);
                    if (!spansContain(parkSpansAll, bx, bz)) continue;
                    if (colHash(ck ^ 0x9A8Cull) % 3u != 0u) continue;
                    for (int d = 0; d < 4; ++d)
                    {
                        const int px = bx + dxs[d], pz = bz + dzs[d];
                        const qint64 pk = pairKey(px, pz);
                        if (roadCols.contains(pk) || colMin.contains(pk) || colWater.contains(pk) || colWool.contains(pk)) continue;
                        const int y0 = surfaceTop(pk) + 1;
                        if (y0 + 5 > 254) continue;
                        bool free = true;
                        for (int dy = 0; dy < 5 && free; ++dy)
                            if (store.get(px, y0 + dy, pz) != IdAir) free = false;
                        bool near = false;
                        for (const QPoint& p : poles)
                            if (std::max(std::abs(p.x() - px), std::abs(p.y() - pz)) < 6) { near = true; break; }
                        if (!free || near) continue;
                        store.set(px, y0, pz, IdCobbleWall);
                        for (int dy = 1; dy <= 3; ++dy) store.set(px, y0 + dy, pz, IdFence);
                        store.set(px, y0 + 4, pz, IdLanternStanding);
                        poles.append(QPoint(px, pz));
                        break;
                    }
                }
            }
        }
        // ——— PORTALS TO THE NETHER in forests (vegpol, CODE 71111110/71126000; request: the forest area is at least
        //     5000 REAL m² — see PortalForest::areaM2 — and no more than one portal within a
        //     radius of 1000 blocks; there are no portals on csrpol). Forests are taken from big to small
        //     (deterministically), a portal gets a forest around which there is no other one yet.
        //     The frame is vanilla: 4×5 obsidian (the bottom/top rows of 4, posts 2×3), in an
        //     ACTIVE one (10% of forests) the interior 2×2 of tiers y+1..y+2 — portal blocks (axis —
        //     the direction of the frame extent: it stands along X → axis=x), the row y+3 inside stays
        //     air. A "ruined" one — vanilla ruins our way: the bottom row is whole, the posts/top
        //     arc with deterministic gaps (~25%), some of the remainder — crying_obsidian.
        //     The frame is entirely inside the forest spot on flat ground (not a road/water/structure), the volume
        //     4×5 is air (the trees are already planted), otherwise we try the next point of this forest. ———
        {
            struct Cand { int x, z; bool runX; };
            QVector<int> order;
            for (int i = 0; i < portalForests.size(); ++i)
                if (portalForests[i].areaM2 >= 2500.0) order.append(i);
            std::sort(order.begin(), order.end(), [&](int a, int b)
            {
                if (portalForests[a].areaM2 != portalForests[b].areaM2)
                    return portalForests[a].areaM2 > portalForests[b].areaM2;
                return portalForests[a].h < portalForests[b].h;
            });
            QVector<QPoint> placedPortals;                        // the centers of the portals already standing (blocks)
            for (int fi : order)
            {
                const PortalForest& pf = portalForests[fi];
                const quint32 h = pf.h;
                // 10% of forests are active by hash; and in a big world (more than 5000 blocks along at least one side) the biggest forest with a portal always gets an ACTIVE one,
                // so that at least one working portal exists (MCX_BIGWORLD=1 enables this rule in the test).
                const bool bigWorld = std::max((mxx - mnx) * tf.sx, (mxy - mny) * tf.sz) >= 5000.0 || qEnvironmentVariableIsSet("MCX_BIGWORLD");
                const bool active = ((h >> 16) % 100u) < 10u || (bigWorld && placedPortals.isEmpty());
                QVector<Cand> cands;                              // the frame entirely in the forest spot
                const bool xFirst = ((h >> 8) & 1u) != 0u;
                {   // we take candidates by segments: along Z — inside a segment (step 5), along X — by checking the neighboring x
                    const int nSp = pf.spans.size();
                    const int sp0 = nSp ? int((h >> 3) % quint32(nSp)) : 0;
                    for (int t = 0; t < nSp && cands.size() < 20000; ++t)
                    {
                        const ColSpan& c = pf.spans[(sp0 + t) % nSp];
                        for (int z = c.z0; z + 3 <= c.z1 && cands.size() < 20000; z += 5)
                        {
                            bool okX = true;
                            for (int i = 1; i < 4 && okX; ++i)
                                if (!spansContain(pf.spans, c.x + i, z)) okX = false;
                            if (okX && xFirst) cands.append(Cand{ c.x, z, true });
                            else cands.append(Cand{ c.x, z, false });
                        }
                    }
                }
                if (cands.isEmpty()) continue;
                const int off = int((h >> 4) % quint32(cands.size()));
                const int tries = std::min(int(cands.size()), 6000);
                for (int ci = 0; ci < tries; ++ci)
                {
                    const Cand& c = cands[int((qint64(off) + qint64(ci) * 7919) % cands.size())];
                    {   // not closer than 1000 blocks to an already standing portal
                        bool far = true;
                        for (const QPoint& pp : placedPortals)
                            if (std::hypot(double(pp.x() - c.x), double(pp.y() - c.z)) < 1000.0) { far = false; break; }
                        if (!far) continue;                       // too close to someone else's portal
                    }
                    int yb = 0; bool ok = true;
                    for (int i = 0; i < 4 && ok; ++i)
                    {   // the floor is flat, not a road/water, the height is within the world bounds
                        const int bx = c.runX ? c.x + i : c.x;
                        const int bz = c.runX ? c.z : c.z + i;
                        const qint64 ck = pairKey(bx, bz);
                        if (colMin.contains(ck) || colWater.contains(ck)
                            || roadCols.contains(ck)) { ok = false; break; }
                        const int y = surfaceTop(ck);
                        if (i == 0) yb = y;
                        else if (y != yb) { ok = false; break; }
                    }
                    if (!ok || yb + 6 > 254) continue;
                    const int y0 = yb + 1;                        // the bottom row of the frame
                    auto cellAt = [&](int i, int dy)
                    { return pairKey(c.runX ? c.x + i : c.x, c.runX ? c.z : c.z + i); };
                    for (int dy = 0; dy < 5 && ok; ++dy)          // the frame volume — air
                        for (int i = 0; i < 4; ++i)
                            if (store.get(keyFirst(cellAt(i, dy)), y0 + dy,
                                          keySecond(cellAt(i, dy))) != IdAir)
                                { ok = false; break; }
                    if (!ok) continue;
                    // ——— here we go: 14 cells of the frame (i — along the axis, dy — height) ———
                    quint32 s = h ^ 0x0BADu;
                    auto lcg = [&s]()
                    { s = s * 1664525u + 1013904223u; return s; };
                    for (int dy = 0; dy < 5; ++dy)
                        for (int i = 0; i < 4; ++i)
                        {
                            const bool frame = dy == 0 || dy == 4
                                            || (i == 0 || i == 3);      // 14 cells of the perimeter
                            if (!frame) continue;
                            const qint64 ck = cellAt(i, dy);
                            const int bx = keyFirst(ck), bz = keySecond(ck);
                            if (!active)
                            {   // "ruined": the bottom is whole, the posts/top break deterministically
                                if (dy > 0 && (lcg() >> 16) % 100u < 25u) continue;
                                store.set(bx, y0 + dy, bz,
                                          (dy > 0 && (lcg() >> 16) % 100u < 20u)
                                              ? IdCryingObsidian : IdObsidian);
                            }
                            else store.set(bx, y0 + dy, bz, IdObsidian);
                        }
                    if (active)
                    {   // active: portal 2×2 (i=1..2, dy=1..2), the row dy=3 inside — air
                        const quint16 pid = c.runX ? IdPortalX : IdPortalZ;
                        for (int dy = 1; dy <= 2; ++dy)
                            for (int i = 1; i <= 2; ++i)
                            {
                                const qint64 ck = cellAt(i, dy);
                                store.set(keyFirst(ck), y0 + dy, keySecond(ck), pid);
                            }
                    }
                    // LAVA PITS around the portal (request): 3..6 pits 1×1 of depth 1 (lava at a level below the floor, air above it)
                    // on a flat free floor within a radius of 5 blocks from the frame; the neighboring floor blocks around a pit are replaced
                    // with magma with a 60% chance. A pit does not touch the frame cells and does not climb onto a road/water.
                    {
                        const quint16 magma = quint16(IdPaletteStart + paletteIndexOf("minecraft:magma_block"));
                        const int nPits = 3 + int(lcg() >> 16) % 4;
                        QVector<QPoint> pits;
                        for (int tr = 0; tr < 60 && pits.size() < nPits; ++tr)
                        {
                            const int i = int(lcg() >> 16) % 12 - 4;            // along the axis: -4..7
                            const int o = int(lcg() >> 16) % 11 - 5;            // across: -5..5
                            if (i >= -1 && i <= 4 && o >= -1 && o <= 1) continue;   // the frame itself and right next to it
                            const int bx = c.runX ? c.x + i : c.x + o;
                            const int bz = c.runX ? c.z + o : c.z + i;
                            bool near = false;
                            for (const QPoint& q : pits)
                                if (std::abs(q.x() - bx) <= 1 && std::abs(q.y() - bz) <= 1) near = true;
                            if (near) continue;
                            bool good = true;
                            for (int dz = -1; dz <= 1 && good; ++dz)
                                for (int dx = -1; dx <= 1; ++dx)
                                {
                                    const qint64 ck = pairKey(bx + dx, bz + dz);
                                    if (colMin.contains(ck) || colWater.contains(ck) || roadCols.contains(ck)
                                        || !ground.contains(pairKey((bx + dx) >> 4, (bz + dz) >> 4))
                                        || surfaceTop(ck) != yb || store.get(bx + dx, yb + 1, bz + dz) != IdAir
                                        || store.get(bx + dx, yb, bz + dz) != IdAir)
                                    { good = false; break; }
                                }
                            if (!good) continue;
                            {   // a pit: the ground top in this column is lowered by 2 (lava at yb-1, air above it)
                                auto git2 = ground.find(pairKey(bx >> 4, bz >> 4));
                                if (git2 != ground.end())
                                {
                                    const int col2 = (bz & 15) * 16 + (bx & 15);
                                    git2->top[col2] = qint16(yb - 2);
                                }
                            }
                            store.set(bx, yb - 1, bz, IdLava);
                            for (int d4 = 0; d4 < 4; ++d4)
                            {
                                const int nx = bx + (d4 == 0) - (d4 == 1), nz = bz + (d4 == 2) - (d4 == 3);
                                if ((lcg() >> 16) % 100u < 60u) store.set(nx, yb, nz, magma);
                            }
                            pits.append(QPoint(bx, bz));
                        }
                    }
                    {   // PORTAL RUINS AS IN THE ORIGINAL: a netherrack spot (rarer toward the edges) with magma inclusions, gold blocks and a chest with ruined_portal loot
                        const quint16 nether = quint16(IdPaletteStart + paletteIndexOf("minecraft:netherrack"));
                        const quint16 magmaR = quint16(IdPaletteStart + paletteIndexOf("minecraft:magma_block"));
                        const quint16 goldB = quint16(IdPaletteStart + paletteIndexOf("minecraft:gold_block"));
                        const double ecx = c.x + (c.runX ? 1.5 : 0.0), ecz = c.z + (c.runX ? 0.0 : 1.5);
                        QVector<QPoint> flatCells;
                        for (int dz = -8; dz <= 8; ++dz)
                            for (int dx = -8; dx <= 8; ++dx)
                            {
                                const int bx = c.x + dx, bz = c.z + dz;
                                const qint64 ck = pairKey(bx, bz);
                                const double ex = (bx - ecx) * (c.runX ? 0.7 : 1.0), ez = (bz - ecz) * (c.runX ? 1.0 : 0.7);
                                const double dd = std::hypot(ex, ez);
                                const int pr = dd < 2.6 ? 90 : dd < 4.6 ? 62 : dd < 6.6 ? 28 : dd < 8.0 ? 8 : 0;
                                if (!pr || (lcg() >> 16) % 100u >= quint32(pr)) continue;
                                if (colMin.contains(ck) || colWater.contains(ck) || roadCols.contains(ck)) continue;
                                const auto git3 = ground.find(pairKey(bx >> 4, bz >> 4));
                                if (git3 == ground.end()) continue;
                                const int col3 = (bz & 15) * 16 + (bx & 15);
                                if (git3->wd[col3] > 0 || surfaceTop(ck) != yb) continue;
                                git3->surf[col3] = ((lcg() >> 16) % 100u < 14u) ? magmaR : nether;
                                if (store.get(bx, yb + 1, bz) == IdAir) flatCells.append(QPoint(bx, bz));
                            }
                        // gold blocks (1–2) on the spot and a chest next to the frame
                        int nGold = 1 + int((lcg() >> 16) % 2u);
                        for (int tr = 0; tr < 40 && nGold > 0 && !flatCells.isEmpty(); ++tr)
                        {
                            const QPoint q = flatCells[int((lcg() >> 16) % quint32(flatCells.size()))];
                            const bool onFrame = (c.runX ? (q.y() == c.z && q.x() >= c.x && q.x() <= c.x + 3) : (q.x() == c.x && q.y() >= c.z && q.y() <= c.z + 3));
                            if (onFrame || store.get(q.x(), yb + 1, q.y()) != IdAir) continue;
                            store.set(q.x(), yb + 1, q.y(), goldB);
                            --nGold;
                        }
                        for (int tr = 0; tr < 60 && !flatCells.isEmpty(); ++tr)
                        {
                            const QPoint q = flatCells[int((lcg() >> 16) % quint32(flatCells.size()))];
                            const int ai = c.runX ? q.x() - c.x : q.y() - c.z, ao = c.runX ? q.y() - c.z : q.x() - c.x;
                            if (ai < -1 || ai > 4 || std::abs(ao) < 2 || std::abs(ao) > 4) continue;
                            if (store.get(q.x(), yb + 1, q.y()) != IdAir) continue;
                            store.set(q.x(), yb + 1, q.y(), IdChest);
                            chests.append(ChestSite{ q.x(), yb + 1, q.y(), kChestLootCount - 2 });
                            break;
                        }
                        PortalStartSite ps;
                        const int fx0 = c.x, fx1 = c.runX ? c.x + 3 : c.x, fz0 = c.z, fz1 = c.runX ? c.z : c.z + 3;
                        ps.bb[0] = fx0 - 3; ps.bb[1] = y0 - 1; ps.bb[2] = fz0 - 3; ps.bb[3] = fx1 + 3; ps.bb[4] = y0 + 5; ps.bb[5] = fz1 + 3;
                        ps.cx = ((ps.bb[0] + ps.bb[3]) / 2) >> 4; ps.cz = ((ps.bb[2] + ps.bb[5]) / 2) >> 4;
                        portalStarts.append(ps);
                        for (int pz = ps.bb[2] >> 4; pz <= (ps.bb[5] >> 4); ++pz)
                            for (int px = ps.bb[0] >> 4; px <= (ps.bb[3] >> 4); ++px)
                                portalsByChunk[pairKey(px, pz)].append(ps);
                    }
                    placedPortals.append(QPoint(c.x, c.z));
                    break;                                          // one portal per forest
                }
            }
        }
    }

    // PARKING LOT MARKING (rdcpol): light gray carpets. From every long enough edge of the outline inward into the polygon, perpendicular
    // to the edge, lines 12 blocks long run with a step of 7 blocks along the edge (if it does not fit into the parking lot — not drawn; if the parking lot is wider than three
    // line lengths (36) — the lines only from one of the opposite sides). A line does not touch (including diagonally) lines already laid from other edges — there are no intersections;
    // if a line touches another object (a building, a road, rails, water, any block at the carpet height) — it is not drawn at all.
    {
        quint16 lgCarpet = 0;
        int nParkWall = 0;
        for (int i = 0; i < kCarpetsCount; ++i)
            if (qstrcmp(kCarpets[i].name, "minecraft:white_carpet") == 0) lgCarpet = quint16(IdCarpetBase + i);   // the parking marking — white
        if (lgCarpet)
            for (const ParkSite& ps : parks)
            {
                QHash<ColKey, int> owner;                                  // a marking cell -> the line number
                int lineNo = 0;
                for (const QVector<QPointF>& ring : ps.rings)
                    for (int e = 0; e < ring.size(); ++e)
                    {
                        const QPointF p0 = ring[e], p1 = ring[(e + 1) % ring.size()];
                        const double len = std::hypot(p1.x() - p0.x(), p1.y() - p0.y());
                        if (len < 8.0) continue;
                        const double dx = (p1.x() - p0.x()) / len, dz = (p1.y() - p0.y()) / len;
                        auto inside = [&](double x, double z) { return ps.cols.contains(pairKey(int(std::floor(x)), int(std::floor(z)))); };
                        double nx = -dz, nz = dx;                           // the normal into the polygon
                        const double mx = (p0.x() + p1.x()) * 0.5, mz = (p0.y() + p1.y()) * 0.5;
                        if (!inside(mx + nx * 2.0, mz + nz * 2.0))
                        {
                            nx = -nx; nz = -nz;
                            if (!inside(mx + nx * 2.0, mz + nz * 2.0)) continue;
                        }
                        for (double t = 3.5; t <= len - 3.5; t += 7.0)
                        {
                            QVector<qint64> cells;
                            bool ok = true;
                            // The parking depth across (up to 36 = 3 line lengths): the first exit from the spot along the normal.
                            // A 12-block line MUST fit entirely, otherwise it is not drawn; if the depth is less than 36, the lines are drawn
                            // only from one side (where the start cell is "earlier" than the exit point), from the opposite one — not.
                            double exitSl = -1.0;
                            int exX = 0, exZ = 0;
                            for (double sl = 0.5; sl <= 36.0; sl += 0.5)
                            {
                                const double x = p0.x() + dx * t + nx * sl, z = p0.y() + dz * t + nz * sl;
                                const int fx = int(std::floor(x)), fz = int(std::floor(z));
                                const qint64 ck = pairKey(fx, fz);
                                if (!ps.cols.contains(ck)) { exitSl = sl; exX = fx; exZ = fz; break; }
                                if (sl <= 12.0 && (cells.isEmpty() || cells.last() != ck)) cells.append(ck);
                            }
                            if (exitSl >= 0.0 && exitSl <= 12.0) continue;              // the line length is greater than the parking lot — no lines
                            if (cells.size() < 5) continue;
                            if (exitSl >= 0.0)
                            {   // depth < 36: only one of the two opposite sides
                                const int sx0 = keyFirst(cells.first()), sz0 = keySecond(cells.first());
                                if (!(sx0 < exX || (sx0 == exX && sz0 < exZ))) continue;
                            }
                            for (qint64 ck : cells)
                            {   // touches another object — the line is not drawn
                                const int cxk = keyFirst(ck), czk = keySecond(ck);
                                if (colMin.contains(ck) || roadCols.contains(ck) || railColsAll.contains(ck) || colWater.contains(ck)
                                    || store.get(cxk, surfaceTop(ck) + 1, czk) != IdAir) { ok = false; break; }
                            }
                            if (!ok) continue;
                            for (qint64 ck : cells)
                            {   // touching someone else's line (3×3) — cancel
                                for (int ax = -1; ax <= 1 && ok; ++ax)
                                    for (int az = -1; az <= 1; ++az)
                                    {
                                        const auto oit = owner.constFind(pairKey(keyFirst(ck) + ax, keySecond(ck) + az));
                                        if (oit != owner.constEnd() && oit.value() != lineNo) { ok = false; break; }
                                    }
                                if (!ok) break;
                            }
                            if (!ok) continue;
                            for (qint64 ck : cells) owner.insert(ck, lineNo);
                            ++lineNo;
                        }
                    }
                for (auto it = owner.constBegin(); it != owner.constEnd(); ++it)
                {
                    const int x = keyFirst(it.key()), z = keySecond(it.key());
                    const int y = surfaceTop(it.key()) + 1;
                    if (y > 254 || store.get(x, y, z) != IdAir) continue;
                    store.set(x, y, z, lgCarpet);
                }
                // STONE FENCE of a parking lot with marking: a ring of columns right behind the spot edge (by 8 neighbors). The fence does not touch the roadway: columns within 1 block of
                // a road (including an adjacent one) stay an opening — the entrance is there. It does not climb onto buildings, water, rails, other parking lots; if there is no entrance (the parking lot does not adjoin a road),
                // we leave a three-cell opening at a random place of the ring.
                if (!owner.isEmpty())
                {
                    QVector<qint64> ringW;
                    for (qint64 ck : ps.cols)
                    {
                        const int bx = keyFirst(ck), bz = keySecond(ck);
                        for (int ddz = -1; ddz <= 1; ++ddz)
                            for (int ddx = -1; ddx <= 1; ++ddx)
                            {
                                if (!ddx && !ddz) continue;
                                const qint64 nk = pairKey(bx + ddx, bz + ddz);
                                if (!ps.cols.contains(nk)) ringW << nk;
                            }
                    }
                    std::sort(ringW.begin(), ringW.end());
                    ringW.erase(std::unique(ringW.begin(), ringW.end()), ringW.end());
                    auto nearRoad = [&](qint64 ck)
                    {
                        const int bx = keyFirst(ck), bz = keySecond(ck);
                        for (int ddz = -1; ddz <= 1; ++ddz)
                            for (int ddx = -1; ddx <= 1; ++ddx)
                                if (roadCols.contains(pairKey(bx + ddx, bz + ddz))) return true;
                        return false;
                    };
                    QVector<char> skip(ringW.size(), 0);
                    bool anyEntrance = false;
                    for (int i = 0; i < ringW.size(); ++i)
                        if (nearRoad(ringW[i])) { skip[i] = 1; anyEntrance = true; }
                    if (!anyEntrance && ringW.size() > 12)
                    {
                        const int g0 = int(colHash(ringW.first() ^ 0x9A7Eull) % quint32(ringW.size()));
                        for (int q = 0; q < 3; ++q) skip[(g0 + q) % ringW.size()] = 1;
                    }
                    int nWall = 0;
                    for (int i = 0; i < ringW.size(); ++i)
                    {
                        if (skip[i]) continue;
                        const qint64 ck = ringW[i];
                        if (colMin.contains(ck) || colWater.contains(ck) || railColsAll.contains(ck) || roadCols.contains(ck)) continue;
                        bool other = false;
                        for (const ParkSite& po : parks) if (&po != &ps && po.cols.contains(ck)) { other = true; break; }
                        if (other) continue;
                        const int x = keyFirst(ck), z = keySecond(ck);
                        const int y = surfaceTop(ck) + 1;
                        if (y > 253 || store.get(x, y, z) != IdAir) continue;
                        store.set(x, y, z, IdStoneFence);
                        ++nWall;
                    }
                    nParkWall += nWall;
                }
            }
    }

    // VILLAGES (request): raids in Minecraft happen in a "village" — an area with villager points of interest (a bell, beds,
    // work blocks). Private houses already carry beds; to make the village real, in the places where private houses cluster
    // (clusters on a 128-block grid) we put a square: a bell, a well, work blocks, lanterns, three villagers and a golem.
    // Villages — NOT closer than 1000 blocks to each other; the square is built at a free place near the cluster center,
    // trees and grass on it are cut down (VoxelStore::erase).
    beginStep(10);                                                   // "villages, signs, basements"
    if (!privCenters.isEmpty())
    {
        struct Bucket { qint64 sx = 0, sz = 0; int n = 0; };
        QHash<ColKey, Bucket> buckets;
        for (const QPoint& pc : privCenters)
        {
            Bucket& b = buckets[pairKey(int(std::floor(pc.x() / 128.0)), int(std::floor(pc.y() / 128.0)))];
            b.sx += pc.x(); b.sz += pc.y(); ++b.n;
        }
        QVector<QPair<int, qint64>> order2;                      // (the number of houses, the cell key) — in descending order
        for (auto it = buckets.constBegin(); it != buckets.constEnd(); ++it)
            order2.append(qMakePair(it.value().n, it.key()));
        std::sort(order2.begin(), order2.end(), [](const QPair<int, qint64>& a, const QPair<int, qint64>& b)
        { return a.first != b.first ? a.first > b.first : a.second < b.second; });
        // The distance between villages — 500 blocks (request: "count by half"; in big areas the villages come out twice
        // as often along each axis).
        QVector<QPoint> vcenters;
        for (const auto& oc : order2)
        {
            const Bucket& b = buckets[oc.second];
            if (b.n < 3 && !vcenters.isEmpty()) break;            // single houses do not form villages themselves
            const QPoint c(int(b.sx / b.n), int(b.sz / b.n));
            const double minD = 500.0;
            bool far = true;
            for (const QPoint& q : vcenters)
                if (std::hypot(double(q.x() - c.x()), double(q.y() - c.y())) < minD) { far = false; break; }
            if (far) vcenters.append(c);
        }
        auto isVeg = [](quint16 bid)
        {
            return bid == IdAir || bid == IdOakLog || bid == IdOakLeaves || bid == IdSpruceLeaves || bid == IdBirchLeaves || bid == IdRedPoplarLeaves || bid == IdOrangePoplarLeaves || bid == IdYellowPoplarLeaves || bid == IdPaleLog || bid == IdPaleLeaves || bid == IdPaleMossCarpet || bid == IdLeafLitter
                || bid == IdPaleHangingMoss || bid == IdEyeOpen || bid == IdEyeClosed || bid == IdGrassPlant || bid == IdTallGrass
                || bid == IdTallGrassUpper || bid == IdWheat || (bid >= IdFlowerBase && bid < IdFlowerMax) || isForestExtraVeg(bid);
        };
        // The site (2R+1)² is free: no houses/water/roads/rails/csrpol wool, the height difference <= 3, only grass/trees on it.
        auto siteOk = [&](int cx, int cz, int R) -> bool
        {
            int tmin = 1 << 20, tmax = -(1 << 20);
            for (int dz = -R; dz <= R; ++dz)
                for (int dx = -R; dx <= R; ++dx)
                {
                    const int x = cx + dx, z = cz + dz;
                    const qint64 ck = pairKey(x, z);
                    if (colMin.contains(ck) || colWater.contains(ck) || colWool.contains(ck)
                        || roadCols.contains(ck) || railColsAll.contains(ck)) return false;
                    if (!ground.contains(pairKey(x >> 4, z >> 4))) return false;
                    const int t = surfaceTop(ck);
                    if (t > 235) return false;
                    tmin = std::min(tmin, t); tmax = std::max(tmax, t);
                    if (tmax - tmin > 3) return false;
                    for (int y = t + 1; y <= t + 8; ++y)
                        if (!isVeg(store.get(x, y, z))) return false;       // a fence/post/furniture — the place is taken
                }
            return true;
        };
        // Searching for a site by rings around vc (a step of 3 blocks along the ring), up to the radius maxR.
        auto findSite = [&](const QPoint& vc, int R, int maxR, int& ox, int& oz) -> bool
        {
            // Rings with a step of 1 up to a radius of 32 and 2 beyond: earlier a step of 3 could skip a free spot exactly the size of the
            // square (the center did not hit the grid) and give a false "no place" -> an extra house demolition.
            for (int r = 0; r <= maxR; r += (r < 32 ? 1 : 2))
            {
                const int aStep = (r < 32) ? 1 : 2;
                for (int a = -r; a <= r; a += (r ? aStep : 1))
                {
                    const int cand[4][2] = { { a, -r }, { a, r }, { -r, a }, { r, a } };
                    for (int q = 0; q < (r ? 4 : 1); ++q)
                        if (siteOk(vc.x() + cand[q][0], vc.y() + cand[q][1], R))
                        { ox = vc.x() + cand[q][0]; oz = vc.y() + cand[q][1]; return true; }
                }
            }
            return false;
        };
        // Demolition of the private house nearest to vc (allowed by the user if there is no place): we erase its blocks, columns,
        // entities and basement. It returns the center of the freed place.
        QSet<int> demolished;
        auto demolishNear = [&](const QPoint& vc, QPoint& outC) -> bool
        {
            int best = -1; double bd = 1e18;
            for (int i = 0; i < privCenters.size(); ++i)
            {
                if (demolished.contains(i)) continue;
                const double d = std::hypot(double(privCenters[i].x() - vc.x()), double(privCenters[i].y() - vc.y()));
                if (d < bd) { bd = d; best = i; }
            }
            if (best < 0 || bd > 160.0) return false;
            demolished.insert(best);
            const QRect bx = privBoxes[best];
            const int x0 = bx.x() - 1, x1 = bx.x() + bx.width(), z0 = bx.y() - 1, z1 = bx.y() + bx.height();
            int ymin = 1 << 20;
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x)
                {
                    const auto it = colMin.constFind(pairKey(x, z));
                    if (it != colMin.constEnd()) ymin = std::min(ymin, it.value());
                }
            if (ymin == (1 << 20)) return false;
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x)
                {
                    for (int y = std::max(-64, ymin - 2); y <= std::min(319, ymin + 70); ++y)
                        if (store.get(x, y, z) != IdAir) store.erase(x, y, z);
                    colMin.remove(pairKey(x, z));
                    colAnchor.remove(pairKey(x, z));
                    // the ground top in the metadata was raised under the house foundation (max(..., colMin-1)): we return the natural
                    // level, otherwise the village square will stand on the house "pedestal", and surfaceTop will diverge from ground.top
                    auto gh = ground.find(pairKey(x >> 4, z >> 4));
                    if (gh != ground.end())
                    {
                        const int col = (z & 15) * 16 + (x & 15);
                        if (gh->wd[col] == 0)
                            gh->top[col] = qint16(groundTopCol(x, z));
                    }
                }
            auto inBox = [&](int x, int z) { return x >= x0 && x <= x1 && z >= z0 && z <= z1; };
            auto prune = [&](auto& v) { for (int i = v.size() - 1; i >= 0; --i) if (inBox(v[i].x, v[i].z)) v.removeAt(i); };
            prune(chests); prune(paints); prune(mannequins); prune(spawners); prune(npcs); prune(frames);
            for (int i = basements.size() - 1; i >= 0; --i)
                if (inBox(basements[i].cx, basements[i].cz)) basements.removeAt(i);
            outC = bx.center();
            return true;
        };
        const quint16 cobble = quint16(IdPaletteStart + paletteIndexOf("minecraft:cobblestone"));
        const bool forceDemolish = qEnvironmentVariableIsSet("MCX_FORCE_DEMOLISH");   // a test hook
        for (const QPoint& vc : vcenters)
        {
            int px = 0, pz = 0, R = 5;
            bool placed = false;
            if (!forceDemolish)
            {
                placed = findSite(vc, 5, 128, px, pz);                        // the full 11×11 square
                if (!placed) { R = 3; placed = findSite(vc, 3, 128, px, pz); }   // a compact 7×7
            }
            if (!placed)
            {   // no place — we demolish the nearest house and put the square in its place
                QPoint c;
                if (demolishNear(vc, c))
                {
                    R = 5; placed = findSite(c, 5, 12, px, pz);
                    if (!placed) { R = 3; placed = findSite(c, 3, 12, px, pz); }
                }
            }
            if (!placed) continue;
            auto topAt = [&](int x, int z) { return surfaceTop(pairKey(x, z)); };
            // we cut down trees/grass on the square and above it
            for (int dz = -R; dz <= R; ++dz)
                for (int dx = -R; dx <= R; ++dx)
                {
                    const int x = px + dx, z = pz + dz, t = topAt(x, z);
                    for (int y = t + 1; y <= t + 16; ++y)
                    {
                        const quint16 bid = store.get(x, y, z);
                        if (bid != IdAir && isVeg(bid)) store.erase(x, y, z);
                    }
                }
            auto put = [&](int dx, int dz, int dy, quint16 bid)
            { const int x = px + dx, z = pz + dz; store.set(x, topAt(x, z) + dy, z, bid); };
            put(0, 0, 1, IdBell);                                         // the bell — the center
            bellSites.append(BellSite{ px, topAt(px, pz) + 1, pz });
            {   // trampled earth: the square spot (a circle R+0.5) — a path with coarse_dirt inclusions, plus 4 trails of 7 blocks
                auto setSurf = [&](int x, int z, quint16 sid) -> bool
                {
                    auto git = ground.find(pairKey(x >> 4, z >> 4));
                    if (git == ground.end()) return false;
                    const int col = (z & 15) * 16 + (x & 15);
                    if (git->wd[col] > 0 || git->surf[col] != IdGrass) return false;
                    git->surf[col] = sid;
                    return true;
                };
                for (int dz = -R; dz <= R; ++dz)
                    for (int dx = -R; dx <= R; ++dx)
                    {
                        if (dx * dx + dz * dz > (R + 0.5) * (R + 0.5)) continue;
                        const int x = px + dx, z = pz + dz;
                        setSurf(x, z, (colHash(pairKey(x, z) ^ 0xD177ull) % 4u == 0u) ? IdCoarseDirt : IdDirtPath);
                    }
                static const int ax[4] = { 1, -1, 0, 0 }, az[4] = { 0, 0, 1, -1 };
                for (int d = 0; d < 4; ++d)
                    for (int k = R + 1; k <= R + 7; ++k)
                    {
                        const int x = px + ax[d] * k, z = pz + az[d] * k;
                        const qint64 ck = pairKey(x, z);
                        if (colMin.contains(ck) || colWater.contains(ck) || colWool.contains(ck)
                            || roadCols.contains(ck) || railColsAll.contains(ck)) break;
                        if (!setSurf(x, z, IdDirtPath)) break;
                        const int t = topAt(x, z);
                        for (int y = t + 1; y <= t + 4; ++y)
                            if (isVeg(store.get(x, y, z)) && store.get(x, y, z) != IdAir) store.erase(x, y, z);
                    }
            }
            if (R >= 5)
            {
                // a 3×3 well at the right-back: a ring of cobblestone, water inside, 4 posts and a slab roof
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -5; dx <= -3; ++dx)
                        put(dx, dz, 1, (dx == -4 && dz == 0) ? quint16(IdWater) : cobble);
                for (int sxp = -5; sxp <= -3; sxp += 2)
                    for (int szp = -1; szp <= 1; szp += 2)
                    { put(sxp, szp, 2, IdFence); put(sxp, szp, 3, IdFence); }
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -5; dx <= -3; ++dx)
                        put(dx, dz, 4, IdSlabBottom);
                // lanterns on posts at the corners of the square
                for (int sxp = -3; sxp <= 3; sxp += 6)
                    for (int szp = -4; szp <= 4; szp += 8)
                    { put(sxp, szp, 1, IdFence); put(sxp, szp, 2, IdFence); put(sxp, szp, 3, IdLanternStanding); }
            }
            // villager work blocks around the bell
            put(3, 0, 1, IdComposter);
            put(0, 3, 1, IdBarrel);
            put(0, -3, 1, IdSmithingTable);
            put(3, 3, 1, IdLoom);
            // villagers and a golem
            static const int kProf[4] = { PfFarmer, PfLibrarian, PfCleric, PfToolsmith };
            for (int ni = 0; ni < 3; ++ni)
            {
                NpcSite v;
                v.kind = 0; v.prof = kProf[ni % 4]; v.level = 2 + ni;
                v.x = px + (ni == 0 ? 1 : ni == 1 ? -1 : 2); v.z = pz + (ni == 0 ? 1 : ni == 1 ? 2 : -1);
                v.y = topAt(v.x, v.z) + 1;
                v.vtype = 0; v.yaw = ni * 90;
                npcs.append(v);
            }
            NpcSite g; g.kind = 3; g.x = px - 1; g.z = pz - 2; g.y = topAt(g.x, g.z) + 1;
            npcs.append(g);
        }
    }

    // SIGNS WITH ADDRESSES: one per house, on the side of the nearest road (searching outward up to 24 blocks over road columns);
    // no road nearby — the first suitable side. Line 1 — TYSTR, line 2 — STREET, line 3 — NUMBR+ADDNUM.
    {
        static const int sdx[4] = { 0, 1, 0, -1 }, sdz[4] = { -1, 0, 1, 0 };
        for (const SignCand& sc : signCands)
        {
            int bestD = -1, bestDist = 1 << 20;
            for (int d = 0; d < 4; ++d)
            {
                if (!sc.ok[d]) continue;
                int dist = 99;
                for (int k = 1; k <= 24; ++k)
                    if (roadCols.contains(pairKey(sc.x[d] + sdx[d] * k, sc.z[d] + sdz[d] * k))) { dist = k; break; }
                if (dist < bestDist) { bestDist = dist; bestD = d; }
            }
            if (bestD < 0) continue;
            const int x = sc.x[bestD], z = sc.z[bestD];
            if (store.get(x, sc.y, z) != IdAir) continue;                           // the cell is taken by someone else's block
            if (store.get(x - sdx[bestD], sc.y, z - sdz[bestD]) == IdAir) continue; // the wall is gone (the house was demolished)
            store.set(x, sc.y, z, quint16(int(IdWallSignBase) + bestD));
            signSites.append(SignSite{ x, sc.y, z, sc.l1, sc.l2, sc.l3 });
        }
    }
    for (const BannerSite& bn : bannerSites)
        bannersByChunk[pairKey(bn.x >> 4, bn.z >> 4)].append(bn);
    for (const LairZone& z : lairZones)
        for (int zcz = z.z0 >> 4; zcz <= (z.z1 >> 4); ++zcz)
            for (int zcx = z.x0 >> 4; zcx <= (z.x1 >> 4); ++zcx)
                zonesByChunk[pairKey(zcx, zcz)].append(z);
    for (const BellSite& bl : bellSites)
        bellsByChunk[pairKey(bl.x >> 4, bl.z >> 4)].append(bl);

    // SIGNPOSTS AT INTERSECTIONS (request): named rdslin streets (TYSTR+STREET). An intersection — the places where the roadways of two DIFFERENT
    // named streets cross or touch (columns with a neighbor of another name). Close points (up to 10 blocks) are merged into
    // one intersection; next to it on free ground by the roadway we put a stone_brick_wall post 4 high and on its sides — signs
    // (waxed, like on houses): line 1 — TYSTR, line 2 — STREET. More than 4 streets — a second row of signs below (up to 8). The post — stone_brick_wall 4 high, a lantern on top.
    {
        static const int ndx[4] = { 1, -1, 0, 0 }, ndz[4] = { 0, 0, 1, -1 };
        // Real intersections — where the AXIS lines of two different roads cross (or abut each other, a tolerance of 1.5 blocks)
        // at an angle of at least ~12°; the roadway width does not matter. At least one name is needed; pedestrian/bike paths
        // are counted only if they have a name themselves; one and the same street (the same TYSTR+STREET) does not form an intersection.
        {
            struct Sg { int line; double x0, z0, x1, z1; };
            QVector<Sg> sgs;
            QHash<ColKey, QVector<int>> sgGrid;
            constexpr double kCell = 24.0, kTol = 1.5;
            for (int li = 0; li < roadLines.size(); ++li)
            {
                const QVector<QPointF>& pt = roadLines[li].pts;
                for (int k = 0; k + 1 < pt.size(); ++k)
                {
                    const Sg g{ li, pt[k].x(), pt[k].y(), pt[k + 1].x(), pt[k + 1].y() };
                    if (std::hypot(g.x1 - g.x0, g.z1 - g.z0) < 1e-6) continue;
                    const int idx = sgs.size();
                    sgs.append(g);
                    const int cx0 = int(std::floor((std::min(g.x0, g.x1) - kTol) / kCell)), cx1 = int(std::floor((std::max(g.x0, g.x1) + kTol) / kCell));
                    const int cz0 = int(std::floor((std::min(g.z0, g.z1) - kTol) / kCell)), cz1 = int(std::floor((std::max(g.z0, g.z1) + kTol) / kCell));
                    for (int cz = cz0; cz <= cz1; ++cz)
                        for (int cx = cx0; cx <= cx1; ++cx)
                            sgGrid[pairKey(cx, cz)].append(idx);
                }
            }
            for (auto it = sgGrid.constBegin(); it != sgGrid.constEnd(); ++it)
            {
                const QVector<int>& lst = it.value();
                const int cellX = keyFirst(it.key()), cellZ = keySecond(it.key());
                for (int ia = 0; ia < lst.size(); ++ia)
                    for (int ib = ia + 1; ib < lst.size(); ++ib)
                    {
                        const Sg& a = sgs[lst[ia]]; const Sg& b = sgs[lst[ib]];
                        const RoadLine& la = roadLines[a.line]; const RoadLine& lb = roadLines[b.line];
                        if (la.obj == lb.obj) continue;                              // parts of one road
                        const bool namedA = la.nameId >= 0, namedB = lb.nameId >= 0;
                        if (!namedA && !namedB) continue;                           // at least one name is needed
                        if (namedA && namedB && la.nameId == lb.nameId) continue;   // the same street
                        if ((la.ped && !namedA) || (lb.ped && !namedB)) continue;   // an unnamed pedestrian/bike path — not an intersection
                        const double d1x = a.x1 - a.x0, d1z = a.z1 - a.z0, d2x = b.x1 - b.x0, d2z = b.z1 - b.z0;
                        const double len1 = std::hypot(d1x, d1z), len2 = std::hypot(d2x, d2z);
                        const double cr = d1x * d2z - d1z * d2x;
                        if (std::abs(cr) / (len1 * len2) < 0.2) continue;           // almost parallel — not an intersection
                        const double ex = b.x0 - a.x0, ez = b.z0 - a.z0;
                        const double t = (ex * d2z - ez * d2x) / cr;                // the position on a
                        const double u = (ex * d1z - ez * d1x) / cr;                // the position on b
                        const double tolA = kTol / len1, tolB = kTol / len2;
                        if (t < -tolA || t > 1.0 + tolA || u < -tolB || u > 1.0 + tolB) continue;
                        const double px = a.x0 + t * d1x, pz = a.z0 + t * d1z;
                        if (int(std::floor(px / kCell)) != cellX || int(std::floor(pz / kCell)) != cellZ) continue;   // we count in one cell
                        juncPts.append(JuncPt{ int(std::floor(px)), int(std::floor(pz)), la.nameId, lb.nameId });
                    }
            }
        }
        std::sort(juncPts.begin(), juncPts.end(), [](const JuncPt& a, const JuncPt& b)
        { return a.x != b.x ? a.x < b.x : (a.z != b.z ? a.z < b.z : (a.a != b.a ? a.a < b.a : a.b < b.b)); });
        QHash<ColKey, QVector<int>> jGrid;                         // a 10×10 grid for merging the points
        for (int i = 0; i < juncPts.size(); ++i)
            jGrid[pairKey(int(std::floor(juncPts[i].x / 10.0)), int(std::floor(juncPts[i].z / 10.0)))].append(i);
        QVector<char> jUsed(juncPts.size(), 0);
        auto isVegJ = [](quint16 bid)
        {
            return bid == IdAir || bid == IdOakLog || bid == IdOakLeaves || bid == IdSpruceLeaves || bid == IdBirchLeaves || bid == IdRedPoplarLeaves || bid == IdOrangePoplarLeaves || bid == IdYellowPoplarLeaves || bid == IdPaleLog || bid == IdPaleLeaves || bid == IdPaleMossCarpet || bid == IdLeafLitter
                || bid == IdPaleHangingMoss || bid == IdEyeOpen || bid == IdEyeClosed || bid == IdGrassPlant || bid == IdTallGrass
                || bid == IdTallGrassUpper || bid == IdWheat || (bid >= IdFlowerBase && bid < IdFlowerMax) || isForestExtraVeg(bid);
        };
        static const int sdxJ[4] = { 0, 1, 0, -1 }, sdzJ[4] = { -1, 0, 1, 0 };
        int posts = 0;
        for (int i = 0; i < juncPts.size(); ++i)
        {
            if (jUsed[i]) continue;
            QSet<int> names;
            double sxm = 0, szm = 0; int cnt = 0;
            const int gx = int(std::floor(juncPts[i].x / 10.0)), gz = int(std::floor(juncPts[i].z / 10.0));
            for (int dgx = -1; dgx <= 1; ++dgx)
                for (int dgz = -1; dgz <= 1; ++dgz)
                {
                    const auto git = jGrid.constFind(pairKey(gx + dgx, gz + dgz));
                    if (git == jGrid.constEnd()) continue;
                    for (int q : git.value())
                    {
                        if (jUsed[q]) continue;
                        if (std::hypot(double(juncPts[q].x - juncPts[i].x), double(juncPts[q].z - juncPts[i].z)) > 10.0) continue;
                        jUsed[q] = 1;
                        if (juncPts[q].a >= 0) names.insert(juncPts[q].a);      // -1 — an unnamed road: it does not go into the list of names
                        if (juncPts[q].b >= 0) names.insert(juncPts[q].b);
                        sxm += juncPts[q].x; szm += juncPts[q].z; ++cnt;
                    }
                }
            if (names.isEmpty() || cnt == 0) continue;
            const int cx = int(std::lround(sxm / cnt)), cz = int(std::lround(szm / cnt));
            // the post place: free 1×1 ground next to the roadway (within 2 blocks), closer to the intersection center
            int bestX = 0, bestZ = 0, bestD = 1 << 20;
            for (int r = 1; r <= 9; ++r)
            {
                for (int dz = -r; dz <= r; ++dz)
                    for (int dx = -r; dx <= r; ++dx)
                    {
                        if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                        const int x = cx + dx, z = cz + dz;
                        const qint64 ck = pairKey(x, z);
                        if (roadCols.contains(ck) || colMin.contains(ck) || colWater.contains(ck) || colWool.contains(ck)
                            || railColsAll.contains(ck)) continue;
                        if (!ground.contains(pairKey(x >> 4, z >> 4))) continue;
                        bool nearRoad = false;
                        for (int k = 0; k < 4 && !nearRoad; ++k)
                            for (int st = 1; st <= 2; ++st)
                                if (roadCols.contains(pairKey(x + ndx[k] * st, z + ndz[k] * st))) { nearRoad = true; break; }
                        if (!nearRoad) continue;
                        const int t = surfaceTop(ck);
                        if (t > 245) continue;
                        bool freeCol = true;
                        for (int y = t + 1; y <= t + 5 && freeCol; ++y)
                            if (!isVegJ(store.get(x, y, z))) freeCol = false;
                        for (int k = 0; k < 4 && freeCol; ++k)           // the sign cells on the sides — are free
                            for (int y = t + 2; y <= t + 3; ++y)
                                if (!isVegJ(store.get(x + sdxJ[k], y, z + sdzJ[k]))) { freeCol = false; break; }
                        if (!freeCol) continue;
                        const int d2 = dx * dx + dz * dz;
                        if (d2 < bestD) { bestD = d2; bestX = x; bestZ = z; }
                    }
                if (bestD < (1 << 20)) break;
            }
            if (bestD == (1 << 20)) continue;
            const int t0 = surfaceTop(pairKey(bestX, bestZ));
            for (int k = 0; k < 4; ++k)                                   // clearing the grass under the post and signs
                for (int y = t0 + 1; y <= t0 + 5; ++y)
                {
                    const int x = bestX + (k == 0 ? 0 : sdxJ[k - 1]), z = bestZ + (k == 0 ? 0 : sdzJ[k - 1]);
                    if (store.get(x, y, z) != IdAir && isVegJ(store.get(x, y, z))) store.erase(x, y, z);
                }
            for (int y = t0 + 1; y <= t0 + 4; ++y)
                store.set(bestX, y, bestZ, IdStoneBrickWall);              // a post of stone brick wall
            store.set(bestX, t0 + 5, bestZ, IdLanternStanding);            // a lantern at the very top
            QList<int> nm = names.values();
            std::sort(nm.begin(), nm.end());
            for (int k = 0; k < nm.size() && k < 8; ++k)
            {
                const int d = k % 4, y = t0 + 3 - (k / 4);
                const int x = bestX + sdxJ[d], z = bestZ + sdzJ[d];
                store.set(x, y, z, quint16(int(IdWallSignBase) + d));
                signSites.append(SignSite{ x, y, z, roadNames[nm[k]].first, roadNames[nm[k]].second, QString() });
            }
            ++posts;
        }
        Q_UNUSED(posts);
    }
    for (const SignSite& sg : signSites)
        signsByChunk[pairKey(sg.x >> 4, sg.z >> 4)].append(sg);

    // BASEMENTS of private houses: we take out a room (the ground top in these columns is lowered to wy-5), lay a cobblestone floor,
    // make a hatch in the house floor (iron, covered with a 3×3 carpet) and a vertical ladder (the holder — a block on the north side),
    // a chest or mobs.
    {
        const quint16 cobbleB = quint16(IdPaletteStart + paletteIndexOf("minecraft:cobblestone"));
        // CHURCH DUNGEON: a maze of corridors 2 blocks wide under the building and around it, in 1–2 levels. A maze cell is a 4×4 block
        // square (the grid step is 5: between cells a 1-block wall), the cell core is 2×2, the passage between neighbors is 2 wide (3 blocks long through the wall).
        // "Transition zones" 3×3 / 4×4 — at forks (always), turns (sometimes) and dead ends (always); a dead end — a room with a chest.
        // The entrance — a ladder from the church; the lower level is connected by a ladder from the farthest dead end of the upper one. The maze walls — the ground itself
        // (only corridors are carved: the ground top in a column is lowered, the ceiling and floor are added with blocks). Chests in dead ends (2–4), an expensive one —
        // one, in the deepest dead end (guarded by 3 mobs); zombies, skeletons, spiders, witches and endermen roam the corridors.
        // deepSpots/deepFloor (optional): the chest cells on the LOWEST level of the maze (not the "expensive" chest — earlier) and the level of their floor —
        // a secret shaft is hidden under one of such chests later.
        auto buildChurchDungeon = [&](const BasementSite& bs, QVector<QPoint>* deepSpots = nullptr, int* deepFloor = nullptr) -> bool
        {
            const int Wy = bs.wy;
            quint32 seed = colHash(pairKey(bs.hx, bs.hz) ^ 0xD06E0ull) | 1u;
            auto rnd = [&seed](int mod) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return mod > 0 ? int(seed % quint32(mod)) : 0; };
            auto colAt = [&](int x, int z, int& col) -> GroundChunk*
            {
                auto it = ground.find(pairKey(x >> 4, z >> 4));
                if (it == ground.end()) return nullptr;
                col = (z & 15) * 16 + (x & 15);
                return &it.value();
            };
            const int fl1 = Wy - 7, fl2 = Wy - 11;
            auto usable = [&](int x, int z, int floorY) -> bool
            {
                if (x == bs.hx && z == bs.hz && floorY != fl1) return false;             // the descent column belongs only to the 1st level
                int col; GroundChunk* g = colAt(x, z, col);
                if (!g || g->wd[col] > 0) return false;
                if (g->top[col] < floorY + 3) return false;                              // above the corridor ceiling at least 1 layer of ground is needed
                for (int y = floorY; y <= floorY + 2; ++y)
                    if (store.get(x, y, z) != IdAir) return false;
                return true;
            };
            static const int dxs[4] = { 0, 1, 0, -1 }, dzs[4] = { -1, 0, 1, 0 };          // 0=N,1=E,2=S,3=W
            const int N = 6, M = 6, P = 5;
            struct Maze { QVector<char> vis, e, s; int cnt = 0; };                         // e — a passage to the east, s — to the south of the cell
            auto newMaze = [&]() { Maze m; m.vis.fill(0, N * M); m.e.fill(0, N * M); m.s.fill(0, N * M); return m; };
            auto passage = [&](const Maze& m, int i, int j, int d) -> bool
            {   // is there a passage from cell (i,j) in direction d
                if (d == 1) return i + 1 < N && m.e[j * N + i];
                if (d == 3) return i > 0 && m.e[j * N + i - 1];
                if (d == 2) return j + 1 < M && m.s[j * N + i];
                return j > 0 && m.s[(j - 1) * N + i];
            };
            int ox = 0, oz = 0;
            auto cx0 = [&](int i) { return ox + P * i + 1; };
            auto cz0 = [&](int j) { return oz + P * j + 1; };
            auto coreOk = [&](int i, int j, int fy) -> bool
            {
                for (int a = 1; a <= 2; ++a)
                    for (int b = 1; b <= 2; ++b)
                        if (!usable(cx0(i) + a, cz0(j) + b, fy)) return false;
                return true;
            };
            // passage blocks: from (i,j) to the east — x cx0+3..cx0+5, z cz0+1..cz0+2; to the south — z cz0+3..cz0+5, x cx0+1..cx0+2
            auto passOk = [&](int i, int j, int d, int fy) -> bool
            {
                if (d == 1) { for (int a = 3; a <= 5; ++a) for (int b = 1; b <= 2; ++b) if (!usable(cx0(i) + a, cz0(j) + b, fy)) return false; }
                else        { for (int a = 1; a <= 2; ++a) for (int b = 3; b <= 5; ++b) if (!usable(cx0(i) + a, cz0(j) + b, fy)) return false; }
                return true;
            };
            auto genMaze = [&](int fy, int sI, int sJ, int blockDir, Maze& m) -> int
            {
                m = newMaze();
                if (!coreOk(sI, sJ, fy)) return 0;
                QVector<QPoint> stk;
                stk.append(QPoint(sI, sJ)); m.vis[sJ * N + sI] = 1; m.cnt = 1;
                while (!stk.isEmpty())
                {
                    const QPoint c = stk.last();
                    int cand[4], nc = 0;
                    for (int d = 0; d < 4; ++d)
                    {
                        const int ni = c.x() + dxs[d], nj = c.y() + dzs[d];
                        if (ni < 0 || ni >= N || nj < 0 || nj >= M || m.vis[nj * N + ni]) continue;
                        if (c.x() == sI && c.y() == sJ && d == blockDir) continue;
                        if (!coreOk(ni, nj, fy)) continue;
                        const bool fwd = (d == 1 || d == 2);
                        if (!(fwd ? passOk(c.x(), c.y(), d, fy) : passOk(ni, nj, (d == 3) ? 1 : 2, fy))) continue;
                        cand[nc++] = d;
                    }
                    if (nc == 0) { stk.removeLast(); continue; }
                    const int d = cand[rnd(nc)];
                    const int ni = c.x() + dxs[d], nj = c.y() + dzs[d];
                    if (d == 1) m.e[c.y() * N + c.x()] = 1; else if (d == 3) m.e[nj * N + ni] = 1;
                    else if (d == 2) m.s[c.y() * N + c.x()] = 1; else m.s[nj * N + ni] = 1;
                    m.vis[nj * N + ni] = 1; ++m.cnt;
                    stk.append(QPoint(ni, nj));
                }
                return m.cnt;
            };
            struct MazeInfo { QVector<int> dist; QVector<QPoint> leaves; QVector<QPoint> order; };
            auto analyze = [&](const Maze& m, int sI, int sJ) -> MazeInfo
            {
                MazeInfo mi;
                mi.dist.fill(-1, N * M);
                QVector<QPoint> q; q.append(QPoint(sI, sJ)); mi.dist[sJ * N + sI] = 0;
                for (int h = 0; h < q.size(); ++h)
                {
                    const QPoint c = q[h];
                    mi.order.append(c);
                    int deg = 0;
                    for (int d = 0; d < 4; ++d)
                    {
                        if (!passage(m, c.x(), c.y(), d)) continue;
                        ++deg;
                        const int ni = c.x() + dxs[d], nj = c.y() + dzs[d];
                        if (mi.dist[nj * N + ni] >= 0) continue;
                        mi.dist[nj * N + ni] = mi.dist[c.y() * N + c.x()] + 1;
                        q.append(QPoint(ni, nj));
                    }
                    if (deg == 1 && !(c.x() == sI && c.y() == sJ)) mi.leaves.append(c);
                }
                return mi;
            };
            // ---- level 1: the entrance — the core of cell (2,0), the descent column = its north-west core block = (hx,hz)
            const int sI1 = 2, sJ1 = 0;
            ox = bs.hx - (P * sI1 + 2); oz = bs.hz - 2;
            Maze mz1, mz2;
            const int n1 = genMaze(fl1, sI1, sJ1, 0, mz1);                               // the north is closed: the ladder holder is there
            if (n1 < 10) return false;
            MazeInfo m1 = analyze(mz1, sI1, sJ1);
            // ---- the lower level: from the farthest dead end of the upper one (the ladder at the wall of the dead-end room farthest from the passage)
            int connI = -1, connJ = -1, connOd = -1, n2 = 0;
            QPoint ladd2(0, 0);                                                           // the ladder block between levels (a column)
            int ladFace = 0;                                                              // where the ladder faces (the index IdLadderN+..)
            MazeInfo m2;
            if (!m1.leaves.isEmpty() && rnd(100) < 70)
            {
                QPoint far = m1.leaves.first();
                for (const QPoint& l : m1.leaves)
                    if (m1.dist[l.y() * N + l.x()] > m1.dist[far.y() * N + far.x()]) far = l;
                int od = -1;
                for (int d = 0; d < 4; ++d) if (passage(mz1, far.x(), far.y(), d)) od = d;
                const int f = (od + 2) % 4;                                               // the far wall of the dead end — the holder
                int lx = cx0(far.x()) + 1, lz = cz0(far.y()) + 1;
                if (f == 2) lz = cz0(far.y()) + 2;
                if (f == 1) lx = cx0(far.x()) + 2;
                n2 = genMaze(fl2, far.x(), far.y(), f, mz2);
                if (n2 >= 8)
                {
                    connI = far.x(); connJ = far.y(); connOd = od; ladd2 = QPoint(lx, lz);
                    ladFace = (f + 2) % 4;
                    m2 = analyze(mz2, connI, connJ);
                }
                else n2 = 0;
            }
            // ---- the set of level columns: cores, passages, rooms 3×3/4×4 (forks, turns, dead ends)
            struct RoomR { int x0, z0, x1, z1; };
            QHash<int, RoomR> rooms1, rooms2;
            auto buildCols = [&](const Maze& m, int fy, bool isLower, QHash<int, RoomR>& rooms) -> QVector<QPoint>
            {
                QSet<ColKey> seen; QVector<QPoint> out;
                auto add = [&](int x, int z) { if (seen.contains(pairKey(x, z))) return; seen.insert(pairKey(x, z)); out.append(QPoint(x, z)); };
                for (int j = 0; j < M; ++j)
                    for (int i = 0; i < N; ++i)
                    {
                        if (!m.vis[j * N + i]) continue;
                        for (int a = 1; a <= 2; ++a) for (int b = 1; b <= 2; ++b) add(cx0(i) + a, cz0(j) + b);          // the core
                        if (i + 1 < N && m.e[j * N + i]) for (int a = 3; a <= 5; ++a) for (int b = 1; b <= 2; ++b) add(cx0(i) + a, cz0(j) + b);
                        if (j + 1 < M && m.s[j * N + i]) for (int a = 1; a <= 2; ++a) for (int b = 3; b <= 5; ++b) add(cx0(i) + a, cz0(j) + b);
                        // a room
                        int deg = 0, dirs[4] = { 0, 0, 0, 0 };
                        for (int d = 0; d < 4; ++d) if (passage(m, i, j, d)) { ++deg; dirs[d] = 1; }
                        const bool isEntrance = (!isLower && i == sI1 && j == sJ1);
                        const bool isConn = (connI >= 0 && i == connI && j == connJ);
                        if (isEntrance || (isConn && isLower)) continue;                  // the entrance and the start of the lower level — only the core
                        const bool straight = (deg == 2 && ((dirs[0] && dirs[2]) || (dirs[1] && dirs[3])));
                        int size = 0;
                        if (deg == 1) size = (rnd(2) ? 4 : 3);
                        if (isConn && !isLower) size = 3;                                  // the connector room — only 3×3, oriented toward the passage
                        else if (deg >= 3) size = (rnd(100) < 60 ? 4 : 3);
                        else if (deg == 2 && !straight && rnd(100) < 45) size = 3;
                        if (!size) continue;
                        RoomR r;
                        if (size == 4) r = RoomR{ cx0(i), cz0(j), cx0(i) + 3, cz0(j) + 3 };
                        else
                        {   // 3×3 inside the cell, must contain the core; at a dead end it grows toward the passage
                            int ox3 = rnd(2), oz3 = rnd(2);
                            if (deg == 1)
                            {
                                const int od = dirs[0] ? 0 : dirs[1] ? 1 : dirs[2] ? 2 : 3;
                                if (od == 0) oz3 = 0; else if (od == 2) oz3 = 1;
                                if (od == 3) ox3 = 0; else if (od == 1) ox3 = 1;
                            }
                            r = RoomR{ cx0(i) + ox3, cz0(j) + oz3, cx0(i) + ox3 + 2, cz0(j) + oz3 + 2 };
                        }
                        bool ok = true;
                        for (int z = r.z0; z <= r.z1 && ok; ++z) for (int x = r.x0; x <= r.x1; ++x) if (!usable(x, z, fy)) { ok = false; break; }
                        if (!ok) continue;
                        // the ladder column down (the L1 connector) must not fall out of the holder wall: the connector room is already oriented
                        for (int z = r.z0; z <= r.z1; ++z) for (int x = r.x0; x <= r.x1; ++x) add(x, z);
                        rooms.insert(j * N + i, r);
                    }
                return out;
            };
            QVector<QPoint> cols2;
            if (n2 > 0) cols2 = buildCols(mz2, fl2, true, rooms2);
            const QVector<QPoint> cols1 = buildCols(mz1, fl1, false, rooms1);
            // ---- writing: first the lower level (it uses the original ground tops), then the upper one
            QHash<ColKey, QPair<int, quint16>> origMap;                                // the original column top/surface before any edits
            auto origOf = [&](int x, int z, GroundChunk* g, int col) -> QPair<int, quint16>
            {
                const qint64 k = pairKey(x, z);
                auto it = origMap.find(k);
                if (it == origMap.end()) it = origMap.insert(k, qMakePair(int(g->top[col]), quint16(g->surf[col])));
                return it.value();
            };
            auto fillCeiling = [&](int x, int z, int fromY, GroundChunk* g, int col)
            {
                const QPair<int, quint16> o = origOf(x, z, g, col);
                const int orig = o.first;
                const quint16 surf = o.second;
                for (int y = fromY; y <= orig; ++y)
                {
                    const quint16 id = (y == orig) ? surf : (y >= orig - 2 ? quint16(IdDirt) : quint16(IdStone));
                    if (store.get(x, y, z) == IdAir) store.set(x, y, z, id);          // building blocks (the church floor) are not overwritten
                }
            };
            const bool hasConn = (n2 > 0);
            auto isConnCol = [&](int x, int z) { return hasConn && x == ladd2.x() && z == ladd2.y(); };
            auto carve = [&](const QVector<QPoint>& cols, int floorY, bool lower)
            {
                for (const QPoint& c : cols)
                {
                    const int x = c.x(), z = c.y();
                    int col; GroundChunk* g = colAt(x, z, col);
                    if (!g) continue;
                    if (!lower && x == bs.hx && z == bs.hz) continue;                       // the descent column — separately
                    const bool isConn = isConnCol(x, z);
                    if (lower)
                    {
                        if (!isConn) fillCeiling(x, z, floorY + 3, g, col);
                        g->top[col] = qint16(floorY - 1);
                        store.set(x, floorY, z, cobbleB);
                        if (store.get(x, floorY - 1, z) == IdAir) store.set(x, floorY - 1, z, cobbleB);       // a floor in two layers
                    }
                    else
                    {
                        for (int y = floorY + 1; y <= floorY + 2; ++y) store.erase(x, y, z);   // air (removes the stone filled by the lower level)
                        fillCeiling(x, z, floorY + 3, g, col);
                        if (!isConn)
                        {
                            g->top[col] = qint16(std::min<int>(g->top[col], floorY - 1));
                            store.set(x, floorY, z, cobbleB);
                            if (store.get(x, floorY - 1, z) == IdAir) store.set(x, floorY - 1, z, cobbleB);   // a floor in two layers
                        }
                        else
                            g->top[col] = qint16(std::min<int>(g->top[col], fl2 - 1));
                    }
                }
            };
            if (n2 > 0) carve(cols2, fl2, true);
            carve(cols1, fl1, false);
            // the descent column from the church
            {
                int col; GroundChunk* g = colAt(bs.hx, bs.hz, col);
                if (!g) return false;
                origOf(bs.hx, bs.hz, g, col);
                store.erase(bs.hx, Wy - 1, bs.hz);
                store.erase(bs.hx, Wy - 2, bs.hz);
                g->top[col] = qint16(fl1 - 1);
                store.set(bs.hx, fl1, bs.hz, cobbleB);
                for (int y = fl1 + 1; y <= Wy - 2; ++y) store.set(bs.hx, y, bs.hz, IdLadderS);   // the holder — north (the maze wall)
            }
            // the ladder between levels
            if (n2 > 0)
            {
                const quint16 lad = quint16(int(IdLadderN) + ladFace);
                for (int y = fl2 + 1; y <= fl1 + 2; ++y) store.set(ladd2.x(), y, ladd2.y(), lad);
            }
            // the hatch and a 3×3 carpet above it (a wooden one — no lever is needed)
            {
                const quint32 hcar = colHash(pairKey(bs.hx, bs.hz) ^ 0xCA7ull);
                const quint16 carpet = quint16(int(IdCarpetBase) + int(hcar % quint32(kCarpetsCount)));
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const int x = bs.hx + dx, z = bs.hz + dz;
                        if (store.get(x, bs.wy, z) != IdAir) continue;
                        if (!(dx == 0 && dz == 0) && store.get(x, bs.wy - 1, z) == IdAir) continue;
                        store.set(x, bs.wy, z, carpet);
                    }
                store.set(bs.hx, bs.wy - 1, bs.hz, IdOakTrapdoor);
            }
            // chests: in dead ends (2–4), one expensive — in the deepest dead end; guards
            struct Spot { int level; QPoint cell; int dist; };
            QVector<Spot> spots;
            for (const QPoint& l : m1.leaves)
                if (!(connI >= 0 && l.x() == connI && l.y() == connJ)) spots.append(Spot{ 1, l, m1.dist[l.y() * N + l.x()] });
            if (n2 > 0)
                for (const QPoint& l : m2.leaves) spots.append(Spot{ 2, l, 100 + m2.dist[l.y() * N + l.x()] });
            std::sort(spots.begin(), spots.end(), [](const Spot& a, const Spot& b) { return a.dist > b.dist; });
            const int nChest = std::min(int(spots.size()), 2 + rnd(3));
            static const int kMobVar[5] = { 0, 1, 2, 3, 7 };                           // zombie, skeleton, spider, witch, enderman
            auto blockOfSpot = [&](const Spot& sp, int& bx, int& bz)
            {   // the chest block: the center of the dead-end room (or of the core)
                const auto& rm = (sp.level == 1) ? rooms1 : rooms2;
                const auto it = rm.constFind(sp.cell.y() * N + sp.cell.x());
                if (it != rm.constEnd()) { bx = (it->x0 + it->x1) / 2; bz = (it->z0 + it->z1) / 2; }
                else { bx = cx0(sp.cell.x()) + 1; bz = cz0(sp.cell.y()) + 1; }
                if (isConnCol(bx, bz) || (sp.level == 1 && bx == bs.hx && bz == bs.hz)) { bx += 1; }
            };
            for (int k = 0; k < nChest; ++k)
            {
                const Spot& sp = spots[k];
                int x, z; blockOfSpot(sp, x, z);
                const int fy = (sp.level == 1 ? fl1 : fl2) + 1;
                store.set(x, fy, z, IdChest);
                const bool rich = (k == 0);
                if (deepSpots && sp.level == (n2 > 0 ? 2 : 1))
                {
                    if (rich) deepSpots->append(QPoint(x, z)); else deepSpots->prepend(QPoint(x, z));
                }
                chests.append(ChestSite{ x, fy, z, rich ? kLootT1 + rnd(kChestLootCount - kLootT1) : pickChestLoot(quint32(rnd(1 << 20)), 55, 45) });
                if (rich)
                {   // guards — 3 mobs on the nearest columns of the same level
                    const QVector<QPoint>& lc = (sp.level == 1) ? cols1 : cols2;
                    QVector<QPoint> near;
                    for (const QPoint& c : lc)
                    {
                        if (c.x() == x && c.y() == z) continue;
                        if (std::max(std::abs(c.x() - x), std::abs(c.y() - z)) <= 3) near.append(c);
                    }
                    int g = 0;
                    while (!near.isEmpty() && g < 3)
                    {
                        const QPoint c = near.takeAt(rnd(int(near.size())));
                        NpcSite m; m.kind = 4; m.variant = kMobVar[rnd(5)]; m.x = c.x(); m.z = c.y(); m.y = fy; m.yaw = rnd(4) * 90;
                        npcs.append(m);
                        ++g;
                    }
                }
            }
            if (deepSpots)
            {   // the other dead ends of the lower level (without a chest) are also candidates for a shaft: the chest will be placed on the spot when the shaft is built
                const int deepLevel = (n2 > 0) ? 2 : 1;
                if (deepFloor) *deepFloor = (deepLevel == 2) ? fl2 : fl1;
                for (int k = nChest; k < spots.size(); ++k)
                    if (spots[k].level == deepLevel)
                    {
                        int x, z; blockOfSpot(spots[k], x, z);
                        deepSpots->append(QPoint(x, z));
                    }
            }
            // roaming mobs along the corridors of both levels
            const int totalCells = n1 + n2;
            const int nMob = std::max(4, std::min(12, totalCells / 5));
            for (int k = 0; k < nMob; ++k)
            {
                const bool low = (n2 > 0 && rnd(100) < 45);
                const QVector<QPoint>& lc = low ? cols2 : cols1;
                if (lc.size() < 4) continue;
                const QPoint c = lc[rnd(int(lc.size()))];
                if ((!low && c.x() == bs.hx && c.y() == bs.hz) || (low && isConnCol(c.x(), c.y()))) continue;
                NpcSite m; m.kind = 4; m.variant = kMobVar[rnd(5)]; m.x = c.x(); m.z = c.y(); m.y = (low ? fl2 : fl1) + 1; m.yaw = rnd(4) * 90;
                npcs.append(m);
            }
            // cobwebs under the corridor ceiling and rare lanterns in room corners (little light; the maze mobs do not despawn anyway — PersistenceRequired)
            {
                const quint16 web = quint16(IdPaletteStart + paletteIndexOf("minecraft:cobweb"));
                auto decorate = [&](const QVector<QPoint>& cols, int floorY, bool lower, const QHash<int, RoomR>& rooms)
                {
                    for (const QPoint& c : cols)
                    {
                        if ((!lower && c.x() == bs.hx && c.y() == bs.hz) || (lower && isConnCol(c.x(), c.y()))) continue;
                        if (store.get(c.x(), floorY + 1, c.y()) != IdAir) continue;                // a chest/ladder
                        if (rnd(100) < 7 && store.get(c.x(), floorY + 2, c.y()) == IdAir) store.set(c.x(), floorY + 2, c.y(), web);
                    }
                    for (auto it = rooms.constBegin(); it != rooms.constEnd(); ++it)
                    {
                        if (rnd(100) >= 55) continue;
                        const int x = it->x0, z = it->z0;
                        if ((!lower && x == bs.hx && z == bs.hz) || (lower && isConnCol(x, z))) continue;
                        if (store.get(x, floorY + 1, z) == IdAir && store.get(x, floorY, z) != IdAir) store.set(x, floorY + 1, z, IdLanternStanding);
                    }
                };
                decorate(cols1, fl1, false, rooms1);
                if (n2 > 0) decorate(cols2, fl2, true, rooms2);
            }
            return true;
        };
        // UNDERGROUND LABORATORY of a science building (44315000): a complex under the building 9 blocks below the walking level — a 3×N corridor along the south from the shaft and up to 6 rooms
        // 7×6 on the sides (openings 3 wide). In the rooms simple redstone mechanisms: a lamp panel with levers, chains of repeaters with a delay, a music
        // panel (buttons + note blocks), a "flower" of pistons around a block with a lever, a safe behind an iron door with buttons, an office. The light — lamps with redstone blocks in the ceiling.
        // The entrance — a hatch under a 3×3 carpet and a ladder at the north end of the corridor. The dead-end chests of the corridor are candidates for the Ancient City entrance (deepSpots/deepFloor).
        auto buildLab = [&](const BasementSite& bs, QVector<QPoint>* deepSpots, int* deepFloor) -> bool
        {
            if (caveSet.empty()) return false;
            const int Wy = bs.wy, fl = Wy - 9, y1 = fl + 1;
            QHash<QString, quint16> stCache;
            auto stId = [&](const char* name, std::initializer_list<std::pair<const char*, const char*>> pr) -> quint16
            {
                CaveState s;
                s.name = QString::fromLatin1(name);
                for (const auto& kv : pr) s.props.append(qMakePair(QString::fromLatin1(kv.first), QString::fromLatin1(kv.second)));
                const QString key = caveStateKey(s);
                const auto ci = stCache.constFind(key);
                if (ci != stCache.constEnd()) return ci.value();
                int idx = -1;
                for (int i = 0; i < caveSet.states.size(); ++i) if (caveStateKey(caveSet.states[i]) == key) { idx = i; break; }
                if (idx < 0)
                {
                    idx = caveSet.states.size();
                    caveSet.states.append(s);
                    caveSet.idOf.append(caveStateToId(s, idx));
                }
                stCache.insert(key, caveSet.idOf[idx]);
                return caveSet.idOf[idx];
            };
            static const char* const kDn[4] = { "north", "east", "south", "west" };
            auto colAt = [&](int x, int z, int& col) -> GroundChunk*
            {
                auto it = ground.find(pairKey(x >> 4, z >> 4));
                if (it == ground.end()) return nullptr;
                col = (z & 15) * 16 + (x & 15);
                return &it.value();
            };
            auto usable = [&](int x, int z) -> bool
            {
                int col; GroundChunk* g = colAt(x, z, col);
                if (!g || g->wd[col] > 0 || g->top[col] < fl + 4) return false;
                for (int y = fl - 1; y <= fl + 3; ++y)
                    if (store.get(x, y, z) != IdAir) return false;
                return true;
            };
            const quint16 floorB = quint16(IdPaletteStart + paletteIndexOf("minecraft:light_gray_concrete"));
            const quint16 wallB = quint16(IdPaletteStart + paletteIndexOf("minecraft:stone_bricks"));
            const quint16 ironB = quint16(IdIronBlock);
            const quint16 lampOn = stId("minecraft:redstone_lamp", { { "lit", "true" } });
            const quint16 lampOff = stId("minecraft:redstone_lamp", { { "lit", "false" } });
            const quint16 rsBlock = stId("minecraft:redstone_block", {});
            static const int dxs[4] = { 0, 1, 0, -1 }, dzs[4] = { -1, 0, 1, 0 };
            const int kRoomU0[3] = { 2, 10, 18 };
            for (int attempt = 0; attempt < 2; ++attempt)
            {
                const int vs = attempt == 0 ? 1 : -1;                                       // the v-axis: +x or -x
                auto wxz = [&](int u, int v, int& x, int& z) { x = bs.hx + vs * (v - 1); z = bs.hz + u; };
                auto dirOf = [&](int du, int dv) -> int { return du > 0 ? 2 : du < 0 ? 0 : (dv * vs > 0 ? 1 : 3); };
                auto ok = [&](int u, int v) { int x, z; wxz(u, v, x, z); return usable(x, z); };
                int Lc = 0;                                                                  // the corridor length: the maximal prefix of suitable columns
                for (int u = 0; u < 26; ++u)
                {
                    if (!(ok(u, 0) && ok(u, 1) && ok(u, 2))) break;
                    Lc = u + 1;
                }
                if (Lc < 10) continue;
                struct RoomL { int u0, side; bool on; };                                      // side: +1 south-v (v>=4), -1 (v<=-2)
                QVector<RoomL> rl;
                int nOn = 0;
                for (int c = 0; c < 3; ++c)
                    for (int side = -1; side <= 1; side += 2)
                    {
                        RoomL r{ kRoomU0[c], side, false };
                        if (r.u0 + 7 <= Lc)
                        {
                            bool good = true;
                            for (int ru = 0; ru < 7 && good; ++ru)
                            {
                                for (int rd = 0; rd < 6 && good; ++rd) good = ok(r.u0 + ru, side > 0 ? 4 + rd : -2 - rd);
                                if (good && ru >= 2 && ru <= 4) good = ok(r.u0 + ru, side > 0 ? 3 : -1);      // the opening in the wall to the corridor
                            }
                            r.on = good;
                        }
                        if (r.on) ++nOn;
                        rl.append(r);
                    }
                if (nOn < 2) continue;
                // ——— the set of columns and writing ———
                QSet<ColKey> carved;
                QVector<QPoint> cols;
                auto addC = [&](int u, int v)
                {
                    int x, z; wxz(u, v, x, z);
                    if (carved.contains(pairKey(x, z))) return;
                    carved.insert(pairKey(x, z));
                    cols.append(QPoint(x, z));
                };
                for (int u = 0; u < Lc; ++u) for (int v = 0; v <= 2; ++v) addC(u, v);
                auto roomUV = [&](const RoomL& r, int ru, int rd, int& u, int& v) { u = r.u0 + ru; v = r.side > 0 ? 4 + rd : -2 - rd; };
                for (const RoomL& r : rl)
                {
                    if (!r.on) continue;
                    for (int ru = 0; ru < 7; ++ru) for (int rd = 0; rd < 6; ++rd) { int u, v; roomUV(r, ru, rd, u, v); addC(u, v); }
                    for (int ru = 2; ru <= 4; ++ru) addC(r.u0 + ru, r.side > 0 ? 3 : -1);     // the opening
                }
                for (const QPoint& c : cols)
                {
                    int col; GroundChunk* g = colAt(c.x(), c.y(), col);
                    if (!g) return false;
                }
                // the original top/surface before edits and the ceiling
                for (const QPoint& c : cols)
                {
                    int col; GroundChunk* g = colAt(c.x(), c.y(), col);
                    const int orig = g->top[col];
                    const quint16 surf = g->surf[col];
                    for (int y = fl + 4; y <= orig; ++y)
                    {
                        const quint16 id = (y == orig) ? surf : (y >= orig - 2 ? quint16(IdDirt) : quint16(IdStone));
                        if (store.get(c.x(), y, c.y()) == IdAir) store.set(c.x(), y, c.y(), id);
                    }
                    g->top[col] = qint16(fl - 1);
                    store.set(c.x(), fl, c.y(), floorB);
                    if (store.get(c.x(), fl - 1, c.y()) == IdAir) store.set(c.x(), fl - 1, c.y(), floorB);
                }
                // the descent column (u=0,v=1): a shaft from the house floor
                {
                    int col; GroundChunk* g = colAt(bs.hx, bs.hz, col);
                    if (!g) return false;
                    store.erase(bs.hx, Wy - 1, bs.hz);
                    store.erase(bs.hx, Wy - 2, bs.hz);
                    for (int y = fl + 4; y <= Wy - 3; ++y) store.erase(bs.hx, y, bs.hz);
                    g->top[col] = qint16(fl - 1);
                    for (int y = fl + 1; y <= Wy - 2; ++y) store.set(bs.hx, y, bs.hz, IdLadderS);   // the holder — north (soil/wall)
                }
                // lining the walls with stone brick (8 neighbors outside the set of columns), ceiling lights
                QSet<ColKey> lined;
                for (const QPoint& c : cols)
                    for (int dz = -1; dz <= 1; ++dz)
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            const qint64 nk = pairKey(c.x() + dx, c.y() + dz);
                            if (carved.contains(nk) || lined.contains(nk)) continue;
                            lined.insert(nk);
                            for (int y = fl + 1; y <= fl + 3; ++y)
                                if (store.get(keyFirst(nk), y, keySecond(nk)) == IdAir)
                                    store.set(keyFirst(nk), y, keySecond(nk), wallB);
                        }
                auto putAt = [&](int u, int v, int dy, quint16 id)
                {
                    int x, z; wxz(u, v, x, z);
                    store.set(x, y1 + dy, z, id);
                };
                auto chestAt = [&](int u, int v, int tbl)
                {
                    int x, z; wxz(u, v, x, z);
                    store.set(x, y1, z, IdChest);
                    chests.append(ChestSite{ x, y1, z, tbl });
                };
                auto lampAt = [&](int u, int v)
                {
                    int x, z; wxz(u, v, x, z);
                    if (store.get(x, fl + 3, z) != IdAir) return;
                    store.set(x, fl + 3, z, lampOn);
                    store.set(x, fl + 4, z, rsBlock);
                };
                for (int u = 3; u < Lc; u += 6) lampAt(u, 1);                                   // the corridor lighting
                // ——— room mechanisms ———
                QHash<ColKey, char> kind;                                                       // (x,z) -> W wire, L lever, R repeater (N-S 'n' / E-W 'e')
                QVector<QPair<QPoint, int>> wires;                                              // the wire positions
                auto mark = [&](int u, int v, char k) { int x, z; wxz(u, v, x, z); kind.insert(pairKey(x, z), k); };
                auto leverFloor = [&](int u, int v) { putAt(u, v, 0, stId("minecraft:lever", { { "face", "floor" }, { "facing", "north" }, { "powered", "false" } })); mark(u, v, 'L'); };
                auto wireAt = [&](int u, int v) { int x, z; wxz(u, v, x, z); wires.append(qMakePair(QPoint(x, z), 0)); mark(u, v, 'W'); };
                auto repeaterAt = [&](int u, int v, int towardInput, int delay)
                {
                    putAt(u, v, 0, stId("minecraft:repeater", { { "delay", delay == 1 ? "1" : delay == 2 ? "2" : delay == 3 ? "3" : "4" }, { "facing", kDn[towardInput] },
                                                                { "locked", "false" }, { "powered", "false" } }));
                    mark(u, v, (towardInput & 1) ? 'e' : 'n');
                };
                auto buttonAt = [&](int u, int v, int faceDir) { putAt(u, v, 0, stId("minecraft:stone_button", { { "face", "wall" }, { "facing", kDn[faceDir] }, { "powered", "false" } })); };
                int nMech = 0;
                int ri = 0;
                for (const RoomL& r : rl)
                {
                    ++ri;
                    if (!r.on) continue;
                    ++nMech;
                    auto RU = [&](int ru, int rd, int& u, int& v) { roomUV(r, ru, rd, u, v); };
                    const int dRu = dirOf(1, 0), dRd = (r.side > 0) ? dirOf(0, 1) : dirOf(0, -1);   // world directions +ru and +rd
                    const int dRuNeg = (dRu + 2) & 3, dRdNeg = (dRd + 2) & 3;
                    int u, v;
                    RU(1, 4, u, v); lampAt(u, v);                                                    // the room ceiling lamps
                    RU(5, 4, u, v); lampAt(u, v);
                    RU(3, 1, u, v); lampAt(u, v);
                    switch (nMech % 6)
                    {
                    case 1:
                        for (int rd = 1; rd <= 5; rd += 2)                                           // three lines: a lever — a wire — a lamp
                        {
                            RU(0, rd, u, v); leverFloor(u, v);
                            for (int ru = 1; ru <= 4; ++ru) { RU(ru, rd, u, v); wireAt(u, v); }
                            RU(5, rd, u, v); putAt(u, v, 0, lampOff);
                        }
                        break;
                    case 2:
                        for (int rd = 1; rd <= 4; rd += 3)                                           // a lever — a chain of repeaters with a delay — lamps
                        {
                            RU(0, rd, u, v); leverFloor(u, v);
                            RU(1, rd, u, v); wireAt(u, v);
                            for (int ru = 2; ru <= 4; ++ru) { RU(ru, rd, u, v); repeaterAt(u, v, dRuNeg, 4); }
                            RU(5, rd, u, v); wireAt(u, v);
                            RU(6, rd, u, v); putAt(u, v, 0, lampOff);
                        }
                        break;
                    case 3:
                        for (int ru = 1; ru <= 5; ru += 1)                                           // music: a note block and a button next to it
                        {
                            RU(ru, 3, u, v);
                            static const char* const kNote[5] = { "0", "4", "7", "12", "16" };
                            putAt(u, v, 0, stId("minecraft:note_block", { { "instrument", "basedrum" }, { "note", kNote[ru - 1] }, { "powered", "false" } }));
                            RU(ru, 2, u, v); buttonAt(u, v, dRdNeg);                                  // a button on the note block itself (from the entrance side)
                        }
                        break;
                    case 4:
                        {   // a "flower" of pistons: a block with a lever in the center, pistons around facing up
                            RU(3, 3, u, v); putAt(u, v, 0, ironB);
                            RU(3, 2, u, v); putAt(u, v, 0, stId("minecraft:lever", { { "face", "wall" }, { "facing", kDn[dRdNeg] }, { "powered", "false" } }));
                            RU(2, 3, u, v); putAt(u, v, 0, stId("minecraft:piston", { { "extended", "false" }, { "facing", "up" } }));
                            RU(4, 3, u, v); putAt(u, v, 0, stId("minecraft:piston", { { "extended", "false" }, { "facing", "up" } }));
                            RU(3, 4, u, v); putAt(u, v, 0, stId("minecraft:piston", { { "extended", "false" }, { "facing", "up" } }));
                        }
                        break;
                    case 5:
                        {   // a safe: a 3×2 niche in the far corner behind an iron door, buttons outside and inside
                            for (int dy = 0; dy < 3; ++dy)
                            {
                                RU(3, 3, u, v); putAt(u, v, dy, wallB);
                                RU(3, 5, u, v); putAt(u, v, dy, wallB);
                                RU(4, 3, u, v); putAt(u, v, dy, wallB);
                                RU(5, 3, u, v); putAt(u, v, dy, wallB);
                                RU(6, 3, u, v); putAt(u, v, dy, wallB);
                            }
                            RU(3, 4, u, v);
                            putAt(u, v, 0, stId("minecraft:iron_door", { { "facing", kDn[dRuNeg] }, { "half", "lower" }, { "hinge", "left" }, { "open", "false" }, { "powered", "false" } }));
                            putAt(u, v, 1, stId("minecraft:iron_door", { { "facing", kDn[dRuNeg] }, { "half", "upper" }, { "hinge", "left" }, { "open", "false" }, { "powered", "false" } }));
                            putAt(u, v, 2, wallB);
                            RU(2, 3, u, v); buttonAt(u, v, dRuNeg);                                   // outside — on the block (3,3)
                            RU(4, 5, u, v); buttonAt(u, v, dRu);                                      // inside — on the block (3,5)
                            RU(6, 5, u, v); chestAt(u, v, kLootT1 + int(colHash(pairKey(bs.hx, bs.hz) ^ 0x5AFEull) % quint32(kChestLootCount - kLootT1)));
                        }
                        break;
                    default:
                        {   // an office
                            RU(0, 4, u, v); putAt(u, v, 0, IdBookshelf); putAt(u, v, 1, IdBookshelf);
                            RU(1, 4, u, v); putAt(u, v, 0, IdBookshelf); putAt(u, v, 1, IdBookshelf);
                            RU(5, 4, u, v); putAt(u, v, 0, IdBookshelf); putAt(u, v, 1, IdBookshelf);
                            RU(6, 4, u, v); putAt(u, v, 0, IdBookshelf); putAt(u, v, 1, IdBookshelf);
                            RU(3, 5, u, v); putAt(u, v, 0, IdCraftingTable);
                            RU(2, 5, u, v); putAt(u, v, 0, IdCartographyTable);
                            RU(4, 5, u, v); putAt(u, v, 0, IdFurnace);
                        }
                        break;
                    }
                    Q_UNUSED(dRu); Q_UNUSED(dRd);
                }
                // wires with connections
                for (const auto& w : wires)
                {
                    QVector<QPair<QString, QString>> pr;
                    for (int d = 0; d < 4; ++d)
                    {
                        const auto kit = kind.constFind(pairKey(w.first.x() + dxs[d], w.first.y() + dzs[d]));
                        bool con = false;
                        if (kit != kind.constEnd())
                            con = (*kit == 'W' || *kit == 'L' || (*kit == 'n' && (d & 1) == 0) || (*kit == 'e' && (d & 1) == 1));
                        pr << qMakePair(QString::fromLatin1(kDn[d]), con ? QStringLiteral("side") : QStringLiteral("none"));
                    }
                    CaveState s;
                    s.name = QStringLiteral("minecraft:redstone_wire");
                    s.props = pr;
                    s.props << qMakePair(QStringLiteral("power"), QStringLiteral("0"));
                    const QString key = caveStateKey(s);
                    int idx = -1;
                    for (int i = 0; i < caveSet.states.size(); ++i) if (caveStateKey(caveSet.states[i]) == key) { idx = i; break; }
                    if (idx < 0) { idx = caveSet.states.size(); caveSet.states.append(s); caveSet.idOf.append(caveStateToId(s, idx)); }
                    store.set(w.first.x(), y1, w.first.y(), caveSet.idOf[idx]);
                }
                // the hatch and a 3×3 carpet above it
                {
                    const quint32 hcar = colHash(pairKey(bs.hx, bs.hz) ^ 0xCA7ull);
                    const quint16 carpet = quint16(int(IdCarpetBase) + int(hcar % quint32(kCarpetsCount)));
                    for (int dz = -1; dz <= 1; ++dz)
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            const int x = bs.hx + dx, z = bs.hz + dz;
                            if (store.get(x, bs.wy, z) != IdAir) continue;
                            if (!(dx == 0 && dz == 0) && store.get(x, bs.wy - 1, z) == IdAir) continue;
                            store.set(x, bs.wy, z, carpet);
                        }
                    store.set(bs.hx, bs.wy - 1, bs.hz, IdOakTrapdoor);
                }
                // a chest at the start of the corridor (everyday/medium) and candidates for a secret shaft — the end of the corridor
                chestAt(2, 0, pickChestLoot(colHash(pairKey(bs.hx, bs.hz) ^ 0x1AB5ull), 40, 50));
                if (deepSpots)
                {
                    for (int u = Lc - 1; u >= std::max(4, Lc - 5); --u)
                    {
                        int x, z; wxz(u, 1, x, z);
                        if (store.get(x, y1, z) == IdAir) deepSpots->append(QPoint(x, z));
                    }
                    if (deepFloor) *deepFloor = fl;
                }
                Q_UNUSED(nMech);
                return true;
            }
            return false;
        };
        // We build the church dungeons first; for each we remember the lower-level chests — a secret shaft is hidden under one of them.
        QVector<QVector<QPoint>> deepSpots(basements.size());
        QVector<int> deepFloor(basements.size(), 0);
        for (int i = 0; i < basements.size(); ++i)
        {
            if (basements[i].church)
            {
                if (!buildChurchDungeon(basements[i], &deepSpots[i], &deepFloor[i])) deepSpots[i].clear();
            }
            else if (basements[i].lab)
            {
                if (!buildLab(basements[i], &deepSpots[i], &deepFloor[i])) deepSpots[i].clear();
            }
        }
        // SECRET PASSAGES: in the biggest church — a shaft to the End portal (a stronghold room, y≈23), in the second biggest — to the Ancient
        // City (Deep Dark, y≈-49). The entrance — not in the church itself, but in the maze under it: the floor under one of the chests of the LOWER level is replaced
        // with a hatch, then a ladder goes down. The structure template is placed so that its entrance column coincides with the shaft; the rectangles of the two
        // structures do not intersect.
        if (!lairSet.empty())
        {
            QVector<int> order;                                   // churches with a maze, in descending "size" (area × height)
            for (int i = 0; i < basements.size(); ++i)
                if (basements[i].church && !deepSpots[i].isEmpty()) order.append(i);
            std::stable_sort(order.begin(), order.end(), [&](int a, int b)
            { return basements[a].size != basements[b].size ? basements[a].size > basements[b].size : basements[a].hx < basements[b].hx; });
            int nChurch = 0;
            for (const BasementSite& b : basements) if (b.church) ++nChurch;
            res.lairInfo.prepend(QObject::tr("churches: %1, with a maze: %2").arg(nChurch).arg(order.size()) + ((res.lairInfo.isEmpty() || res.lairInfo.startsWith(QLatin1String("; "))) ? QString() : QStringLiteral("; ")));
            const int sS = lairSet.find(QStringLiteral("stronghold")), sC = lairSet.find(QStringLiteral("ancient_city"));
            auto churchCentre = [&](int bi) { return QPointF(basements[bi].cx, basements[bi].cz); };
            // does dead end c fit under the entrance of structure s (the chunks around the shaft and the whole main hall, the depth) — without regard to the neighboring structure
            auto candOk = [&](int s2, int bi, const QPoint& c, LairPlace& pl) -> bool
            {
                const LairStruct& ls = lairSet.st[s2];
                if (deepFloor[bi] - ls.floorY < 8) return false;
                const int X = c.x(), Z = c.y();
                if (!ground.contains(pairKey(X >> 4, Z >> 4)) || !ground.contains(pairKey((X - 1) >> 4, (Z - 1) >> 4))) return false;
                pl = LairPlace(); pl.s = s2; pl.tx = X - ls.ex; pl.tz = Z - ls.ez; pl.sx = X; pl.sz = Z;
                const int rx0 = ((ls.room[0] + pl.tx) >> 4) - 1, rx1 = ((ls.room[3] + pl.tx) >> 4) + 1;
                const int rz0 = ((ls.room[2] + pl.tz) >> 4) - 1, rz1 = ((ls.room[5] + pl.tz) >> 4) + 1;
                for (int dcz = rz0; dcz <= rz1; ++dcz)
                    for (int dcx = rx0; dcx <= rx1; ++dcx)
                        if (!ground.contains(pairKey(dcx, dcz))) return false;
                for (int dcz = -1; dcz <= 1; ++dcz)
                    for (int dcx = -1; dcx <= 1; ++dcx)
                        if (!ground.contains(pairKey((X >> 4) + dcx, (Z >> 4) + dcz))) return false;
                return true;
            };
            // the shaft of one structure must not pass through the rectangle of another
            auto conflict = [&](const LairPlace& a, const LairPlace& b) -> bool
            { return lairSet.rectOf(a).adjusted(-2, -2, 2, 2).contains(b.sx, b.sz) || lairSet.rectOf(b).adjusted(-2, -2, 2, 2).contains(a.sx, a.sz); };
            // the first suitable dead end of church bi for structure s2; dead ends are tried from the farthest from the point away (the maze "goes away" from the neighboring church)
            auto planFor = [&](int s2, int bi, const LairPlace* avoid, const QPointF& away, LairPlace& out) -> bool
            {
                QVector<QPoint> cands = deepSpots[bi];
                std::stable_sort(cands.begin(), cands.end(), [&](const QPoint& a, const QPoint& b)
                { return std::hypot(a.x() - away.x(), a.y() - away.y()) > std::hypot(b.x() - away.x(), b.y() - away.y()); });
                for (const QPoint& c : cands)
                {
                    LairPlace pl;
                    if (!candOk(s2, bi, c, pl)) continue;
                    if (avoid && conflict(pl, *avoid)) continue;
                    out = pl;
                    return true;
                }
                return false;
            };
            // a pair: structure s1 in church b1 and s2 in b2 — both are placed together (so that the shaft of the first does not interfere with the second)
            auto tryPair = [&](int s1, int b1, int s2, int b2, LairPlace& p1, LairPlace& p2) -> bool
            {
                QVector<QPoint> cands = deepSpots[b1];
                const QPointF away = churchCentre(b2);
                std::stable_sort(cands.begin(), cands.end(), [&](const QPoint& a, const QPoint& b)
                { return std::hypot(a.x() - away.x(), a.y() - away.y()) > std::hypot(b.x() - away.x(), b.y() - away.y()); });
                for (const QPoint& c : cands)
                {
                    LairPlace pl1;
                    if (!candOk(s1, b1, c, pl1)) continue;
                    LairPlace pl2;
                    if (planFor(s2, b2, &pl1, churchCentre(b1), pl2)) { p1 = pl1; p2 = pl2; return true; }
                }
                return false;
            };
            auto buildShaft = [&](int bi, const LairPlace& pl)
            {
                const LairStruct& ls = lairSet.st[pl.s];
                const int X = pl.sx, Z = pl.sz, fl = deepFloor[bi], fy = ls.floorY;
                GroundChunk& g = ground.find(pairKey(X >> 4, Z >> 4)).value();
                g.top[(Z & 15) * 16 + (X & 15)] = qint16(fy);
                store.set(X, fy, Z, cobbleB);
                for (int y = fy + 1; y <= fl - 1; ++y)
                {
                    store.set(X, y, Z, IdLadderS);
                    if (store.get(X, y, Z - 1) == IdAir) store.set(X, y, Z - 1, cobbleB);
                }
                store.set(X, fl, Z, IdOakTrapdoor);
                if (store.get(X, fl + 1, Z) == IdAir)
                {   // the dead end had no chest — we put it on top of the hatch (the entrance is hidden exactly under the chest)
                    store.set(X, fl + 1, Z, IdChest);
                    chests.append(ChestSite{ X, fl + 1, Z, pickChestLoot(colHash(pairKey(X, Z) ^ 0xC4E57ull), 40, 60) });
                }
                lairSet.placed.append(pl);
                if (basements[bi].forced)
                    res.lairInfo += QObject::tr("; %1 — hatch under the chest x=%2 z=%3 (the building nearest to the point on the map)").arg(ls.id).arg(X).arg(Z);
                else if (basements[bi].lab)
                    res.lairInfo += QObject::tr("; %1 — hatch under the chest x=%2 z=%3 (laboratory under a research building or a school, a spare entrance)").arg(ls.id).arg(X).arg(Z);
                else
                    res.lairInfo += QObject::tr("; %1 — hatch under the chest x=%2 z=%3 (church #%4 of %5 by size)")
                                        .arg(ls.id).arg(X).arg(Z).arg(order.indexOf(bi) + 1).arg(order.size());
                if (qEnvironmentVariableIsSet("MCX_LAIRDBG"))
                    std::fprintf(stderr, "[lair] %s shaft x=%d z=%d floorY=%d levelFloor=%d tx=%d tz=%d\n", qPrintable(ls.id), X, Z, fy, fl, pl.tx, pl.tz);
            };
            // churches set by the user by chunk coordinates (WorldOptions::lairChurchChunks: "x z; x z" or "x y z; x y z" as in F3)
            QVector<int> forced;
            {
                const QStringList groups = world.lairChurchChunks.split(QRegularExpression(QStringLiteral("[;|\\n]")), Qt::SkipEmptyParts);
                QVector<QPoint> chunks;
                static const QRegularExpression numRe(QStringLiteral("-?\\d+"));
                for (const QString& g : groups)
                {
                    QVector<int> nums;
                    auto it = numRe.globalMatch(g);
                    while (it.hasNext()) nums.append(it.next().captured().toInt());
                    if (nums.size() >= 3) chunks.append(QPoint(nums[0], nums[2]));
                    else if (nums.size() == 2) chunks.append(QPoint(nums[0], nums[1]));
                }
                if (groups.size() < 2 && chunks.isEmpty())
                {   // without ";" — six/four numbers in a row
                    QVector<int> nums;
                    auto it = numRe.globalMatch(world.lairChurchChunks);
                    while (it.hasNext()) nums.append(it.next().captured().toInt());
                    if (nums.size() == 6) chunks << QPoint(nums[0], nums[2]) << QPoint(nums[3], nums[5]);
                    else if (nums.size() == 4) chunks << QPoint(nums[0], nums[1]) << QPoint(nums[2], nums[3]);
                }
                for (const QPoint& ch : chunks)
                {
                    int best = -1; double bd = 1e18;
                    for (int bi : order)
                    {
                        const double d = std::hypot(basements[bi].cx - (ch.x() * 16 + 8), basements[bi].cz - (ch.y() * 16 + 8));
                        if (d < bd) { bd = d; best = bi; }
                    }
                    if (best >= 0 && bd <= 48.0 && !forced.contains(best)) forced.append(best);
                    else res.lairInfo += QObject::tr("; no church with a maze in chunk (%1, %2)").arg(ch.x()).arg(ch.y());
                }
            }
            // the building separately for the stronghold / the Ancient City (WorldOptions::strongholdChunk / cityChunk: "x z" or "x y z" as in F3): the NEAREST of those that
            // were marked for the point (any code); if none of them got a maze — the old rule (a church with a maze within 48 blocks of the point)
            auto churchByChunk = [&](const QString& text, const QString& what, int role) -> int
            {
                static const QRegularExpression numRe2(QStringLiteral("-?\\d+"));
                QVector<int> nums;
                auto it = numRe2.globalMatch(text);
                while (it.hasNext()) nums.append(it.next().captured().toInt());
                if (nums.size() < 2) return -1;
                const QPoint ch = nums.size() >= 3 ? QPoint(nums[0], nums[2]) : QPoint(nums[0], nums[1]);
                int best = -1; double bd = 1e18;
                for (int bi : order)
                {
                    if (basements[bi].forced != role) continue;
                    const double d = std::hypot(basements[bi].cx - (ch.x() * 16 + 8), basements[bi].cz - (ch.y() * 16 + 8));
                    if (d < bd) { bd = d; best = bi; }
                }
                if (best >= 0) return best;
                for (int bi : order)
                {
                    const double d = std::hypot(basements[bi].cx - (ch.x() * 16 + 8), basements[bi].cz - (ch.y() * 16 + 8));
                    if (d < bd) { bd = d; best = bi; }
                }
                if (best >= 0 && (bd <= 48.0 || text.trimmed().startsWith(QLatin1Char('~')))) return best;     // "~" — a choice by a click on the map: just the nearest church
                res.lairInfo += QObject::tr("; %1: no church with a maze in chunk (%2, %3)").arg(what).arg(ch.x()).arg(ch.y());
                return -1;
            };
            const int fixS = churchByChunk(world.strongholdChunk, QStringLiteral("stronghold"), 1);
            const int fixC = churchByChunk(world.cityChunk, QStringLiteral("Ancient City"), 2);
            QVector<int> autoOrder;                                // for the automatic choice — without the buildings that were marked for a point
            for (int bi : order) if (!basements[bi].forced) autoOrder.append(bi);
            LairPlace pl1, pl2;
            bool done = false;
            if (sS >= 0 && sC >= 0 && (fixS >= 0 || fixC >= 0) && fixS != fixC)
            {   // buildings are set for specific structures (by points on the map): the second one (if not set) is chosen automatically
                if (fixS >= 0 && fixC >= 0)
                {
                    if (tryPair(sS, fixS, sC, fixC, pl1, pl2)) { buildShaft(fixS, pl1); buildShaft(fixC, pl2); done = true; }
                    else
                    {   // the two points are close (the rectangles of the structures get in each other's way): the stronghold stays where the point is,
                        // the city goes to the nearest marked building where it does not conflict with it
                        if (planFor(sS, fixS, nullptr, QPointF(-1e9, -1e9), pl1))
                        {
                            buildShaft(fixS, pl1);
                            done = true;
                            QVector<int> cityCands;
                            cityCands.append(fixC);
                            for (int bi : order) if (basements[bi].forced == 2 && bi != fixC) cityCands.append(bi);
                            bool cityOk = false;
                            for (int bi : cityCands)
                                if (bi != fixS && planFor(sC, bi, &pl1, QPointF(pl1.sx, pl1.sz), pl2)) { buildShaft(bi, pl2); cityOk = true; break; }
                            if (!cityOk) res.lairInfo += QObject::tr("; ancient_city — the point on the map is too close to the stronghold: the entrances would get in each other's way");
                        }
                    }
                }
                else
                {
                    for (int k = 0; k < autoOrder.size() && !done; ++k)
                    {
                        const int bs_ = fixS >= 0 ? fixS : autoOrder[k];
                        const int bc_ = fixC >= 0 ? fixC : autoOrder[k];
                        if (bs_ == bc_) continue;
                        if (tryPair(sS, bs_, sC, bc_, pl1, pl2)) { buildShaft(bs_, pl1); buildShaft(bc_, pl2); done = true; }
                    }
                    if (!done)
                    {   // there is no suitable second building: we place the chosen structure alone, the other one is left to the fallbacks below
                        const int fb = fixS >= 0 ? fixS : fixC;
                        if (planFor(fixS >= 0 ? sS : sC, fb, nullptr, QPointF(-1e9, -1e9), pl1)) { buildShaft(fb, pl1); done = true; }
                    }
                }
                if (!done) res.lairInfo += QObject::tr("; the entrances cannot be placed in the given buildings — choosing automatically");
            }
            if (!done && sS >= 0 && sC >= 0 && forced.size() >= 2)
            {   // two given churches: the stronghold — in the bigger one (if it works out), otherwise vice versa
                const int bigger = (basements[forced[0]].size >= basements[forced[1]].size) ? forced[0] : forced[1];
                const int smaller = (bigger == forced[0]) ? forced[1] : forced[0];
                if (tryPair(sS, bigger, sC, smaller, pl1, pl2)) { buildShaft(bigger, pl1); buildShaft(smaller, pl2); done = true; }
                else if (tryPair(sS, smaller, sC, bigger, pl1, pl2)) { buildShaft(smaller, pl1); buildShaft(bigger, pl2); done = true; }
                else res.lairInfo += QObject::tr("; both entrances cannot be placed in the given churches (too few chunks around or the shafts interfere with each other) — choosing automatically");
            }
            if (!done)
            {   // automatically: the biggest by "size"; the stronghold in the biggest of the suitable ones, the city in the next one (together, so as not to interfere)
                if (sS >= 0 && sC >= 0 && !qEnvironmentVariableIsSet("MCX_CITY_FROM_LAB"))        // debug: the city only through the laboratory
                {
                    for (int i = 0; i < autoOrder.size() && !done; ++i)
                        for (int j = 0; j < autoOrder.size() && !done; ++j)
                        {
                            if (i == j) continue;
                            if (tryPair(sS, autoOrder[i], sC, autoOrder[j], pl1, pl2)) { buildShaft(autoOrder[i], pl1); buildShaft(autoOrder[j], pl2); done = true; }
                        }
                }
                if (!done)
                {   // at least one structure
                    bool placedS = false;
                    for (int bi : autoOrder)
                        if (sS >= 0 && !placedS && planFor(sS, bi, nullptr, QPointF(-1e9, -1e9), pl1)) { buildShaft(bi, pl1); placedS = true; break; }
                    if (!placedS) res.lairInfo += QObject::tr("; stronghold — no suitable church was found");
                }
            }
            // FALLBACK for the Ancient City: there is no suitable church — the entrance from the underground laboratory of a research building (the biggest — first), then of a school
            if (sC >= 0)
            {
                bool cityPlaced = false;
                const LairPlace* strong = nullptr;
                for (const LairPlace& pp : lairSet.placed) { if (pp.s == sC) cityPlaced = true; else if (pp.s == sS) strong = &pp; }
                if (!cityPlaced)
                {
                    QVector<int> labs;
                    for (int i = 0; i < basements.size(); ++i) if (basements[i].lab && !deepSpots[i].isEmpty()) labs.append(i);
                    std::stable_sort(labs.begin(), labs.end(), [&](int a, int b)
                    {   // research buildings first, schools after them; inside a group — the biggest first
                        if (basements[a].school != basements[b].school) return !basements[a].school;
                        return basements[a].size != basements[b].size ? basements[a].size > basements[b].size : basements[a].hx < basements[b].hx;
                    });
                    const LairPlace avoidCopy = strong ? *strong : LairPlace();
                    for (int bi : labs)
                    {
                        LairPlace plc;
                        if (planFor(sC, bi, strong ? &avoidCopy : nullptr, strong ? QPointF(strong->sx, strong->sz) : QPointF(-1e9, -1e9), plc))
                        {
                            buildShaft(bi, plc);
                            cityPlaced = true;
                            break;
                        }
                    }
                    if (!cityPlaced)
                        res.lairInfo += QObject::tr("; ancient_city — neither a suitable church, nor a research building, nor a school with a laboratory was found (the export edge or the shafts interfere with each other)");
                }
            }
        }
        for (const BasementSite& bs : basements)
        {
            if (bs.church || bs.lab) continue;
            bool ok = true;
            for (int z = bs.z0 - 1; z <= bs.z1 + 1 && ok; ++z)     // the room + a ring of walls: the ground must be solid down to wy-2
                for (int x = bs.x0 - 1; x <= bs.x1 + 1; ++x)
                {
                    const auto git = ground.constFind(pairKey(x >> 4, z >> 4));
                    if (git == ground.constEnd()) { ok = false; break; }
                    const int tp = git->top[(z & 15) * 16 + (x & 15)];
                    if (tp < bs.wy - (bs.church ? 3 : 2) || git->wd[(z & 15) * 16 + (x & 15)] > 0) { ok = false; break; }
                }
            if (!ok) continue;
            if (bs.church)
                for (int z = bs.z0; z <= bs.z1; ++z)
                    for (int x = bs.x0; x <= bs.x1; ++x)
                        store.erase(x, bs.wy - 2, z);                       // the bottom row of the church's double floor slab — into the room
            for (int z = bs.z0; z <= bs.z1; ++z)
                for (int x = bs.x0; x <= bs.x1; ++x)
                {
                    auto git = ground.find(pairKey(x >> 4, z >> 4));
                    git->top[(z & 15) * 16 + (x & 15)] = qint16(bs.wy - 5);
                    store.set(x, bs.wy - 4, z, cobbleB);                    // the basement floor
                }
            store.erase(bs.hx, bs.wy - 1, bs.hz);                           // an opening in the house floor
            for (int y = bs.wy - 3; y <= bs.wy - 2; ++y)
                store.set(bs.hx, y, bs.hz, IdLadderS);                      // facing south: the holder — north
            store.set(bs.x0, bs.wy - 3, bs.z1, IdLanternStanding);          // light in two corners by the far wall
            store.set(bs.x1, bs.wy - 3, bs.z1, IdLanternStanding);
            // a 3×3 carpet on top of the hatch (the center — the hatch cell): we put it on free cells of the house floor; the hatch itself — always
            {
                const quint32 hcar = colHash(pairKey(bs.hx, bs.hz) ^ 0xCA7ull);
                const quint16 carpet = quint16(int(IdCarpetBase) + int(hcar % quint32(kCarpetsCount)));
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const int x = bs.hx + dx, z = bs.hz + dz;
                        const bool center = (dx == 0 && dz == 0);
                        if (store.get(x, bs.wy, z) != IdAir) continue;                       // occupied by furniture/a block
                        if (!center && store.get(x, bs.wy - 1, z) == IdAir) continue;       // no floor under the carpet
                        store.set(x, bs.wy, z, carpet);
                    }
                // The hatch: while it is covered with a carpet (the carpet on the hatch cell always lands — it is free by the search condition), in 30% of cases
                // we put an ordinary wooden one (opens by hand, no lever is needed); otherwise an iron one + a lever on the ceiling next to
                // the ladder (powers the hatch through the floor block). A hatch not covered with a carpet is always iron.
                const bool covered = (store.get(bs.hx, bs.wy, bs.hz) >= IdCarpetBase
                                      && store.get(bs.hx, bs.wy, bs.hz) < IdCarpetBase + kCarpetsCount);
                const bool woodHatch = covered && (colHash(pairKey(bs.hx, bs.hz) ^ 0x7A9Dull) % 100u) < 30u;
                store.set(bs.hx, bs.wy - 1, bs.hz, woodHatch ? IdOakTrapdoor : IdIronTrapdoor);   // flush with the floor
                if (!woodHatch)
                    store.set(bs.hx - 1, bs.wy - 2, bs.hz, IdLeverCeil);
            }
            const quint32 hc = colHash(pairKey(bs.cx, bs.cz) ^ 0xBA5E1ull);
            if (bs.content == 0)
            {
                const int tbl = bs.church ? pickChestLoot(hc >> 3, 20, 30)    // church: 20% everyday, 30% medium, 50% expensive
                                          : pickChestLoot(hc >> 3, 30, 35);   // house: 30% everyday, 35% medium, 35% expensive
                store.set(bs.x1, bs.wy - 3, bs.z0 + 1, IdChest);            // a chest in the corner by the east wall
                chests.append(ChestSite{ bs.x1, bs.wy - 3, bs.z0 + 1, tbl });
                if (tbl >= kLootT1)
                {   // the expensive chest is guarded by 2–3 mobs next to it (the nearest free cells of the room)
                    QVector<QPoint> near;
                    for (int z = bs.z0; z <= bs.z1; ++z)
                        for (int x = bs.x0; x <= bs.x1; ++x)
                        {
                            if ((x == bs.hx && z == bs.hz) || (x == bs.x1 && z == bs.z0 + 1)) continue;
                            if (z == bs.z1 && (x == bs.x0 || x == bs.x1)) continue;     // lanterns
                            near.append(QPoint(x, z));
                        }
                    std::sort(near.begin(), near.end(), [&](const QPoint& p, const QPoint& q)
                    {
                        const int dp = std::abs(p.x() - bs.x1) + std::abs(p.y() - (bs.z0 + 1));
                        const int dq = std::abs(q.x() - bs.x1) + std::abs(q.y() - (bs.z0 + 1));
                        return dp != dq ? dp < dq : (p.y() != q.y() ? p.y() < q.y() : p.x() < q.x());
                    });
                    const int nGuard = std::min(int(near.size()), 2 + int((hc >> 6) % 2u));
                    for (int i = 0; i < nGuard; ++i)
                    {
                        NpcSite m;
                        m.kind = 4; m.variant = bs.mobType; m.x = near[i].x(); m.z = near[i].y(); m.y = bs.wy - 3;
                        m.yaw = int((hc >> (16 + i)) % 4u) * 90;
                        npcs.append(m);
                    }
                }
            }
            else
            {
                QVector<QPoint> cells;                                      // the room cells, except the descent and lanterns
                for (int z = bs.z0; z <= bs.z1; ++z)
                    for (int x = bs.x0; x <= bs.x1; ++x)
                    {
                        if ((x == bs.hx && z == bs.hz) || (z == bs.z1 && (x == bs.x0 || x == bs.x1))) continue;
                        cells.append(QPoint(x, z));
                    }
                const int area = (bs.x1 - bs.x0 + 1) * (bs.z1 - bs.z0 + 1);
                const int n = std::min(int(cells.size()), 3 + int((hc >> 4) % 3u) + (area >= 20 ? 2 : 1));   // 4..7 mobs
                const int st = int((hc >> 9) % quint32(std::max(1, int(cells.size()))));
                auto gcdI = [](int p, int q) { while (q) { const int r = p % q; p = q; q = r; } return p; };
                int stp = 1;                                                // the step is coprime with the number of cells — mobs do not coincide
                for (int cand : { 5, 3, 7, 2, 4 })
                    if (!cells.isEmpty() && gcdI(cand, int(cells.size())) == 1) { stp = cand; break; }
                for (int i = 0; i < n; ++i)
                {
                    const QPoint c = cells[(st + i * stp) % cells.size()];
                    NpcSite m;
                    m.kind = 4; m.variant = bs.mobType;
                    m.x = c.x(); m.z = c.y(); m.y = bs.wy - 3;
                    m.yaw = int((hc >> (13 + i)) % 4u) * 90;
                    npcs.append(m);
                }
            }
        }
    }

    lap("before 3.575");
    // 3.575 Objects from points on the map (sources with a modelField, step 3.575): memorials and monuments,
    // fountains, communication towers, waste bins, lighting masts, benches, sculptures and bus-stop shelters
    // stand on the READY ground (after the street furniture; the
    // trees were not planted around them — their columns were reserved in noTreePadCols). Which structure to build
    // is decided by the model number from point_models.csv (the object's code from the modelField column);
    // "atop a building" is the communication tower only (from its roof), the rest are skipped if a
    // structure/water/roadway lies under the point. The variant of every object is a hash of its column.
    {
        if (!spots.isEmpty())
        {
            auto blk = [](const char* name) -> quint16
            { return quint16(IdPaletteStart + paletteIndexOf(name)); };
            const quint16 qStone = blk("minecraft:stone"), qCobb = blk("minecraft:cobblestone"),
                        qMossy = blk("minecraft:mossy_cobblestone"), qSBrick = blk("minecraft:stone_bricks"),
                        qBrick = blk("minecraft:bricks"), qPDsl = blk("minecraft:polished_deepslate"),
                        qCDsl = blk("minecraft:cobbled_deepslate"), qPBlk = blk("minecraft:polished_blackstone"),
                        qBlk = blk("minecraft:blackstone"), qGran = blk("minecraft:granite"),
                        qDior = blk("minecraft:diorite"), qAnd = blk("minecraft:andesite"),
                        qSmth = blk("minecraft:smooth_stone"), qGold = blk("minecraft:gold_block"),
                        qQuartz = blk("minecraft:quartz_block"), qPCalc = blk("minecraft:polished_calcite"),
                        qGlow = blk("minecraft:glowstone");
            static const quint16 kStairs[4] = { IdStoneStairsN, IdStoneStairsE, IdStoneStairsS, IdStoneStairsW };
            int nPlaced = 0, nSkipped = 0;
            // Roadside greenery (leaves, plants, carpets, vines) does not forbid the install: it is
            // sheared off inside the object footprint. A trunk (log) or anything built stays a blocker.
            auto clearVeg = [&](int x0, int x1, int y0, int y1, int z0, int z1)
            {
                for (int x = x0; x <= x1; ++x)
                    for (int z = z0; z <= z1; ++z)
                        for (int y = y0; y <= y1; ++y)
                        {
                            const quint16 b = store.get(x, y, z);
                            if (b && b != IdAir && isVegBlockId(b))
                            {
                                const QString bn = blockName(b);
                                if (!bn.contains(QLatin1String("_log")) && !bn.contains(QLatin1String("stem")))
                                    store.erase(x, y, z);
                            }
                        }
            };
            for (const SpotSite& sp : spots)
            {
                const qint64 ck = pairKey(sp.x, sp.z);
                if (colWater.contains(ck) || roadCols.contains(ck)) { ++nSkipped; continue; }   // a pit/roadway under the object
                const int gy = surfaceTop(ck) + 1;
                if (gy + 40 > 255) continue;
                // the highest solid non-vegetation block of the column: for a tower — the roof to grow from,
                // for the others — the fact that something is already built here
                int topSolid = -1;
                for (int y = 255; y >= gy; --y)
                {
                    const quint16 b = store.get(sp.x, y, sp.z);
                    if (b && !isVegBlockId(b)) { topSolid = y; break; }
                }
                const quint32 hsh = colHash(ck ^ 0x53507u);
                switch (sp.model)
                {
                case kComposter:                                         // a waste bin — just a composter
                    store.set(sp.x, gy, sp.z, IdComposter);
                    ++nPlaced;
                    break;
                case kStatue:                                            // a MEMORIAL — a small statue (6 variants)
                {
                    if (topSolid >= gy) { ++nSkipped; break; }           // something is already built here
                    const int cx = sp.x, cz = sp.z;
                    switch (hsh % 6u)
                    {
                    case 0:                                              // an obelisk on a two-wide plinth
                        store.set(cx, gy, cz, qMossy); store.set(cx + 1, gy, cz, qCobb);
                        store.set(cx, gy, cz + 1, qCobb); store.set(cx + 1, gy, cz + 1, qMossy);
                        store.set(cx, gy + 1, cz, qPDsl); store.set(cx, gy + 2, cz, qPDsl); store.set(cx, gy + 3, cz, qPDsl);
                        store.set(cx, gy + 4, cz, kStairs[(hsh >> 8) % 4u]);
                        break;
                    case 1:                                              // a brick stele with a memorial plaque
                        for (int dx = 0; dx <= 1; ++dx)
                            for (int dz = 0; dz <= 1; ++dz) { store.set(cx + dx, gy, cz + dz, qBrick); store.set(cx + dx, gy + 1, cz + dz, qBrick); }
                        store.set(cx, gy + 2, cz, qSBrick); store.set(cx, gy + 3, cz, qSBrick);
                        store.set(cx, gy + 4, cz, IdPressurePlateStone);
                        break;
                    case 2:                                              // an eternal flame on a black post with corner fence posts
                        store.set(cx, gy, cz, qPBlk); store.set(cx, gy + 1, cz, qPBlk);
                        store.set(cx, gy + 2, cz, IdLanternStanding);
                        store.set(cx - 1, gy, cz - 1, IdCobbleWall); store.set(cx + 1, gy, cz - 1, IdCobbleWall);
                        store.set(cx - 1, gy, cz + 1, IdCobbleWall); store.set(cx + 1, gy, cz + 1, IdCobbleWall);
                        break;
                    case 3:                                              // a megalith: a boulder of mossy stone
                        for (int dx = 0; dx <= 1; ++dx)
                            for (int dz = 0; dz <= 1; ++dz)
                            {
                                const quint32 hb = colHash(pairKey(cx + dx, cz + dz) ^ 0x4EB01ull);
                                const quint16 mb = hb % 3u == 0u ? qMossy : (hb % 3u == 1u ? qCobb : qAnd);
                                store.set(cx + dx, gy, cz + dz, mb);
                                if (hb % 7u < 3u) store.set(cx + dx, gy + 1, cz + dz, mb);
                            }
                        break;
                    case 4:                                              // a broken column on a platform
                        for (int dx = -1; dx <= 1; ++dx)
                            for (int dz = -1; dz <= 1; ++dz) store.set(cx + dx, gy, cz + dz, qCobb);
                        store.set(cx - 1, gy + 1, cz - 1, qSmth); store.set(cx + 1, gy + 1, cz - 1, qSmth);
                        store.set(cx - 1, gy + 1, cz + 1, qSmth); store.set(cx + 1, gy + 1, cz + 1, qSmth);
                        store.set(cx, gy + 1, cz, qSBrick); store.set(cx, gy + 2, cz, qSBrick); store.set(cx, gy + 3, cz, qSBrick);
                        store.set(cx, gy + 4, cz, kStairs[(hsh >> 8) % 4u]);
                        break;
                    default:                                             // a shattered deepslate stele
                        store.set(cx, gy, cz, qCDsl); store.set(cx + 1, gy, cz, qCDsl);
                        store.set(cx, gy, cz + 1, qCDsl); store.set(cx + 1, gy, cz + 1, qCDsl);
                        store.set(cx, gy + 1, cz, qBlk); store.set(cx, gy + 2, cz, qBlk);
                        store.set(cx, gy + 3, cz, qCDsl);
                        store.set(cx, gy + 4, cz, kStairs[(hsh >> 8) % 4u]);
                        break;
                    }
                    ++nPlaced;
                    break;
                }
                case kStele:                                             // a MONUMENT — a stele in several materials (3 variants)
                {
                    if (topSolid >= gy) { ++nSkipped; break; }
                    const int cx = sp.x, cz = sp.z;
                    switch (hsh % 3u)
                    {
                    case 0:                                              // a dark obelisk with a gilded top
                        for (int dx = -1; dx <= 1; ++dx)
                            for (int dz = -1; dz <= 1; ++dz) store.set(cx + dx, gy, cz + dz, qStone);
                        for (int dx = 0; dx <= 1; ++dx)
                            for (int dz = 0; dz <= 1; ++dz) store.set(cx + dx, gy + 1, cz + dz, qPDsl);
                        for (int dy = 2; dy <= 6; ++dy) store.set(cx, gy + dy, cz, qPBlk);
                        store.set(cx, gy + 7, cz, qGold);
                        break;
                    case 1:                                              // a brick stele with an eternal flame (granite podium)
                        for (int dx = -1; dx <= 1; ++dx)
                            for (int dz = -1; dz <= 1; ++dz) store.set(cx + dx, gy, cz + dz, qGran);
                        for (int dx = 0; dx <= 1; ++dx)
                            for (int dz = 0; dz <= 1; ++dz)
                                for (int dy = 1; dy <= 4; ++dy) store.set(cx + dx, gy + dy, cz + dz, qBrick);
                        for (int dx = 0; dx <= 1; ++dx)
                            for (int dz = 0; dz <= 1; ++dz) store.set(cx + dx, gy + 5, cz + dz, qSBrick);
                        store.set(cx, gy + 6, cz, IdLanternStanding);
                        break;
                    default:                                              // a stone-brick column with side walls on a diorite base
                        for (int dx = 0; dx <= 1; ++dx)
                            for (int dz = 0; dz <= 1; ++dz) store.set(cx + dx, gy, cz + dz, qDior);
                        for (int dy = 1; dy <= 6; ++dy) store.set(cx, gy + dy, cz, qSBrick);
                        store.set(cx - 1, gy, cz, IdStoneBrickWall); store.set(cx - 1, gy + 1, cz, IdStoneBrickWall);
                        store.set(cx + 2, gy, cz, IdStoneBrickWall); store.set(cx + 2, gy + 1, cz, IdStoneBrickWall);
                        store.set(cx, gy + 7, cz, kStairs[(hsh >> 8) % 4u]);
                        break;
                    }
                    ++nPlaced;
                    break;
                }
                case kFountain:                                          // a FOUNTAIN: a stone ring, a pool, a post with water running down it
                {
                    if (topSolid >= gy) { ++nSkipped; break; }
                    const int cx = sp.x, cz = sp.z;
                    const bool mossyRim = (hsh >> 6) % 4u == 0u;         // an old weathered ring sometimes
                    const quint16 ringMat = mossyRim ? qMossy : qSBrick;
                    for (int dx = -2; dx <= 2; ++dx)
                        for (int dz = -2; dz <= 2; ++dz)
                        {
                            const int r = std::max(std::abs(dx), std::abs(dz));
                            if (r == 2) { store.set(cx + dx, gy, cz + dz, ringMat); store.set(cx + dx, gy + 1, cz + dz, ringMat); }
                            else if (dx || dz) { store.set(cx + dx, gy, cz + dz, qStone); store.set(cx + dx, gy + 1, cz + dz, IdWater); }
                        }
                    store.set(cx, gy, cz, qStone); store.set(cx, gy + 1, cz, qSBrick); store.set(cx, gy + 2, cz, qSBrick);
                    store.set(cx, gy + 3, cz, IdWater);                  // the source atop the post
                    static const int kD4[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                    for (const auto& d : kD4)                            // water STREAMS running down the post (level=8 — falling)
                    {
                        store.set(cx + d[0], gy + 3, cz + d[1], IdWaterFalling);
                        store.set(cx + d[0], gy + 2, cz + d[1], IdWaterFalling);
                    }
                    ++nPlaced;
                    break;
                }
                case kTower:                                             // a COMMUNICATION TOWER: an iron base, a spire of stone fence, 3 quartz blocks on top
                {   // ~32 blocks above the ground; from the roof of a high building — a 6-block body + the antenna
                    const int baseY = colMin.contains(ck) ? std::max(gy, topSolid + 1) : gy;
                    const int above = baseY - gy;
                    const int body = above >= 32 ? 6 : std::max(6, 32 - above - 4);
                    if (baseY + body + 3 > 255) { ++nSkipped; break; }
                    store.set(sp.x, baseY, sp.z, IdIronBlock);
                    for (int dy = 1; dy <= body; ++dy) store.set(sp.x, baseY + dy, sp.z, IdCobbleWall);
                    for (int dy = 1; dy <= 3; ++dy) store.set(sp.x, baseY + body + dy, sp.z, qQuartz);
                    ++nPlaced;
                    break;
                }
                case kLightPole:                                         // a LIGHTING MAST: polished calcite with glowstone lights
                    if (topSolid >= gy) { ++nSkipped; break; }           // something is already built here
                    for (int dy = 0; dy < 3; ++dy) store.set(sp.x, gy + dy, sp.z, qPCalc);
                    store.set(sp.x, gy + 3, sp.z, qGlow);                // a request: 3 calcite, glowstone, 2 calcite, glowstone
                    store.set(sp.x, gy + 4, sp.z, qPCalc);
                    store.set(sp.x, gy + 5, sp.z, qPCalc);
                    store.set(sp.x, gy + 6, sp.z, qGlow);
                    ++nPlaced;
                    break;
                case kBench:                                             // a STREET BENCH — 2x1 oak stairs (a request)
                {   // the bench axis is chosen by the column hash; the seat faces along it, the backrest on the side;
                    // both cells must be free ground — no road, no water, no structure, nothing already built
                    static const int kAx[4][2] = { {0, -1}, {1, 0}, {0, 1}, {-1, 0} };   // N/E/S/W (kDirNames order)
                    const int k = int(hsh % 4u);
                    const int x2 = sp.x + kAx[k][0], z2 = sp.z + kAx[k][1];
                    const qint64 ck2 = pairKey(x2, z2);
                    const int gyB = std::max(gy, surfaceTop(ck2) + 1);   // the seat is level: the higher of the two columns
                    clearVeg(std::min(sp.x, x2), std::max(sp.x, x2), gyB, gyB + 1,
                             std::min(sp.z, z2), std::max(sp.z, z2));      // a leaf or a bush in the seat footprint is sheared
                    if (colWater.contains(ck2) || roadCols.contains(ck2) || colMin.contains(ck2)
                        || store.get(sp.x, gyB, sp.z) != IdAir || store.get(x2, gyB, z2) != IdAir
                        || store.get(sp.x, gyB + 1, sp.z) != IdAir || store.get(x2, gyB + 1, z2) != IdAir)
                    { ++nSkipped; break; }
                    const quint16 st = quint16(IdStairsN + ((k + 1) & 3));   // the seat turned sideways to its axis
                    store.set(sp.x, gyB, sp.z, st);
                    store.set(x2, gyB, z2, st);
                    ++nPlaced;
                    break;
                }
                case kSculpture:                                         // a SMALL SCULPTURE (3 variants)
                {
                    const bool stand = (hsh % 3u) == 2u;                // the 2x2 variant shares one level floor
                    int gyB = gy;
                    if (stand)
                        for (int dx = 0; dx <= 1; ++dx)
                            for (int dz = 0; dz <= 1; ++dz)
                                gyB = std::max(gyB, surfaceTop(pairKey(sp.x + dx, sp.z + dz)) + 1);
                    clearVeg(sp.x, sp.x + 1, gyB, gyB + 3, sp.z, sp.z + 1);   // the widest variant is 2x2x4
                    if (topSolid >= gy) { ++nSkipped; break; }
                    if (!stand)
                    {   // the 1x1 variants stand on the node column itself
                        if (hsh % 3u == 0u)
                        {   // a modernist: a dark stone mass on a diorite pedestal
                            store.set(sp.x, gyB, sp.z, qDior); store.set(sp.x, gyB + 1, sp.z, qPDsl);
                            store.set(sp.x, gyB + 2, sp.z, qBlk); store.set(sp.x, gyB + 3, sp.z, qPBlk);
                        }
                        else
                        {   // a bust on a stone-brick column
                            store.set(sp.x, gyB, sp.z, qSBrick); store.set(sp.x, gyB + 1, sp.z, qSBrick);
                            store.set(sp.x, gyB + 2, sp.z, qSBrick); store.set(sp.x, gyB + 3, sp.z, qAnd);
                        }
                        ++nPlaced;
                        break;
                    }
                    {   // a sculptor's stand: a cobble pedestal and a stone cutter; the whole 2x2 needs free
                        // air above the level floor (a trunk in a corner blocks it)
                        bool ok2 = true;
                        for (int dx = 0; dx <= 1 && ok2; ++dx)
                            for (int dz = 0; dz <= 1 && ok2; ++dz)
                            {
                                const qint64 bk = pairKey(sp.x + dx, sp.z + dz);
                                if (colWater.contains(bk) || roadCols.contains(bk) || colMin.contains(bk)) ok2 = false;
                                for (int dy = 0; dy <= 1 && ok2; ++dy)
                                    if (store.get(sp.x + dx, gyB + dy, sp.z + dz) != IdAir) ok2 = false;
                            }
                        if (ok2)
                        {
                            for (int dx = 0; dx <= 1; ++dx)             // the pedestal, plinth-filled to the level floor
                                for (int dz = 0; dz <= 1; ++dz)
                                {
                                    const int t0 = surfaceTop(pairKey(sp.x + dx, sp.z + dz));
                                    for (int y = t0 + 1; y < gyB && t0 >= 0; ++y)
                                        store.set(sp.x + dx, y, sp.z + dz, qCobb);
                                    store.set(sp.x + dx, gyB, sp.z + dz, qCobb);
                                }
                            store.set(sp.x, gyB + 1, sp.z, qAnd);
                            store.set(sp.x + 1, gyB + 1, sp.z + 1, qAnd);
                            store.set(sp.x, gyB + 1, sp.z + 1, quint16(IdStonecutterBase + int((hsh >> 8) % 4u)));
                            ++nPlaced;
                        }
                        else ++nSkipped;
                    }
                    break;
                }
                case kBusStop:                                           // a BUS STOP SHELTER: 5x3, the long side along the road (requests)
                {   // the open side looks at the nearest MOTOR road (a ring up to 10 blocks; pedestrian and bike
                    // paths do not count — a user request; an ordinary roadway is only a fallback), else by hash;
                    // the shelter is shifted OFF the road — 2..3 blocks away from the node, the footprint must be free
                    static const int kD[4][2] = { {0, -1}, {1, 0}, {0, 1}, {-1, 0} };    // N/E/S/W (kDirNames order)
                    int od = -1;
                    for (int r = 1; r <= 10 && od < 0; ++r)
                        for (int k = 0; k < 4 && od < 0; ++k)
                            if (autoRoadCols.contains(pairKey(sp.x + kD[k][0] * r, sp.z + kD[k][1] * r))) od = k;
                    for (int r = 1; r <= 10 && od < 0; ++r)               // no motor road near — any roadway will do
                        for (int k = 0; k < 4 && od < 0; ++k)
                            if (roadCols.contains(pairKey(sp.x + kD[k][0] * r, sp.z + kD[k][1] * r))) od = k;
                    if (od < 0) od = int(hsh % 4u);
                    const int rx = kD[od][0], rz = kD[od][1];             // the roadway direction (the open side faces it)
                    const bool alongX = (rz != 0);                        // the road runs N/S — the 5-block side lies along X
                    bool built = false;
                    for (int sh = 2; sh <= 3 && !built; ++sh)
                    {
                        const int bx0 = sp.x - rx * sh, bz0 = sp.z - rz * sh;   // the box stands OFF the road, behind the node
                        const int ax = alongX ? 2 : 1, az = alongX ? 1 : 2;     // 5 blocks along the road, 3 across it
                        // the FLOOR IS THE GROUND SURFACE ITSELF (a request: flush, no raised plinth): the paving
                        // replaces the turf of the highest footprint level, low columns are raised to it
                        int gyB = gy - 1;
                        bool have = true;
                        for (int dx = -ax; dx <= ax && have; ++dx)
                            for (int dz = -az; dz <= az && have; ++dz)
                            {
                                const qint64 bk = pairKey(bx0 + dx, bz0 + dz);
                                if (colWater.contains(bk) || roadCols.contains(bk) || colMin.contains(bk)
                                    || railColsAll.contains(bk)
                                    || !ground.contains(pairKey((bx0 + dx) >> 4, (bz0 + dz) >> 4))) have = false;
                            }
                        if (have)
                            for (int dx = -ax; dx <= ax; ++dx)            // the paving level — the highest column
                                for (int dz = -az; dz <= az; ++dz)
                                    gyB = std::max(gyB, surfaceTop(pairKey(bx0 + dx, bz0 + dz)));
                        if (have)
                            clearVeg(bx0 - ax, bx0 + ax, gyB, gyB + 4, bz0 - az, bz0 + az);   // leaves and bushes off the box
                        bool free = have;
                        for (int dx = -ax; dx <= ax && free; ++dx)
                            for (int dz = -az; dz <= az && free; ++dz)
                                for (int dy = 0; dy <= 4 && free; ++dy)
                                    if (store.get(bx0 + dx, gyB + dy, bz0 + dz) != IdAir) free = false;
                        if (!free) continue;
                        for (int dx = -ax; dx <= ax; ++dx)                 // the floor: the paving replaces the surface
                            for (int dz = -az; dz <= az; ++dz)             // block of the ground (flush join with the terrain)
                            {
                                GroundChunk& g = ground.find(pairKey((bx0 + dx) >> 4, (bz0 + dz) >> 4)).value();
                                const int col = ((bz0 + dz) & 15) * 16 + ((bx0 + dx) & 15);
                                g.top[col] = qint16(gyB);                  // level the whole footprint to the paving row
                                g.surf[col] = qSmth;
                            }
                        for (int dx = -ax; dx <= ax; ++dx)                 // the walls: perimeter, the road side open
                            for (int dz = -az; dz <= az; ++dz)
                            {
                                if (std::abs(dx) < ax && std::abs(dz) < az) continue;   // the interior row stays free
                                if (dx * rx + dz * rz == 1) continue;      // the whole long side toward the road is open
                                for (int dy = 1; dy <= 3; ++dy) store.set(bx0 + dx, gyB + dy, bz0 + dz, qSBrick);
                            }
                        for (int dx = -ax; dx <= ax; ++dx)                 // the flat roof
                            for (int dz = -az; dz <= az; ++dz) store.set(bx0 + dx, gyB + 4, bz0 + dz, qSBrick);
                        store.set(bx0, gyB + 3, bz0, IdLanternHung);       // a lamp under the roof
                        built = true;
                        ++nPlaced;
                    }
                    if (!built) ++nSkipped;
                    break;
                }
                default:
                    ++nSkipped;                                          // an unknown code — nothing is built
                    break;
                }
            }
            char buf[128];
            snprintf(buf, sizeof buf, "3.575 point objects: placed %d, skipped %d", nPlaced, nSkipped);
            tmcStepLog(buf);
        }
    }

    lap("before 3.59");
    beginStep(9);
    // 3.59 Extra ground fill for "late" chunks: the site fences (3.57) and lamp posts are written
    // into the store AFTER the main fill and at the rectangle edge can create a chunk that did not
    // get into fillChunks — such a chunk is still written (its blocks are in the store), but without ground: in the game
    // a fence/post would hang above the abyss (verify "bad" columns). The same formulas as above.
    for (const qint64 ckKey : store.chunks.keys())
        if (!ground.contains(ckKey))
        {
            GroundChunk g = makeGround(ckKey);
            assignCave(g, ckKey);
            ground.insert(ckKey, g);
        }

    lap("before 3.59b");
    // 3.59b The "chamfer" of the junction with the game world (a user request instead of the "Ground up to the junction" choice
    // removed from the dialog — the apron is now always off): instead of a sheer cliff the outer edge of
    // our ground steps down toward the border — the column before the last one above the abyss goes down by 1,
    // the outermost — by 2. Pond columns (wd>0) and columns with an object (a structure, a fence,
    // a post, a planting — anything rising up to 7 blocks above the surface) are not lowered.
    {
        static const qint8 dX[4] = { -1, 1, 0, 0 }, dZ[4] = { 0, 0, -1, 1 };
        for (auto git = ground.begin(); git != ground.end(); ++git)
        {
            GroundChunk& g = git.value();
            const int cx = int(keyFirst(git.key())), cz = int(keySecond(git.key()));
            QVector<quint8> drop(256, 0);
            bool any = false;
            for (int d = 0; d < 4; ++d)
            {
                if (ground.contains(pairKey(cx + int(dX[d]), cz + int(dZ[d])))
                    || inExportRect(cx + int(dX[d]), cz + int(dZ[d])))
                    continue;                                   // this side is not a joint
                const bool neg = (dX[d] < 0) || (dZ[d] < 0);
                // A ragged edge: the chamfer line is shifted inward by 0..3 blocks (noise along the edge by the world coordinate, a step of 3 blocks); the strip between the edge and the line
                // stays lowered by 2, then steps 2 and 1. A column occupied by an object (except vegetation) is not lowered; vegetation is lowered together with the column.
                for (int k = 0; k < 16; ++k)
                {
                    const int along = (d < 2) ? (cz << 4) + k : (cx << 4) + k;
                    const int o = int(colHash(pairKey(along / 3, d + 77) ^ 0xFA5CAull) % 4u);
                    for (int p = 0; p <= o + 1 && p < 16; ++p)
                    {
                        const int off = neg ? p : 15 - p;
                        const int lx = (d < 2) ? off : k, lz = (d < 2) ? k : off;
                        const int col = lz * 16 + lx;
                        const quint8 want = quint8(p == o + 1 ? 1 : 2);
                        if (drop[col] >= want || g.wd[col] > 0)
                            continue;
                        const int bx = (cx << 4) + lx, bz = (cz << 4) + lz;
                        bool freeCol = true;                    // a column occupied by an object is not lowered (vegetation — it can be)
                        for (int y = int(g.top[col]) - 3; y <= int(g.top[col]) + 7; ++y)
                        {
                            const quint16 sb = store.get(bx, y, bz);
                            if (sb && !isVegBlockId(sb)) { freeCol = false; break; }
                        }
                        if (!freeCol)
                            continue;
                        const int delta = int(want) - int(drop[col]);                 // by how much more we lower (on a repeated pass over the column)
                        for (int y = int(g.top[col]) + 1; y <= int(g.top[col]) + 24; ++y)   // vegetation goes down together with the ground
                        {
                            const quint16 sb = store.get(bx, y, bz);
                            if (!sb) continue;
                            store.erase(bx, y, bz);
                            store.set(bx, y - delta, bz, sb);
                        }
                        drop[col] = qMax(drop[col], want);
                        any = true;
                    }
                }
            }
            if (!any)
                continue;
            int sFloor = std::numeric_limits<int>::max();
            for (int col = 0; col < 256; ++col)
            {
                if (drop[col])
                    g.top[col] = qint16(int(g.top[col]) - drop[col]);
                sFloor = std::min(sFloor, int(g.top[col]) - (g.wd[col] > 0 ? int(g.wd[col]) + 3 : 3));
            }
            g.stoneFloor = sFloor;
            taperDrop.insert(git.key(), drop);
        }
    }

    lap("before 3.58");
    // 3.58 Fence connections: the fence connections are a PERSISTENT block state (Properties
    // north/east/south/west), the client draws the arms by them, NOT by the neighboring blocks. Without the properties
    // the world loads with "bare sticks", and only break+place recomputes them (a user remark).
    // We write the connection bitmask into the fence id (blockProperties derives the palette properties from it).
    {
        auto connects = [](quint16 nid)
        {   // a fence "absorbs": the same fence (already stamped), any gate and any full
          // cube of the object palette (building walls). Plants, water, carpets, stone walls — no.
            return (nid >= quint16(IdFence) && nid < quint16(IdFenceMax))
                || (nid >= quint16(IdGateN) && nid <= quint16(IdGateW))
                || (nid >= quint16(IdPaletteStart) && nid < quint16(IdPaletteStart) + kPaletteCount);
        };
        for (auto cit = store.chunks.begin(); cit != store.chunks.end(); ++cit)
        {
            const int bx0 = keyFirst(cit.key()) << 4, bz0 = keySecond(cit.key()) << 4;
            for (auto sit = cit->begin(); sit != cit->end(); ++sit)
            {
                const int by0 = sit.key() << 4;
                QVector<quint16>& sec = sit.value();
                for (int i = 0; i < sec.size(); ++i)
                {
                    if (sec[i] != quint16(IdFence)) continue;   // not a fence or already stamped
                    const int x = bx0 + (i & 15), y = by0 + ((i >> 8) & 15), z = bz0 + ((i >> 4) & 15);
                    int conn = 0;
                    if (connects(store.get(x, y, z - 1))) conn |= 1;   // north = -Z
                    if (connects(store.get(x + 1, y, z))) conn |= 2;   // east  = +X
                    if (connects(store.get(x, y, z + 1))) conn |= 4;   // south = +Z
                    if (connects(store.get(x - 1, y, z))) conn |= 8;   // west  = -X
                    sec[i] = quint16(int(IdFence) + conn);
                }
            }
        }
    }

    {   // 3.58b Connections of stone fences (parking lots): with the same ones, gates and full cubes of the object palette
        auto connectsW = [](quint16 nid)
        {
            return (nid >= quint16(IdStoneFence) && nid < quint16(IdStoneFenceMax)) || nid == quint16(IdStoneBrickWall)
                || (nid >= quint16(IdPaletteStart) && nid < quint16(IdPaletteStart) + kPaletteCount);
        };
        for (auto cit = store.chunks.begin(); cit != store.chunks.end(); ++cit)
        {
            const int bx0 = keyFirst(cit.key()) << 4, bz0 = keySecond(cit.key()) << 4;
            for (auto sit = cit->begin(); sit != cit->end(); ++sit)
            {
                const int by0 = sit.key() << 4;
                QVector<quint16>& sec = sit.value();
                for (int i = 0; i < sec.size(); ++i)
                {
                    if (sec[i] != quint16(IdStoneFence)) continue;
                    const int x = bx0 + (i & 15), y = by0 + ((i >> 8) & 15), z = bz0 + ((i >> 4) & 15);
                    int conn = 0;
                    if (connectsW(store.get(x, y, z - 1))) conn |= 1;
                    if (connectsW(store.get(x + 1, y, z))) conn |= 2;
                    if (connectsW(store.get(x, y, z + 1))) conn |= 4;
                    if (connectsW(store.get(x - 1, y, z))) conn |= 8;
                    sec[i] = quint16(int(IdStoneFence) + conn);
                }
            }
        }
    }
    lap("before 3.6");
    beginStep(8);
    // 3.6 "Just turf": on empty grass columns (without objects, water and plantings) deterministically
    // (~1/8 by the column hash) we plant short grass — otherwise the exported terrain looks
    // bald and "smooth", as if paved.
    {
        // We count the candidates in parallel by chunk blocks (read only: ground/colMin/colWater/store), and write into the store
        // AFTER — in one thread. The main thing: the "every 8th column" hash is checked FIRST — earlier for each
        // of the 256 columns of a chunk two accesses to the huge colMin/colWater hashes were made (hundreds of millions of accesses,
        // ~5 minutes on a big project), although only every eighth reached the work.
        const QVector<qint64> fl(fillChunks.constBegin(), fillChunks.constEnd());
        const int nF = fl.size();
        constexpr int kBlk = 128;
        struct Plant { int x, y, z; };
        const int nBlk = (nF + kBlk - 1) / kBlk;
        QVector<QVector<Plant>> outs(nBlk);
        QThreadPool sodPool;
        sodPool.setMaxThreadCount(std::max(1, QThread::idealThreadCount()));
        QVector<QFuture<void>> sf;
        sf.reserve(nBlk);
        for (int b = 0; b < nBlk; ++b)
            sf << QtConcurrent::run(&sodPool, [&, b]()
            {
                const int c1 = std::min(nF, (b + 1) * kBlk);
                for (int ci = b * kBlk; ci < c1; ++ci)
                {
                    if (cancel && cancel->load()) return;
                    const qint64 ckKey = fl[ci];
                    const auto git = ground.constFind(ckKey);          // the fill guaranteed the metadata
                    if (git == ground.constEnd()) continue;
                    const GroundChunk& g = git.value();
                    const int bx0 = keyFirst(ckKey) << 4, bz0 = keySecond(ckKey) << 4;
                    for (int lz = 0; lz < 16; ++lz)
                        for (int lx = 0; lx < 16; ++lx)
                        {
                            const int cx = bx0 + lx, cz = bz0 + lz;
                            const qint64 ck = pairKey(cx, cz);
                            if (colHash(ck) % 8u != 0u) continue;                       // 7/8 of the columns — immediately skipped
                            const int top = g.top[lz * 16 + lx];
                            if (top > 254) continue;
                            if (g.surf[lz * 16 + lx] != IdGrass) continue;              // wool/stone/paint — skipped
                            if (colMin.contains(ck) || colWater.contains(ck)) continue; // under an object/water
                            if (store.get(cx, top, cz) != IdAir) continue;              // the top block is occupied by an object
                            if (store.get(cx, top + 1, cz) != IdAir) continue;          // already occupied by something
                            outs[b].append(Plant{ cx, top + 1, cz });
                        }
                }
            });
        for (int b = 0; b < nBlk; ++b)
        {
            sf[b].waitForFinished();
            if (progress && (b & 0x7) == 0)
                progress(std::min((b + 1) * kBlk, nF), nF, 8);                              // the "grass (turf)" stage
            if (cancel && cancel->load())
            {
                for (QFuture<void>& f : sf) f.waitForFinished();
                res.cancelled = true;
                res.error = QObject::tr("Export cancelled.");
                return res;
            }
        }
        for (const QVector<Plant>& v : outs)
            for (const Plant& pl : v)
                store.set(pl.x, pl.y, pl.z, IdGrassPlant);
    }

    lap("before 3.59c");
    beginStep(9);
    // 3.59c "Flowing out" on the chamfer (a user request — a living junction; what it floods/burns — does not matter):
    // at hash places of the chamfer we write a water source (6..10 blocks) or lava (2..4) into the cells of the new
    // surface. Water/lava without Properties — a full source (IdWater/IdLava), and when a chunk
    // loads the game spreads it beyond our area, into the generated world. The points are deterministic
    // (colHash by the world coordinates) — a repeated export of the same scene reproduces them.
    // DISABLED (request: waterfalls and lava falls at the world edge only break integrity — we leave just the chamfer); enabled by MCX_EDGE_FLUIDS=1.
    if (qEnvironmentVariableIsSet("MCX_EDGE_FLUIDS"))
    {
        static const qint8 dX[4] = { -1, 1, 0, 0 }, dZ[4] = { 0, 0, -1, 1 };
        for (auto tit = taperDrop.constBegin(); tit != taperDrop.constEnd(); ++tit)
        {
            const int cx = int(keyFirst(tit.key())), cz = int(keySecond(tit.key()));
            const QVector<quint8>& drop = tit.value();
            for (int d = 0; d < 4; ++d)
            {
                if (ground.contains(pairKey(cx + int(dX[d]), cz + int(dZ[d])))
                    || inExportRect(cx + int(dX[d]), cz + int(dZ[d])))
                    continue;                                   // the side is not a junction
                const bool neg = (dX[d] < 0) || (dZ[d] < 0);
                const int off = neg ? 0 : 15;                   // the edge row of the chamfer
                const int inX = -int(dX[d]), inZ = -int(dZ[d]); // inward — into our ground
                for (int k0 = 0; k0 <= 12; k0 += 4)             // 4 candidates per chunk side
                {
                    const int lx0 = (d < 2) ? off : k0, lz0 = (d < 2) ? k0 : off;
                    if (drop[lz0 * 16 + lx0] < 2)
                        continue;                               // the chamfer did not lie here (an object/pond)
                    const qint64 spot = pairKey((cx << 4) + lx0, (cz << 4) + lz0);
                    if (colHash(spot ^ 0xFACADEull) % 48u != 0u)
                        continue;                               // ~1 point per 48 edge columns
                    const quint32 t = colHash(spot ^ 0x1BEEDull);
                    const bool lava = (t % 4u) == 0u;           // 25% lava, 75% water
                    const int n = lava ? 30 + int(t >> 8) % 21 : 150 + int(t >> 8) % 101; // lava 30..50 (as the water was), water 150..250 (was 6..10: ×25)
                    const quint16 fid = lava ? IdLava : IdWater;
                    // cells: from the edge inward (d=0..2), then the next "row" along the junction
                    int placed = 0;
                    for (int idx = 0; idx < n; ++idx)
                    {
                        const int r = idx / 3, din = idx % 3;   // r — along the junction (the long line), din=0 — the edge cell
                        const int lx = lx0 + inX * din + (d < 2 ? 0 : r);
                        const int lz = lz0 + inZ * din + (d < 2 ? r : 0);
                        // the line goes along the junction and beyond the chunk border — we look for the neighboring chunk by ground
                        const int bx = (cx << 4) + lx, bz = (cz << 4) + lz;
                        const auto git = ground.constFind(pairKey(bx >> 4, bz >> 4));
                        if (git == ground.constEnd())
                            break;
                        const GroundChunk& g = git.value();
                        const int col = (bz & 15) * 16 + (bx & 15);
                        if (g.wd[col] > 0)
                            break;                              // we do not climb into ponds
                        const int y = g.top[col];
                        if (store.get(bx, y, bz) || store.get(bx, y + 1, bz))
                            continue;                           // the cell is occupied — the source will go further
                        store.set(bx, y, bz, fid);
                        fluidsByChunk[pairKey(bx >> 4, bz >> 4)].append(FluidSite{ bx, y, bz, lava });
                        ++placed;
                    }
                    Q_UNUSED(placed);
                }
            }
        }
    }

    lap("before 3.576");
    // 3.576 Rail platforms (rdcpol 62134000): a stone brick half SLAB over the stone-brick paving
    // (a user request: "the platform is raised by half a block"). Runs LAST — after every ground correction
    // (3.59 adds the ground of the "late" chunks, 3.59b/c bevel the world edge): only here the full set of
    // the saved columns and their true surfaces are known. The slab reads the ground chunk itself: its top row
    // and the condition that the paving is still paving (not overwritten by a road or water). Where something
    // solid already stands (a lamp post, a shelter) the slab gives up its cell (a request); low plants go first.
    if (!platformCols.isEmpty())
    {
        const quint16 kSlab = quint16(IdPaletteStart + paletteIndexOf("minecraft:stone_brick_slab"));
        const quint16 kPave = quint16(IdPaletteStart + paletteIndexOf("minecraft:stone_bricks"));
        int nSlab = 0;
        for (const qint64 ck : platformCols)
        {
            const int bx = keyFirst(ck), bz = keySecond(ck);
            const auto git = ground.constFind(pairKey(bx >> 4, bz >> 4));
            if (git == ground.constEnd()) continue;              // the platform crossed the world edge — no ground here
            const int col = (bz & 15) * 16 + (bx & 15);
            if (git.value().surf[col] != kPave) continue;        // the paving was hidden under a road or a square — no slab
            const int y0 = git.value().top[col] + 1;
            // NOTE: no colMin/railColsAll guards — the yard rails and the station raise them over whole platforms
            // while the row above their paving stays free (rails that really lie on this row are caught below).
            if (colWater.contains(ck)) continue;                 // water over the paving — no slab
            int yTop = y0;
            quint16 cur = store.get(bx, yTop, bz);
            if (cur != IdAir && !isVegBlockId(cur))
            {   // something stands on this ground row: when the ground was dug down for an underground hall or a
                // tunnel, its old surface was restored over the ceiling as paving — the slab belongs there (a request)
                int ry = 0;
                for (int yy = y0 + 32; yy > y0; --yy)   // from the top: the restored surface is the highest one
                {
                    if (store.get(bx, yy, bz) != kPave) continue;
                    const quint16 ab = store.get(bx, yy + 1, bz);
                    const quint16 bl = store.get(bx, yy - 1, bz);
                    if ((ab == IdAir || isVegBlockId(ab)) && bl != IdAir) { ry = yy; break; }   // paving over solid fill, not a lone block in a shaft
                }
                if (!ry) continue;                               // a real obstacle (a post, a rail) — the slab gives up
                yTop = ry + 1;
            }
            cur = store.get(bx, yTop, bz);
            if (cur != IdAir)
            {
                if (!isVegBlockId(cur)) continue;                        // should not happen: guarded above
                store.erase(bx, yTop, bz);
                const quint16 up = store.get(bx, yTop + 1, bz);
                if (up && isVegBlockId(up)) store.erase(bx, yTop + 1, bz);   // tall grass — both of its rows
            }
            store.set(bx, yTop, bz, kSlab);
            ++nSlab;
        }
        char sbuf[96];
        snprintf(sbuf, sizeof sbuf, "3.576 platform slabs: %d of %d columns", nSlab, int(platformCols.size()));
        tmcStepLog(sbuf);
    }

    lap("before 4.");
    tmcStepLog("post: chunk assembly start");
    if (!assembleChunks()) return res;

    if (!writeZip()) return res;
    return res;
}

// Builds the world (the former McWorldExporter::Export body).
McWorldExporter::Result exportWorld(const QVector<McWorldExporter::McObject>& objectsIn,
                                    const McWorldExporter::HeightSampler& heightAt,
                                    const QString& path,
                                    const QString& levelName,
                                    const std::atomic_bool* cancel,
                                    McWorldExporter::Format format,
                                    const McWorldExporter::ProgressFn& progress,
                                    const McWorldExporter::WorldOptions& world)
{
    return ExportRun(objectsIn, heightAt, path, levelName, cancel, format, progress, world).run();
}
