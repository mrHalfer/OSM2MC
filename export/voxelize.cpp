#include "export/voxelize.h"
#include "export/blocks.h"
#include <QSet>
#include <QString>
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <limits>


// Everything a single-object voxelization needs: the object mesh, the transform,
// the store to write into and the collected outputs (the old parameter list).
class ObjectVoxelizer
{
public:
    ObjectVoxelizer(const McWorldExporter::McObject& obj_, const VxTransform& tf_, quint16 id_,
                    VoxelStore& store_, const VoxelOut& out_, const std::atomic_bool* cancel_,
                    int sink_, int extraBlocks_, bool floors_,
                    quint16 structId_, bool woodSingle_, int privMode_, int deepRole_);

    // Voxelizes the object; returns the number of written blocks (0 - skipped).
    int run();

private:
    const McWorldExporter::McObject& obj;
    const VxTransform& tf;
    quint16 id;
    VoxelStore& store;
    const VoxelOut& out;
    const std::atomic_bool* cancel;
    int sink;
    int extraBlocks;
    bool floors;
    quint16 structId;
    bool woodSingle;
    int privMode;
    int deepRole;

    // The collected outputs, aliased from `out` once: the same names as the
    // old parameters, so the stage methods read/write them directly.
    const bool raisedObj;
    QHash<ColKey, int>& colMin;
    QHash<ColKey, int>* colAnchor;
    QVector<ChestSite>* chests;
    QVector<PaintingSite>* paints;
    QVector<MannequinSite>* mannequins;
    QVector<SpawnerSite>* spawners;
    int* storyCounter;
    QVector<NpcSite>* npcs;
    QVector<FrameSite>* frames;
    QVector<BasementSite>* basements;
    QVector<SignCand>* signs;
    QVector<BannerSite>* banners;
    QVector<LairZone>* zones;
    QVector<IslandSite>* islands;
    int* privMats;


    // The rasterized mesh (filled by buildGrid()): the grid cell (x,y,z) sits at
    // ((z*sy + y)*sx + x), the world block of the cell is (minx+x, miny+y, minz+z).
    QByteArray grid;
    QVector<int> surfaceCells;
    int sx = 0, sy = 0, sz = 0;
    int minx = 0, miny = 0, minz = 0;
    long long vol = 0;
    int volI = 0;
    int rowLo = 0, rowHi = -1;

    // The facade profile (computed by calcFloors): floor rhythm and window flags.
    int storyP = 0, nStories = 0, winDyMax = -1;                // the floor step, floors, top window row
    int lyLow = 0, lyHigh = -1;                                 // the lowest/highest surface row
    int sxy = 0;                                                // sx * sy (grid stride of one storey)
    bool panor = false, glassMat = false, panoWin = false, bigWin = false, noWin = false;
    bool office = false, resid = false, hospital = false;
    bool priv = false, shedThin = false, lowF = false, thinW = false, wide2 = false;
    bool lair = false;                                          // the bandit lair roll outcome

    // Counters and marks of the building passes.
    int added = 0;                                              // the written blocks (the return value)
    int nWindows = 0;                                           // the windows (0 - cobwebs inside)
    QVector<qint64> entranceKeys;                               // the entrance cells (furniture keeps out)
    QSet<int> forcedWall;                                       // the outer cells where a window is forbidden
    QSet<ColKey> stairXZ;                                       // the cells (lx,lz) of flights and ladders: furniture is not placed in front of or under them
    QSet<ColKey> doorCells;
    QSet<ColKey> partSet;                 // the blocks of inter-room partitions — we hang paintings on them

    // The wall-patch extent (calcPatch): the facade rectangle and the ridge axis.
    int fx0 = 0, fx1 = -1, fz0 = 0, fz1 = -1;
    bool ridgeZ = false;
    int a0 = 0, a1 = -1, b0 = 0, b1 = -1;

    // Rasterizes the mesh into the grid and picks the private-house material.
    // Returns false when the object must be skipped (empty mesh, a huge extent).
    bool buildGrid();
    // The structural material (foundation/slabs/roof rows) for the wall row ry.
    quint16 structFor(int ry) const;
    // The interior of a closed mesh: a 3D flood fill of the outer air; the cells it
    // did not reach are the body; a merged complex removes the inner walls; the deeper
    // body cells stay air, the 2-block shell becomes the lining (grid 4).
    void floodFillBody();
    // Computes the floor rhythm, the facade flags, the lair roll (the old inline block).
    void calcFloors();
    // The window definition: 0 - not a window; else the offset of the "inner" wall
    // block behind the glass (-1 in X, -sxy in Z; positive values face the other way).
    int windowDir(int lx, int ly, int lz) const;
    // Writes the wall patch and the windows into the store (entrances already marked).
    void buildWalls();
    // Computes the facade rectangle and the ridge axis of the wall patch.
    void calcPatch();
    // Places the entrances (doors, buttons, plates) and marks forcedWall.
    void buildEntrances();
    // Cobwebs in windowless buildings and the interior lining (grid 4) pass.
    void buildLining();
    // The inter-floor flights and vertical ladders (marks "5" in the grid).
    void buildStairs();
    // The 1-block slabs at every next floor level (grid "3" -> store).
    void buildSlabs();
    // The plank ceiling over a one-storey private house (no inter-floor slab).
    void buildCeiling();
    // The address plate candidates (one per compass side, chosen in Export()).
    void buildAddressPlate();
    // The room partitions (a guillotine cut of the cavity) and their door cells.
    void buildPartitions();
    // The gable (oak half-block) roof of the private-sector codes.
    void buildGableRoof();
    // Torches and hanging lanterns on the inner walls of the floors.
    void buildLight();
    // "Interiors" v2: furniture, paintings, mannequins, shelves, carpets.
    void buildInteriors();
    // The ominous banners outside the lair (one per compass side).
    void buildLairBanners();
    // A floating island over a kindergarten.
    void buildIsland();
    // The raider spawn zone inside a large industrial lair.
    void buildRaiderZone();
    // A basement room with loot under a private house.
    void buildBasement();
    // The aviation warning lights on tall buildings.
    void buildBeacons();
    // Writes one block (with the foundation sink) and marks the object column.
    void addVoxel(int bx, int by, int bz, quint16 vid = 0)
    {
        store.set(bx, by - sink, bz, vid ? vid : id);         // sink — the foundation: the lower courses go into the ground
        if (raisedObj) return;                                // a floating part: it does not mark the column as a structure column
        const qint64 ck = pairKey(bx, bz);
        if (!obj.floatCols.isEmpty() && obj.floatCols.contains(ck)) return;       // a glued complex: under a raised part the ground does not rise
        const int sy2 = by - sink;
        auto it = colMin.find(ck);
        if (it == colMin.end() || sy2 < it.value())
            colMin[ck] = sy2;
        if (colAnchor)                                        // the ORIGINAL (not sunk) bottom boundary —
        {                                                     // we use it to check the ground level around the object
            auto a = colAnchor->find(ck);
            if (a == colAnchor->end() || by < a.value())
                (*colAnchor)[ck] = by;
        }
    }
    // Decor hanging at height on someone else's wall: writes the block only,
    // without touching the ground column.
    void addDecor(int bx, int by, int bz, quint16 vid)
    {   // Decor hanging AT HEIGHT on someone else's wall (aviation beacons): it must NOT touch the ground column —
        // colMin would have been raised to the torch tier, and a dirt/stone
        // column would have grown under an empty point (the same mechanism as the "armor" of L-shaped private-sector houses).
        store.set(bx, by - sink, bz, vid);
    }

    // The cell (lx,ly,lz) sits above a cavity (3) - the interior of the object.
    bool interiorCol(int lx, int ly, int lz) const
    {
        const int c = lz * sxy + ly * sx + lx;
        return grid[c - sx] == char(3) || grid[c - 2 * sx] == char(3);
    }

    // The key of a world cell in doorCells: (x+32768)<<29 | (z+32768)<<12 | y.
    static qint64 dkey(int bx, int by, int bz)
    { return (qint64(bx + 32768) << 29) | (qint64(bz + 32768) << 12) | qint64(by); };
    // The facade-line hash for the lair roll and the mall mullion phase.
    static quint32 lineHash(qint64 k)
    {   // the same mix as colHash in Export (there the lambda is local — here is our own)
        const qint32 x = keyFirst(k), z = keySecond(k);
        quint32 h = quint32(x) * 374761393u + quint32(z) * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }
};

ObjectVoxelizer::ObjectVoxelizer(const McWorldExporter::McObject& obj_, const VxTransform& tf_, quint16 id_,
                                 VoxelStore& store_, const VoxelOut& out_, const std::atomic_bool* cancel_,
                                 int sink_, int extraBlocks_, bool floors_,
                                 quint16 structId_, bool woodSingle_, int privMode_, int deepRole_)
    : obj(obj_), tf(tf_), id(id_), store(store_), out(out_), cancel(cancel_),
      sink(sink_), extraBlocks(extraBlocks_), floors(floors_), structId(structId_),
      woodSingle(woodSingle_), privMode(privMode_), deepRole(deepRole_),
      raisedObj(obj_.kind == McWorldExporter::McObject::Building && obj_.raised > 0.0),
      colMin(*out_.colMin), colAnchor(out_.colAnchor),
      chests(out_.chests), paints(out_.paints), mannequins(out_.mannequins),
      spawners(out_.spawners), storyCounter(out_.storyCounter),
      npcs(out_.npcs), frames(out_.frames), basements(out_.basements),
      signs(out_.signs), banners(out_.banners), zones(out_.zones), islands(out_.islands), privMats(out_.privMats)
{
    // A part raised above the ground (min_height): a floating body - no foundation into the ground,
    // the ground under it is not touched and not raised (colMin/colAnchor stay without it),
    // no basement and no dungeon below it.
    if (raisedObj) { sink = 0; colAnchor = nullptr; basements = nullptr; deepRole = 0; }
}

//! Voxelizes an object and writes the result into the store; returns the number of written blocks (0 — the object
//! was skipped: an empty mesh or an extent beyond reason). colMin — the minimum voxel Y of the object per
//! XZ column (for the subsequent ground fill), it is updated.
int voxelizeObject(const McWorldExporter::McObject& obj, const VxTransform& tf, quint16 id,
                   VoxelStore& store, const VoxelOut& out, const std::atomic_bool* cancel,
                   int sink, int extraBlocks, bool floors,
                   quint16 structId, bool woodSingle, int privMode, int deepRole)
{
    return ObjectVoxelizer(obj, tf, id, store, out, cancel, sink, extraBlocks, floors,
                           structId, woodSingle, privMode, deepRole).run();
}

bool ObjectVoxelizer::buildGrid()
{
    const int nv = obj.vertices.size();
    if (nv < 3 || obj.indices.size() < 3)
        return false;

    // Into block coordinates + the extent
    QVector<QVector3D> bc;
    bc.reserve(nv);
    long long mnx = std::numeric_limits<long long>::max(), mxx = std::numeric_limits<long long>::min();
    long long mnz = mnx, mxz = mxx;
    long long mny = std::numeric_limits<long long>::max(), mxy = -mny;
    double yMinF = std::numeric_limits<double>::max(), yMaxF = -yMinF;
    for (int i = 0; i < nv; ++i)
    {
        const QVector3D& v = obj.vertices[i];
        const double bx = toBlockX(tf, v.x());
        const double by = toBlockY(tf, v.z());
        const double bz = toBlockZ(tf, v.y());
        bc << QVector3D(float(bx), float(by), float(bz));
        yMinF = std::min(yMinF, by);
        yMaxF = std::max(yMaxF, by);
        const long long ibx = (long long)std::floor(bx), ibz = (long long)std::floor(bz),
                        iby = (long long)std::floor(by);
        mnx = std::min(mnx, ibx); mxx = std::max(mxx, ibx + 1);
        mnz = std::min(mnz, ibz); mxz = std::max(mxz, ibz + 1);
        mny = std::min(mny, iby); mxy = std::max(mxy, iby + 1);
    }
    // "Growing" of structures: we pull the top upward by extraBlocks blocks (the bottom stays in place) — otherwise
    // 4.5 m houses with a 2-block foundation stay pitiful kitchen gardens (a user request:
    // all structures +2 blocks, below 6 m — another +2).
    if (extraBlocks > 0 && yMaxF - yMinF > 1e-3)
    {
        const double h = yMaxF - yMinF;
        const double k = (h + extraBlocks) / h;
        for (QVector3D& p : bc)
            p.setY(float(yMinF + (double(p.y()) - yMinF) * k));
        mxy = std::max(mxy, (long long)std::floor(yMinF + h * k) + 1);
    }
    if (mxx <= mnx || mxy <= mny || mxz <= mnz)
        return false;

    // The extent in integer cells + a gap of 1 on all sides (the shell of the flood fill must have an "air
    // corridor" around the object)
    const long long gx0 = mnx - 1, gx1 = mxx + 1;
    const long long gy0 = mny - 1, gy1 = mxy + 1;
    const long long gz0 = mnz - 1, gz1 = mxz + 1;
    const long long sxl = gx1 - gx0, syl = gy1 - gy0, szl = gz1 - gz0;
    // Reasonable limits: giant objects (flat forest polygons tens of km across) are skipped explicitly —
    // their rasterization would eat memory/time out of proportion to the result.
    if (sxl > 100000 || syl > 4096 || szl > 100000)
        return false;
    vol = sxl * syl * szl;
    if (vol > 64ll * 1024 * 1024)
        return false;
    volI = int(vol);   // the volume is certainly < INT_MAX (the check above) — it is convenient to index a QByteArray with an int

    sx = int(sxl); sy = int(syl); sz = int(szl);
    minx = int(gx0); miny = int(gy0); minz = int(gz0);

    grid.fill(0, volI);

    for (int i = 0; i + 2 < obj.indices.size(); i += 3)
    {
        if ((i & 0xFFFF) == 0 && cancel && cancel->load()) break;
        const quint32 i0 = obj.indices[i], i1 = obj.indices[i + 1], i2 = obj.indices[i + 2];
        if (i0 >= quint32(nv) || i1 >= quint32(nv) || i2 >= quint32(nv)) continue;
        rasterizeTriangle(bc[i0], bc[i1], bc[i2], minx, miny, minz, sx, sy, sz, grid, surfaceCells);
    }
    if (surfaceCells.isEmpty())
        return false;
    // The structural part with a given wall material (structId != 0): the foundation (the two bottom rows),
    // the floors between stories and the roof (the two top rows) — gray concrete whatever is in the walls.
    rowLo = std::numeric_limits<int>::max(); rowHi = -1;
    if (structId || privMode)
        for (int cell : surfaceCells)
        {
            const int ry = (cell / sx) % sy;
            rowLo = std::min(rowLo, ry); rowHi = std::max(rowHi, ry);
        }
    // A private house 44400002/44400100 (privMode): BRICK for a REAL second storey (walls high enough for the
    // slab (lyLow+5) plus a full floor over it: rowHi - rowLo >= 9) OR for a large enough house with a slab
    // height (dh >= 6 and footprint >= the calibration threshold, 200 m2 — calibrated on the Verbilki-mini clip
    // to ~50/50, a user request; MCX_PRIVBRICK overrides it for tuning without a rebuild). A small or low house stands LOGS;
    // for the log ones nStories is forced to 1 below, so no second-floor slab is built and the interior stays
    // one tall room. With a slab — the walls are BRICK, the foundation/slab gray concrete; without — LOGS with
    // a plank ceiling (the gables and the roof are planks in both cases).
    if (privMode)
    {
        static const double kBrickArea = []
        {
            bool ok = false;
            const double v = qgetenv("MCX_PRIVBRICK").toDouble(&ok);
            return (ok && v > 0.0) ? v : 200.0;
        }();
        const int dh = rowHi - rowLo;
        const double footprint = double(std::max(sx - 2, 1)) * double(std::max(sz - 2, 1));   // XZ cells of the extent, the 1-cell shell excluded
        const bool slabNeeded = dh >= 9 || (dh >= 6 && footprint >= kBrickArea);
        id = slabNeeded ? quint16(IdPaletteStart + paletteIndexOf("minecraft:bricks")) : quint16(IdOakLog);
        structId = slabNeeded ? quint16(IdPaletteStart + paletteIndexOf("minecraft:light_gray_concrete")) : quint16(0);
        woodSingle = !slabNeeded;
        if (privMats) ++privMats[slabNeeded ? 0 : 1];
    }
    return true;
}

quint16 ObjectVoxelizer::structFor(int ry) const
{ return (structId && (ry <= rowLo + 1 || ry >= rowHi - 1)) ? structId : quint16(0); }

void ObjectVoxelizer::floodFillBody()
{
    // The interior of a closed mesh: a 3D flood fill of the outer air from the extent boundary; the cells
    // it did not reach are the body of the object.
    if (obj.closed && vol <= 8ll * 1024 * 1024)
    {
        QVector<int> queue;
        queue.reserve(int(vol) / 8 + 16);
        auto tryPush = [&](int x, int y, int z)
        {
            const int cell = (z * sy + y) * sx + x;
            if (!grid[cell]) { grid[cell] = 2; queue.append(cell); }   // 2 = outer air
        };
        for (int z = 0; z < sz; ++z)
            for (int x = 0; x < sx; ++x)
            {
                tryPush(x, 0, z);
                tryPush(x, sy - 1, z);
            }
        for (int z = 0; z < sz; ++z)
            for (int y = 0; y < sy; ++y)
            {
                tryPush(0, y, z);
                tryPush(sx - 1, y, z);
            }
        for (int y = 0; y < sy; ++y)
            for (int x = 0; x < sx; ++x)
            {
                tryPush(x, y, 0);
                tryPush(x, y, sz - 1);
            }
        for (int qi = 0; qi < queue.size(); ++qi)
        {
            const int cell = queue[qi];
            const int x = cell % sx;
            const int y = (cell / sx) % sy;
            const int z = cell / (sx * sy);
            if (x > 0)        tryPush(x - 1, y, z);
            if (x + 1 < sx)   tryPush(x + 1, y, z);
            if (y > 0)        tryPush(x, y - 1, z);
            if (y + 1 < sy)   tryPush(x, y + 1, z);
            if (z > 0)        tryPush(x, y, z - 1);
            if (z + 1 < sz)   tryPush(x, y, z + 1);
        }
        for (int c = 0; c < volI; ++c)
            if (!grid[c])
                grid[c] = 3;                                  // 3 = the body (air unreachable from outside)
        if (obj.merged)
        {   // A complex of touching parts (an industrial zone): the wall between two parts — surface cells (1) with the body (3) on both
            // sides at the same height (along X or Z; up to 2 cells thick if the part walls did not coincide) — is removed, the parts
            // become one volume. Roofs/floors and outer walls (with outer air on at least one side) remain.
            const int sxyL = sx * sy;
            QVector<int> kill;
            for (int c = 0; c < volI; ++c)
            {
                if (grid[c] != char(1)) continue;
                const int lx = c % sx, lz = c / sxyL;
                const int ly = (c / sx) % sy;
                for (int axis = 0; axis < 3; ++axis)
                {   // 0 — X, 1 — Z, 2 — Y: a slab between two bodies one above the other (a raised part over its base building)
                    const int step = axis == 0 ? 1 : (axis == 1 ? sxyL : sx);
                    const bool inb1 = axis == 0 ? (lx > 0 && lx + 1 < sx) : (axis == 1 ? (lz > 0 && lz + 1 < sz) : (ly > 0 && ly + 1 < sy));
                    if (!inb1) continue;
                    if (grid[c - step] == char(3) && grid[c + step] == char(3)) { kill.append(c); break; }
                    // a double wall: 3,1,1,3
                    const bool inb2 = axis == 0 ? (lx > 0 && lx + 2 < sx) : (axis == 1 ? (lz > 0 && lz + 2 < sz) : (ly > 0 && ly + 2 < sy));
                    if (inb2 && grid[c - step] == char(3) && grid[c + step] == char(1) && grid[c + 2 * step] == char(3))
                    { kill.append(c); kill.append(c + step); break; }
                }
            }
            for (int c : kill) grid[c] = char(3);
            surfaceCells.erase(std::remove_if(surfaceCells.begin(), surfaceCells.end(), [&](int c) { return grid[c] != char(1); }),
                               surfaceCells.end());                                   // the removed cells are not a surface
        }
        // The cavity: we do not fill a building with blocks entirely — it is empty inside. We keep a "2-block" shell: the voxels
        // of the rasterized surface (1) plus one layer of the body adjacent to the surface/air (4).
        // The deeper body cells (all 6 neighbors are body too) stay air.
        QVector<int> skin;
        const int sxy = sx * sy;
        for (int c = 0; c < volI; ++c)
        {
            if (grid[c] != char(3)) continue;
            const int lx = c % sx, ly = (c / sx) % sy, lz = c / sxy;
            bool nearAir = false;                             // the cell borders 0/1/2 (or the edge) — a wall
            if (ly == 0 || grid[c - sx] != char(3)) nearAir = true;
            if (ly == sy - 1 || grid[c + sx] != char(3)) nearAir = true;
            if (!nearAir && (lx == 0 || grid[c - 1] != char(3))) nearAir = true;
            if (!nearAir && (lx == sx - 1 || grid[c + 1] != char(3))) nearAir = true;
            if (!nearAir && (lz == 0 || grid[c - sxy] != char(3))) nearAir = true;
            if (!nearAir && (lz == sz - 1 || grid[c + sxy] != char(3))) nearAir = true;
            if (nearAir) skin << c;
        }
        for (int c : skin)
            grid[c] = char(4);                                // 4 = the inner lining (written)
    }
}

void ObjectVoxelizer::calcFloors()
{
    // "Floors" and windows (bldpol only, a closed mesh — see floors): the floor height is preferably
    // 4 blocks of air + 1 block of slab; if with 5-6-block floors the same number fits as
    // with 4, we stretch them (fill the volume, do not lose floors). The floor slabs — over the whole patch
    // inside the shell; the windows — 2-block strips with a 1-block inset from the floor/slab, every 4th
    // column of the facade, through the whole thickness of the wall (glass + a hole behind it).
    storyP = 0; nStories = 0; winDyMax = -1;                // the floor step (air + slab), the number of floors,
                                                                // the top row of windows (the bottom of the roof lining)
    lyLow = sy; lyHigh = -1;
    // The flags of the window profile (needed both by the floor computation and by windowDir — declared before both).
    // The exception is shopping malls (CODE 44318000, a user request): they get "panoramic windows" —
    // a continuous strip of glass in bands of NOT LESS THAN 5 blocks in a row: a period of 6..9 columns, one column
    // of the period is a blind "mullion"; the mullion phase depends only on the facade line, so the mullions
    // stand vertically on all floors. We do not blind the corners — the strip wraps around the corner ("curved" windows
    // with an x/z offset of 1 block were allowed by the user).
    panor = (obj.code == 44318000);
    // The material "glass": the walls are concrete (the inside is concrete too), and the windows are the PANORAMIC ones, as in the malls (only the windows, the rest of the mall logic is not touched).
    glassMat = (id == quint16(IdGlass));
    if (glassMat) id = structId ? structId : nearestBlockId(obj.color);
    panoWin = panor || glassMat;
    // "Non-residential" buildings (a user request): WINDOWS 2×3 — a panel of 2 panes (4 blocks of width with
    // mullions), the strip height is 3 rows (an inset of 1 from the floor, the top flush with the slab). The codes: 44310000,
    // 44340000 and the series 44311000..44317000. SHOPPING MALLS (44318000) are NOT among them: the user
    // clarified that malls keep the FORMER panorama (strips of 5..8 with glazing around the corners).
    // For the 2×3 strip we fix the floor at 3+1 (shaft >= 4).
    bigWin = !glassMat && (obj.code == 44310000 || obj.code == 44340000
                         || (obj.code >= 44311000 && obj.code <= 44317000 && obj.code % 1000 == 0));
    // Sheds and utility structures (CODE 44000000, a user request) — no windows at all.
    noWin = (obj.code == 44000000);
    // Non-residential "office" buildings (a request): general-purpose, administrative, educational, children's,
    // hospitals, scientific, cultural, museums (44310000..44317000, without malls 44318000 — they have their own line) and the church
    // 44319000. They are furnished like residential ones, but WITHOUT beds and WITHOUT mannequins, with desks/shelves/fittings.
    office = (obj.code >= 44310000 && obj.code <= 44317000 && obj.code % 1000 == 0)
                        || obj.code == 44319000;
    resid = (obj.code == 44400000);    // multi-storey residential: carpets in compositions (runners, checkerboard, edging)
    hospital = (obj.code == 44314000);         // a "healthcare institution": beds, a doctor, patients
    // The private sector (the pitched-roof codes) — a common flag of ALL passes (a user request):
    // there is no flat roof at all (a foundation + walls), the 1-block floor slab is sunk by 1 block,
    // the 1st-floor room is 4 blocks, the 2nd floor = the whole remainder up to the slopes (with an attic), the inter-floor
    // flight is ordinary (oak). Declared before the floor computation — it is needed already there (and before wide2 —
    // the window profile looks at it too).
    priv = obj.kind == McWorldExporter::McObject::Building && obj.closed
                      && (obj.code == 44400002 || obj.code == 44400100 || obj.code == 44400110);
    // Sheds/utility structures (44000000, a request): the walls, floor and roof are ONE block (no lining), the foundation is 1 block (sink=1). The floor level,
    // as for the private sector, is one row lower (lowF): the walking row is lyLow+1.
    shedThin = obj.kind == McWorldExporter::McObject::Building && obj.closed && obj.code == 44000000;
    lowF = priv || shedThin;
    thinW = obj.lift || obj.thin;                     // small building parts: walls 1 block thick, like the elevator shafts (the floors and the roof stay)
    // "Residential" (CODE 44400000, a user request): SQUARE 2×2 windows — two neighboring columns
    // of glass for the same 2 rows of height (rows 1..2 above the slab). The PRIVATE SECTOR is also 2×2
    // (a request: "windows 1×2 on the first floor and 1×1 in the attic — that is just ugly"): under a slope
    // the strip is cut by a row or two higher, 2×1 remains — the user allowed it. Other codes
    // (in no list) — ordinary narrow vertical 1×2. Sheds (noWin) — without windows.
    wide2 = (obj.code == 44400000) || priv;
    if (floors && obj.closed)
    {
        for (int cell : surfaceCells)
        {
            const int ly = (cell / sx) % sy;
            lyLow = std::min(lyLow, ly);
            lyHigh = std::max(lyHigh, ly);
        }
        // In the private sector the walking level of the 1st floor is one block lower (the floor slab is 1 block) — we measure the shaft from it.
        const int shaftH = std::max(0, (lyHigh - 2) - (lowF ? lyLow + 1 : lyLow + 2) + 1);
        winDyMax = shaftH - 1;                                  // above — the roof lining, not a facade
        const int n4 = (shaftH + 1) / 5;                        // how many "floors of 4" fit
        nStories = std::max(1, n4);                             // even a short little house has a "first" floor (windows)
        int storyH = 4;
        if (n4 > 0)                                             // the remaining air is divided equally among the floors, but not above 6
            storyH = std::min(6, std::max(4, (shaftH - (nStories - 1)) / nStories));
        if (bigWin && shaftH >= 4)                              // a 2×3 window: a floor of 3 air + a slab
        {
            storyH = 3;
            nStories = std::max(1, (shaftH + 1) / 4);           // the top strip flush with the slab — fits
        }
        if (priv)                                               // the private sector (a request): the 1st-floor room is exactly
        {                                                       // 4 blocks, the step is 4 (the slab stands INSTEAD of the ceiling),
            storyH = 4;                                         // exactly two floors — the 2nd = the whole remainder up to the slopes
            nStories = woodSingle ? 1 : 2;                      // (an attic or a small loft — whatever fits);
                                                                // a wooden house (MAT=5) — exactly one floor, no ceiling for the attic
            storyP = storyH;                                    // windows do not get above the 2nd tier of the strip
        }
        else storyP = storyH + 1;
    }
    sxy = sx * sy;
    // The window definition: 0 — not a window; otherwise — the offset of the "inner" wall block behind the glass
    // (±1 in X, ±sxy in Z). The window strips are on the 1st and 2nd blocks ABOVE the floor/slab (raised by a block from
    // the floor level, as the user asked — not flush with it), along the facade — every 4th column.
    // The 3×5 windows of "non-residential" (bigWin), the mall panorama (panor) and the square 2×2 of "residential" (wide2) have their own profiles below.
    // BANDIT LAIR (a request): industrial zone buildings (44340000) — 30%, multi-storey residential (44400000, floors >= 2) — 5%. In a lair on the outside
    // hang ominous banners, the interiors are camp-style (barrels, hay bales, chests with outpost loot, banners), raiders live inside.
    // A test hook: MCX_FORCE_LAIR=1 makes any such building a lair.
    lair = false;
    if (floors && obj.closed && storyP > 0 && !obj.lift && obj.kind == McWorldExporter::McObject::Building)
    {
        const quint32 hl = lineHash(pairKey(minx, minz) ^ 0x1A1E5ull) % 100u;
        const bool force = qEnvironmentVariableIsSet("MCX_FORCE_LAIR");
        const bool never = qEnvironmentVariableIsSet("MCX_NO_LAIR");                 // a test hook: no lairs at all
        if (never) lair = false;
        else if (obj.code == 44340000) lair = force || hl < 30u;
        else if (obj.code == 44400000 && nStories >= 2) lair = force || hl < 5u;
    }
}

int ObjectVoxelizer::windowDir(int lx, int ly, int lz) const
    {
        if (noWin) return 0;                                    // a shed — no windows (a user request)
        if (storyP == 0 || ly >= lyHigh) return 0;              // the roof — not a facade
        const int dy = ly - lyLow - (lowF ? 1 : 2);             // from the walking floor of the 1st floor (private — a block lower)
        if (dy < 0 || dy > winDyMax) return 0;                  // above the shaft (the roof lining row) — not a window
        const int inStory = dy % storyP;
        if (bigWin)                                             // a 2×3 window: 3 rows of glass (a request)
        {
            if (inStory < 1 || inStory > 3) return 0;           // an inset of 1 from the floor, the top flush with the slab
        }
        else if (panoWin)                                       // the mall panorama: the strip height depends on the floor height
        {   // an inset of 1 from the floor; the strip height is at least 2 rows, and if the floor allows more than 2, exactly 2 blocks of wall
            // remain between the top of the strip and the ceiling (a floor has storyP-1 rows of air: the window occupies rows 1 .. storyP-4)
            if (inStory < 1 || inStory > std::max(2, storyP - 4)) return 0;
        }
        else if (inStory != 1 && inStory != 2) return 0;        // a residential strip — two rows from the floor
        if (dy / storyP >= nStories) return 0;                  // above the last strip — it is already an attic
        const int c = lz * sxy + ly * sx + lx;
        {   // the two upper rows of a column are a roof (also the roof of a lower part of a stepped complex): never glass
            const bool up1 = (ly + 1 >= sy) || grid[c + sx] == char(0) || grid[c + sx] == char(2);
            const bool up2 = (ly + 2 >= sy) || grid[c + 2 * sx] == char(0) || grid[c + 2 * sx] == char(2);
            if (up1 || up2) return 0;
        }
        const bool xn = (lx == 0 || grid[c - 1] == char(0) || grid[c - 1] == char(2));
        const bool xp = (lx == sx - 1 || grid[c + 1] == char(0) || grid[c + 1] == char(2));
        const bool zn = (lz == 0 || grid[c - sxy] == char(0) || grid[c - sxy] == char(2));
        const bool zp = (lz == sz - 1 || grid[c + sxy] == char(0) || grid[c + sxy] == char(2));
        const bool xw = xn || xp, zw = zn || zp;                // where the wall face looks
        if (!xw && !zw) return 0;                               // a pocket in the thickness — not a facade
        if (bigWin)
        {
            // A 2×3 panel (a request): 2 panes + 2 blind mullions per period, the phase from the start of
            // the facade; the corner is blind (unlike the mall panorama, the strip does not wrap around the corner).
            if (xw == zw) return 0;
            const int along = xw ? lz : lx;
            const int m = (((along - 1) % 4) + 4) % 4;          // 0..1 — glass, 2..3 — a mullion
            if (m >= 2) return 0;
            return xw ? (xn ? 1 : -1) : (zn ? sxy : -sxy);
        }
        if (panoWin)
        {
            const bool useX = xw || !zw;                        // a mall corner is glass too, X has priority
            const int along = useX ? lz : lx;
            // The key of the facade LINE (not of the column!): X-facing — the side + lz; Z-facing — the side +
            // the side offset by minx (a constant per facade ⇒ "along" only sets the phase).
            const qint64 lk = useX ? pairKey(qint64(minx) + (xn ? 0 : 1), qint64(minz) + lz)
                                   : pairKey(qint64(minx) + (zn ? 2 : 3), qint64(minz) + (zn ? 0 : 1));
            const quint32 lh = lineHash(lk ^ 0xC0DE5ull);
            const int P = 6 + int(lh % 4u);                     // the period 6..9 ⇒ glass bands 5..8
            const int ph = int((lh >> 8) % quint32(P));
            if ((((along + ph) % P) + P) % P == 0) return 0;    // one column of the period is a mullion
            return useX ? (xn ? 1 : -1) : (zn ? sxy : -sxy);
        }
        if (xw == zw) return 0;                                 // a corner (both) or a lining (neither) — blind
        const int along = xw ? lz : lx;                         // the facade column runs along the second axis
        const int m = (((along % 4) + 4) % 4);                  // columns of period 4
        if (wide2 ? (m != 1 && m != 2) : m != 1) return 0;      // 44400000 — a square 2×2 (a pair of glass
                                                                  // columns), the others — a narrow 1×2 (one)
        return xw ? (xn ? 1 : -1) : (zn ? sxy : -sxy);           // the "inner" block behind the glass — FROM
                                                                  // the street into the wall thickness
}

void ObjectVoxelizer::buildWalls()
{
    nWindows = 0;                                     // windows in the building (without a single window — cobwebs inside)
    for (int cell : surfaceCells)
    {
        if (grid[cell] == char(5)) continue;              // a pre-marked hole — we do not write it
        const int lx = cell % sx;
        const int ly = (cell / sx) % sy;
        const int lz = cell / (sx * sy);
        if (priv && lyHigh >= 0 && ly == lyHigh && interiorCol(lx, ly, lz))
            continue;                                     // the private sector: we write the flat cap
                                                          // only on the wall ring (the slopes hold onto it)
        const int dir = forcedWall.contains(cell) ? 0 : windowDir(lx, ly, lz);   // there are no windows at an entrance
        addVoxel(minx + lx, miny + ly, minz + lz, dir ? (lair ? IdBlackGlass : IdGlass) : structFor(ly));
        if (dir) ++nWindows;
        if (dir)                                                // a window all the way through (a user request):
        {                                                       // the second block of the two-layer wall behind the glass
            const bool ok = (dir == 1 && lx + 1 < sx) || (dir == -1 && lx > 0)
                         || (dir == sxy && lz + 1 < sz) || (dir == -sxy && lz > 0);
            if (ok)
            {
                const int inner = cell + dir;
                if (grid[inner] == char(4))
                {
                    grid[inner] = char(5);                      // 5 — a hole behind the glass (a skip in the lining)
                    // A flower pot on the windowsill (a user request): with a 30% chance behind the
                    // LOWER glass of the strip a flower_pot with a random flower appears — it is visible from
                    // the street through the glass, it stands on the wall block of the row under the window. The exception is malls.
                    // A holder is mandatory: the row under the hole must be written (4) or be blind (1).
                    const quint32 hp = lineHash(pairKey(minx + lx, minz + lz) ^ (qint64(ly) << 32)
                                                 ^ 0xF10E5ull);
                    const int below = grid[inner - sx];         // ly>=1: the window is not in the bottom row
                    // In a 2×2 window the windowsill is under the LEFT glass of the pair (a window = one planter, not
                    // two; this also keeps the pot density from eating the "see-through" of the windows in the big-check).
                    const int alongP = (dir == 1 || dir == -1) ? lz : lx;
                    const bool potCol = !wide2 || ((((alongP % 4) + 4) % 4) == 1);
                    if (!panor && !obj.lift && potCol && (hp % 100u) < 30u
                        && (ly - lyLow - (lowF ? 1 : 2)) % storyP == 1  // the pot — under the LOWER glass of the strip
                        && (below == char(4) || below == char(1)))
                    {
                        const int px = minx + lx + (dir == 1 ? 1 : dir == -1 ? -1 : 0);
                        const int pz = minz + lz + (dir == sxy ? 1 : dir == -sxy ? -1 : 0);
                        const int fl = int((hp >> 8) % quint32(kFlowersCount));
                        addVoxel(px, miny + ly, pz, quint16(IdFlowerPotBase + fl));
                    }
                }
            }
        }
        ++added;
    }
}

void ObjectVoxelizer::calcPatch()
{
    // The wall patch by surface cells — needed by the pitched roof (the roof used to compute it itself).
    fx0 = sx; fx1 = -1; fz0 = sz; fz1 = -1;
    for (int cell : surfaceCells)
    {
        const int lx = cell % sx, lz = cell / sxy;
        fx0 = std::min(fx0, lx); fx1 = std::max(fx1, lx);
        fz0 = std::min(fz0, lz); fz1 = std::max(fz1, lz);
    }
    ridgeZ = (fz1 - fz0) >= (fx1 - fx0);             // the ridge along Z ⇒ slopes along X
    a0 = ridgeZ ? fx0 : fz0; a1 = ridgeZ ? fx1 : fz1; // across the slopes
    b0 = ridgeZ ? fz0 : fx0; b1 = ridgeZ ? fz1 : fx1; // along the ridge
}

void ObjectVoxelizer::buildEntrances()
{
    // ——— BUILDING ENTRANCES ———
    // Buildings (except elevator shafts LIFT=1 and sheds 44000000) get an entrance in the 1st-floor wall: a 1×2 opening through both
    // walls (the outer layer — a door, the inner one — emptiness). The entrance is MANDATORY: first we look for an ideal place (a flat
    // wall without windows), then — any with both wall layers, windows nearby are replaced by a wall (forcedWall). Ordinary houses —
    // one entrance, apartment buildings (44400000) — from 1 to 5 entrances along the facade length. Doors: 90% iron ("locked"),
    // 10% wooden of the wall color; malls and churches — double wooden ones; for large malls (a plan from 1200 m²) in front of the
    // doors there are stone pressure plates. The entrance cells go into doorCells: furniture does not grow in the passage.
    if (floors && obj.closed && storyP > 0 && !obj.lift && !noWin && !raisedObj
        && obj.kind == McWorldExporter::McObject::Building && lyHigh >= lyLow + 4)
    {
        const int lyF0 = lowF ? lyLow + 1 : lyLow + 2;               // the walking row of the 1st floor
        static const int ddx[4] = { 0, 1, 0, -1 }, ddz[4] = { -1, 0, 1, 0 };   // N, E, S, W
        // the lateral axis of the leaves: for N/S walls — along X, for E/W — along Z (the second leaf with an offset of +1)
        auto latX = [&](int d) { return ddz[d] != 0 ? 1 : 0; };
        auto latZ = [&](int d) { return ddx[d] != 0 ? 1 : 0; };
        const bool church = (obj.code == 44319000);
        const bool dbl = panor || church;                            // a double door
        struct EntCand { int lx, lz, d; };
        auto gAt = [&](int x, int y, int z) -> char
        { return (x < 0 || x >= sx || z < 0 || z >= sz || y < 0 || y >= sy) ? char(0) : grid[z * sxy + y * sx + x]; };
        auto isOut = [&](int x, int y, int z) { const char g = gAt(x, y, z); return g == char(0) || g == char(2); };
        // the strictness level: 0 — ideal (without windows, the side cells are wall); 1 — any wall with two layers; 2 — only
        // the outer cell with a neighbor open to the outside
        auto okLeaf = [&](int lx, int lz, int d, int level) -> bool
        {
            for (int r = 0; r < 2; ++r)
            {
                const int y = lyF0 + r;
                if (gAt(lx, y, lz) != char(1) || !isOut(lx + ddx[d], y, lz + ddz[d])) return false;
                if (level >= 2) continue;
                if (gAt(lx - ddx[d], y, lz - ddz[d]) != char(4)) return false;
                const char g2 = gAt(lx - 2 * ddx[d], y, lz - 2 * ddz[d]);
                if (g2 != char(3) && g2 != char(4)) return false;
                if (level == 0)
                {
                    if (windowDir(lx, y, lz)) return false;
                    for (int t = -1; t <= 1; t += 2)               // on the sides — a wall too (not a corner and not the edge of the opening)
                    {
                        const int tx = lx + t * latX(d), tz = lz + t * latZ(d);
                        if (gAt(tx, y, tz) != char(1) || !isOut(tx + ddx[d], y, tz + ddz[d])) return false;
                    }
                }
            }
            return true;
        };
        QVector<EntCand> chosen;
        const bool apt = (obj.code == 44400000);
        const int Lmax = std::max(fx1 - fx0 + 1, fz1 - fz0 + 1);
        const int nEnt = apt ? std::max(1, std::min(5, int(Lmax / 22.0 + 0.5))) : 1;
        const int minSep = apt ? std::max(8, Lmax / (nEnt + 1)) : 0;
        const quint32 hE = lineHash(pairKey(minx, minz) ^ 0xD00DEull);
        for (int level = 0; level <= 2 && chosen.size() < nEnt; ++level)
        {
            QVector<EntCand> cands;
            for (int cell : surfaceCells)
            {
                if ((cell / sx) % sy != lyF0) continue;
                const int lx = cell % sx, lz = cell / sxy;
                for (int d = 0; d < 4; ++d)
                    if (okLeaf(lx, lz, d, level)
                        && (!dbl || okLeaf(lx + latX(d), lz + latZ(d), d, level)))
                        cands.append(EntCand{ lx, lz, d });
            }
            if (cands.isEmpty()) continue;
            std::sort(cands.begin(), cands.end(), [](const EntCand& a, const EntCand& b)
            { return a.lz != b.lz ? a.lz < b.lz : (a.lx != b.lx ? a.lx < b.lx : a.d < b.d); });
            const int n0 = cands.size(), start = int(hE % quint32(n0));
            for (int t = 0; t < n0 && chosen.size() < nEnt; ++t)
            {
                const EntCand& c = cands[(start + t) % n0];
                bool far = true;
                for (const EntCand& q : chosen)
                    if (std::max(std::abs(q.lx - c.lx), std::abs(q.lz - c.lz)) < std::max(minSep, dbl ? 3 : 1)) { far = false; break; }
                if (far) chosen.append(c);
            }
        }
        if (!chosen.isEmpty())
        {
            // the door wood color — by the color of the building wall block (not needed for an iron one)
            int wr = 162, wg = 130, wb = 78;
            {
                const int pi = int(id) - int(IdPaletteStart);
                if (pi >= 0 && pi < kPaletteCount) { wr = kBlockPalette[pi].r; wg = kBlockPalette[pi].g; wb = kBlockPalette[pi].b; }
                else if (id == IdIronBlock) { wr = wg = wb = 200; }
                else if (id == IdDirt) { wr = 134; wg = 96; wb = 67; }
                else if (id == IdOakLog) { wr = 102; wg = 81; wb = 49; }
            }
            int woodType = 0, bestD = std::numeric_limits<int>::max();
            const int sat = std::max({ wr, wg, wb }) - std::min({ wr, wg, wb });
            if (sat < 40)
            {   // gray/white walls: the nearest "wooden" by color would come out pink (cherry) — we take by brightness
                const int lum = (wr + wg + wb) / 3;
                woodType = lum < 80 ? 5 /*dark oak*/ : lum < 140 ? 1 /*spruce*/ : 2 /*birch*/;
            }
            else
                for (int w = 0; w < 11; ++w)
                {
                    const int dr = kDoorRgb[w][0] - wr, dg = kDoorRgb[w][1] - wg, db = kDoorRgb[w][2] - wb;
                    const int dd = dr * dr + dg * dg + db * db;
                    if (dd < bestD) { bestD = dd; woodType = w; }
                }
            const int planArea = (fx1 - fx0 + 1) * (fz1 - fz0 + 1);
            const bool bigShop = panor && planArea >= 1200;
            // the "left" side of the view (the view along facing): N->west, E->north, S->east, W->south; the second leaf stands
            // in the +lateral direction (east for N/S walls, south for E/W), the hinges are on the outer sides of the pair
            static const int leftIsNeg[4] = { 1, 1, 0, 0 };           // is the left side of the view in the negative lateral axis?
            for (int ci = 0; ci < chosen.size(); ++ci)
            {
                const EntCand& c = chosen[ci];
                const quint32 hd = lineHash(pairKey(minx + c.lx, minz + c.lz) ^ 0xD00F5ull);
                const bool iron = !panor && !church && (hd % 100u) < 90u;   // malls and churches — wooden
                const int type = iron ? 11 : woodType;
                const int leaves = dbl ? 2 : 1;
                for (int li = 0; li < leaves; ++li)
                {
                    const int lx = c.lx + li * latX(c.d), lz = c.lz + li * latZ(c.d);
                    int hinge = 0;                                      // 0 — left, 1 — right
                    if (dbl) hinge = (li == 0) ? (leftIsNeg[c.d] ? 0 : 1) : (leftIsNeg[c.d] ? 1 : 0);
                    for (int r = 0; r < 3; ++r)
                    {
                        const int y = lyF0 + r;
                        if (r < 2)
                        {
                            const int outerCell = lz * sxy + y * sx + lx;
                            grid[outerCell] = char(5);                  // the outer layer — a door (an ordinary wall is not written)
                            const int ix = lx - ddx[c.d], iz = lz - ddz[c.d];
                            if (gAt(ix, y, iz) == char(4)) grid[iz * sxy + y * sx + ix] = char(5);   // the inner layer — an opening
                            addVoxel(minx + lx, miny + y, minz + lz,
                                     quint16(int(IdDoorBase) + (((type * 4 + c.d) * 2 + hinge) * 2 + r)));
                            ++added;
                            if (r == 0 && iron)
                    {   // a stone button FROM INSIDE at an iron door: we put it into the inner-layer cell beside the opening, it is
                        // "glued" to the neighboring block of the outer layer (it touches the door) and powers it — the door opens
                        const int tx = c.lx + (ddx[c.d] == 0 ? 1 : 0), tz = c.lz + (ddx[c.d] == 0 ? 0 : 1);
                        const int ix = tx - ddx[c.d], iz = tz - ddz[c.d];
                        if (gAt(tx, y, tz) == char(1) && gAt(ix, y, iz) == char(4))
                        {
                            grid[iz * sxy + y * sx + ix] = char(5);          // the inner lining is not written here
                            addVoxel(minx + ix, miny + y, minz + iz,
                                     quint16(int(IdStoneButtonBase) + ((c.d + 2) & 3)));   // looks inward
                            ++added;
                        }
                        // ~20% of locked iron doors: a button OUTSIDE too (on the outer face of the same wall block) — the door can be opened from the street
                        if (gAt(tx, y, tz) == char(1) && ((hd >> 12) % 100u) < 20u)
                        {
                            const int ox = tx + ddx[c.d], oz = tz + ddz[c.d];
                            const char go = gAt(ox, y, oz);
                            if ((go == char(0) || go == char(2)) && store.get(minx + ox, miny + y - sink, minz + oz) == IdAir)
                            {
                                addDecor(minx + ox, miny + y, minz + oz, quint16(int(IdStoneButtonBase) + (c.d & 3)));   // looks outward
                                ++added;
                            }
                        }
                    }
                    for (int k = 0; k <= 2; ++k)                 // the entrance cells: the wall, the opening, the room behind it
                            {
                                const int bx = minx + lx - k * ddx[c.d], bz = minz + lz - k * ddz[c.d], by = miny + y;
                                entranceKeys.append((qint64(bx + 32768) << 29) | (qint64(bz + 32768) << 12) | qint64(by));
                            }
                        }
                        // windows above the door and at the jambs are forbidden: a wall (at the edges of the pair — 1 cell wider)
                        for (int t = -1; t <= 1; ++t)
                            if (!dbl || (li == 0 ? t <= 0 : t >= 0))
                                forcedWall.insert((lz + t * latZ(c.d)) * sxy + y * sx + (lx + t * latX(c.d)));
                        if (r == 0 && bigShop)
                            addVoxel(minx + lx + ddx[c.d], miny + y, minz + lz + ddz[c.d], IdPressurePlateStone);
                    }
                }
            }
        }
    }
}

void ObjectVoxelizer::buildLining()
{
    // Dark buildings without a single window: cobwebs in the upper corners of rooms (at the ceiling, where at least two walls meet).
    if (nWindows == 0 && obj.closed && sx >= 5 && sz >= 5 && sy >= 4)
    {
        const quint16 web = quint16(IdPaletteStart + paletteIndexOf("minecraft:cobweb"));
        for (int lz = 1; lz < sz - 1; ++lz)
            for (int ly = 1; ly < sy - 1; ++ly)
                for (int lx = 1; lx < sx - 1; ++lx)
                {
                    const int c = lz * sxy + ly * sx + lx;
                    if (grid[c] != char(3) || (grid[c + sx] != char(1) && grid[c + sx] != char(4))) continue;
                    int walls = 0;
                    const int nb[4] = { c - 1, c + 1, c - sxy, c + sxy };
                    for (int n : nb) if (grid[n] == char(1) || grid[n] == char(4)) ++walls;
                    if (walls < 2) continue;
                    if (lineHash(pairKey(minx + lx, minz + lz) ^ (qint64(ly) << 32) ^ 0xC0B3EBull) % 100u < 35u)
                        addVoxel(minx + lx, miny + ly, minz + lz, web);
                }
    }
    if (obj.closed)
    {
        for (int c = 0; c < volI; ++c)
        {
            if (grid[c] != char(4)) continue;                 // only the lining (the deep body is emptiness)
            const int lx = c % sx;
            const int ly = (c / sx) % sy;
            const int lz = c / (sx * sy);
            if (priv && lyHigh >= 0 && ly == lyHigh - 1 && interiorCol(lx, ly, lz)) continue;
                                                                    // private sector: the roof lining
                                                                    // is not written in the interior — above the 2nd floor
                                                                    // there is no ceiling, only slopes
            if (lowF && ly == lyLow + 1 && grid[c + sx] == char(3)) continue;
                                                                    // a 1-block foundation (a request): the floor slab
                                                                    // lining row is not written in the interior —
                                                                    // the walking cell of the 1st floor is a block lower
            // LIFT==1 (a user request): WALLS 1 block thick — the wall lining
            // (a horizontal neighbor — the surface "1") is NOT written into the store; the grid is NOT touched,
            // everything that consumes the grid (slopes/cavity/stairs/partitions) lives as before.
            // Cells with "1" above/below — the FLOOR/ROOF lining, requested "do not touch" —
            // stay: the walls are thin, while the floor/ceiling are closed over the whole patch (without gaps at the walls).
            if (thinW || shedThin)
            {
                const bool side1 = (lx > 0 && grid[c - 1] == char(1))
                    || (lx + 1 < sx && grid[c + 1] == char(1))
                    || (lz > 0 && grid[c - sxy] == char(1))
                    || (lz + 1 < sz && grid[c + sxy] == char(1));
                const bool vert1 = (ly > 0 && grid[c - sx] == char(1))
                    || (ly + 1 < sy && grid[c + sx] == char(1));
                if (side1 && (!vert1 || shedThin)) continue;                // a shed: the side lining is not written at the floor/roof either
                if (ly + 1 < sy && grid[c + sx] == char(1)) continue;      // the lining UNDER the roof: the shaft ceiling is 1 block, not 2
            }
            const int bx = minx + lx, by = miny + ly, bz = minz + lz;
            if (store.get(bx, by - sink, bz) != IdAir) continue;   // the surface takes priority
            // "- sink" is mandatory: the store is written at the sunk level (addVoxel), and without the shift
            // the check of the two rows missed — the row under the cap landed on the cap and the lining
            // vanished as a strip around the whole perimeter (a "holey belt" in rooms).
            addVoxel(bx, by, bz, structFor(ly));
            ++added;
        }
    }
}

void ObjectVoxelizer::buildStairs()
{
    // Inter-floor stairs (a user request, globally for multi-storey buildings). The "head-on" criterion:
    // we scan the cavity of the 1st floor row by row — where a flight window fits
    // (storyP+1) x 2..3 blocks (the very "patch of ~6x10") — we put a staircase there; in LARGE
    // houses (a cavity >= 200 cells) — a SECOND one, in the suitable window farthest from the first. The floor
    // flights stand one above another in the same shafts. If a flight did not fit or on
    // some floor there is a hole/obstacle in the window — that floor is served by a VERTICAL ladder
    // along a blind wall. Steps and ladders are marked "5": the slab pass skips
    // such cells (set(IdAir) is a no-op: a slab can only be NOT written), above the head of
    // the top steps air remains, a torch/lamp/chest will not climb into the shaft. The heights
    // are honest: the slab of floor k — lyLow+1+k*storyP (the same formula as in the slab pass),
    // step j occupies the cell lyS-R+j (R = storyP, +1 in the private sector), the top one stands INSTEAD of
    // the slab. "+sink" is
    // not needed here: the foundation is already in the grid, an external shift dropped the flight under the floor.
    if (floors && obj.closed && storyP > 0 && nStories >= 2 && lyLow + 2 <= lyHigh - 2)
    {
        // The private sector: the walking level of the 1st floor is lyLow+1 (a 1-block foundation), one step more
        // per flight (the lowest one stands IN the walking cell); the cavity mask — by the row lyLow+2,
        // where the interior is still "3" (on the floor row of a thick mesh slab there would be a lining).
        const int R = priv ? storyP + 1 : storyP;               // steps per flight (1 step = 1 block)
        const int lyC0 = lyLow + 2;                             // the row of the cavity mask (the air "3")
        QVector<char> cav(sx * sz, 0);
        for (int lz = 0; lz < sz; ++lz)
            for (int lx = 0; lx < sx; ++lx)
                cav[lz * sx + lx] = (grid[lz * sxy + lyC0 * sx + lx] == char(3));
        auto rectCav = [&](int x0, int z0, int w, int h)
        {   // the whole rectangle x0..x0+w-1, z0..z0+h-1 — a cavity?
            for (int z = z0; z < z0 + h; ++z)
                for (int x = x0; x < x0 + w; ++x)
                    if (!cav[z * sx + x]) return false;
            return true;
        };
        // The flight slot — the FIRST window that fits (R+1) x width: width 3 (a user request —
        // "make 3 instead of 2 blocks of width"), did not fit — 2, then 1;
        // a run along X, then along Z. THE FLIGHT IS TURNED (a user request: "the entrance to stairs
        // is always right against a wall — turn the direction, it is easier to enter"): the cell at
        // the start wall is the UPPER LANDING (the slab cell of the k-th floor at the "5" of the top step),
        // the steps go FROM it into the depth of the room; the bottom step — behind it there is always an entrance
        // cell (in a room R+2 fits — there it is free, otherwise the bottom step is at the far
        // wall, but a block away from it). "Uphill" — AGAINST the scan direction (-X / -Z).
        struct Slot { int s0, t0, w; bool runX; };
        const int need = R + 1;                                 // the upper landing + the steps
        auto slotRect = [need](const Slot& s)                   // the slot rectangle [xa0,xa1,za0,za1]
        {
            return s.runX ? QVector<int>{ s.s0, s.s0 + need - 1, s.t0, s.t0 + s.w - 1 }
                          : QVector<int>{ s.t0, s.t0 + s.w - 1, s.s0, s.s0 + need - 1 };
        };
        // avoid==nullptr — the first one that fits (slot 0, the old behavior). Otherwise — we look for a WIDE
        // (first 3, then 2, then 1) rectangle that fits, does not intersect avoid even
        // diagonally (a gap >= 1) and is no closer than minD from its center (sqrt: minD is the square of the
        // distance): width is more important than a couple of extra blocks of distance (a request: "the staircase — 3 blocks").
        auto findSlot = [&](const Slot* avoid, double minD = 0.0, bool entry = false) -> Slot
        {
            Slot best{ -1, 0, 0, true };
            const QVector<int> av = avoid ? slotRect(*avoid) : QVector<int>();
            for (int w = 3; w >= 1; --w)
            {
                Slot wBest{ -1, 0, 0, true };
                double wBestD = -1.0;
                for (int rz = 0; rz < 2; ++rz)                  // the window along X (0) or along Z (1)
                {
                    const bool rX = (rz == 0);
                    const int aMax = rX ? sx : sz, bMax = rX ? sz : sx;
                    if (aMax < need || bMax < w) continue;
                    for (int t = 0; t + w <= bMax; ++t)
                        for (int a = 0; a + need <= aMax; ++a)
                        {
                            if (!rectCav(rX ? a : t, rX ? t : a, rX ? need : w, rX ? w : need))
                                continue;
                            if (entry)
                            {   // a request: "a base right against a wall/partition — move it away
                                // by 1 block so that the cell IN FRONT of the bottom step is free".
                                // The entrance cell = a step beyond the window on the side of the bottom step
                                // (the bottom one is at the max end, the cavity is where the window is).
                                if (a + need >= aMax) continue;   // beyond the window the grid edge = a wall
                                if (!(rX ? rectCav(a + need, t, 1, w) : rectCav(t, a + need, w, 1)))
                                    continue;
                            }
                            Slot cand{ a, t, w, rX };
                            if (!avoid) return cand;            // first-fit: the first one is it
                            const QVector<int> r = slotRect(cand);
                            if (r[0] <= av[1] + 1 && av[0] - 1 <= r[1]
                                && r[2] <= av[3] + 1 && av[2] - 1 <= r[3])
                                continue;                       // it touches slot 0 — not suitable
                            const double dx = (r[0] + r[1] - av[0] - av[1]) * 0.5;
                            const double dz = (r[2] + r[3] - av[2] - av[3]) * 0.5;
                            const double d = dx * dx + dz * dz;
                            if (d <= wBestD) continue;
                            wBestD = d;
                            wBest = cand;
                        }
                }
                if (wBest.s0 >= 0 && wBestD >= minD) { best = wBest; break; }
            }
            return best;
        };
        QVector<Slot> slotList;                                 // the name "slots" is taken by a Qt macro
        // First — windows WITH A FREE ENTRANCE CELL in front of the bottom step (a request
        // "pull away from the wall by 1 block IF POSSIBLE"); in a cramped house there may be no such window
        // at all — then we place it as we can ("if there are no more places — fine").
        Slot first = findSlot(nullptr, 0.0, true);
        if (first.s0 < 0) first = findSlot(nullptr);
        if (first.s0 >= 0) slotList << first;
        // The second staircase (a user request: "in large-area buildings one staircase is
        // not enough — put the second in the farthest suitable place"). "Large" —
        // a cavity >= 200 cells; the second shaft must fit separately from the first and no closer than
        // 6 blocks between centers, otherwise we settle for one.
        {
            int cavCount = 0;
            for (char c : cav) cavCount += (c != 0);
            if (first.s0 >= 0 && cavCount >= 200)
            {
                Slot far = findSlot(&first, 36.0, true);        // the same entrance priority
                if (far.s0 < 0) far = findSlot(&first, 36.0);
                if (far.s0 >= 0) slotList << far;
            }
        }
        // A vertical ladder (plan B for a floor): a column in the cavity right against a REAL
        // wall — we check the holder in the store over the WHOLE climb height (the grid "remembers"
        // the lining, but windows/doors have already been cut out — the server would drop a ladder in a hole
        // when loading the chunk). The ladder cells are marked "5": both a hole in the slab and "do not go here" for a light/chest.
        const quint16 ladId[4] = { IdLadderN, IdLadderE, IdLadderS, IdLadderW };
        const int ladDx[4] = { 0, -1, 0, 1 }, ladDz[4] = { 1, 0, -1, 0 };   // where the holder is
        int prevLadLx = -1, prevLadLz = -1;                      // the ladder cell of the previous flight: the next one is searched as close as possible (if possible — strictly above it, a common shaft)
        auto ladderFlight = [&](int lyBot, int lyTop)           // lyTop — the slab cell (it becomes a hole)
        {
            // Pass 0 — an "inner" ladder: the cell remains a cavity on ALL tiers of the flight and on the row of exit to the floor above (it does not go into the narrowing of a composite building / does not
            // hit the roof of the lower part); the holder is a wall — a ready one, and where there is none in the cavity, it is built up by a post of the building material. Pass 1 — the former one
            // (as it turns out: "if it is really bad — the ladder will be strange"). Among the suitable cells the one nearest to the ladder of the previous flight is taken (above it — if possible).
            for (int pass = 0; pass < 2; ++pass)
            {
                int bestLx = -1, bestLz = -1, bestD = 0, bestScore = std::numeric_limits<int>::max();
                QVector<int> bestBuild;
                for (int lz = 1; lz < sz - 1; ++lz)
                    for (int lx = 1; lx < sx - 1; ++lx)
                    {
                        if (!cav[lz * sx + lx]) continue;
                        if (pass == 0)
                        {
                            bool inside = lyTop + 1 < sy;
                            for (int ly = lyBot; ly <= lyTop + 1 && inside; ++ly)
                                if (grid[lz * sxy + ly * sx + lx] != char(3)) inside = false;
                            if (!inside) continue;
                        }
                        for (int d = 0; d < 4; ++d)
                        {
                            const int hlx = lx + ladDx[d], hlz = lz + ladDz[d];
                            const int hx = minx + hlx, hz = minz + hlz;
                            bool holds = true;
                            QVector<int> toBuild;                        // the tiers where the holder must be built up
                            for (int ly = lyBot; ly <= lyTop && holds; ++ly)
                            {
                                if (store.get(hx, miny + ly - sink, hz) != IdAir) continue;
                                if (pass == 0 && hlx >= 0 && hlx < sx && hlz >= 0 && hlz < sz && grid[hlz * sxy + ly * sx + hlx] == char(3)) toBuild.append(ly);
                                else holds = false;
                            }
                            if (!holds) continue;
                            bool free = true;
                            for (int ly = lyBot; ly <= lyTop && free; ++ly)
                                if (store.get(minx + lx, miny + ly - sink, minz + lz) != IdAir) free = false;
                            if (!free) continue;
                            const int score = prevLadLx < 0 ? (lz * sx + lx)
                                                            : ((lx - prevLadLx) * (lx - prevLadLx) + (lz - prevLadLz) * (lz - prevLadLz)) * 8 + int(toBuild.size());
                            if (score < bestScore) { bestScore = score; bestLx = lx; bestLz = lz; bestD = d; bestBuild = toBuild; }
                        }
                    }
                if (bestLx < 0) continue;
                const int lx = bestLx, lz = bestLz, d = bestD;
                const int hlx = lx + ladDx[d], hlz = lz + ladDz[d];
                for (int ly : bestBuild) { addVoxel(minx + hlx, miny + ly, minz + hlz, id); grid[hlz * sxy + ly * sx + hlx] = char(4); ++added; }   // a holder post
                for (int ly = lyBot; ly <= lyTop; ++ly)
                {
                    addVoxel(minx + lx, miny + ly, minz + lz, ladId[d]);
                    stairXZ.insert(pairKey(lx, lz));
                    grid[lz * sxy + ly * sx + lx] = char(5);
                    ++added;
                }
                prevLadLx = lx; prevLadLz = lz;
                return;
            }
        };
        // (priv is the common flag of the private sector, declared before the surface pass)
        // The flight of slot sl to the floor with the slab plane lyS: commit=false — only a check "did it fit", commit=true — it also places the steps/the head.
        auto tryFlight = [&](const Slot& sl, int lyS, bool commit) -> bool
        {
            const int s0 = sl.s0, t0 = sl.t0, width = sl.w;
            const bool runX = sl.runX;
            bool fits = s0 >= 0;                            // "holes": is the whole flight a cavity (3)?
            for (int wI = 0; wI < width && fits; ++wI)
                for (int j = 1; j <= R && fits; ++j)
                {
                    const int lx = runX ? (s0 + R - j + 1) : (t0 + wI), lz = runX ? (t0 + wI) : (s0 + R - j + 1);
                    const int lyJ = lyS - R + j;
                    const char gS = grid[lz * sxy + lyJ * sx + lx];
                    // the private sector: the bottom step stands in the row of the unwritten floor lining (4),
                    // the top one — in the row of the roof lining under the roof (4), if the slab = lyHigh-1
                    if (gS != char(3) && !priv) fits = false;
                    else if (priv && gS != char(3) && gS != char(4)) fits = false;
                }
            // "No ceiling above the staircase" (a user request): the SLAB cells above the last three steps
            // — where the feet and the head of the person climbing stand — must be a cavity (in fact
            // the player hit their head even at R-3: when stepping up the head flies into the cell further,
            // so the freedom window is extended by one more block). If there is a wall there (a facade/partition is already
            // in the store) — the flight is unsuitable, the floor is given to a vertical ladder.
            const int jHead0 = std::max(1, R - 3);          // the bottom step of the "freedom window"
            for (int wI = 0; wI < width && fits; ++wI)
                for (int j = jHead0; j <= R - 1 && fits; ++j)
                {
                    const int lx = runX ? (s0 + R - j + 1) : (t0 + wI), lz = runX ? (t0 + wI) : (s0 + R - j + 1);
                    const char gH = grid[lz * sxy + lyS * sx + lx];
                    if (gH != char(3) && !priv) fits = false;   // private: the slab = lyHigh-1 — a lining (4)
                    else if (priv && gH != char(3) && gH != char(4)) fits = false;
                }
            // The head above the TOP steps: two rows above the plane (the feet and the head on the landing of the next floor) must be a
            // cavity. Important for composite buildings of different heights: the number of floors is counted by the highest part, and a flight could
            // hit the roof of the low one (the roof/lining above the landing).
            if (!priv)
                for (int wI = 0; wI < width && fits; ++wI)
                    for (int j = jHead0; j <= R && fits; ++j)
                    {
                        const int lx = runX ? (s0 + R - j + 1) : (t0 + wI), lz = runX ? (t0 + wI) : (s0 + R - j + 1);
                        if (lyS + 2 >= sy || grid[lz * sxy + (lyS + 1) * sx + lx] != char(3) || grid[lz * sxy + (lyS + 2) * sx + lx] != char(3))
                            fits = false;
                    }
            // A step must not lie on something already standing (someone else's flight/ladder/furniture in the store)
            if (fits && !commit)
                for (int wI = 0; wI < width && fits; ++wI)
                    for (int j = 1; j <= R && fits; ++j)
                    {
                        const int lx = runX ? (s0 + R - j + 1) : (t0 + wI), lz = runX ? (t0 + wI) : (s0 + R - j + 1);
                        const int lyJ = lyS - R + j;
                        if (store.get(minx + lx, miny + lyJ - sink, minz + lz) != IdAir) fits = false;
                    }
            if (!fits) return false;                        // this slot did not fit for the floor
            if (!commit) return true;
            // The steps are STONE in all buildings except the private sector (the pitched roof codes —
            // there the flight is oak, to match the pitched roof). A user request.
            const quint16 fid = runX ? (priv ? IdStairsW : IdStoneStairsW)  // "uphill" AGAINST
                                     : (priv ? IdStairsN : IdStoneStairsN); // the scan: along -X / -Z
            for (int wI = 0; wI < width; ++wI)
                for (int j = 1; j <= R; ++j)
                {
                    const int lx = runX ? (s0 + R - j + 1) : (t0 + wI), lz = runX ? (t0 + wI) : (s0 + R - j + 1);
                    const int ly = lyS - R + j;
                    stairXZ.insert(pairKey(lx, lz));
                    addVoxel(minx + lx, miny + ly, minz + lz, fid);   // j=R stands INSTEAD of the slab:
                    grid[lz * sxy + ly * sx + lx] = char(5);          // the slab pass sees "5" — skips
                    ++added;
                }
            // The head: columns jHead0..R-1 at the slab level — the slab does not lie there (the mark "5"),
            // otherwise the person climbing hits their head on the floor (a request "there must be no ceiling
            // above the staircase" + a follow-up request: 2×2 windows are not enough, remove one more block of slab).
            for (int wI = 0; wI < width; ++wI)
                for (int j = jHead0; j <= R - 1; ++j)
                {
                    const int lx = runX ? (s0 + R - j + 1) : (t0 + wI), lz = runX ? (t0 + wI) : (s0 + R - j + 1);
                    grid[lz * sxy + lyS * sx + lx] = char(5);
                }
            return true;
        };
        for (int k = 1; k < nStories; ++k)                      // the flight floor(k-1) -> floor k
        {
            const int lyS = lyLow + 1 + k * storyP;             // the slab plane — the same formula
            if (lyS > lyHigh - (lowF ? 1 : 2)) break;           // as in the slab pass below; the private sector:
                                                                // the slab INSTEAD of the ceiling — the row lyHigh-1 is allowed
            bool anyFit = false;
            for (const Slot& sl : slotList)
                if (tryFlight(sl, lyS, true)) anyFit = true;
            if (!anyFit)
            {   // A building of different heights (for example low wings at the edges and a higher middle): for the upper floors that exist only over part of the building there are no stairs
                // of the first floor — we look for a NEW slot right at this level (the widest window, as far as possible from those already standing); the next floors use the same one.
                Slot nb{ -1, 0, 0, true };
                for (int w = 3; w >= 1 && nb.s0 < 0; --w)
                {
                    double nbD = -1.0;
                    for (int rz = 0; rz < 2; ++rz)
                    {
                        const bool rX = (rz == 0);
                        const int aMax = rX ? sx : sz, bMax = rX ? sz : sx;
                        if (aMax < need || bMax < w) continue;
                        for (int t = 0; t + w <= bMax; ++t)
                            for (int a = 0; a + need <= aMax; ++a)
                            {
                                const Slot cand{ a, t, w, rX };
                                if (!tryFlight(cand, lyS, false)) continue;
                                const QVector<int> r = slotRect(cand);
                                double dmin = 1e18;
                                for (const Slot& o : slotList)
                                {
                                    const QVector<int> ro = slotRect(o);
                                    const double dx = (r[0] + r[1] - ro[0] - ro[1]) * 0.5, dz = (r[2] + r[3] - ro[2] - ro[3]) * 0.5;
                                    dmin = std::min(dmin, dx * dx + dz * dz);
                                }
                                if (slotList.isEmpty()) dmin = 0.0;
                                if (dmin > nbD) { nbD = dmin; nb = cand; }
                            }
                    }
                }
                if (nb.s0 >= 0)
                {
                    slotList << nb;
                    anyFit = tryFlight(nb, lyS, true);
                }
            }
            if (!anyFit)                                        // not a single flight fit — a vertical one
                ladderFlight(lyS - R + 1, lyS);                 // the bottom ladder — in the walking cell of the floor
        }
    }
}

void ObjectVoxelizer::buildSlabs()
{
    // Inter-floor slabs (1 block) — deep body cells at the floor level of each next floor;
    // the roof serves as the top slab itself, the floor of the first floor is already two-layered (surface+lining).
    // The formula lyLow+1+k*storyP = the walking level of the previous floor + storyP. "+sink" must not
    // be woven in: the foundation is already inside the grid — a shift diverged from the stairs (slabs above a flight).
    if (floors && obj.closed && storyP > 0)
    {
        for (int k = 1; k < nStories; ++k)
        {
            const int lyS = lyLow + 1 + k * storyP;
            if (lyS > lyHigh - (lowF ? 1 : 2)) break;           // above — it is already the roof lining; the private sector:
                                                                // the slab in the row lyHigh-1 = the ceiling of the 1st floor
            for (int lz = 0; lz < sz; ++lz)
                for (int lx = 0; lx < sx; ++lx)
                {
                    const int c = lz * sxy + lyS * sx + lx;
                    if (grid[c] != char(3) && !((lowF || thinW) && grid[c] == char(4))) continue;
                                                                // the deep body = the slab itself; in the private
                                                                // sector the slab may lie in the row of the removed
                                                                // lining (the store is empty there — a skip above);
                                                                // LIFT (thin walls): the wall lining row
                                                                // is empty in the store — the slab reaches the wall
                    const int bx = minx + lx, by = miny + lyS, bz = minz + lz;
                    if (store.get(bx, by - sink, bz) != IdAir) continue;
                    addVoxel(bx, by, bz, structId);              // the floor slab — gray concrete (structId==0 -> the wall material)
                    ++added;
                }
        }
    }
}

void ObjectVoxelizer::buildCeiling()
{
    // A one-storey private house (log walls): there is no floor between stories (fewer than 6 rows, the slab
    // lyLow+5 does not lie), but a ceiling over the room is needed — planks on the top row of walls (lyHigh), under
    // the roof slopes. We never write the flat interior cap of a private house, so here the cell
    // is still a "surface" (1) or an empty lining — we write it with planks over the whole patch.
    if (woodSingle && priv && floors && obj.closed && storyP > 0 && lyHigh >= 0)
    {
        const int lyS = lyHigh;
        for (int lz = 0; lz < sz; ++lz)
            for (int lx = 0; lx < sx; ++lx)
            {
                const int c = lz * sxy + lyS * sx + lx;
                if (grid[c] != char(1) && grid[c] != char(3) && grid[c] != char(4)) continue;
                const int bx = minx + lx, by = miny + lyS, bz = minz + lz;
                if (store.get(bx, by - sink, bz) != IdAir) continue;
                addVoxel(bx, by, bz, IdOakPlanks);
                ++added;
            }
    }
}

void ObjectVoxelizer::buildAddressPlate()
{
    // THE ADDRESS PLATE (a request): one candidate per compass side — a wall of the 1st floor at eye height, free outside,
    // not a window and not next to an entrance; closer to the middle of the facade. The needed side (the nearest to a road) is chosen in Export().
    if (signs && floors && obj.closed && storyP > 0 && !obj.lift && obj.kind == McWorldExporter::McObject::Building
        && (!obj.streetType.isEmpty() || !obj.street.isEmpty() || !obj.number.isEmpty()))
    {
        const int lySg = (lowF ? lyLow + 1 : lyLow + 2) + 1;
        if (lySg < lyHigh - 1)
        {
            static const int sdx[4] = { 0, 1, 0, -1 }, sdz[4] = { -1, 0, 1, 0 };
            SignCand sc;
            sc.y = miny + lySg - sink;
            sc.l1 = obj.streetType; sc.l2 = obj.street; sc.l3 = obj.number;
            int bestScore[4] = { 1 << 20, 1 << 20, 1 << 20, 1 << 20 };
            const int ccx = (fx0 + fx1) / 2, ccz = (fz0 + fz1) / 2;
            for (int cell : surfaceCells)
            {
                if ((cell / sx) % sy != lySg || grid[cell] != char(1)) continue;
                const int lx = cell % sx, lz = cell / sxy;
                if (windowDir(lx, lySg, lz)) continue;
                for (int d = 0; d < 4; ++d)
                {
                    const int ox = lx + sdx[d], oz = lz + sdz[d];
                    const char go = (ox < 0 || ox >= sx || oz < 0 || oz >= sz) ? char(0) : grid[oz * sxy + lySg * sx + ox];
                    if (go != char(0) && go != char(2)) continue;
                    if (store.get(minx + ox, sc.y, minz + oz) != IdAir) continue;
                    bool nearDoor = false;
                    for (int t = -1; t <= 1 && !nearDoor; ++t)
                        for (int rr = -1; rr <= 0; ++rr)
                        {
                            const int tx = lx + (sdx[d] == 0 ? t : 0), tz = lz + (sdx[d] == 0 ? 0 : t);
                            if (doorCells.contains(dkey(minx + tx, miny + lySg + rr, minz + tz))) { nearDoor = true; break; }
                        }
                    if (nearDoor) continue;
                    const int sc2 = std::abs(sdx[d] == 0 ? lx - ccx : lz - ccz);
                    if (sc2 < bestScore[d])
                    {
                        bestScore[d] = sc2;
                        sc.x[d] = minx + ox; sc.z[d] = minz + oz; sc.ok[d] = true;
                    }
                }
            }
            if (sc.ok[0] || sc.ok[1] || sc.ok[2] || sc.ok[3]) signs->append(sc);
        }
    }
}

void ObjectVoxelizer::buildPartitions()
{
    // Room partitions (a user request: "large buildings without walls inside — divide them
    // into spacious rooms with passages"). A guillotine cut of the set of cavity cells: the building is
    // a prism, the mask of the 1st floor suits all floors. Rooms of 100..300 cells; shopping malls
    // (44318000) — 300..900. If the area > max, but any division gives a half < min — we do not divide
    // (the very "rare exception"). The columns of stair shafts/ladders (with the mark "5") are excluded
    // from the mask — a partition never enters a shaft. In each wall exactly one passage
    // of width 1 (malls — 2) and height 2: the room graph is a tree, so everything is connected. The partitions
    // are laid with the building wall block and marked "4" in the grid — chests/spawners/light do not
    // climb onto them, and torches can hang on them as on ordinary walls.
    // Metro station buildings (44330000) stay ONE open ticket hall (a user request: "do not split the
    // ideal vestibule building into rooms") — the staircase and its portal frame need the open space.
    if (floors && obj.closed && storyP > 0 && obj.code != 44330000 && lyLow + 2 <= lyHigh - 4)
    {
        const int minA = obj.code == 44318000 ? 300 : 100;
        const int maxA = obj.code == 44318000 ? 900 : 300;
        const int doorW = obj.code == 44318000 ? 2 : 1;
        const int lyF0 = lyLow + 2;                             // the walking level of the 1st floor
        QVector<QPair<int, int>> cells0;
        for (int lz = 0; lz < sz; ++lz)
            for (int lx = 0; lx < sx; ++lx)
            {
                if (grid[lz * sxy + lyF0 * sx + lx] != char(3)) continue;
                bool shaft = false;                             // is the column a shaft? (steps/ladders)
                for (int ly = lyF0; ly <= lyHigh && !shaft; ++ly)
                    if (grid[lz * sxy + ly * sx + lx] == char(5)) shaft = true;
                if (!shaft) cells0.append(qMakePair(lx, lz));
            }
        if (cells0.size() > maxA)                               // there is something to cut
        {
            quint32 ph = 2166136261u;                           // the same FNV+xorshift as for treasures
            auto pmix = [&ph](long long v)
            { for (int b = 0; b < 8; ++b) { ph ^= quint32((v >> (b * 8)) & 0xFF) + quint32(b * 31 + 7); ph *= 16777619u; } };
            pmix(minx); pmix(miny); pmix(minz); pmix(sx); pmix(sz); pmix(cells0.size());
            auto prnd = [&ph](int mod) -> int
            { ph ^= ph << 13; ph ^= ph >> 17; ph ^= ph << 5; return mod > 0 ? int(ph % quint32(mod)) : 0; };
            struct Wall { int posZ; bool alongX; QVector<QPair<int, int>> cells; int d0, d1; };
            //   alongX=true — we cut along Z (the wall cells have z==posZ, the wall runs along X)
            QVector<Wall> walls;
            auto split = [&](auto&& self, const QVector<QPair<int, int>>& cs, int depth) -> void
            {
                if (cs.size() <= maxA || depth > 8) return;
                int xa0 = sx, xa1 = -1, za0 = sz, za1 = -1;
                QSet<ColKey> set;
                for (const auto& p : cs)
                {
                    xa0 = qMin(xa0, p.first); xa1 = qMax(xa1, p.first);
                    za0 = qMin(za0, p.second); za1 = qMax(za1, p.second);
                    set.insert((qint64(p.first) << 20) | p.second);
                }
                const int total = cs.size();
                int axes[2] = { 1, 0 };                         // the long side of the bbox first
                if ((xa1 - xa0) > (za1 - za0)) { axes[0] = 0; axes[1] = 1; }
                for (int ai = 0; ai < 2; ++ai)
                {
                    const bool byZ = axes[ai] == 1;             // do we cut along Z?
                    const int c0 = byZ ? za0 : xa0, c1 = byZ ? za1 : xa1;
                    QVector<int> cnt(c1 - c0 + 1, 0);
                    for (const auto& p : cs) ++cnt[(byZ ? p.second : p.first) - c0];
                    int bestPos = -1, bestDiff = 1 << 30, pref = 0;
                    for (int c = c0; c <= c1; ++c)
                    {
                        const int left = pref, right = total - pref - cnt[c - c0];
                        pref += cnt[c - c0];
                        if (!cnt[c - c0] || left < minA || right < minA) continue;
                        const int diff = qAbs(left - right);
                        if (diff < bestDiff) { bestDiff = diff; bestPos = c; }
                    }
                    if (bestPos < 0) continue;                  // this axis cannot be split into minima
                    QVector<QPair<int, int>> wc, ls, rs;
                    QVector<int> doorCand;                      // the orthogonal coordinates of cells with cells on both sides
                    for (const auto& p : cs)
                    {
                        const int c = byZ ? p.second : p.first;
                        const int orth = byZ ? p.first : p.second;
                        if (c == bestPos)
                        {
                            wc.append(p);
                            const bool hasA = byZ ? set.contains((qint64(p.first) << 20) | (p.second - 1))
                                                  : set.contains((qint64(p.first - 1) << 20) | p.second);
                            const bool hasB = byZ ? set.contains((qint64(p.first) << 20) | (p.second + 1))
                                                  : set.contains((qint64(p.first + 1) << 20) | p.second);
                            if (hasA && hasB) doorCand.append(orth);
                        }
                        else if (c < bestPos) ls.append(p);
                        else rs.append(p);
                    }
                    if (wc.isEmpty() || ls.isEmpty() || rs.isEmpty()) continue;
                    std::sort(doorCand.begin(), doorCand.end());
                    Wall w;
                    w.alongX = byZ;                             // we cut along Z => the wall runs along X
                    w.posZ = bestPos;
                    w.cells = wc;
                    w.d0 = w.d1 = 0;
                    if (!doorCand.isEmpty())
                    {
                        int di = prnd(doorCand.size());
                        w.d0 = doorCand[di];
                        w.d1 = w.d0;
                        if (doorW > 1 && di + 1 < doorCand.size() && doorCand[di + 1] == w.d0 + 1)
                            w.d1 = w.d0 + 1;
                    }
                    walls << w;
                    self(self, ls, depth + 1);
                    self(self, rs, depth + 1);
                    return;
                }
            };
            split(split, cells0, 0);
            // Thin walls (lift): the wall lining is empty in the store, and the end of a partition
            // "does not reach" the facade by a block (a see-through gap, the same disease as with
            // slabs). We complete the char4 rows at the ends into the line — the writing loop below
            // will accept them itself (the store check protects against a double write).
            if (thinW || shedThin)
                for (Wall& w : walls)
                {
                    QSet<ColKey> have;
                    for (const auto& p : w.cells) have.insert((qint64(p.first) << 20) | p.second);
                    const int dx = w.alongX ? 1 : 0, dz = w.alongX ? 0 : 1;
                    const int n = w.cells.size();
                    for (int i = 0; i < n; ++i)             // the inner neighbors are "3", they will not pass the gate
                        for (int s = -1; s <= 1; s += 2)
                        {
                            const int nx = w.cells[i].first + dx * s, nz = w.cells[i].second + dz * s;
                            if (nx < 1 || nx >= sx - 1 || nz < 1 || nz >= sz - 1) continue;
                            if (grid[nz * sxy + lyF0 * sx + nx] != char(4)) continue;
                            if (have.contains((qint64(nx) << 20) | nz)) continue;
                            have.insert((qint64(nx) << 20) | nz);
                            w.cells.append(qMakePair(nx, nz));
                        }
                }
            // The walls of the floors: from the walking level to the bottom cell of the slab of the next floor
            // (the top floor — up to the ceiling: the column goes up to the first non-air cell, the roof
            // will stop it itself). The passage is 2 blocks high. The private sector — ONLY the 1st floor
            // (a user request: "a partition goes above the first floor by 1 block,
            // partitions are not needed on the second floor"): the 2nd floor = a mezzanine under the slopes.
            for (int k = 0; k < (priv ? 1 : qMax(1, nStories)); ++k)
            {
                const int lyBot = (lowF && k == 0 ? lyLow + 1 : lyF0) + k * storyP;  // the floor of a private house — a block lower
                const int lyTop = (k + 1 < nStories) ? qMin(lyLow + (k + 1) * storyP, lyHigh - 3)
                                                     : (priv ? lyHigh - 2 : lyHigh - 1);  // a private house has no ceiling —
                                                                 // we leave the lining row to the roof skips
                if (lyBot > lyTop) continue;
                for (const Wall& w : walls)
                    for (const auto& pc : w.cells)
                    {
                        const int lx = pc.first, lz = pc.second;
                        const int orth = w.alongX ? lx : lz;   // along what the opening is counted
                        for (int ly = lyBot; ly <= lyTop; ++ly)
                        {
                            if (orth >= w.d0 && orth <= w.d1 && ly <= lyBot + 1)
                            {   // the opening — we remember both cells (for the "Interiors")
                                const int byd = miny + ly;
                                if (byd >= 0) doorCells.insert(dkey(minx + lx, byd, minz + lz));
                                continue;
                            }
                            const int c = lz * sxy + ly * sx + lx;
                            if (grid[c] != char(3)
                                && !(((lowF && ly == lyLow + 1) || thinW || shedThin) && grid[c] == char(4))) continue;
                                                                // private: the bottom row of the partition stands
                                                                // on the row of the removed floor lining (the store is empty);
                                                                // lift: the ends of the line — an empty wall lining
                            const int bx = minx + lx, by = miny + ly, bz = minz + lz;
                            if (store.get(bx, by - sink, bz) != IdAir) continue;
                            addVoxel(bx, by, bz);
                            partSet.insert(dkey(bx, by, bz));
                            grid[c] = char(4);
                            ++added;
                        }
                    }
            }
        }
    }
    // "Treasures" (bldpol with floors only, a user request): a chest on EVERY 5th FLOOR of the city —
    // the floor counter runs through all buildings (5 one-storey in a row — a treasure in the fifth house;
    // 5 two-storey — in the third on the 1st floor and in the fifth on the 2nd). In 50% of cases we try a DOUBLE
    // chest: two chest blocks type=left/right, facing=north, neighbors along X — vanilla joins them
    // into one inventory for two loots; if there is no free pair — a single one. The stack/place/tables — from
    // an xorshift seeded with the house geometry: the same input — the same layout.
    if (floors && obj.closed && storyP > 0 && storyCounter && chests && sx >= 8 && sz >= 8)
    {
        quint32 rh = 2166136261u;
        auto mix = [&rh](long long v)
        { for (int b = 0; b < 8; ++b) { rh ^= quint32((v >> (b * 8)) & 0xFF) + quint32(b * 31 + 7); rh *= 16777619u; } };
        mix(minx); mix(miny); mix(minz); mix(sx); mix(sz); mix(nStories);
        auto rnd = [&rh](int mod) -> int
        { rh ^= rh << 13; rh ^= rh >> 17; rh ^= rh << 5; return mod > 0 ? int(rh % quint32(mod)) : 0; };
        for (int k = 0; k < nStories; ++k)
        {
            ++*storyCounter;
            // Monster spawners (a user request): the same "city" floor counter as for
            // chests; on every 15th floor — 50/50 to put a spawner into the cavity of this floor,
            // the type: 70% zombie / 15% skeleton / 15% spider. The randomness is a SEPARATE xorshift stream
            // (seeded by the geometry + the floor number) so that the chest layout does not shift.
            // Sheds/utility structures (CODE 44000000, a request): spawners twice as often — the second slot of the cycle
            // (every 15th floor of the city + shifted by 7), the rest of the choice (50/50, the type) is the same.
            // PRIVATE houses (a request): NO spawners above ground — the cellar with its mob pile is their place for that.
            if (!priv && (*storyCounter % 15 == 0 || (obj.code == 44000000 && *storyCounter % 15 == 7)))
            {
                quint32 sh = 2166136261u;
                auto smix = [&sh](long long v)
                { for (int b = 0; b < 8; ++b) { sh ^= quint32((v >> (b * 8)) & 0xFF) + quint32(b * 31 + 7); sh *= 16777619u; } };
                smix(minx); smix(miny); smix(minz); smix(sx); smix(sz); smix(nStories); smix(*storyCounter);
                auto srnd = [&sh](int mod) -> int
                { sh ^= sh << 13; sh ^= sh >> 17; sh ^= sh << 5; return mod > 0 ? int(sh % quint32(mod)) : 0; };
                if (srnd(2) == 0)                               // 50/50 — we place it
                {
                    const int lyP = (lowF && k == 0 ? lyLow + 1 : lyLow + 2) + k * storyP;  // the cell on the floor
                    if (lyP <= lyHigh - 3)
                    {
                        const int tp = srnd(100);               // the type: 70/15/15
                        const int type = tp < 70 ? 0 : (tp < 85 ? 1 : 2);  // kSpawnerTypes
                        for (int tryN = 0; tryN < 60; ++tryN)
                        {
                            const int lx = 3 + srnd(sx - 6), lz = 3 + srnd(sz - 6);
                            const int c = lz * sxy + lyP * sx + lx;
                            if (grid[c] != char(3)
                                && !(lowF && lyP == lyLow + 1 && grid[c] == char(4))) continue;  // the cavity (the floor of a private house — 4)
                            {   // the room is no smaller than 7×7: around the spawner (the center) — a 7×7 square of free floor cells
                                bool roomOk = (lx >= 3 && lx + 3 < sx && lz >= 3 && lz + 3 < sz);
                                for (int dz = -3; dz <= 3 && roomOk; ++dz)
                                    for (int dx = -3; dx <= 3; ++dx)
                                    {
                                        const char g2 = grid[(lz + dz) * sxy + lyP * sx + lx + dx];
                                        if (g2 != char(3) && !(lowF && lyP == lyLow + 1 && g2 == char(4))) { roomOk = false; break; }
                                    }
                                if (!roomOk) continue;
                            }
                            const char below = grid[c - sx];
                            if (below != char(1) && below != char(3) && below != char(4)) continue;
                            const int bx = minx + lx, by = miny + lyP, bz = minz + lz;
                            if (store.get(bx, by - sink, bz) != IdAir) continue;
                            addVoxel(bx, by, bz, IdSpawner);     // one block for all types —
                            if (spawners)                        //  the monster is in the block_entity
                                spawners->append(SpawnerSite{ bx, by - sink, bz, type });
                            ++added;
                            break;
                        }
                    }
                }
            }
            if (*storyCounter % 5 != 0) continue;                   // every 5th floor of the city
            const int lyC = (lowF && k == 0 ? lyLow + 1 : lyLow + 2) + k * storyP;  // the cell on the floor of this floor
            if (lyC > lyHigh - 3) continue;                         // there is no full air above — the floor is simply counted
            const bool wantDouble = rnd(2) == 0;                    // 50% — a double one (a user request)
            for (int tryN = 0; tryN < 24; ++tryN)
            {
                const int lx = 3 + rnd(sx - 6);                     // not by the wall: 2 wall layers + a passage
                const int lz = 3 + rnd(sz - 6);
                const int c = lz * sxy + lyC * sx + lx;
                if (grid[c] != char(3)
                    && !(lowF && lyC == lyLow + 1 && grid[c] == char(4))) continue;  // the cavity of the house
                                                                      // (in the private sector the floor is the removed lining "4")
                const char below = grid[c - sx];                    // the chest stands on the floor/slab
                if (below != char(1) && below != char(3) && below != char(4)) continue;
                const int bx = minx + lx, by = miny + lyC, bz = minz + lz;
                if (store.get(bx, by - sink, bz) != IdAir) continue;
                const bool shedChest = (obj.code == 44000000);       // sheds: 35% of chests are expensive (and then mobs are nearby), for houses 15%
                bool dbl = false;
                if (wantDouble && lx + 1 <= sx - 4)                 // the right half — a block to the east
                {
                    const char below2 = grid[c + 1 - sx];
                    if ((grid[c + 1] == char(3)
                         || (lowF && lyC == lyLow + 1 && grid[c + 1] == char(4)))
                        && (below2 == char(1) || below2 == char(3) || below2 == char(4))
                        && store.get(bx + 1, by - sink, bz) == IdAir)
                    {
                        addVoxel(bx, by, bz, IdChestL);
                        addVoxel(bx + 1, by, bz, IdChestR);         // each block with its own table — in a double one 2 locs
                        chests->append(ChestSite{ bx, by - sink, bz, pickChestLoot(quint32(rnd(1 << 24)), shedChest ? 45 : 55, shedChest ? 20 : 30) });
                        chests->append(ChestSite{ bx + 1, by - sink, bz, pickChestLoot(quint32(rnd(1 << 24)), shedChest ? 45 : 55, shedChest ? 20 : 30) });
                        added += 2;
                        dbl = true;
                    }
                }
                if (!dbl)
                {
                    addVoxel(bx, by, bz, IdChest);
                    chests->append(ChestSite{ bx, by - sink, bz, pickChestLoot(quint32(rnd(1 << 24)), shedChest ? 45 : 55, shedChest ? 20 : 30) });
                    ++added;
                }
                if (shedChest && npcs && chests->last().tbl >= kLootT1)
                {   // an expensive chest in a shed is guarded by 2–3 mobs of one kind in neighboring cells
                    quint32 gh = quint32(bx) * 374761393u ^ quint32(bz) * 668265263u ^ quint32(by) * 2246822519u ^ 0x6A4Du;
                    gh ^= gh >> 15; gh *= 2246822519u; gh ^= gh >> 13;
                    const int want = 2 + int(gh % 2u), mobKind = int((gh >> 8) % 4u);   // 4 kinds equally
                    int got = 0;
                    for (int rr = 1; rr <= 2 && got < want; ++rr)
                        for (int q = 0; q < 16 && got < want; ++q)
                        {
                            const int ang = (q + int(gh >> 12)) % 16;
                            static const int gx[16] = { 1, 1, 0, -1, -1, -1, 0, 1, 2, 2, 0, -2, -2, -2, 0, 2 };
                            static const int gz[16] = { 0, 1, 1, 1, 0, -1, -1, -1, 0, 1, 2, 1, 0, -1, -2, -1 };
                            const int mx = bx + (rr == 1 ? gx[ang % 8] : gx[8 + ang % 8]);
                            const int mz = bz + (rr == 1 ? gz[ang % 8] : gz[8 + ang % 8]);
                            const int lx2 = mx - minx, lz2 = mz - minz;
                            if (lx2 < 1 || lx2 >= sx - 1 || lz2 < 1 || lz2 >= sz - 1) continue;
                            if (grid[lz2 * sxy + lyC * sx + lx2] != char(3)) continue;
                            if (store.get(mx, by - sink, mz) != IdAir || store.get(mx, by - sink + 1, mz) != IdAir) continue;
                            if (store.get(mx, by - sink - 1, mz) == IdAir) continue;
                            NpcSite m;
                            m.kind = 4; m.variant = mobKind; m.x = mx; m.z = mz; m.y = by - sink;
                            m.yaw = int((gh >> (3 + got)) % 4u) * 90;
                            npcs->append(m);
                            ++got;
                        }
                }
                break;
            }
        }
    }
}

void ObjectVoxelizer::buildGableRoof()
{
    // The gable roof of the private sector (a user request) for bldpol with the codes
    // 44400002 / 44400100 / 44400110: two slopes of oak half blocks (oak_slab, alternating
    // type=bottom/top: a rise of 0.5 block per column — "first a half block in the lower position,
    // then in the upper", the roof is half as tall as a stair one), the ridge along the long side;
    // the gable triangles are filled with the building wall block. The cap cells stay in the GRID
    // (supAt reads them), but the store of the cap/roof lining in the interior of a private house
    // is not written — there is no flat roof.
    // The extent is the REAL walls (the surface cells restriction), not the grid: the grid is wider by a 1-cell
    // frame, the cornice with stairs wrote blocks into that frame outside the facade, their colMin raised the ground
    // fill to the cornice height — a "stone armor" grew around the house (the user's remark).
    if (obj.kind == McWorldExporter::McObject::Building && obj.closed
        && (obj.code == 44400002 || obj.code == 44400100 || obj.code == 44400110)
        && lyHigh >= 0)
    {
        if (fx1 - fx0 >= 4 && fz1 - fz0 >= 4)                   // the wall patch and the a/b axes are computed above,
        {                                                       // before the surface pass (the roof needs them)
        const int W = a1 - a0 + 1;                              // the width across the slopes
        const int half = qMin(W / 2, 7);                        // slope columns per side (the ceiling — against y>319)
        // A half-block slope (a user request: "stair roofs are TOO tall — build
        // from half blocks: first the lower, then the upper"): the column jj from the cornice carries
        // an oak_slab at the level lyHigh+1+jj/2 (the type alternates bottom/top by parity) — the surface
        // rises by 0.5 block per column instead of a whole one, the ridge is about half as tall.
        // jj is clamped to half-1: for wide houses a flat run goes with the same slab as
        // the last step (a joint without a ledge), a separate "ridge of stairs" is not needed.
        auto skatCell = [&](int d)                              // d — the column across the slope
        {
            const int jj = qMin(qMin(d - a0, a1 - d), half - 1);
            return qMakePair(lyHigh + 1 + (jj >> 1), quint16(jj & 1) ? IdSlabTop : IdSlabBottom);
        };
        // The "armor" — L-shaped houses cover the polygon notch with the bbox roof: the slabs/gables
        // written OVER EMPTINESS raised the ground fill to the cornice with their colMin (pillars
        // of stone and earth at the walls — the user's remark). The roof attaches only to those columns
        // where under it there is really a roof/wall of the grid (the capital draws char 1 over the whole
        // polygon patch — the polygon notch stays zero).
        auto supAt = [&](int lx, int lz)
        {
            for (int ly = lyHigh; ly >= lyHigh - 1 && ly >= 0; --ly)
            {
                const char g = grid[(lz * sy + ly) * sx + lx];
                if (g == char(1) || g == char(4)) return true;
            }
            return false;
        };
        for (int d = a0; d <= a1; ++d)                          // the slabs from the cornices to the ridge
        {
            const auto sc = skatCell(d);
            for (int t = b0; t <= b1; ++t)
            {
                if (ridgeZ) { if (supAt(d, t)) addVoxel(minx + d, miny + sc.first, minz + t, sc.second); }
                else        { if (supAt(t, d)) addVoxel(minx + t, miny + sc.first, minz + d, sc.second); }
            }
        }
        // The gables: triangles at both ends — the same block as the walls.
        for (int end = 0; end < 2; ++end)
        {
            const int tEnd = end ? b1 : b0;
            for (int d = a0; d <= a1; ++d)
            {
                if (!(ridgeZ ? supAt(d, tEnd) : supAt(tEnd, d))) continue;  // above the L notch — do not pour
                const int top = skatCell(d).first;          // the bottom face of the slope — the slab above d
                for (int ly = lyHigh + 1; ly < top; ++ly)
                {
                    const quint16 gId = privMode ? quint16(IdOakPlanks) : id;   // the gables of private houses — planks
                    if (ridgeZ) addVoxel(minx + d,  miny + ly, minz + tEnd, gId);
                    else        addVoxel(minx + tEnd, miny + ly, minz + d, gId);
                }
            }
        }
        // The cornice "soffit": in each column of the wall ring the air gap from the top of the
        // ceiling (the capital) to the first roof plank above it
        // is sewn up with oak planks. Where the slope slab lies on the capital — the soffit is zero (the first
        // block above the ceiling is already dense). Columns without a roof above them (the notch of an L polygon) — we
        // do not touch. We do not write the lattice above lyHigh: it is not there, the light below the cornice will not climb there anyway.
        for (int lz = fz0; lz <= fz1; ++lz)
            for (int lx = fx0; lx <= fx1; ++lx)
            {
                if (!(lx == fx0 || lx == fx1 || lz == fz0 || lz == fz1)) continue;  // only the perimeter
                int top = -1;                                       // the first roof plank above the column
                for (int ly = lyHigh + 1; ly <= lyHigh + half + 1; ++ly)
                    if (store.get(minx + lx, miny + ly - sink, minz + lz) != IdAir) { top = ly; break; }
                if (top < 0) continue;
                int ceilTop = -1;                                   // the ceiling of the column (the capital/wall)
                for (int ly = lyHigh; ly >= lyHigh - 2 && ceilTop < 0; --ly)
                    if (store.get(minx + lx, miny + ly - sink, minz + lz) != IdAir) ceilTop = ly;
                if (ceilTop < 0) continue;
                for (int ly = ceilTop + 1; ly < top; ++ly)
                    addVoxel(minx + lx, miny + ly, minz + lz, IdOakPlanks);
            }
        // "Sewing up the real perimeter" (a user request: "the second floor of the private sector
        // without gables and a soffit — after building the roof walk along the perimeter of the building
        // and finish with planks from the top block of the real wall to the roof of stairs"):
        // the soffit above knows only the bbox frame, while the real walls may end INSIDE
        // the frame (the notch of an L polygon — the end of a wing stands in the middle of the span and does not reach
        // the slope: a through gap of a couple of floors). A wall column is one where at lyHigh
        // the store has a block (we remove the interior cap of a private house — the cavity above
        // the slab would not be filled that way). Above a wall column we look for the first roof plank
        // (a slope slab/a gable) and sew up the air gap under it with oak planks.
        // Where the slab lies right on the wall — a no-op; above a roof notch — we do not touch.
        for (int lz = fz0; lz <= fz1; ++lz)
            for (int lx = fx0; lx <= fx1; ++lx)
            {
                if (store.get(minx + lx, miny + lyHigh - sink, minz + lz) == IdAir) continue;
                int top = -1;
                for (int ly = lyHigh + 1; ly <= lyHigh + half + 2; ++ly)
                    if (store.get(minx + lx, miny + ly - sink, minz + lz) != IdAir) { top = ly; break; }
                if (top < 0) continue;                                  // above the notch — do not pour
                for (int ly = lyHigh + 1; ly < top; ++ly)
                    addVoxel(minx + lx, miny + ly, minz + lz, IdOakPlanks);
            }
        }
    }
}

void ObjectVoxelizer::buildLight()
{
    // The light inside (a user request): torches on the inner walls of floors — not often (a block
    // ~1/10 by the cell hash, no closer than 6 blocks to already hanging lights) and carrying lamps under
    // the floor (lantern hanging=true) with the same distance, so that the light comes in "spots", not a garland.
    // A torch occupies a cavity cell next to the holder wall; facing is AWAY from the wall (the vanilla
    // semantics). A hole behind glass (5) does not count as a holder — a torch does not fit in a window.
    if (floors && obj.closed && storyP > 0)
    {
        auto cellHash = [](int x, int y, int z) -> quint32      // a deterministic "noise" of a cell
        {
            quint32 h = quint32(x) * 374761393u + quint32(y) * 668265263u + quint32(z) * 2246822519u;
            h = (h ^ (h >> 15)) * 2246822519u;
            return h ^ (h >> 13);
        };
        struct LightPt { int x, y, z; };
        // where a light already hangs — for the distance; an 8×8 grid by (x,z): going over ALL lights for
        // every candidate cell was quadratic (large buildings — hundreds of millions of checks)
        QHash<ColKey, QVector<LightPt>> lights;
        auto lightKey = [](int x, int z) { return pairKey(x >> 3, z >> 3); };
        auto hasSlabAt = [&](int ly)                          // at the level ly a floor slab really lies
        {
            if (ly > (priv ? lyHigh - 1 : lyHigh - 2)) return false;  // private: the last slab = lyHigh-1
            for (int k = 1; k < nStories; ++k)
                if (ly == lyLow + 1 + k * storyP) return true;
            return false;
        };
        auto tooClose = [&lights, hasSlabAt](int bx, int by, int bz, int minD)
        {   // The lights of floors stand evenly in "spots" within their tier, but the distance is computed
            // in THREE-DIMENSIONAL distance and taking the floors into account: if between the heights of two points
            // a slab lies somewhere — these are different floors, and they do not "eat" each other (otherwise
            // the light of the first floor would burn out all the light of the second, and vice versa — which made
            // the stair openings the only sources in multi-storey buildings).
            for (int gx = (bx - minD + 1) >> 3; gx <= (bx + minD - 1) >> 3; ++gx)
            for (int gz = (bz - minD + 1) >> 3; gz <= (bz + minD - 1) >> 3; ++gz)
            {
                const auto bit = lights.constFind(pairKey(gx, gz));
                if (bit == lights.constEnd()) continue;
                for (const LightPt& p : bit.value())
                {
                if (p.x == bx && p.z == bz && p.y == by) continue;
                const int dy = std::abs(p.y - by);
                const int d2 = std::max(std::abs(p.x - bx), std::abs(p.z - bz));
                if (d2 >= minD) continue;
                bool split = false;                           // a slab strictly BETWEEN them?
                for (int ly = std::min(p.y, by) + 1; !split && ly < std::max(p.y, by); ++ly)
                    if (hasSlabAt(ly)) split = true;
                if (!split && dy < minD) return true;         // one tier — we keep the distance volumetrically
                }
            }
            return false;
        };
        for (int k = 0; k < nStories; ++k)
        {
            const int lyF = (lowF && k == 0 ? lyLow + 1 : lyLow + 2) + k * storyP;  // the floor level of the story
            // — torches: two rows per floor (a user request — "evenly over all floors"):
            // 2 blocks above the floor and 2 blocks above the slab of the next tier (the top row of an ordinary
            // floor = the bottom row of the next one, so there are no duplicates). The cells of the cavity with a wall neighbor.
            const int lyRows[2] = { lyF + 2, lyF + storyP + 2 };
            for (int row = 0; row < 2; ++row)
            {
                const int lyT = lyRows[row];
                if (lyT <= lyHigh - 2)
                {
                for (int lz = 1; lz < sz - 1; ++lz)
                    for (int lx = 1; lx < sx - 1; ++lx)
                    {
                        const int c = lz * sxy + lyT * sx + lx;
                        if (grid[c] != char(3)) continue;
                        quint16 tid = 0;                      // we look for the holder in a fixed order
                        // The holder is a cell where a block REALLY stands (the store): the grid knows the lining,
                        // but windows/doors/furniture have already cut holes in it — a torch must not be hung in such a hole,
                        // the server will smear an unsupported wall_torch when loading the chunk.
                        // The grid axes coincide with the world axes (addVoxel: minx+lx, miny+ly, minz+lz).
                        auto holder = [&](int off)
                        { return grid[c + off] == char(1) || grid[c + off] == char(4); };
                        const int bxc = minx + lx, by = miny + lyT, bzc = minz + lz;
                        if (lx + 1 < sx && holder(1) && store.get(bxc + 1, by - sink, bzc) != IdAir) tid = IdWallTorchW;
                        else if (lx > 0 && holder(-1) && store.get(bxc - 1, by - sink, bzc) != IdAir) tid = IdWallTorchE;
                        else if (lz + 1 < sz && holder(sxy) && store.get(bxc, by - sink, bzc + 1) != IdAir) tid = IdWallTorchN;
                        else if (lz > 0 && holder(-sxy) && store.get(bxc, by - sink, bzc - 1) != IdAir) tid = IdWallTorchS;
                        if (!tid) continue;
                        // We do not hang a torch above a step: "there must be no ceiling above a staircase" —
                        // a torch in the cell above the head of a flight is the same violator (and the
                        // player knocks it down by touching it with their head). The steps are already in the store by this moment.
                        {
                            const quint16 underId = store.get(bxc, by - sink - 1, bzc);
                            if (underId >= IdStoneStairsN && underId <= IdStoneStairsW) continue;
                        }
                        const int bx = bxc, bz = bzc;
                        if (cellHash(bx, by, bz) % 10u != 0u) continue;   // "not too often"
                        if (tooClose(bx, by, bz, 6)) continue;
                        if (store.get(bx, by - sink, bz) != IdAir) continue;
                        addVoxel(bx, by, bz, tid);
                        lights[lightKey(bx, bz)].append(LightPt{ bx, by, bz });
                        ++added;
                    }
                }
            }
            // — lamps: the cell right under the floor. For ordinary floors this is the level above the slab, for
            // the LAST floor — the cell under the roof lining (otherwise in one-storey buildings the lamps have nothing to hang
            // from). The holder is a written slab (grid 3 at the slab level) or the lining (1/4).
            const int lyL = (k + 1 < nStories) ? lyLow + (k + 1) * storyP : lyHigh - 2;
                                                                // the row under the slab k+1 (for the former scheme
                                                                // = lyF + storyP - 2, the same formula)
            if (lyL > lyF + 2 && lyL <= lyHigh - 2)
            {
                for (int lz = 2; lz < sz - 2; ++lz)
                    for (int lx = 2; lx < sx - 2; ++lx)
                    {
                        const int c = lz * sxy + lyL * sx + lx;
                        if (grid[c] != char(3)) continue;
                        const char up = grid[c + sx];
                        if (up != char(1) && up != char(4) && up != char(3)) continue;
                        if (up == char(3) && !hasSlabAt(lyL + 1)) continue;
                        // The holder must be a REAL block in the store: the grid "remembers" the lining/slab,
                        // but the slab pass skips cells with the mark "5" (a stair opening!), and windows
                        // cut the lining — a hanging lantern without a block above would be dropped by the server onto the floor
                        // when loading the chunk.
                        {
                            const int hx = minx + lx, hz = minz + lz;
                            if (store.get(hx, miny + lyL + 1 - sink, hz) == IdAir) continue;
                        }
                        const int bx = minx + lx, by = miny + lyL, bz = minz + lz;
                        if (cellHash(bx ^ 0x5EED, by, bz ^ 0x2B1D) % 10u != 0u) continue;   // lamps are slightly rarer than torches
                        if (tooClose(bx, by, bz, 4)) continue;   // "not close to torches", but in moderation: in
                                                                  // small houses a distance of 6 would eat the whole ceiling
                        if (store.get(bx, by - sink, bz) != IdAir) continue;
                        addVoxel(bx, by, bz, IdLanternHung);
                        lights[lightKey(bx, bz)].append(LightPt{ bx, by, bz });
                        ++added;
                    }
            }
        }
    }
}

void ObjectVoxelizer::buildInteriors()
{
    // "Interiors" v2 (a request: "houses stopped being boxes"; edits after the first test:
    // the table top is a slab ON TOP of the fence (type=top: the bottom slab sank into the post —
    // a "half block in the center"); the chairs are turned: facing = AWAY from the table, the bottom to the table; no more
    // than TWO tables per room; the spreading is OVER ALL rooms (a room = a 4-connected component
    // of free cells, openings are the boundaries): in each residential room 1..4 items, and not
    // a "heap" by the seniority of the floor traversal; paintings on the walls (motif 1×1 — they do not overlap
    // windows/openings); malls (panor, a request: "in shops — shelves, racks, several little tables,
    // mannequins, sometimes with armor") — their own line: shelf rows along the walls and as "islands",
    // a counter, tables and mannequins = armor_stand entities (armor — only on the armor stand).
    // Furniture goes ONLY into the air above the walking row; chests/stairs/light are already in
    // the store — not touched; under a candidate there must be no flight head/ladder (the gate
    // "stone steps").
    if (floors && obj.closed && storyP > 0
        && (priv || panor || office || obj.code == 44340000 || (!obj.lift && !bigWin && !noWin)))
    {
        const int nFin = qMax(1, nStories);           // the private sector too: brick houses furnish the 2nd floor
                                                      //  as well (the MANSARD, a user request: beds/bookshelves/carpets upstairs);
                                                      //  log houses have nStories = 1, so they stay one-storey
        struct FinPt { int x, y, z; };
        QVector<FinPt> finPts;                                  // what is already furnished (distances)
        QSet<ColKey> occ;                                       // the furniture cells (repeats)
        int npcTotal = 0;                                       // how many residents have already been settled in the building
        int petTotal = 0;                                       // pets in the building: cats in apartment blocks, dogs (wolves) in private houses
        bool lecternDone = false;                               // a church: a lectern with a book — one per building
        int lairCapsLeft = -1;                                  // how many standard-bearers are still to be assigned (computed from the building area on first use)
        auto doorNear = [&](int bx, int by, int bz)
        {   // at least one cell of a door opening nearby — we do not place
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (doorCells.contains(dkey(bx + dx, by + dy, bz + dz))) return true;
            return false;
        };
        const int dvec[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };  // 0=N,1=E,2=S,3=W
        const quint16 stairId[4] = { IdStairsN, IdStairsE, IdStairsS, IdStairsW };
        auto finHash = [](int bx, int bz, int by, quint32 salt) -> quint32
        {
            quint32 h = quint32(bx) * 374761393u ^ quint32(bz) * 668265263u
                      ^ quint32(by) * 2246822519u ^ salt;
            h ^= h >> 15; h *= 2246822519u; return h ^ (h >> 13);
        };
        auto farOtherFin = [&](int bx, int by, int bz, int minD)
        {   // is it free to stand aside from all the furniture already placed on the tier
            for (const FinPt& p : finPts)
                if (std::abs(p.y - by) < 2 &&
                    qMax(std::abs(p.x - bx), std::abs(p.z - bz)) < minD) return false;
            return true;
        };
        for (int k = 0; k < nFin; ++k)
        {
            const int lyF = (lowF && k == 0 ? lyLow + 1 : lyLow + 2) + k * storyP;
            // The MANSARD of a brick private house (a user request): such OSM houses are often "~1.5 storeys" —
            // the gable stands only 1-2 rows above the eaves, and the strict margin cut the 2nd floor off
            // completely (0 furnished mansards). For the upper private floors the margin is relaxed to 1 row:
            // low headroom is not a problem — cellFree rejects cells under the roof lining and in the walls, so
            // only what really fits (a bed, a bookshelf, a carpet) is placed up there.
            if (lyF > lyHigh - (priv && k > 0 ? 1 : 2)) continue;
            // IN FRONT OF THE STAIRS AND UNDER THEM — ALWAYS FREE (a requirement): furniture/decor is not placed within a radius of 1 cell of any step or ladder
            // (in height — from the slab of this floor to the top of the flight), i.e. neither approaches, nor landings, nor the space under the flight are cluttered.
            static const bool noStairFree = qEnvironmentVariableIsSet("MCX_NO_STAIRFREE");   // debugging: switch the rule off (to check the gate)
            QVector<char> stNear(sx * sz, -1);
            auto nearStairs = [&](int lx, int lz) -> bool
            {
                char& cv = stNear[lz * sx + lx];
                if (cv >= 0) return cv != 0;
                bool hit = false;
                for (int dz = -1; dz <= 1 && !hit; ++dz)
                    for (int dx = -1; dx <= 1 && !hit; ++dx)
                        if (stairXZ.contains(pairKey(lx + dx, lz + dz))) hit = true;
                cv = hit ? 1 : 0;
                return hit;
            };
            auto cellFree = [&](int lx, int lz, int row)
            {   // the cell (lx, lyF+row, lz) — a cavity, air, outside openings and furniture.
                // The walking row of the private sector is marked "4" in the grid (the floor lining row is removed —
                // the same nuance as in the partition/stair masks above), but it is empty in the store.
                const int ly = lyF + row;
                if (lx < 1 || lx >= sx - 1 || lz < 1 || lz >= sz - 1 || ly > lyHigh) return false;
                const char gc = grid[lz * sxy + ly * sx + lx];
                if (gc != char(3) && !(lowF && row == 0 && gc == char(4))) return false;
                const int bx = minx + lx, bz = minz + lz, byc = miny + ly;
                if (store.get(bx, byc - sink, bz) != IdAir) return false;
                if (row == 0)
                {   // under a candidate a FLOOR is MANDATORY (a complaint "interior elements hang
                    // in the air in the stair flights": a hole of the shaft in the slab = there is no air under
                    // the feet, and furniture/a machine on NoGravity hung in the opening). Plus
                    // not the flight head/the ladder hatch: a step of the upper floor would stand
                    // as a "ceiling" above a step of the lower one (the "stone steps" gate).
                    const quint16 und = store.get(bx, byc - 1 - sink, bz);
                    if (und == IdAir) return false;
                    if ((und >= IdStairsN && und <= IdStairsW)
                        || (und >= IdLadderN && und <= IdLadderW)
                        || (und >= IdStoneStairsN && und <= IdStoneStairsW)) return false;
                }
                if (occ.contains(dkey(bx, byc, bz))) return false;
                if (!noStairFree && nearStairs(lx, lz)) return false;
                return !doorNear(bx, byc, bz);
            };
            auto put = [&](int lx, int lz, int row, quint16 vid, int* placed)
            {
                const int bx = minx + lx, bz = minz + lz, byc = miny + lyF + row;
                addVoxel(bx, byc, bz, vid);
                if (vid == IdBarrel && chests)                     // a barrel is a container with loot (the BE barrel)
                    chests->append(ChestSite{ bx, byc - sink,
                                              bz, int(lineHash(pairKey(bx, bz) ^ (qint64(byc) << 32) ^ 0xBA77E1ull) % quint32(kBarrelLootCount)), true });
                if (priv && chests && (vid == IdFurnace || (vid >= IdFurnaceBase && vid < IdFurnaceMax)))
                {   // a private-house stove is a WORKING one (a user request): the BE carries fuel (and often food),
                    // the contents are a hash of the cell — stable between the runs of the same seed
                    ChestSite st;
                    st.x = bx; st.y = byc - sink; st.z = bz;
                    st.stove = 1 + int(finHash(bx, bz, byc, 0x570C9u) % 256u);
                    chests->append(st);
                }
                occ.insert(dkey(bx, byc, bz));
                finPts.append(FinPt{ bx, byc, bz });
                ++*placed; ++added;
            };
            auto wallAt = [&](int ax, int az, int d)
            { return store.get(minx + ax + dvec[d][0], miny + lyF - sink,
                               minz + az + dvec[d][1]) != IdAir; };
            auto putMannequin = [&](int bx, int bz, int byc) -> bool
            {   // A MANNEQUIN: an armor_stand entity (no block), sometimes in armor. Pos is the cell
                // center; yaw is a multiple of 90; the armor material is cheap (leather/chainmail/iron...).
                if (!mannequins) return false;
                const quint32 ah = finHash(bx, bz, byc, 0xAACC0u);
                MannequinSite ms;
                ms.x = bx; ms.y = byc - sink; ms.z = bz;
                ms.yaw = int((ah >> 12) % 4u) * 90;
                ms.armor = ah % 100u < 75u ? int((ah >> 6) & 0xFu) : 0;   // 75% "a showcase in armor"
                if (ms.armor == 0 && ah % 100u < 75u) ms.armor = 0xC;     // chestplate+helmet (bits 2,3)
                const quint32 am = finHash(bx, bz, byc, 0x93u);
                // The rarity of mannequin armor (a request): leather most often (56%), chainmail a bit rarer (22%), gold even
                // rarer (16%), iron — 4%, diamond — the minimum (2%). The kArmorMats indices: 0 leather, 1 chainmail, 2 iron,
                // 3 gold, 4 diamond.
                const quint32 ar = am % 100u;
                ms.mat = ar < 56u ? 0 : ar < 78u ? 1 : ar < 94u ? 3 : ar < 98u ? 2 : 4;
                mannequins->append(ms);
                occ.insert(dkey(bx, byc, bz));                      // the cell is "occupied"
                finPts.append(FinPt{ bx, byc, bz });
                return true;
            };
            // The ground floor of a METRO STATION building (44330000) is the entrance vestibule (a request:
            // "the whole 1st floor of a metro entrance building must look like a vestibule — a bed next to
            // the entrance is strange"): checker tiles over the whole floor, a ticket counter with a bell,
            // benches, vending machines, waste bins, potted plants, lamp posts, info panels and posters. The
            // corridor in front of the entrance doors stays free of furniture (a request: "the most important
            // thing is that it does not get in the way of the entrance"). The shaft itself walks out through
            // its own light portal frame (digStationHall), no ticket rows (a round-4 request: "just a frame
            // and an entrance").
            if (obj.code == 44330000 && k == 0)
            {
                auto blkName = [](const char* name) -> quint16
                { return quint16(IdPaletteStart + paletteIndexOf(name)); };
                const quint16 vTileA = blkName("minecraft:polished_andesite"),
                            vTileB = blkName("minecraft:polished_diorite"),
                            vDsl   = blkName("minecraft:polished_deepslate"),
                            vBlk   = blkName("minecraft:polished_blackstone"),
                            vChis  = blkName("minecraft:chiseled_stone_bricks");
                int placedV = 0;
                // tiles: the whole walking floor of the storey is re-laid in the polished checker (as the
                // underground hall itself, so the stair door walks out onto the same pattern)
                int cxSum = 0, czSum = 0, cN = 0;
                for (int lz = 1; lz < sz - 1; ++lz)
                    for (int lx = 1; lx < sx - 1; ++lx)
                    {
                        const char gc = grid[lz * sxy + (lyF)*sx + lx];
                        if (gc != char(3) && gc != char(4)) continue;
                        const int bx = minx + lx, bz = minz + lz;
                        if (store.get(bx, miny + lyF - 1 - sink, bz) == IdAir) continue;   // no slab — no tile
                        addVoxel(bx, miny + lyF - 1, bz, ((lx >> 1) + (lz >> 1)) & 1 ? vTileA : vTileB);
                        cxSum += lx; czSum += lz; ++cN;
                    }
                if (cN == 0) continue;
                const double cxA = double(cxSum) / cN, czA = double(czSum) / cN;
                // the corridor in front of the ENTRANCE doors stays free (a request: "the most important
                // thing is that the furniture does not get in the way of the entrance"): two cells deep
                QVector<char> doorNear(sx * sz, 0);
                {
                    QVector<int> fronts;
                    for (int lz = 1; lz < sz - 1; ++lz)
                        for (int lx = 1; lx < sx - 1; ++lx)
                            for (int d = 0; d < 4; ++d)
                            {
                                const int wx = lx + dvec[d][0], wz = lz + dvec[d][1];
                                bool door = false;
                                for (int q = -1; q <= 2 && !door; ++q)
                                {
                                    const quint16 b = store.get(minx + wx, miny + lyF + q - sink, minz + wz);
                                    if (b >= IdDoorBase && b < IdDoorMax) door = true;
                                }
                                if (door) { fronts.append(lz * sx + lx); break; }
                            }
                    for (int f : fronts)
                    {
                        const int fx = f % sx, fz = f / sx;
                        for (int dz = -2; dz <= 2; ++dz)
                            for (int dx = -2; dx <= 2; ++dx)
                            {
                                if (std::abs(dx) + std::abs(dz) > 2) continue;
                                const int nx = fx + dx, nz = fz + dz;
                                if (nx < 1 || nx >= sx - 1 || nz < 1 || nz >= sz - 1) continue;
                                doorNear[nz * sx + nx] = 1;
                            }
                    }
                }
                // the ticket counter: the wall cell nearest to the floor centre, a two-wide polished counter
                // with a stair on top facing into the hall
                int cntLx = -1, cntLz = -1, cntD = 0;
                double cntBest = 1e18;
                for (int lz = 1; lz < sz - 1; ++lz)
                    for (int lx = 1; lx < sx - 1; ++lx)
                    {
                        if (!cellFree(lx, lz, 0) || doorNear[lz * sx + lx]) continue;
                        for (int d = 0; d < 4; ++d)
                        {
                            if (!wallAt(lx, lz, d)) continue;
                            const double dx = lx + dvec[d][0] * 0.5 - cxA, dz = lz + dvec[d][1] * 0.5 - czA;
                            const double dist = dx * dx + dz * dz;
                            if (dist >= cntBest) continue;
                            cntBest = dist; cntLx = lx; cntLz = lz; cntD = d;
                        }
                    }
                if (cntLx >= 0)
                {
                    const int ox = -dvec[cntD][0], oz = -dvec[cntD][1];        // into the hall
                    const int s0 = cntLx + ox, s1 = cntLz + oz;
                    const bool sideFree = cellFree(s0 + oz, s1 + ox, 0);       // the counter stands sideways
                    put(cntLx, cntLz, 0, vDsl, &placedV);
                    if (cellFree(s0, s1, 0))
                    {
                        put(s0, s1, 0, vDsl, &placedV);
                        const bool outZ = (oz != 0);                             // the counter faces along Z
                        const quint16 cntStair = outZ ? (oz > 0 ? IdStoneStairsS : IdStoneStairsN)
                                                      : (ox > 0 ? IdStoneStairsE : IdStoneStairsW);
                        if (cellFree(s0, s1, 1)) put(s0, s1, 1, cntStair, &placedV);
                        if (sideFree) put(s0 + oz, s1 + ox, 0, vDsl, &placedV);
                    }
                    if (cellFree(cntLx, cntLz, 1))
                        put(cntLx, cntLz, 1, IdBell, &placedV);      // the stationmaster's bell on the counter
                }
                // benches, waste bins and potted plants by the walls, posters above them; the lamp posts,
                // vending machines and info panels (chiseled plaques) as before
                const quint16 kBenchStair[4] = { quint16(IdStairsN), quint16(IdStairsE),
                                                 quint16(IdStairsS), quint16(IdStairsW) };   // the dvec order
                for (int lz = 1; lz < sz - 1; ++lz)
                    for (int lx = 1; lx < sx - 1; ++lx)
                    {
                        if (!cellFree(lx, lz, 0)) continue;
                        const quint32 vh = finHash(minx + lx, minz + lz, miny + lyF, 0x5E57u);
                        bool onWall = false;
                        int wd = 0;
                        for (int d = 0; d < 4; ++d)
                            if (wallAt(lx, lz, d)) { onWall = true; wd = d; break; }
                        if (!onWall) continue;
                        const quint32 v30 = vh % 30u;
                        if (v30 == 0u && farOtherFin(minx + lx, miny + lyF, minz + lz, 3))
                        {   // a lamp post in the corner of the hall
                            put(lx, lz, 0, IdCobbleWall, &placedV);
                            if (cellFree(lx, lz, 1)) put(lx, lz, 1, IdLanternStanding, &placedV);
                        }
                        else if (v30 >= 1u && v30 <= 4u && farOtherFin(minx + lx, miny + lyF, minz + lz, 2))
                        {   // a vending machine against the wall: a dark column with a lit face panel
                            put(lx, lz, 0, vBlk, &placedV);
                            if (cellFree(lx, lz, 1)) put(lx, lz, 1, vChis, &placedV);
                        }
                        else if (v30 >= 5u && v30 <= 7u && !doorNear[lz * sx + lx]
                                 && farOtherFin(minx + lx, miny + lyF, minz + lz, 3))
                        {   // a waiting bench: stone steps with the tall (backrest) side to the wall
                            // (facing = uphill, the same stairs as the benches of the underground hall)
                            const quint16 bStair = kBenchStair[wd];
                            put(lx, lz, 0, bStair, &placedV);
                            for (int s = -1; s <= 1; s += 2)              // a neighbour seat along the wall
                            {
                                const int nx = lx + (wd % 2 ? 0 : s), nz = lz + (wd % 2 ? s : 0);
                                if (nx < 1 || nz < 1 || !cellFree(nx, nz, 0) || doorNear[nz * sx + nx]
                                    || !wallAt(nx, nz, wd)
                                    || !farOtherFin(minx + nx, miny + lyF, minz + nz, 2)) continue;
                                put(nx, nz, 0, bStair, &placedV);
                            }
                        }
                        else if (v30 == 8u && farOtherFin(minx + lx, miny + lyF, minz + lz, 2))
                        {   // a waste bin: a barrel against the wall
                            put(lx, lz, 0, IdBarrel, &placedV);
                        }
                        else if (v30 == 9u && farOtherFin(minx + lx, miny + lyF, minz + lz, 1))
                        {   // a potted plant by the wall
                            put(lx, lz, 0, quint16(IdFlowerPotBase + int(finHash(minx + lx, minz + lz,
                                    miny + lyF, 0xF10Au) % quint32(kFlowersCount))), &placedV);
                        }
                        else if (v30 >= 10u && v30 <= 12u && cellFree(lx, lz, 1))
                        {   // an info panel: a chiseled plaque on the wall at the eye level
                            const int wx = lx + dvec[wd][0], wz = lz + dvec[wd][1];
                            quint16 wb0 = store.get(minx + wx, miny + lyF - sink, minz + wz);
                            quint16 wb1 = store.get(minx + wx, miny + lyF + 1 - sink, minz + wz);
                            if (wb0 != IdAir && wb0 != IdGlass && wb1 != IdAir && wb1 != IdGlass)
                                put(lx, lz, 1, vChis, &placedV);
                        }
                        else if (v30 >= 13u && v30 <= 14u && cellFree(lx, lz, 1))
                        {   // a poster: a blank banner hung flat on a wall (its facing looks out of the wall)
                            const int wx = lx + dvec[wd][0], wz = lz + dvec[wd][1];
                            quint16 wb0 = store.get(minx + wx, miny + lyF - sink, minz + wz);
                            quint16 wb1 = store.get(minx + wx, miny + lyF + 1 - sink, minz + wz);
                            if (wb0 != IdAir && wb0 != IdGlass && wb1 != IdAir && wb1 != IdGlass)
                                put(lx, lz, 1, quint16(IdWallBannerBase + ((wd + 2) & 3)), &placedV);
                        }
                    }
                continue;
            }
            // The free cells of the walking row are the basis of rooms; a room = a 4-connected
            // component (openings, shafts and the door buffer are the natural boundaries).
            QVector<char> open(sx * sz, 0);
            int openN = 0;
            for (int lz = 2; lz < sz - 2; ++lz)
                for (int lx = 2; lx < sx - 2; ++lx)
                    if (cellFree(lx, lz, 0)) { open[lz * sx + lx] = 1; ++openN; }
            if (priv && k == 1 && privMats && openN >= 5) ++privMats[2];  // the mansard of a brick house is REAL (furniture can stand on it)
            QVector<int> roomOf(sx * sz, -1);
            QVector<QVector<int>> rooms;
            {   // DFS labeling (the stack is a QVector; the room order = the order of the first cells)
                QVector<int> stk;
                for (int i0 = 0; i0 < sx * sz; ++i0)
                {
                    if (!open[i0] || roomOf[i0] >= 0) continue;
                    const int rid = rooms.size();
                    rooms.append(QVector<int>());
                    stk.append(i0); roomOf[i0] = rid;
                    while (!stk.isEmpty())
                    {
                        const int v = stk.takeLast();
                        rooms[rid].append(v);
                        const int vx = v % sx, vz = v / sx;
                        for (int q = 0; q < 4; ++q)
                        {
                            const int nx = vx + (q == 0 ? 1 : q == 1 ? -1 : 0);
                            const int nz = vz + (q == 2 ? 1 : q == 3 ? -1 : 0);
                            if (nx < 0 || nx >= sx || nz < 0 || nz >= sz) continue;
                            const int w = nz * sx + nx;
                            if (!open[w] || roomOf[w] >= 0) continue;
                            roomOf[w] = rid; stk.append(w);
                        }
                    }
                }
            }
            int floorBlocks = 0;
            const int capFloor = panor ? qMax(30, openN / 3)       // a mall — we are not afraid of being "flooded":
                                       : qMax(10, openN / 6);      // a hall must look like a hall
            // A SCHOOL (44312000): the rooms are classrooms (80%: a blackboard at the north wall, a teacher's desk, desks in rows with chairs, a passage after
            // every 3 desks) or libraries (20%: rows of shelves in 2 tiers with passages and reading tables). A CHURCH (44319000): in
            // spacious rooms by the wall an obsidian altar 3×1 with candles.
            const bool schoolF = (obj.code == 44312000 || obj.code == 44315000), churchF = (obj.code == 44319000);   // a scientific building is furnished like a school (laboratory classrooms, libraries)
            // A SCHOOL: the plan of all rooms is computed in advance (a blackboard on a shared wall makes the neighboring room a classroom, facing the same blackboard).
            struct SBox { int minX = 0, maxX = -1, minZ = 0, maxZ = -1; };
            struct SchoolPlan { int kind = -1; int f = 0; bool owner = false; QVector<QPoint> board; };   // kind: 0 ordinary, 1 classroom, 2 library
            QVector<SBox> sbox(rooms.size());
            QVector<SchoolPlan> splan(rooms.size());
            auto inRoomId = [&](int x, int z, int rid) { return x >= 0 && x < sx && z >= 0 && z < sz && roomOf[z * sx + x] == rid; };
            auto depthOf = [&](const SBox& b, int f, int lx, int lz) { return f == 0 ? lz - b.minZ : f == 2 ? b.maxZ - lz : f == 1 ? b.maxX - lx : lx - b.minX; };
            auto latOf   = [&](const SBox& b, int f, int lx, int lz) { return f == 0 ? lx - b.minX : f == 2 ? b.maxX - lx : f == 1 ? lz - b.minZ : b.maxZ - lz; };
            auto cellAt  = [&](const SBox& b, int f, int lt, int dp) -> QPoint
            {
                switch (f)
                {
                case 0: return QPoint(b.minX + lt, b.minZ + dp);
                case 2: return QPoint(b.maxX - lt, b.maxZ - dp);
                case 1: return QPoint(b.maxX - dp, b.minZ + lt);
                default: return QPoint(b.minX + dp, b.maxZ - lt);
                }
            };
            // a wall cell is suitable for a blackboard: a solid block at height 0..2 (not glass, not an opening, not a door)
            auto wallBlank = [&](int wx, int wz) -> bool
            {
                for (int row = 0; row <= 2; ++row)
                {
                    const quint16 b = store.get(minx + wx, miny + lyF + row - sink, minz + wz);
                    if (b == IdAir || b == IdGlass || (b >= IdDoorBase && b < IdDoorMax)) return false;
                }
                return true;
            };
            // The best blackboard segment on side f of room rid: a continuous solid section of length >= 4; among the suitable ones we take the nearest to
            // the middle of the wall, the blackboard (up to 6 blocks) is centered on the segment closer to the middle of the wall. Returns the WALL cells.
            auto boardOnSide = [&](int rid, int f, QVector<QPoint>& out) -> bool
            {
                const SBox& b = sbox[rid];
                const int Lmax = (f % 2 == 0) ? (b.maxX - b.minX) : (b.maxZ - b.minZ);
                QVector<char> blank(Lmax + 1, 0);
                for (int lt = 0; lt <= Lmax; ++lt)
                {
                    const QPoint c = cellAt(b, f, lt, 0);
                    if (!inRoomId(c.x(), c.y(), rid)) continue;
                    if (depthOf(b, f, c.x(), c.y()) != 0) continue;
                    if (wallBlank(c.x() + dvec[f][0], c.y() + dvec[f][1])) blank[lt] = 1;
                }
                const double mid = Lmax * 0.5;
                int bestLt0 = -1, bestW = 0;
                double bestD = 1e9;
                for (int lt = 0; lt <= Lmax;)
                {
                    if (!blank[lt]) { ++lt; continue; }
                    int e = lt;
                    while (e + 1 <= Lmax && blank[e + 1]) ++e;
                    const int run = e - lt + 1;
                    if (run >= 4)
                    {
                        const int W = std::min(6, run);
                        // the blackboard position inside the segment, the nearest to the middle of the wall
                        int st = int(std::lround(mid - W * 0.5));
                        st = std::max(lt, std::min(st, e - W + 1));
                        const double d = std::abs(st + W * 0.5 - mid);
                        if (d < bestD) { bestD = d; bestLt0 = st; bestW = W; }
                    }
                    lt = e + 1;
                }
                if (bestLt0 < 0) return false;
                out.clear();
                for (int k = 0; k < bestW; ++k)
                {
                    const QPoint c = cellAt(b, f, bestLt0 + k, 0);
                    out.append(QPoint(c.x() + dvec[f][0], c.y() + dvec[f][1]));
                }
                return true;
            };
            if (schoolF)
            {
                for (int r2 = 0; r2 < rooms.size(); ++r2)
                {
                    SBox& b = sbox[r2];
                    b.minX = sx; b.minZ = sz;
                    for (int idx : rooms[r2])
                    {
                        b.minX = std::min(b.minX, idx % sx); b.maxX = std::max(b.maxX, idx % sx);
                        b.minZ = std::min(b.minZ, idx / sx); b.maxZ = std::max(b.maxZ, idx / sx);
                    }
                }
                for (int r2 = 0; r2 < rooms.size(); ++r2)
                {
                    if (rooms[r2].size() < 24 || splan[r2].kind != -1) continue;       // small rooms are standard; already decided ones are not touched
                    const SBox& b = sbox[r2];
                    const quint32 hs = finHash(minx + b.minX, minz + b.minZ, miny + lyF, 0x5C001u);
                    bool classWanted = (hs % 100u) < 80u;
                    bool placed = false;
                    if (classWanted)
                        for (int k = 0; k < 4 && !placed; ++k)
                        {
                            const int f = (int(hs % 4u) + k) % 4;
                            QVector<QPoint> wall;
                            if (!boardOnSide(r2, f, wall)) continue;
                            // the neighboring room behind the wall: if it is already decided as a non-classroom or as a classroom with ANOTHER blackboard — we skip this side
                            const QPoint wc = wall[wall.size() / 2];
                            const int nbx = wc.x() + dvec[f][0], nbz = wc.y() + dvec[f][1];
                            const int nb = (nbx >= 0 && nbx < sx && nbz >= 0 && nbz < sz) ? roomOf[nbz * sx + nbx] : -1;
                            if (nb >= 0 && nb != r2 && rooms[nb].size() >= 24 && splan[nb].kind != -1) continue;
                            splan[r2].kind = 1; splan[r2].f = f; splan[r2].owner = true; splan[r2].board = wall;
                            if (nb >= 0 && nb != r2 && rooms[nb].size() >= 24)
                            {   // the neighboring room sees the same blackboard -> also a classroom, facing it
                                splan[nb].kind = 1; splan[nb].f = (f + 2) % 4; splan[nb].owner = false; splan[nb].board = wall;
                            }
                            placed = true;
                        }
                    if (!placed)
                    {
                        // no suitable blind wall (or a library came up): a library (20% / 50% when there is no wall) or an ordinary one
                        const bool lib = classWanted ? ((hs >> 8) % 2u == 0u) : true;
                        splan[r2].kind = lib ? 2 : 0;
                    }
                }
            }
            // access to stairs: desks and chairs are not placed closer than 2 cells to a stair shaft/hatch (the "5" cells of the grid), so as not to block the entrance
            auto nearShaft = [&](int x, int z) -> bool
            {
                for (int dz = -2; dz <= 2; ++dz)
                    for (int dx = -2; dx <= 2; ++dx)
                    {
                        const int nx = x + dx, nz = z + dz;
                        if (nx < 0 || nx >= sx || nz < 0 || nz >= sz) continue;
                        for (int row = 0; row <= 3; ++row)
                            if (lyF + row < sy && grid[nz * sxy + (lyF + row) * sx + nx] == char(5)) return true;
                    }
                return false;
            };
            auto schoolRoom = [&](int rid) -> bool
            {   // true — the room is furnished here (a classroom/library); false — leave it standard
                const SchoolPlan& pl = splan[rid];
                if (pl.kind != 1 && pl.kind != 2) return false;
                const SBox& b = sbox[rid];
                auto inRoom = [&](int x, int z) { return inRoomId(x, z, rid); };
                auto ok2 = [&](int x, int z) { return inRoom(x, z) && cellFree(x, z, 0) && cellFree(x, z, 1) && !nearShaft(x, z); };
                int dm = 0;
                if (pl.kind == 1)
                {   // a classroom
                    const quint16 boardId = quint16(IdPaletteStart + paletteIndexOf("minecraft:black_concrete"));
                    const int f = pl.f;
                    const int Lmax = (f % 2 == 0) ? (b.maxX - b.minX) : (b.maxZ - b.minZ);
                    const int Dmax = (f % 2 == 0) ? (b.maxZ - b.minZ) : (b.maxX - b.minX);
                    const int back = (f + 2) % 4;
                    if (pl.owner)
                        for (const QPoint& wc : pl.board)                                           // the blackboard REPLACES the wall blocks (3 high)
                            for (int row = 0; row <= 2; ++row)
                                put(wc.x(), wc.y(), row, boardId, &dm);
                    // the center of the blackboard along the lateral axis of this room
                    int latSum = 0;
                    for (const QPoint& wc : pl.board)
                        latSum += latOf(b, f, wc.x() - dvec[f][0], wc.y() - dvec[f][1]);
                    const int bc = pl.board.isEmpty() ? Lmax / 2 : latSum / pl.board.size();
                    {
                        const QPoint c = cellAt(b, f, bc, 2);
                        if (ok2(c.x(), c.y()))
                        {
                            if (obj.code == 44315000 && chests)
                            {   // a research institute: instead of a teacher's desk — a lectern with an open book (physics formulas), facing the class
                                put(c.x(), c.y(), 0, quint16(int(IdLecternBase) + back), &dm);
                                ChestSite ls; ls.x = minx + c.x(); ls.y = miny + lyF - sink; ls.z = minz + c.y();
                                ls.lectern = 1000 + int(finHash(minx + c.x(), minz + c.y(), miny + lyF, 0xF0121u) % quint32(kSciBooksCount));
                                chests->append(ls);
                            }
                            else { put(c.x(), c.y(), 0, IdFence, &dm); put(c.x(), c.y(), 1, IdPressurePlateOak, &dm); }   // the teacher's desk
                        }
                    }
                    for (int dp = 4; dp <= Dmax - 1; dp += 4)                                       // rows every 4: a passage 2 wide
                        for (int lt = 1; lt <= Lmax - 1; ++lt)
                        {
                            if ((lt - 1) % 5 >= 3) continue;                                        // three desks in a row, then a passage 2 wide
                            const QPoint c = cellAt(b, f, lt, dp), ch = cellAt(b, f, lt, dp + 1);
                            if (!ok2(c.x(), c.y()) || !inRoom(ch.x(), ch.y()) || !cellFree(ch.x(), ch.y(), 0) || nearShaft(ch.x(), ch.y())) continue;
                            put(c.x(), c.y(), 0, IdFence, &dm);                                     // a desk
                            put(c.x(), c.y(), 1, IdPressurePlateOak, &dm);
                            put(ch.x(), ch.y(), 0, stairId[back], &dm);                             // a chair behind the desk (facing the blackboard)
                        }
                    return true;
                }
                // a library
                for (int z = b.minZ + 1; z <= b.maxZ - 1; z += 4)
                    for (int x = b.minX + 1; x <= b.maxX - 1; ++x)
                    {
                        if ((x - b.minX) % 6 == 5) continue;                                         // a passage between sections
                        if (!inRoom(x, z) || !cellFree(x, z, 0) || nearShaft(x, z)) continue;
                        put(x, z, 0, IdBookshelf, &dm);
                        if (cellFree(x, z, 1)) put(x, z, 1, IdBookshelf, &dm);
                    }
                for (int z = b.minZ + 3; z <= b.maxZ - 1; z += 4)                                     // reading tables in the passages
                    for (int x = b.minX + 2; x <= b.maxX - 2; x += 5)
                        if (ok2(x, z))
                        {
                            put(x, z, 0, IdFence, &dm);
                            put(x, z, 1, IdPressurePlateOak, &dm);
                            if (cellFree(x, z + 1, 0) && inRoom(x, z + 1) && !nearShaft(x, z + 1)) put(x, z + 1, 0, stairId[2], &dm);
                            if (cellFree(x, z - 1, 0) && inRoom(x, z - 1) && !nearShaft(x, z - 1)) put(x, z - 1, 0, stairId[0], &dm);
                        }
                return true;
            };
            // A LAIR: camp rooms — by the walls barrels (stacked), hay bales, chests with outpost loot, ominous banners on the walls;
            // in the center — tables with "chairs", barrels, bales.
            auto lairRoom = [&](const QVector<int>& cs, int rid) -> bool
            {
                const int want = qBound(2, int(cs.size()) / 10, 16);
                const int st = int(finHash(minx, minz, miny + lyF + rid, 0x1A12u) % quint32(cs.size()));
                int placed = 0, dm = 0;
                const int outpostTbl = kChestLootCount - 1;                                    // pillager_outpost — the last one in kChestLoot
                for (int t = 0; t < cs.size() && placed < want; ++t)
                {
                    const int idx = cs[(st + qint64(t) * 7) % cs.size()];
                    const int lx = idx % sx, lz = idx / sx;
                    if (!cellFree(lx, lz, 0)) continue;
                    const int bx = minx + lx, bz = minz + lz, byc = miny + lyF;
                    if (!farOtherFin(bx, byc, bz, 2)) continue;
                    const quint32 h = finHash(bx, bz, byc, 0x1A13u);
                    const int roll = int(h % 100u);
                    int wd = -1;
                    for (int d = 0; d < 4 && wd < 0; ++d) if (wallAt(lx, lz, d)) wd = d;
                    if (wd >= 0)
                    {
                        if (roll < 28)
                        {   // barrels in two tiers
                            put(lx, lz, 0, IdBarrel, &dm);
                            if (cellFree(lx, lz, 1)) put(lx, lz, 1, IdBarrel, &dm);
                        }
                        else if (roll < 48)
                        {   // hay bales
                            put(lx, lz, 0, IdHayBale, &dm);
                            if (cellFree(lx, lz, 1) && (h >> 8) % 2u) put(lx, lz, 1, IdHayBale, &dm);
                        }
                        else if (roll < 64 && chests)
                        {   // a chest with outpost loot: more often outposts, sometimes better
                            put(lx, lz, 0, IdChest, &dm);
                            chests->append(ChestSite{ bx, byc - sink, bz, (h >> 12) % 100u < 60u ? outpostTbl : pickChestLoot(h >> 5, 40, 40) });
                        }
                        else if (roll < 82 && banners && cellFree(lx, lz, 1))
                        {   // an ominous banner on the wall (looking into the room)
                            const int fd = (wd + 2) % 4;
                            put(lx, lz, 1, quint16(int(IdWallBannerBase) + fd), &dm);
                            banners->append(BannerSite{ bx, byc + 1 - sink, bz, fd });
                        }
                        else continue;
                    }
                    else
                    {   // the center of the room
                        if (roll < 40 && cellFree(lx, lz, 1))
                        {   // a table
                            put(lx, lz, 0, IdFence, &dm);
                            put(lx, lz, 1, IdPressurePlateOak, &dm);
                            for (int sd = 0; sd <= 2; sd += 2)
                            {
                                const int cx2 = lx + dvec[sd][0], cz2 = lz + dvec[sd][1];
                                if (cellFree(cx2, cz2, 0)) put(cx2, cz2, 0, stairId[sd], &dm);
                            }
                        }
                        else if (roll < 70) put(lx, lz, 0, IdBarrel, &dm);
                        else put(lx, lz, 0, IdHayBale, &dm);
                    }
                    ++placed;
                }
                return true;
            };
            // AN INDUSTRIAL ZONE (44340000): workshops by theme — a forge (blast furnaces, anvils, crafting tables), a foundry (lava baths in a frame of blackstone,
            // cauldrons with lava, iron blocks), a workshop (crafting tables, grindstones, stonecutters, lanterns), a warehouse (barrels, crate-chests, iron).
            const bool industrialF = (obj.code == 44340000);
            auto industrialRoom = [&](const QVector<int>& cs, int rid) -> bool
            {
                const quint32 h0 = finHash(minx, minz, miny + lyF + rid, 0x1D57u);
                const int tp = int(h0 % 100u);
                const int theme = tp < 28 ? 0 : (tp < 50 ? 1 : (tp < 75 ? 2 : 3));       // 0 forge, 1 foundry, 2 workshop, 3 warehouse
                int dm = 0, placed = 0;
                const int st = int((h0 >> 8) % quint32(cs.size()));
                // a 2×2 lava bath in a 4×4 frame (a foundry — always, a forge — in half of the rooms): the lava is bounded by a ring of blackstone
                const bool wantPit = (theme == 1) || (theme == 0 && ((h0 >> 17) & 1u));
                if (wantPit && cs.size() >= 30)
                    for (int t = 0; t < cs.size(); ++t)
                    {
                        const int idx = cs[(st + qint64(t) * 5) % cs.size()];
                        const int lx0 = idx % sx, lz0 = idx / sx;
                        bool ok = true;
                        for (int dz = 0; dz < 4 && ok; ++dz)
                            for (int dx = 0; dx < 4; ++dx)
                                if (lx0 + dx >= sx || lz0 + dz >= sz || roomOf[(lz0 + dz) * sx + lx0 + dx] != rid
                                    || !cellFree(lx0 + dx, lz0 + dz, 0) || !cellFree(lx0 + dx, lz0 + dz, 1)) { ok = false; break; }
                        if (!ok) continue;
                        for (int dz = 0; dz < 4; ++dz)
                            for (int dx = 0; dx < 4; ++dx)
                            {
                                const bool inner = (dx == 1 || dx == 2) && (dz == 1 || dz == 2);
                                put(lx0 + dx, lz0 + dz, 0, inner ? quint16(IdLava) : quint16(IdBlackstone), &dm);
                            }
                        ++placed;
                        break;
                    }
                const int want = qBound(2, int(cs.size()) / 9, 14);
                for (int t = 0; t < cs.size() && placed < want; ++t)
                {
                    const int idx = cs[(st + 1 + qint64(t) * 7) % cs.size()];
                    const int lx = idx % sx, lz = idx / sx;
                    if (!cellFree(lx, lz, 0)) continue;
                    const int bx = minx + lx, bz = minz + lz, byc = miny + lyF;
                    if (!farOtherFin(bx, byc, bz, 2)) continue;
                    const quint32 h = finHash(bx, bz, byc, 0x1D58u);
                    int wd = -1;
                    for (int d = 0; d < 4 && wd < 0; ++d) if (wallAt(lx, lz, d)) wd = d;
                    if (wd >= 0)
                    {   // a station of three cells along a wall
                        const int fd = (wd + 2) % 4, ax = (wd + 1) % 4;
                        bool ok = true;
                        for (int k = 0; k < 3 && ok; ++k)
                        {
                            const int x = lx + dvec[ax][0] * k, z = lz + dvec[ax][1] * k;
                            if (x < 0 || x >= sx || z < 0 || z >= sz || roomOf[z * sx + x] != rid
                                || !cellFree(x, z, 0) || !wallAt(x, z, wd)) ok = false;
                        }
                        if (!ok) continue;
                        quint16 a0, a1, a2;
                        const quint16 anvilId = quint16(int(IdAnvilBase) + ((fd + 1) % 4));
                        const quint16 bfId = quint16(int(IdBlastFurnaceBase) + fd);
                        const quint16 grId = quint16(int(IdGrindstoneBase) + fd);
                        const quint16 scId = quint16(int(IdStonecutterBase) + fd);
                        switch (theme)
                        {
                        case 0:  a0 = bfId;        a1 = anvilId;      a2 = IdSmithingTable; break;   // a forge
                        case 1:  a0 = IdLavaCauldron; a1 = IdIronBlock; a2 = bfId;           break;   // a foundry
                        case 2:  a0 = IdCraftingTable; a1 = grId;     a2 = scId;             break;   // a workshop
                        default: a0 = IdBarrel;    a1 = IdIronBlock;  a2 = IdBarrel;         break;   // a warehouse
                        }
                        const quint16 row[3] = { a0, a1, a2 };
                        for (int k = 0; k < 3; ++k)
                        {
                            const int x = lx + dvec[ax][0] * k, z = lz + dvec[ax][1] * k;
                            put(x, z, 0, row[k], &dm);
                            if (theme == 3 && cellFree(x, z, 1)) put(x, z, 1, (h >> (k * 3)) & 1u ? quint16(IdBarrel) : quint16(IdIronBlock), &dm);
                            if (theme == 1 && k == 1 && cellFree(x, z, 1)) put(x, z, 1, IdIronBlock, &dm);
                        }
                        if (theme == 3 && chests && cellFree(lx + dvec[fd][0], lz + dvec[fd][1], 0) && (h >> 12) % 3u == 0u)
                        {   // a crate-chest in front of a rack
                            const int cx2 = lx + dvec[fd][0], cz2 = lz + dvec[fd][1];
                            put(cx2, cz2, 0, IdChest, &dm);
                            chests->append(ChestSite{ minx + cx2, byc - sink, minz + cz2, pickChestLoot(h >> 5, 80, 20) });
                        }
                    }
                    else
                    {   // the center of the workshop: machines/racks by theme
                        if (theme == 2 && cellFree(lx, lz, 1))
                        {   // a crafting table on a table-crafting table with a lamp
                            put(lx, lz, 0, IdCraftingTable, &dm);
                            put(lx, lz, 1, IdLanternStanding, &dm);
                        }
                        else if (theme == 0)
                        {   // an anvil + a cauldron
                            put(lx, lz, 0, (h & 1u) ? quint16(int(IdAnvilBase) + int((h >> 3) % 4u)) : quint16(IdLavaCauldron), &dm);
                        }
                        else if (theme == 1) put(lx, lz, 0, (h & 1u) ? quint16(IdIronBlock) : quint16(IdLavaCauldron), &dm);
                        else
                        {   // a warehouse: a stack
                            put(lx, lz, 0, (h & 1u) ? quint16(IdBarrel) : quint16(IdIronBlock), &dm);
                            if (cellFree(lx, lz, 1)) put(lx, lz, 1, IdBarrel, &dm);
                        }
                    }
                    ++placed;
                }
                return true;
            };
            // The altar of a church: an obsidian pattern by the wall (rows from top to bottom; '#' — obsidian, 'c' — a candle (2), 'C' — a candle (4); a space — empty).
            // The first pattern by hash that fits the room width and height is taken (a wall behind over the whole width, the cells are free).
            struct AltarPat { int w, h; const char* rows[6]; };
            static const AltarPat kAltars[] = {
                { 3, 2, { "cCc", "###" } },                                   // a row of three blocks with candles
                { 3, 4, { " C ", "###", " # ", " # " } },                     // a cross
                { 5, 5, { "  C  ", "  #  ", "#####", "  #  ", "  #  " } },    // a large cross
                { 5, 5, { "c c c", "# # #", "#####", "  #  ", "  #  " } },    // a trident with candles on the points
                { 5, 4, { "# # #", "#####", "  #  ", "  #  " } },            // a trident
                { 5, 3, { "  C  ", " ### ", "#####" } },                      // a ziggurat
                { 5, 3, { "c C c", "#####", "#   #" } },                      // a long throne on legs
                { 3, 3, { " C ", "###", "# #" } },                            // a throne
            };
            static const int kAltarN = int(sizeof(kAltars) / sizeof(kAltars[0]));
            auto churchAltar = [&](const QVector<int>& cs, int rid)
            {
                const int p0 = int(finHash(minx, minz, miny + lyF + rid, 0xA17Bu) % quint32(kAltarN));
                const int st0 = int(finHash(minx, minz, miny + lyF + rid, 0xA17Au) % quint32(cs.size()));
                for (int pi = 0; pi < kAltarN; ++pi)
                {
                    const AltarPat& pat = kAltars[(p0 + pi) % kAltarN];
                    for (int t = 0; t < cs.size(); ++t)
                    {
                        const int idx = cs[(st0 + t) % cs.size()];
                        const int lx = idx % sx, lz = idx / sx;
                        for (int wd = 0; wd < 4; ++wd)
                        {
                            const int a = (wd + 1) % 4;                                          // along the wall
                            bool ok = true;
                            for (int u = 0; u < pat.w && ok; ++u)
                            {
                                const int x = lx + dvec[a][0] * u, z = lz + dvec[a][1] * u;
                                if (x < 0 || x >= sx || z < 0 || z >= sz || roomOf[z * sx + x] != rid || !wallAt(x, z, wd)) { ok = false; break; }
                                for (int r = 0; r < pat.h && ok; ++r)
                                    if (pat.rows[pat.h - 1 - r][u] != ' ' && !cellFree(x, z, r)) ok = false;
                            }
                            if (!ok) continue;
                            int dm = 0;
                            for (int u = 0; u < pat.w; ++u)
                            {
                                const int x = lx + dvec[a][0] * u, z = lz + dvec[a][1] * u;
                                for (int r = 0; r < pat.h; ++r)
                                {
                                    const char ch = pat.rows[pat.h - 1 - r][u];
                                    if (ch == '#') put(x, z, r, IdObsidian, &dm);
                                    else if (ch == 'c') put(x, z, r, quint16(int(IdCandleBase) + 1), &dm);
                                    else if (ch == 'C') put(x, z, r, quint16(int(IdCandleBase) + 3), &dm);
                                }
                            }
                            return;
                        }
                    }
                }
            };
            // ——— RESIDENTIAL APARTMENT BUILDINGS (44400000): rooms are furnished sensibly, not scattered. A bedroom: a bed with the headboard to the wall, nightstands with lamps, a wardrobe, flowers.
            // A living room: a sofa by the wall facing the TV (on a chest of drawers opposite), a coffee table between them, bookshelves, a floor lamp, plants. A kitchen: a work line
            // by the wall (a stove, a crafting table, a sink-cauldron, barrels), a dining table with chairs. Everything — only in cells that passed cellFree (no approaches to stairs, no doors, no
            // openings), furniture stands by the walls — the center of the room and the passages are free. Returns true if the room was furnished (then the general set of random items is not needed).
            auto residRoom = [&](const QVector<int>& cs, int rid) -> bool
            {
                const int n = cs.size();
                if (n < 10) return false;
                const quint32 hr0 = finHash(minx + rid * 13, minz + k * 7, miny + lyF, 0x2E51Du);
                int dummy = 0;
                auto free1 = [&](int lx, int lz, int row) { return cellFree(lx, lz, row); };
                // how many wall neighbors a cell has and which of them is the first
                auto wallDirOf = [&](int lx, int lz, int skipD = -1) -> int
                {
                    for (int dd = 0; dd < 4; ++dd) if (dd != skipD && wallAt(lx, lz, dd)) return dd;
                    return -1;
                };
                auto order = [&](quint32 seed) { QVector<int> o; o.reserve(n); const int st = int(seed % quint32(n)); for (int i = 0; i < n; ++i) o << cs[(st + i * 7) % n]; return o; };
                // a wall nearby: a solid block on any of the three lower rows (windows with a sill also count as a wall — furniture goes under a window)
                auto wallAny = [&](int ax, int az, int d) -> bool
                {
                    for (int row = 0; row < 3; ++row)
                        if (store.get(minx + ax + dvec[d][0], miny + lyF + row - sink, minz + az + dvec[d][1]) != IdAir) return true;
                    return false;
                };
                // a segment along a wall: len cells in a row by wall d, in front of them (into the room) a free cell
                auto runAt = [&](int lx, int lz, int d, int len, int rows) -> bool
                {
                    const int s0 = (d == 0 || d == 2) ? 1 : 0, s1 = (d == 0 || d == 2) ? 0 : 1;       // along the wall: by an N/S wall — along X, by E/W — along Z
                    const int fd = (d + 2) % 4;
                    for (int i = 0; i < len; ++i)
                    {
                        const int x = lx + s0 * i, z = lz + s1 * i;
                        for (int row = 0; row < rows; ++row) if (!free1(x, z, row)) return false;
                        if (!wallAny(x, z, d)) return false;
                        if (!free1(x + dvec[fd][0], z + dvec[fd][1], 0)) return false;     // the approach
                    }
                    return true;
                };
                auto findRun = [&](quint32 seed, int len, int rows, int& ox, int& oz, int& od, int forbidD = -1) -> bool
                {
                    const QVector<int> o = order(seed);
                    for (int idx : o)
                    {
                        const int lx = idx % sx, lz = idx / sx;
                        for (int q = 0; q < 4; ++q)
                        {
                            const int d = int(((seed >> 5) + quint32(q)) % 4u);
                            if (d == forbidD) continue;
                            if (runAt(lx, lz, d, len, rows)) { ox = lx; oz = lz; od = d; return true; }
                        }
                    }
                    return false;
                };
                auto alongOf = [&](int d, int& s0, int& s1) { s0 = (d == 0 || d == 2) ? 1 : 0; s1 = (d == 0 || d == 2) ? 0 : 1; };
                auto lamp = [&](int lx, int lz)
                {   // a floor lamp: a fence + a lantern
                    if (free1(lx, lz, 0) && free1(lx, lz, 1)) { put(lx, lz, 0, IdFence, &dummy); put(lx, lz, 1, IdLanternStanding, &dummy); return true; }
                    return false;
                };
                auto plant = [&](int lx, int lz)
                {
                    if (!free1(lx, lz, 0)) return false;
                    put(lx, lz, 0, quint16(IdFlowerPotBase + int(finHash(minx + lx, minz + lz, miny + lyF, 0xF10Au) % quint32(kFlowersCount))), &dummy);
                    return true;
                };
                auto bookcase = [&](int lx, int lz, int d, int len)
                {   // a bookcase along a wall: 2 tiers
                    int s0, s1; alongOf(d, s0, s1);
                    for (int i = 0; i < len; ++i)
                    {
                        put(lx + s0 * i, lz + s1 * i, 0, IdBookshelf, &dummy);
                        if (free1(lx + s0 * i, lz + s1 * i, 1)) put(lx + s0 * i, lz + s1 * i, 1, IdBookshelf, &dummy);
                    }
                };
                auto table = [&](int lx, int lz, int axis) -> bool
                {   // a dining table: a fence + a plate, two chairs at the ends
                    if (!free1(lx, lz, 0) || !free1(lx, lz, 1)) return false;
                    put(lx, lz, 0, IdFence, &dummy);
                    put(lx, lz, 1, IdPressurePlateOak, &dummy);
                    for (int sd = 0; sd <= 2; sd += 2)
                    {
                        const int d = axis + sd;
                        const int cx2 = lx + dvec[d][0], cz2 = lz + dvec[d][1];
                        if (free1(cx2, cz2, 0)) put(cx2, cz2, 0, stairId[d], &dummy);
                    }
                    return true;
                };
                bool any = false;
                int rx = 0, rz = 0, rd = 0;
                // a large room is divided into zones: about one per 45 cells (up to 6), the types alternate — a bedroom / a living room / a kitchen
                const int zones = n < 55 ? 1 : std::min(6, n / 45);
                for (int zone = 0; zone < zones; ++zone)
                {
                const quint32 hr = hr0 + quint32(zone) * 0x9E3779B1u;
                int type;                                                   // 0 bedroom, 1 living room, 2 kitchen-dining room
                if (zones > 1) type = int((hr0 % 3u + quint32(zone) * 2u) % 3u);
                else if (n < 22) type = (hr % 100u < 70u) ? 0 : 2;
                else type = (hr % 100u < 35u) ? 0 : ((hr % 100u < 75u) ? 1 : 2);
                if (type == 0)
                {   // ——— BEDROOM ———
                    // a bed: the headboard by a wall (the cell by the wall), the foot — a cell into the room; a nightstand is possible on each side of the headboard
                    if (findRun(hr, 1, 2, rx, rz, rd))
                    {
                        const int fd = (rd + 2) % 4;
                        const int fx = rx + dvec[fd][0], fz = rz + dvec[fd][1];
                        if (free1(fx, fz, 1))
                        {
                            const int col = int(finHash(minx, minz, miny, 0xBED1u) % quint32(kBedsCount));
                            const int base = int(IdBedBase) + col * 8 + rd * 2;           // foot→head looks into the wall
                            put(fx, fz, 0, quint16(base), &dummy);
                            put(rx, rz, 0, quint16(base + 1), &dummy);
                            any = true;
                            int s0, s1; alongOf(rd, s0, s1);
                            for (int sg = -1; sg <= 1; sg += 2)
                            {   // nightstands at the sides of the headboard (a barrel + a light)
                                const int nx = rx + s0 * sg, nz = rz + s1 * sg;
                                if (((hr >> (3 + sg)) & 3u) == 0u) continue;
                                if (free1(nx, nz, 0) && wallAt(nx, nz, rd)) { put(nx, nz, 0, IdComposter, &dummy); if (free1(nx, nz, 1)) put(nx, nz, 1, IdLanternStanding, &dummy); }
                            }
                            // a carpet by the bed 2×1 along the feet (if free)
                            const int cid = int(IdCarpetBase) + int(finHash(minx, minz, miny, 0xCA21u) % quint32(kCarpetsCount));
                            const int cx3 = fx + dvec[fd][0], cz3 = fz + dvec[fd][1];
                            if (free1(cx3, cz3, 0)) put(cx3, cz3, 0, quint16(cid), &dummy);
                        }
                    }
                    // a wardrobe: barrels in two tiers, 2 cells along a wall
                    if (findRun(hr * 3u + 11u, 2, 2, rx, rz, rd))
                    {
                        int s0, s1; alongOf(rd, s0, s1);
                        for (int i = 0; i < 2; ++i) { put(rx + s0 * i, rz + s1 * i, 0, IdBarrel, &dummy); put(rx + s0 * i, rz + s1 * i, 1, IdBarrel, &dummy); }
                        any = true;
                    }
                    // a writing table with a lamp or flowers
                    if (n >= 16 && findRun(hr * 5u + 3u, 1, 2, rx, rz, rd))
                    {
                        if ((hr >> 9) & 1u) { put(rx, rz, 0, IdFence, &dummy); put(rx, rz, 1, IdPressurePlateOak, &dummy); const int fd2 = (rd + 2) % 4; const int ccx = rx + dvec[fd2][0], ccz = rz + dvec[fd2][1]; if (free1(ccx, ccz, 0)) put(ccx, ccz, 0, stairId[rd], &dummy); }
                        else plant(rx, rz);
                    }
                    if (findRun(hr * 7u + 5u, 1, 2, rx, rz, rd)) lamp(rx, rz);
                }
                else if (type == 1)
                {   // ——— LIVING ROOM ———
                    // a sofa of 3 steps with the back to a wall, a TV on a chest of drawers by the opposite wall, a table in the middle
                    const bool sofaOk = findRun(hr, 3, 1, rx, rz, rd);
                    if (qEnvironmentVariableIsSet("MCX_LAIRDBG")) std::fprintf(stderr, "[resid] sofa %d at %d,%d d=%d\n", int(sofaOk), rx, rz, rd);
                    if (sofaOk)
                    {
                        int s0, s1; alongOf(rd, s0, s1);
                        for (int i = 0; i < 3; ++i) put(rx + s0 * i, rz + s1 * i, 0, stairId[rd], &dummy);
                        any = true;
                        const int fd = (rd + 2) % 4;
                        const int mx = rx + s0 + dvec[fd][0] * 2, mz = rz + s1 + dvec[fd][1] * 2;      // the table 2 cells in front of the middle seat
                        // the TV: we go from the sofa across the room to the opposite wall
                        int tx = rx + s0 + dvec[fd][0], tz = rz + s1 + dvec[fd][1], steps = 1;
                        while (free1(tx + dvec[fd][0], tz + dvec[fd][1], 0) && steps < 14) { tx += dvec[fd][0]; tz += dvec[fd][1]; ++steps; }
                        if (steps >= 4 && wallAny(tx, tz, fd) && free1(tx, tz, 0) && free1(tx, tz, 1))
                        {
                            put(tx, tz, 0, IdBookshelf, &dummy);
                            put(tx, tz, 1, IdBlackGlass, &dummy);
                            if (free1(tx + s0, tz + s1, 0) && wallAny(tx + s0, tz + s1, fd)) lamp(tx + s0, tz + s1);
                        }
                        if (free1(mx, mz, 0)) put(mx, mz, 0, IdSlabBottom, &dummy);                      // a coffee table between the sofa and the TV
                    }
                    // bookshelves on the other wall
                    if (findRun(hr * 3u + 1u, 3, 2, rx, rz, rd)) { bookcase(rx, rz, rd, 3); any = true; }
                    if (findRun(hr * 5u + 7u, 1, 2, rx, rz, rd)) lamp(rx, rz);
                    for (int i = 0; i < 2; ++i) if (findRun(hr * (7u + i) + 13u, 1, 1, rx, rz, rd)) plant(rx, rz);
                    // a dining area in a large living room
                    if (n >= 55)
                    {
                        const QVector<int> o = order(hr * 9u + 4u);
                        for (int idx : o)
                        {
                            const int lx = idx % sx, lz = idx / sx;
                            if (wallDirOf(lx, lz) >= 0) continue;                    // the table — deep in the room
                            if (!free1(lx, lz, 0) || !free1(lx, lz, 1) || !free1(lx + 1, lz, 0) || !free1(lx - 1, lz, 0) || !free1(lx, lz + 1, 0) || !free1(lx, lz - 1, 0)) continue;
                            if (table(lx, lz, (hr >> 6) & 1u)) break;
                        }
                    }
                }
                else
                {   // ——— KITCHEN-DINING ROOM ———
                    // a work line along a wall: a stove, a sink (a cauldron), a crafting table, a barrel-"refrigerator" in two tiers
                    const int runLen = n >= 30 ? 4 : 3;
                    if (findRun(hr, runLen, 2, rx, rz, rd))
                    {
                        int s0, s1; alongOf(rd, s0, s1);
                        const quint16 kit[4] = { IdFurnace, IdCauldron, IdCraftingTable, IdBarrel };
                        for (int i = 0; i < runLen; ++i)
                        {
                            put(rx + s0 * i, rz + s1 * i, 0, kit[(i + int(hr % 4u)) % 4], &dummy);
                            if (kit[(i + int(hr % 4u)) % 4] == IdBarrel && free1(rx + s0 * i, rz + s1 * i, 1)) put(rx + s0 * i, rz + s1 * i, 1, IdBarrel, &dummy);
                        }
                        any = true;
                    }
                    // a dining table with chairs
                    {
                        const QVector<int> o = order(hr * 11u + 2u);
                        for (int idx : o)
                        {
                            const int lx = idx % sx, lz = idx / sx;
                            if (!free1(lx, lz, 0) || !free1(lx, lz, 1) || !free1(lx + 1, lz, 0) || !free1(lx - 1, lz, 0) || !free1(lx, lz + 1, 0) || !free1(lx, lz - 1, 0)) continue;
                            if (n >= 30 && wallDirOf(lx, lz) >= 0) continue;
                            if (table(lx, lz, (hr >> 6) & 1u)) { any = true; break; }
                        }
                    }
                    if (findRun(hr * 3u + 9u, 1, 1, rx, rz, rd)) plant(rx, rz);
                    if (findRun(hr * 5u + 1u, 1, 2, rx, rz, rd)) lamp(rx, rz);
                }
                if (qEnvironmentVariableIsSet("MCX_LAIRDBG")) std::fprintf(stderr, "[resid] room %d n=%d zone %d/%d type %d any=%d\n", rid, n, zone, zones, type, int(any));
                }   // zones
                return any;
            };
            for (int r = 0; r < rooms.size(); ++r)
            {   // ——— the furnishing of ONE room (evenly, not as a heap at the start of the traversal) ———
                const QVector<int>& cells = rooms[r];
                if (lair && cells.size() >= 8 && lairRoom(cells, r)) continue;
                if (industrialF && cells.size() >= 12 && industrialRoom(cells, r)) continue;
                if (schoolF && cells.size() >= 24 && schoolRoom(r)) continue;
                if (churchF && cells.size() >= 16) churchAltar(cells, r);
                if (churchF && !lecternDone && chests && cells.size() >= 12)
                {   // a lectern with a book: by a wall in a free cell, facing the room; the book text is chosen by the building hash
                    const int st0 = int(finHash(minx, minz, miny + lyF + r, 0x1EC7u) % quint32(cells.size()));
                    for (int t = 0; t < cells.size() && !lecternDone; ++t)
                    {
                        const int idx = cells[(st0 + t) % cells.size()];
                        const int lx = idx % sx, lz = idx / sx;
                        if (!cellFree(lx, lz, 0) || !cellFree(lx, lz, 1)) continue;
                        for (int wd = 0; wd < 4; ++wd)
                        {
                            if (!wallAt(lx, lz, wd)) continue;
                            int dm = 0;
                            put(lx, lz, 0, quint16(int(IdLecternBase) + (wd + 2) % 4), &dm);
                            ChestSite ls; ls.x = minx + lx; ls.y = miny + lyF - sink; ls.z = minz + lz;
                            ls.lectern = int(finHash(minx, minz, miny, 0xB00Cu) % quint32(kBooksCount));
                            chests->append(ls);
                            lecternDone = true;
                            break;
                        }
                    }
                }
                // The target count is SIZE-BASED (cells/40): a room of any area gets
                // "its own" items — a large studio (bigPriv 553 cells) is not shortchanged, a tiny
                // bedroom does not turn into a warehouse. Malls are denser (a request "the halls are half-empty"):
                // cells/35, up to 20 items per hall.
                const bool residDone = resid && !lair && !qEnvironmentVariableIsSet("MCX_NO_RESIDROOM") && cells.size() >= 10 && residRoom(cells, r);
                const int want = residDone ? 0
                               : panor ? qBound(8, cells.size() / 24, 28)
                                       : qBound(priv ? 2 : 1, cells.size() / (priv ? 40 : 30), priv ? 10 : 8);
                // Malls: the budget is SPLIT. The room traversal order is a DFS from a corner, the first h%6 cells
                // are always wall ones: without a reserve want is eaten by shelves, the center of the hall
                // is NEVER looked at — there were no islands/tables/mannequins at all.
                const int freeWant = panor ? qBound(3, want / 2, 8) : 0;
                const int wallCap = want - freeWant;
                int pieces = 0, tables = 0, wallN = 0, menN = 0;  // "no more than 2 tables per room"
                const int paintWant = panor ? 0 : (obj.code == 44313000 ? qMin(6 + int(cells.size() / 14), 40) : qMin(2 + int(cells.size() / 80), 6));   // a kindergarten — the walls are hung with paintings
                int paintsN = 0;
                for (int ci = 0; ci < cells.size() && pieces < want && floorBlocks < capFloor;
                     ++ci)
                {
                    const int idx = cells[ci];
                    const int lx = idx % sx, lz = idx / sx;
                    const int bx = minx + lx, bz = minz + lz, byc = miny + lyF;
                    // A candidate must be clean (and ON TOP OF THE FLOOR — the direct puts of the wall
                    // series do not ask cellFree themselves, that is how "floating" furniture got in).
                    if (!cellFree(lx, lz, 0)) continue;         // occupied/a door/a hole — skip
                    const quint32 h = finHash(bx, bz, byc, 0xF1C3E0u);
                    if (pieces && h % 6u != 0u) continue;        // the first item is guaranteed,
                                                                 // after that — every sixth candidate
                        const bool twoUp = ((h >> 3) & 1u) != 0u;   // the second tier of a wardrobe/rack
                    int wdir[4], nw = 0;
                    for (int d = 0; d < 4; ++d) if (wallAt(lx, lz, d)) wdir[nw++] = d;
                    ++pieces;
                    if (panor && nw) ++wallN;
                    if (nw)
                    {   // ——— the wall series: along the wall we are adjacent to ———
                        const int wd = wdir[int(finHash(bx, bz, byc, 0xD4u) % quint32(nw))];  // int() AFTER the remainder!
                        const bool alongX = (wd == 1 || wd == 3);   // a row along the wall
                        const int s0 = alongX ? 1 : 0, s1 = alongX ? 0 : 1;
                        const int h2 = int(h % 100u);
                        if (panor)
                        {   // ——— MALL: a rack along a wall / barrels / a counter ———
                            if (h2 < 66)
                            {   // a rack 4..8: bookshelf (every third is a barrel of "goods")
                                const int want2 = 4 + int((h >> 7) % 5u);
                                int okN = 0;
                                for (; okN < want2; ++okN)
                                {
                                    const int rx = lx + s0 * okN, rz = lz + s1 * okN;
                                    if (!cellFree(rx, rz, 0)) break;
                                }
                                for (int i = 0; i < okN; ++i)
                                {
                                    put(lx + s0 * i, lz + s1 * i, 0,
                                        i % 3 == 2 ? IdBarrel : IdBookshelf, &floorBlocks);
                                    if (twoUp && cellFree(lx + s0 * i, lz + s1 * i, 1))
                                        put(lx + s0 * i, lz + s1 * i, 1, IdBookshelf, &floorBlocks);
                                }
                            }
                            else if (h2 < 80)
                            {   // barrels (a storage zone)
                                put(lx, lz, 0, IdBarrel, &floorBlocks);
                                if (twoUp && cellFree(lx, lz, 1)) put(lx, lz, 1, IdBarrel, &floorBlocks);
                                if (((h >> 5) & 3u) && cellFree(lx + s0, lz + s1, 0))
                                    put(lx + s0, lz + s1, 0, IdBarrel, &floorBlocks);
                            }
                            else if (cellFree(lx + s0, lz + s1, 0))
                            {   // a counter: a crafting table as a cash register + a smithing table
                                put(lx, lz, 0, IdCraftingTable, &floorBlocks);
                                put(lx + s0, lz + s1, 0, IdSmithingTable, &floorBlocks);
                                const int cd = (wd + 2) % 4;        // the seller "in the hall" — a chair
                                if (cellFree(lx + dvec[cd][0], lz + dvec[cd][1], 0))
                                    put(lx + dvec[cd][0], lz + dvec[cd][1], 0, stairId[cd], &floorBlocks);
                            }
                            else put(lx, lz, 0, IdBookshelf, &floorBlocks);
                            continue;
                        }
                        if (h2 < 30)                              // bookshelves (a row of 1..3)
                        {
                            const int lenW = 1 + int((h >> 7) % 3u);
                            int okN = 0;
                            for (; okN < lenW; ++okN)
                            {
                                const int rx = lx + s0 * okN, rz = lz + s1 * okN;
                                if (!cellFree(rx, rz, 0)) break;
                                if (twoUp && !cellFree(rx, rz, 1)) break;
                            }
                            for (int i = 0; i < okN; ++i)
                            {
                                put(lx + s0 * i, lz + s1 * i, 0, IdBookshelf, &floorBlocks);
                                if (twoUp) put(lx + s0 * i, lz + s1 * i, 1, IdBookshelf, &floorBlocks);
                            }
                            continue;
                        }
                        if (h2 < 40 && !panor && tables < (office ? 4 : 2) && cellFree(lx, lz, 1))
                        {   // A TABLE BY THE WALL (v2: in small rooms a "free" cell is
                            // rare, tables died): a fence + a top SLAB ON TOP, chairs — only from
                            // the open sides (there is no chair at a wall), the back AWAY from the table
                            ++tables;
                            put(lx, lz, 0, IdFence, &floorBlocks);
                            put(lx, lz, 1, IdPressurePlateOak, &floorBlocks);
                            const int cd = ((h >> 6) & 1u) ? 1 : 0;     // the chair axis N-S / E-W
                            for (int s = 0; s <= 2; s += 2)
                            {
                                const int d = cd + s;
                                const int chx = lx + dvec[d][0], chz = lz + dvec[d][1];
                                if (cellFree(chx, chz, 0)) put(chx, chz, 0, stairId[d], &floorBlocks);
                            }
                            continue;
                        }
                        if (h2 < 52 && !(priv && k > 0) && cellFree(lx + s0, lz + s1, 0))
                        {   // a workplace: a crafting table + a second block (4 kinds, the grindstone as a "straw")
                            const int sec2[4] = { int(IdCartographyTable), int(IdLoom),
                                                  int(IdSmithingTable), int(IdGrindstone) };
                            put(lx, lz, 0, IdCraftingTable, &floorBlocks);
                            put(lx + s0, lz + s1, 0, quint16(sec2[int((h >> 6) % 4u)]), &floorBlocks);
                            const int cd = (wd + 2) % 4;          // a chair from the room side
                            if (cellFree(lx + dvec[cd][0], lz + dvec[cd][1], 0))
                                put(lx + dvec[cd][0], lz + dvec[cd][1], 0, stairId[cd], &floorBlocks);
                            continue;
                        }
                        if (h2 < 58 && !(priv && k > 0))
                        {   // a barrel cabinet (50% a second tier, 75% a partner along the wall)
                            put(lx, lz, 0, IdBarrel, &floorBlocks);
                            if (twoUp && cellFree(lx, lz, 1)) put(lx, lz, 1, IdBarrel, &floorBlocks);
                            if (((h >> 5) & 3u) && cellFree(lx + s0, lz + s1, 0))
                                put(lx + s0, lz + s1, 0, IdBarrel, &floorBlocks);
                            continue;
                        }
                        if (h2 < 66 && !priv && cellFree(lx + s0, lz + s1, 0))
                        {   // a kitchen corner: a stove + a barrel/cauldron/composter (private houses get NONE:
                            //   their stoves come only from the dedicated pass below, so a house cannot exceed its 1..2)
                            const int kit[3] = { int(IdBarrel), int(IdCauldron), int(IdComposter) };
                            put(lx, lz, 0, IdFurnace, &floorBlocks);
                            put(lx + s0, lz + s1, 0, quint16(kit[int((h >> 9) % 3u)]), &floorBlocks);
                            continue;
                        }
                        if (h2 < 72 && !(priv && k > 0))
                        {   // a seat by a wall: a step with the back to the wall (the bottom into the room)
                            put(lx, lz, 0, stairId[wd], &floorBlocks);
                            continue;
                        }
                        {   // a bed: the foot by a wall, the head a block from it, air above both
                            const int outD = (wd + 2) % 4;
                            const int hx = lx + dvec[outD][0], hz = lz + dvec[outD][1];
                            if (!office && cellFree(lx, lz, 1) && cellFree(hx, hz, 0) && cellFree(hx, hz, 1)
                                && farOtherFin(bx, byc, bz, 2))
                            {
                                const int col = int(finHash(bx, bz, byc, 0xBED0u) % quint32(kBedsCount));
                                const int base = int(IdBedBase) + col * 8 + outD * 2;
                                put(lx, lz, 0, quint16(base), &floorBlocks);      // foot
                                put(hx, hz, 0, quint16(base + 1), &floorBlocks);  // head
                            }
                            else put(lx, lz, 0, IdBookshelf, &floorBlocks);       // refused — a shelf
                            continue;
                        }
                    }
                    // ——— standing in the middle of the room ———
                    // Malls: a distance of 3 — denser (a request "the halls are half-empty"), but the islands
                    // still do not merge into a single rack "fence"
                    if (!farOtherFin(bx, byc, bz, panor ? 3 : 2)) { --pieces; continue; }
                    const int h3 = int(h % 100u);
                    if (panor)
                    {   // ——— MALL: an island rack / a little table / a mannequin / tubs ———
                        if (h3 < 52 || h3 >= 90)
                        {   // an "island" rack 4..8 along the long axis (double-sided)
                            const bool ax = ((h >> 6) & 1u) != 0u;
                            const int q0 = ax ? 1 : 0, q1 = ax ? 0 : 1;
                            const int lenA = 4 + int((h >> 9) % 5u);
                            int okN = 0;
                            for (; okN < lenA; ++okN)
                                if (!cellFree(lx + q0 * okN, lz + q1 * okN, 0)) break;
                            if (okN == 0) { --pieces; continue; }
                            for (int i = 0; i < okN; ++i)
                            {
                                put(lx + q0 * i, lz + q1 * i, 0,
                                    i % 3 == 2 ? IdBarrel : IdBookshelf, &floorBlocks);
                                // the 2nd tier 75% (was 50%) — the hall looks like a "shop"
                                if ((twoUp || ((h >> 2) & 1u) != 0u) && cellFree(lx + q0 * i, lz + q1 * i, 1))
                                    put(lx + q0 * i, lz + q1 * i, 1, IdBookshelf, &floorBlocks);
                            }
                            continue;
                        }
                        if (h3 < 62)
                        {   // a fitting table (a common limit — two per room)
                            if (tables >= (office ? 4 : 2) || !cellFree(lx, lz, 1))
                            {   // occupied/full — a floor lamp instead of a table
                                if (cellFree(lx, lz, 1))
                                { put(lx, lz, 0, IdFence, &floorBlocks);
                                  put(lx, lz, 1, IdLanternStanding, &floorBlocks); }
                                continue;
                            }
                            ++tables;
                            put(lx, lz, 0, IdFence, &floorBlocks);
                            put(lx, lz, 1, IdPressurePlateOak, &floorBlocks);   // the top ON the post
                            const int cd = ((h >> 6) & 1u) ? 1 : 0;
                            for (int s = 0; s <= 2; s += 2)
                            {
                                const int d = cd + s;
                                const int chx = lx + dvec[d][0], chz = lz + dvec[d][1];
                                // a chair: the back (the high side) AWAY from the table — the bottom to the table
                                if (cellFree(chx, chz, 0)) put(chx, chz, 0, stairId[d], &floorBlocks);
                            }
                            continue;
                        }
                        if (h3 < 88 && mannequins)
                        {   // A MANNEQUIN (26% of the free places of the hall + a guarantee of >=2 per hall below)
                            if (!cellFree(lx, lz, 1) || !cellFree(lx, lz, 2)) { --pieces; continue; }
                            putMannequin(bx, bz, byc);
                            ++menN;
                            continue;
                        }
                        if (h3 < 90 && cellFree(lx, lz, 0))
                        {   // a planter with a flower (does not require two tiers — just a pot)
                            put(lx, lz, 0, quint16(IdFlowerPotBase + int(h >> 11) % kFlowersCount), &floorBlocks);
                            continue;
                        }
                        continue;
                    }
                    if (h3 < 26)
                    {   // A DUMMY TABLE: a fence + a plate-top ON TOP (oak_pressure_plate —
                        // a request instead of a slab), chairs — steps with the bottom to the table (facing away from
                        // the table), a limit of 2 per room
                        if (tables >= (office ? 4 : 2) || !cellFree(lx, lz, 1))
                        {   // the limit/no room — a floor lamp or a lonely tabletop-shelf
                            if (cellFree(lx, lz, 1))
                            { put(lx, lz, 0, IdFence, &floorBlocks);
                              put(lx, lz, 1, IdLanternStanding, &floorBlocks); }
                            else put(lx, lz, 0, IdBookshelf, &floorBlocks);
                            continue;
                        }
                        ++tables;
                        put(lx, lz, 0, IdFence, &floorBlocks);
                        put(lx, lz, 1, IdPressurePlateOak, &floorBlocks);
                        const int cd = ((h >> 6) & 1u) ? 1 : 0;     // the chair axis N-S / E-W
                        for (int s = 0; s <= 2; s += 2)             // two opposite chairs
                        {
                            const int d = cd + s;
                            const int chx = lx + dvec[d][0], chz = lz + dvec[d][1];
                            if (cellFree(chx, chz, 0)) put(chx, chz, 0, stairId[d], &floorBlocks);
                        }
                        continue;
                    }
                    if (h3 < 36)
                    {   // a floor lamp: a fence post + a lantern on top
                        if (!cellFree(lx, lz, 1)) { put(lx, lz, 0, IdBookshelf, &floorBlocks); continue; }
                        put(lx, lz, 0, IdFence, &floorBlocks);
                        put(lx, lz, 1, IdLanternStanding, &floorBlocks);
                        continue;
                    }
                    if (h3 < 48)
                    {   // a workshop: a loom/grindstone/smithing station (+ a partner)
                        const int work[3] = { int(IdLoom), int(IdGrindstone), int(IdSmithingTable) };
                        put(lx, lz, 0, quint16(work[int((h >> 6) % 3u)]), &floorBlocks);
                        if (((h >> 13) & 3u) && cellFree(lx + 1, lz, 0))
                            put(lx + 1, lz, 0, ((h >> 8) & 1u) ? IdCauldron : IdComposter,
                                &floorBlocks);
                        continue;
                    }
                    if (h3 < 58)
                    {   // a corner with flowers: two pots in a row
                        if (!cellFree(lx + 1, lz, 0)) { put(lx, lz, 0, IdComposter, &floorBlocks);
                                                       continue; }
                        put(lx, lz, 0, quint16(IdFlowerPotBase + int(h >> 11) % kFlowersCount), &floorBlocks);
                        put(lx + 1, lz, 0, quint16(IdFlowerPotBase + int(h >> 14) % kFlowersCount), &floorBlocks);
                        continue;
                    }
                    if (h3 < 66)
                    {   // a utility corner: a cauldron + a composter
                        put(lx, lz, 0, IdCauldron, &floorBlocks);
                        if (cellFree(lx, lz + 1, 0)) put(lx, lz + 1, 0, IdComposter, &floorBlocks);
                        continue;
                    }
                    if (h3 < 80 && !resid && !(priv && k > 0))
                    {   // a 2×2 carpet (in the mansard the carpets are laid by the compositions below)
                        if (!(cellFree(lx + 1, lz, 0) && cellFree(lx, lz + 1, 0)
                              && cellFree(lx + 1, lz + 1, 0)))
                        { put(lx, lz, 0, IdComposter, &floorBlocks); continue; }
                        const quint16 cid = quint16(int(IdCarpetBase)
                                                    + int((h >> 8) % quint32(kCarpetsCount)));
                        put(lx, lz, 0, cid, &floorBlocks);
                        put(lx + 1, lz, 0, cid, &floorBlocks);
                        put(lx, lz + 1, 0, cid, &floorBlocks);
                        put(lx + 1, lz + 1, 0, cid, &floorBlocks);
                        continue;
                    }
                    put(lx, lz, 0, IdBookshelf, &floorBlocks);      // a fallback — so that every
                }                                                   // room has something
                // ——— a guaranteed table in a LARGE room (v2: in cut-up rooms a table
                // rarely turns up, and in a bigPriv studio there were none at all) — one, if
                // the main pass placed none ———
                if (!panor && !residDone && tables == 0 && cells.size() >= 120)
                {
                    for (int ci = 0; ci < cells.size(); ++ci)
                    {
                        const int idx = cells[ci];
                        const int lx = idx % sx, lz = idx / sx;
                        const int bx = minx + lx, bz = minz + lz, byc = miny + lyF;
                        if (occ.contains(dkey(bx, byc, bz))) continue;
                        if (!cellFree(lx, lz, 1) || !farOtherFin(bx, byc, bz, 2)) continue;
                        ++tables;
                        put(lx, lz, 0, IdFence, &floorBlocks);
                        put(lx, lz, 1, IdPressurePlateOak, &floorBlocks);
                        const int cd = (finHash(bx, bz, byc, 0x7AB1u) >> 5) & 1u ? 1 : 0;
                        for (int s = 0; s <= 2; s += 2)
                        {
                            const int d = cd + s;
                            const int chx = lx + dvec[d][0], chz = lz + dvec[d][1];
                            if (cellFree(chx, chz, 0)) put(chx, chz, 0, stairId[d], &floorBlocks);
                        }
                        break;
                    }
                }
                // ——— AT LEAST TWO mannequins in each mall hall (a request "put mannequins" +
                // "the halls are half-empty"): even if the free baskets missed a mannequin ———
                if (panor && mannequins && menN < 2 && cells.size() >= 150)
                {
                    for (int ci = 0; ci < cells.size() && menN < 2; ++ci)
                    {
                        const int idx = cells[ci];
                        const int lx = idx % sx, lz = idx / sx;
                        const int bx = minx + lx, bz = minz + lz, byc = miny + lyF;
                        if (occ.contains(dkey(bx, byc, bz))) continue;
                        bool wallAdj = false;
                        for (int d = 0; d < 4 && !wallAdj; ++d)
                            if (wallAt(lx, lz, d)) wallAdj = true;
                        if (wallAdj) continue;                     // only the "center of the hall"
                        if (!farOtherFin(bx, byc, bz, 3)) continue;
                        if (!cellFree(lx, lz, 0)                     // there is a floor, air, outside doors
                            || !cellFree(lx, lz, 1) || !cellFree(lx, lz, 2)) continue;
                        if (putMannequin(bx, bz, byc)) ++menN;
                    }
                }
                // ——— PAINTINGS (painting entities; there is no "painting" block in the game — before, a non-existent
                // block was written, and there were no paintings at all). They hang on the row +1 above the floor (the eye level), in the cell in front of
                // the wall; first of all — on inter-room partitions (there is the most room there), then
                // on any blind walls. Only 1×1 motifs, neighboring blocks are not affected. ———
                if (paintWant > 0 && paints)
                {
                    for (int pass = 0; pass < 2; ++pass)               // 0 — partitions only, 1 — any walls
                        for (int ci = 0; ci < cells.size() && paintsN < paintWant; ++ci)
                        {
                            const int idx = cells[ci];
                            const int lx = idx % sx, lz = idx / sx;
                            const int bx = minx + lx, bz = minz + lz;
                            const quint32 hp = finHash(bx, bz, miny + lyF, 0xC47u);
                            if (hp % (pass == 0 ? 3u : 7u) != 0u) continue;
                            if (!cellFree(lx, lz, 1)) continue;         // is the hanging tier free?
                            int face = -1;
                            for (int d = 0; d < 4 && face < 0; ++d)
                            {   // the holder is a wall block of the PAINTING TIER (row +1): the window glass
                                // (strips at +1/+2) and openings ("5") are cut off by themselves
                                const int hx0 = lx + dvec[d][0], hz0 = lz + dvec[d][1];
                                const quint16 hid = store.get(minx + hx0, miny + lyF + 1 - sink,
                                                              minz + hz0);
                                if (hid == IdAir) continue;
                                if (blockName(hid).contains(QLatin1String("glass"))) continue;
                                if (grid[hz0 * sxy + (lyF + 1) * sx + hx0] != char(1)
                                    && grid[hz0 * sxy + (lyF + 1) * sx + hx0] != char(4)) continue;
                                if (pass == 0 && !partSet.contains(dkey(minx + hx0, miny + lyF + 1, minz + hz0))) continue;
                face = (d + 2) % 4;                     // facing away from the wall, into the room
                            }
                            if (face < 0) continue;
                            occ.insert(dkey(bx, miny + lyF + 1, bz));
                            ++paintsN;
                            paints->append(PaintingSite{ bx, miny + lyF + 1 - sink, bz, face,
                                                         int(hp >> 8) % kPaintMotifsCount });
                        }
                }
            }
            // ——— STANDS WITH FRAMES (malls): a panel of 3 plank blocks 2 high, with item frames on both sides (swords and
            // tools of different materials: wood 66%, stone 24%, gold 7%, iron 2.5%, diamond 0.5%; wear —
            // durability remains 2..36%; very rarely a golden apple and a golden carrot). ———
            auto placeRack = [&](int lx, int lz, bool ax) -> bool
            {
                if (!frames) return false;
                const int q0 = ax ? 1 : 0, q1 = ax ? 0 : 1;               // the panel axis
                const int p0 = ax ? 0 : 1, p1 = ax ? 1 : 0;               // the perpendicular: the sides with frames
                for (int i = -1; i <= 1; ++i)
                    for (int row = 0; row < 2; ++row)
                    {
                        if (!cellFree(lx + q0 * i, lz + q1 * i, row)) return false;
                        for (int sd = -1; sd <= 1; sd += 2)
                            if (!cellFree(lx + q0 * i + p0 * sd, lz + q1 * i + p1 * sd, row)) return false;
                    }
                int dummyPlaced = 0;
                for (int i = -1; i <= 1; ++i)
                    for (int row = 0; row < 2; ++row)
                    {
                        put(lx + q0 * i, lz + q1 * i, row, IdOakPlanks, &dummyPlaced);
                        for (int sd = -1; sd <= 1; sd += 2)
                        {
                            const int fx = lx + q0 * i + p0 * sd, fz = lz + q1 * i + p1 * sd;
                            const int bx = minx + fx, bz = minz + fz, byc = miny + lyF + row;
                            occ.insert(dkey(bx, byc, bz));                 // the frame cell is occupied
                            FrameSite fr;
                            fr.x = bx; fr.y = byc - sink; fr.z = bz;
                            // the side outward from the panel: by +p -> where the perpendicular is directed
                            fr.face = ax ? (sd > 0 ? 2 : 0) : (sd > 0 ? 1 : 3);
                            const quint32 hf = finHash(bx, bz, byc, 0xF4A3Eu);
                            const int itemRoll = int(hf % 100u);
                            if (itemRoll < 3) fr.kind = 5;                 // a golden apple (rarer: there is less good stuff on racks)
                            else if (itemRoll < 6) fr.kind = 6;            // a golden carrot
                            else
                            {
                                const int tr = itemRoll - 6;                // 0..93: a sword ~40, a pickaxe/axe/shovel/hoe ~13 each
                                fr.kind = tr < 40 ? 0 : 1 + (tr - 40) / 14;
                                if (fr.kind > 4) fr.kind = 4;
                                // a rack: wood 66%, stone 24%, gold 7%, iron 2.5%, diamond 0.5%
                                const quint32 hm = finHash(bx, bz, byc, 0x3A71u) % 1000u;
                                fr.mat = hm < 660u ? 0 : hm < 900u ? 1 : hm < 970u ? 2 : hm < 995u ? 3 : 4;
                                const int mxd = kToolMaxDur[fr.mat];
                                const int leftPct = 2 + int(finHash(bx, bz, byc, 0x71B9u) % 35u);   // 2..36 %
                                fr.dmg = std::max(0, std::min(mxd - 1, mxd - int(std::lround(mxd * leftPct / 100.0))));
                            }
                            frames->append(fr);
                        }
                    }
                return true;
            }
            ;
            // ——— SACRIFICIAL STANDS (malls): an obsidian 1×1 pedestal, on its top face — a frame turned upward, with a sword
            // (the material and wear by the same chances as on the stands). ———
            auto placeAltar = [&](int lx, int lz) -> bool
            {
                if (!frames || !cellFree(lx, lz, 0) || !cellFree(lx, lz, 1)) return false;
                int dummyPlaced = 0;
                put(lx, lz, 0, IdObsidian, &dummyPlaced);
                const int bx = minx + lx, bz = minz + lz, byc = miny + lyF + 1;
                occ.insert(dkey(bx, byc, bz));
                FrameSite fr;
                fr.x = bx; fr.y = byc - sink; fr.z = bz;
                fr.face = 4;                                               // up
                fr.kind = 0;
                // the altar: noticeably more good stuff — wood 20%, stone 25%, gold 25%, iron 18%, diamond 12%
                const quint32 hm = finHash(bx, bz, byc, 0x5AC1u) % 100u;
                fr.mat = hm < 20u ? 0 : hm < 45u ? 1 : hm < 70u ? 2 : hm < 88u ? 3 : 4;
                const int mxd = kToolMaxDur[fr.mat];
                const int leftPct = 2 + int(finHash(bx, bz, byc, 0x71B9u) % 35u);
                fr.dmg = std::max(0, std::min(mxd - 1, mxd - int(std::lround(mxd * leftPct / 100.0))));
                frames->append(fr);
                return true;
            };
            // ——— FILLING THE VOIDS (a request: "no empty areas larger than ~5×5 should remain" in malls; apartment and
            // non-residential buildings — a bit denser, a 7×7 window). After the usual placement, in one pass we look for squares of free cells
            // (a cell is free if one can walk/place on it — cellFree) and put an item in the center; what is placed
            // only REDUCES the number of free cells, so one pass is enough. ———
            {
                const bool aptHere = (obj.code == 44400000);
                const int G = panor ? 5 : ((aptHere || office) ? 7 : 0);
                if (G > 0)
                {
                    const int hR = G / 2;
                    QVector<char> freeMap(sx * sz, 0);
                    for (int lz = 1; lz < sz - 1; ++lz)
                        for (int lx = 1; lx < sx - 1; ++lx)
                            freeMap[lz * sx + lx] = cellFree(lx, lz, 0) ? 1 : 0;
                    auto occupy = [&](int cx, int cz, int rad)
                    {
                        for (int dz = -rad; dz <= rad; ++dz)
                            for (int dx = -rad; dx <= rad; ++dx)
                            {
                                const int x = cx + dx, z = cz + dz;
                                if (x >= 0 && x < sx && z >= 0 && z < sz) freeMap[z * sx + x] = 0;
                            }
                    };
                    int dummy = 0;
                    for (int lz = hR; lz < sz - hR; ++lz)
                        for (int lx = hR; lx < sx - hR; ++lx)
                        {
                            if (!freeMap[lz * sx + lx]) continue;
                            bool all = true;
                            for (int dz = -hR; dz <= hR && all; ++dz)
                                for (int dx = -hR; dx <= hR; ++dx)
                                    if (!freeMap[(lz + dz) * sx + lx + dx]) { all = false; break; }
                            if (!all) continue;
                            const int bx = minx + lx, bz = minz + lz, byc = miny + lyF;
                            const quint32 hg = finHash(bx, bz, byc, 0x6A9u);
                            const int roll = int(hg % 100u);
                            const bool ax = ((hg >> 7) & 1u) != 0u;           // the row/chair axis
                            const int q0 = ax ? 1 : 0, q1 = ax ? 0 : 1;
                            if (panor)
                            {
                                if (roll < 25 && placeRack(lx, lz, ax))
                                {   // a stand with frames (swords/tools) — already placed
                                }
                                else if (roll < 35 && placeAltar(lx, lz))
                                {   // a sacrificial stand with a sword — already placed
                                }
                                else if (roll < 50)
                                {   // a trading "island" of 5 cells: racks and barrels in two tiers
                                    for (int i = -2; i <= 2; ++i)
                                    {
                                        const int x = lx + q0 * i, z = lz + q1 * i;
                                        if (!cellFree(x, z, 0)) continue;
                                        put(x, z, 0, (i + 2) % 3 == 2 ? IdBarrel : IdBookshelf, &dummy);
                                        if (cellFree(x, z, 1)) put(x, z, 1, IdBookshelf, &dummy);
                                    }
                                }
                                else if (roll < 78 && cellFree(lx, lz, 1))
                                {   // a little table with two "chairs"-steps
                                    put(lx, lz, 0, IdFence, &dummy);
                                    put(lx, lz, 1, IdPressurePlateOak, &dummy);
                                    for (int sd = 0; sd <= 2; sd += 2)
                                    {
                                        const int d = (ax ? 1 : 0) + sd;
                                        const int cx2 = lx + dvec[d][0], cz2 = lz + dvec[d][1];
                                        if (cellFree(cx2, cz2, 0)) put(cx2, cz2, 0, stairId[d], &dummy);
                                    }
                                }
                                else
                                {   // "goods": barrels as a 2×2 block in one tier
                                    for (int dz = 0; dz <= 1; ++dz)
                                        for (int dx = 0; dx <= 1; ++dx)
                                            if (cellFree(lx + dx, lz + dz, 0)) put(lx + dx, lz + dz, 0, IdBarrel, &dummy);
                                }
                            }
                            else
                            {
                                if (roll < 40 && cellFree(lx, lz, 1))
                                {   // a table with chairs
                                    put(lx, lz, 0, IdFence, &dummy);
                                    put(lx, lz, 1, IdPressurePlateOak, &dummy);
                                    for (int sd = 0; sd <= 2; sd += 2)
                                    {
                                        const int d = (ax ? 1 : 0) + sd;
                                        const int cx2 = lx + dvec[d][0], cz2 = lz + dvec[d][1];
                                        if (cellFree(cx2, cz2, 0)) put(cx2, cz2, 0, stairId[d], &dummy);
                                    }
                                }
                                else if (roll < 65 && cellFree(lx, lz, 1))
                                {   // a floor lamp
                                    put(lx, lz, 0, IdFence, &dummy);
                                    put(lx, lz, 1, IdLanternStanding, &dummy);
                                }
                                else if (roll < 85)
                                {   // a 2×2 carpet
                                    const quint16 cid = quint16(int(IdCarpetBase) + int((hg >> 8) % quint32(kCarpetsCount)));
                                    for (int dz = 0; dz <= 1; ++dz)
                                        for (int dx = 0; dx <= 1; ++dx)
                                            if (cellFree(lx + dx, lz + dz, 0)) put(lx + dx, lz + dz, 0, cid, &dummy);
                                }
                                else
                                {   // a rack of two blocks
                                    for (int i = 0; i <= 1; ++i)
                                    {
                                        const int x = lx + q0 * i, z = lz + q1 * i;
                                        if (!cellFree(x, z, 0)) continue;
                                        put(x, z, 0, IdBookshelf, &dummy);
                                        if (cellFree(x, z, 1)) put(x, z, 1, IdBookshelf, &dummy);
                                    }
                                }
                            }
                            occupy(lx, lz, 2);                                   // with a reserve: neighbors must not give a new square
                        }
                }
            }
            if (panor && frames)
            {   // a guaranteed pass: a mall gets no fewer than a few stands (by hall size), in free places
                QVector<int> openCells;
                for (int lz = 2; lz < sz - 2; ++lz)
                    for (int lx = 2; lx < sx - 2; ++lx)
                        if (cellFree(lx, lz, 0)) openCells.append(lz * sx + lx);
                const int target = qBound(2, openCells.size() / 150, 12);
                int racks = 0;
                for (const FrameSite& fs : *frames) { Q_UNUSED(fs); }
                if (!openCells.isEmpty())
                {
                    const int nOpen = openCells.size();
                    const int start = int(finHash(minx, minz, miny + lyF, 0x4ACCu) % quint32(nOpen));
                    const int stride = 7 + 2 * (nOpen % 11);               // the step is odd, to go around the hall in a scatter
                    for (int t = 0; t < qMin(nOpen, 6000) && racks < target; ++t)
                    {
                        const int idx2 = openCells[(start + qint64(t) * stride) % nOpen];
                        const int lx = idx2 % sx, lz = idx2 / sx;
                        const bool ax = (finHash(minx + lx, minz + lz, miny + lyF, 0x9A1u) & 1u) != 0u;
                        if (placeRack(lx, lz, ax) || placeRack(lx, lz, !ax)) ++racks;
                    }
                    int altars = 0;
                    const int targetA = qBound(2, nOpen / 220, 10);
                    for (int t = 0; t < qMin(nOpen, 4000) && altars < targetA; ++t)
                    {
                        const int idx2 = openCells[(start + 3 + qint64(t) * (stride + 2)) % nOpen];
                        if (placeAltar(idx2 % sx, idx2 / sx)) ++altars;
                    }
                }
            }
            // ——— STOVES of private houses (a user request): 1..2 furnaces on the 1st floor along the walls, every one
            // with fuel in the block entity (put() registers the ChestSite::stove for any furnace it places here).
            // The random kitchen-corner stoves are switched off for priv above, so this pass is the only source. ———
            if (priv && k == 0 && chests)
            {
                int haveSt = 0;
                for (int lz = 1; lz < sz - 1; ++lz)
                    for (int lx = 1; lx < sx - 1; ++lx)
                    {
                        const quint16 b0 = store.get(minx + lx, miny + lyF - sink, minz + lz);
                        if (b0 == IdFurnace || (b0 >= IdFurnaceBase && b0 < IdFurnaceMax)) ++haveSt;
                    }
                const int wantStoves = 1 + int(finHash(minx, minz, miny + lyF, 0x570C1u) % 2u);   // 1..2 (a request: not more than two stoves in a house)
                if (haveSt < wantStoves)
                {
                    QVector<int> cand;
                    for (int lz = 1; lz < sz - 1; ++lz)
                        for (int lx = 1; lx < sx - 1; ++lx)
                            if (cellFree(lx, lz, 0) && cellFree(lx, lz, 1) &&
                                (wallAt(lx, lz, 0) || wallAt(lx, lz, 1) || wallAt(lx, lz, 2) || wallAt(lx, lz, 3)))
                                cand.append(lz * sx + lx);
                    const int nC = cand.size();
                    const int st0 = nC ? int(finHash(minx, minz, miny + lyF, 0x570C2u) % quint32(nC)) : 0;
                    int dummySt = 0;
                    for (int t = 0; t < nC && haveSt < wantStoves; ++t)
                    {
                        const int idx = cand[(st0 + t * 5) % nC];
                        const int lx = idx % sx, lz = idx / sx;
                        if (!cellFree(lx, lz, 0) || !cellFree(lx, lz, 1)) continue;   // the cell could have been taken by the previous stove
                        int wd = -1;
                        for (int d = 0; d < 4; ++d) if (wallAt(lx, lz, d)) { wd = d; break; }
                        if (wd < 0) continue;
                        const int bx = minx + lx, bz = minz + lz, byc = miny + lyF;
                        if (!farOtherFin(bx, byc, bz, 2)) continue;
                        put(lx, lz, 0, quint16(int(IdFurnaceBase) + (wd + 2) % 4), &dummySt);   // the front into the room
                        ++haveSt;
                    }
                }
            }
            // ——— CARPET COMPOSITIONS in residential multi-storey buildings: instead of random squares — a whole carpet in a free part of the room: a runner
            // with an edging, a large carpet in a checkerboard of contrasting colors or a carpet with a double border. They do not cover everything: a passage
            // remains along the edges, and furniture cells and door openings are not affected (cellFree). ———
            if ((resid || priv) && !qEnvironmentVariableIsSet("MCX_NORUGS"))
            {
                // pairs of contrasting colors (the kCarpets indices): white/black, red/white, blue/light blue, green/lime, purple/pink,
                // brown/orange, cyan/white, gray/light gray, black/yellow, red/black, blue/white, brown/white
                static const int kPairs[][2] = { { 0, 15 }, { 14, 0 }, { 11, 3 }, { 13, 5 }, { 10, 6 }, { 12, 1 },
                                                 { 9, 0 }, { 7, 8 }, { 15, 4 }, { 14, 15 }, { 11, 0 }, { 12, 0 } };
                for (int rid = 0; rid < rooms.size(); ++rid)
                {
                    const QVector<int>& cs = rooms[rid];
                    if (cs.size() < 14) continue;
                    const quint32 hr = finHash(minx, minz, miny + lyF + rid, 0xCA9E7u);
                    if (hr % 100u >= 75u) continue;                                     // not in every room
                    int bx0 = sx, bx1 = -1, bz0 = sz, bz1 = -1;
                    for (int idx : cs) { bx0 = std::min(bx0, idx % sx); bx1 = std::max(bx1, idx % sx); bz0 = std::min(bz0, idx / sx); bz1 = std::max(bz1, idx / sx); }
                    const int W = bx1 - bx0 + 1, Hh = bz1 - bz0 + 1;
                    QVector<char> fr(W * Hh, 0);
                    for (int idx : cs)
                        if (cellFree(idx % sx, idx / sx, 0)) fr[(idx / sx - bz0) * W + (idx % sx - bx0)] = 1;
                    // the largest free rectangle (histograms)
                    QVector<int> hgt(W, 0);
                    int bestA = 0, rx0 = 0, rz0 = 0, rw = 0, rh = 0;
                    for (int z = 0; z < Hh; ++z)
                    {
                        for (int x = 0; x < W; ++x) hgt[x] = fr[z * W + x] ? hgt[x] + 1 : 0;
                        QVector<int> stk;
                        for (int x = 0; x <= W; ++x)
                        {
                            const int cur = (x < W) ? hgt[x] : 0;
                            while (!stk.isEmpty() && hgt[stk.last()] >= cur)
                            {
                                const int hh = hgt[stk.takeLast()];
                                const int left = stk.isEmpty() ? 0 : stk.last() + 1;
                                const int width = x - left;
                                if (hh * width > bestA) { bestA = hh * width; rx0 = left; rz0 = z - hh + 1; rw = width; rh = hh; }
                            }
                            stk.append(x);
                        }
                    }
                    if (rw < 3 || rh < 3 || bestA < 12) continue;
                    // a passage along the edge: in a spacious area we step back by a cell
                    if (rw >= 6 && rh >= 6) { ++rx0; ++rz0; rw -= 2; rh -= 2; }
                    const int pr = int((hr >> 8) % (sizeof(kPairs) / sizeof(kPairs[0])));
                    const quint16 cA = quint16(int(IdCarpetBase) + kPairs[pr][0]), cB = quint16(int(IdCarpetBase) + kPairs[pr][1]);
                    const int pr2 = int((hr >> 14) % (sizeof(kPairs) / sizeof(kPairs[0])));
                    const quint16 cC = quint16(int(IdCarpetBase) + kPairs[pr2][(hr >> 20) & 1u]);
                    int dm2 = 0;
                    const int kind = int((hr >> 22) % 3u);
                    int w = rw, h = rh;
                    bool horiz = w >= h;                                                 // a runner along the long side
                    if (kind == 2 && std::max(w, h) >= 5 && std::min(w, h) >= 3)
                    {   // a runner: width 3 (an edging | the middle | an edging) and length up to 9, in the center of the free area
                        const int len = std::min(std::max(w, h), 9);
                        const int wid = 3;
                        const int rw2 = horiz ? len : wid, rh2 = horiz ? wid : len;
                        const int ox = rx0 + (w - rw2) / 2, oz = rz0 + (h - rh2) / 2;
                        for (int dz = 0; dz < rh2; ++dz)
                            for (int dx = 0; dx < rw2; ++dx)
                            {
                                const int u = horiz ? dx : dz, v = horiz ? dz : dx, ul = horiz ? rw2 : rh2;
                                const bool edge = (v == 0 || v == wid - 1 || u == 0 || u == ul - 1);
                                put(bx0 + ox + dx, bz0 + oz + dz, 0, edge ? cA : cB, &dm2);
                            }
                        continue;
                    }
                    w = std::min(w, 8); h = std::min(h, 8);
                    const int ox = rx0 + (rw - w) / 2, oz = rz0 + (rh - h) / 2;
                    if (kind == 0 && w >= 4 && h >= 4)
                    {   // a checkerboard of contrasting colors
                        for (int dz = 0; dz < h; ++dz)
                            for (int dx = 0; dx < w; ++dx)
                                put(bx0 + ox + dx, bz0 + oz + dz, 0, ((dx + dz) & 1) ? cB : cA, &dm2);
                    }
                    else if (w >= 3 && h >= 3)
                    {   // a carpet with an edging: the outer ring cA, inside cB (in a large one — another ring cC)
                        for (int dz = 0; dz < h; ++dz)
                            for (int dx = 0; dx < w; ++dx)
                            {
                                const int d = std::min(std::min(dx, w - 1 - dx), std::min(dz, h - 1 - dz));
                                const quint16 c = d == 0 ? cA : (d == 1 && w >= 7 && h >= 7 ? cC : cB);
                                put(bx0 + ox + dx, bz0 + oz + dz, 0, c, &dm2);
                            }
                    }
                }
            }
            // ——— BEDS in private houses (a request: "far from in every one"): if no beds came up randomly on a floor, we place
            // 1 (2 in spacious ones): the foot edge by a wall/furniture, the head — a block into the room, air above both. ———
            if (priv || hospital)
            {
                auto bedsOnFloor = [&]() -> int
                {
                    int n = 0;
                    for (int lz = 1; lz < sz - 1; ++lz)
                        for (int lx = 1; lx < sx - 1; ++lx)
                        {
                            const quint16 b0 = store.get(minx + lx, miny + lyF - sink, minz + lz);
                            if (b0 >= IdBedBase && b0 < IdBedMax) ++n;
                        }
                    return n / 2;
                };
                const int wantBeds = hospital ? qBound(2, openN / 28, 10) : (openN >= 90 ? 2 : 1);
                int have = bedsOnFloor();
                // A LOW mansard (a brick house "~1.5 storeys"): the row above the attic floor is the top mesh row
                // and is NEVER a cavity, so the 2-row headroom would ban beds even though a bed fits (it is one
                // block tall). Up there only the walking row is checked — the bed stands under the rafters.
                const bool lowAttic = (priv && k > 0 && lyF >= lyHigh - 1);
                auto headClear = [&](int ax, int az)
                { return lowAttic || cellFree(ax, az, 1); };
                if (have < wantBeds)
                {
                    QVector<int> cand;
                    for (int lz = 1; lz < sz - 1; ++lz)
                        for (int lx = 1; lx < sx - 1; ++lx)
                            if (cellFree(lx, lz, 0) && headClear(lx, lz)) cand.append(lz * sx + lx);
                    const int nC = cand.size();
                    const int st0 = nC ? int(finHash(minx, minz, miny + lyF, 0xBED1u) % quint32(nC)) : 0;
                    for (int pass = 0; pass < 2 && have < wantBeds; ++pass)    // pass 0 — only by a wall/furniture, pass 1 — anywhere
                        for (int t = 0; t < nC && have < wantBeds; ++t)
                        {
                            const int idx = cand[(st0 + t * 5) % nC];
                            const int lx = idx % sx, lz = idx / sx;
                            if (!cellFree(lx, lz, 0) || !headClear(lx, lz)) continue;
                            for (int outD = 0; outD < 4; ++outD)
                            {
                                const int hx = lx + dvec[outD][0], hz = lz + dvec[outD][1];
                                if (!cellFree(hx, hz, 0) || !headClear(hx, hz)) continue;
                                if (pass == 0 && cellFree(lx - dvec[outD][0], lz - dvec[outD][1], 0)) continue;   // a wall behind
                                const int col = int(finHash(minx + lx, minz + lz, miny + lyF, 0xBED0u) % quint32(kBedsCount));
                                const int base = int(IdBedBase) + col * 8 + outD * 2;
                                int dummyBed = 0;
                                put(lx, lz, 0, quint16(base), &dummyBed);       // foot
                                put(hx, hz, 0, quint16(base + 1), &dummyBed);   // head
                                ++have;
                                break;
                            }
                        }
                }
            }
            // ——— LIVE RESIDENTS of this floor (villager / wandering_trader / trader_llama entities) ———
            // Roles: malls — various traders, including wandering ones with a llama; private houses and residential — residents;
            // a church (44319000) — a priest (cleric); schools/kindergartens/research buildings (44312000/44313000/44315000) —
            // a librarian ("scholar", in a kindergarten also children); other non-residential — employees. A cell must be
            // free for 3 blocks upward; in a house the slabs do not intersect with furniture (occ).
            if (npcs)
            {
                const bool church = (obj.code == 44319000);
                const bool school = (obj.code == 44312000 || obj.code == 44315000);
                const bool kinder = (obj.code == 44313000);
                int nWant = 0;
                if (panor)        nWant = qBound(2, openN / 90, 8);
                else if (church)  nWant = openN >= 200 ? 2 : 1;
                else if (hospital) nWant = openN >= 200 ? 3 : 2;           // doctors and nurses (the cleric profession)
                else if (school)  nWant = openN >= 150 ? 7 : 5;           // a teacher + pupils (children) at desks
                else if (kinder)  nWant = openN >= 150 ? 14 : (openN >= 70 ? 10 : 7);   // a kindergarten: many children
                else if (priv)    nWant = k == 0 ? (openN >= 70 ? 3 : (openN >= 30 ? 2 : 1)) : 0;  // the family lives on the 1st floor, the mansard is furniture only
                else if (industrialF) nWant = openN >= 200 ? 3 : (openN >= 80 ? 2 : 1);   // workshop workers
                else if (office)  nWant = openN >= 150 ? 2 : 1;
                else              nWant = (k % 3 == 0) ? 1 : 0;           // apartment buildings: a resident every other floor or two
                if (!priv && !panor && npcTotal >= (kinder ? 18 : (school || hospital ? 10 : 4))) nWant = 0;   // a 40-storey tower does not get overgrown with a crowd (a school may have a class of children)
                if (lair) nWant = 0;                                      // raiders live in a lair, not residents
                static const int kMerch[] = { PfButcher, PfFarmer, PfFisherman, PfFletcher, PfLeather,
                                              PfShepherd, PfToolsmith, PfCartographer, PfWeaponsmith };
                static const int kResid[] = { PfFarmer, PfShepherd, PfFisherman, PfMason, PfNitwit, PfNone,
                                              PfButcher, PfFletcher, PfLeather, PfToolsmith, PfLibrarian };
                auto findCell = [&](quint32 seed, int& ox, int& oz, int needRows) -> bool
                {   // a cell free for needRows cells in one of the rooms (the start and the traversal — by hash)
                    if (rooms.isEmpty()) return false;
                    const int r0 = int(seed % quint32(rooms.size()));
                    for (int rr = 0; rr < rooms.size(); ++rr)
                    {
                        const QVector<int>& cs = rooms[(r0 + rr) % rooms.size()];
                        const int c0 = int((seed >> 7) % quint32(cs.size()));
                        for (int t = 0; t < qMin(int(cs.size()), 120); ++t)
                        {
                            const int idx = cs[(c0 + t * 7) % cs.size()];
                            const int lx = idx % sx, lz = idx / sx;
                            bool ok = true;
                            for (int row = 0; row < needRows && ok; ++row) ok = cellFree(lx, lz, row);
                            if (ok) { ox = lx; oz = lz; return true; }
                        }
                    }
                    return false;
                };
                for (int n = 0; n < nWant; ++n)
                {
                    int lx = 0, lz = 0;
                    const quint32 hn = finHash(minx + n * 31, minz + k, miny + lyF, 0x7117u);
                    if (!findCell(hn, lx, lz, 3)) break;
                    const int bx = minx + lx, bz = minz + lz, byc = miny + lyF;
                    NpcSite ns;
                    ns.x = bx; ns.y = byc - sink; ns.z = bz;
                    ns.yaw = int((hn >> 11) % 4u) * 90;
                    ns.vtype = (hn >> 16) % 100u < 45u ? 0 : int((hn >> 20) % 7u);
                    ns.level = 1 + int((hn >> 24) % 4u);
                    if (panor)
                    {
                        const int roll = int(hn % 100u);
                        if (roll < 55) { ns.kind = 0; ns.prof = kMerch[(hn >> 3) % 9u]; }
                        else
                        {   // a wandering trader; in 45% of cases a llama nearby
                            ns.kind = 1;
                            int ax = 0, az = 0;
                            if (roll >= 78)
                                for (int d = 0; d < 4; ++d)
                                {
                                    ax = lx + dvec[d][0]; az = lz + dvec[d][1];
                                    if (cellFree(ax, az, 0) && cellFree(ax, az, 1)
                                        && !(ax == lx && az == lz))
                                    {
                                        NpcSite llama = ns;
                                        llama.kind = 2; llama.x = minx + ax; llama.z = minz + az;
                                        llama.variant = int((hn >> 5) % 4u);
                                        npcs->append(llama);
                                        occ.insert(dkey(minx + ax, byc, minz + az));
                                        break;
                                    }
                                }
                        }
                    }
                    else if (industrialF)
                    {   // the smiths of the workshop: an armorer, a weaponsmith, a toolsmith, a mason
                        static const int kSmith[] = { 0, PfWeaponsmith, PfToolsmith, PfMason };
                        ns.prof = kSmith[(hn >> 3) % 4u];
                    }
                    else if (church || hospital) ns.prof = PfCleric;
                    else if (school)
                    {
                        if (n == 0) ns.prof = (hn % 4u == 0) ? PfCartographer : PfLibrarian;     // the teacher
                        else if (obj.code == 44315000) { static const int kSci[] = { PfLibrarian, PfCartographer, PfToolsmith, PfMason }; ns.prof = kSci[(hn >> 3) % 4u]; }   // a scientific building: only adult scientists
                        else { ns.baby = true; ns.prof = PfNone; ns.level = 1; }                  // pupils
                    }
                    else if (kinder)
                    {
                        if (n == 0) ns.prof = PfLibrarian;                  // the "scholar" kindergarten teacher
                        else { ns.baby = true; ns.prof = PfNone; ns.level = 1; }   // children
                    }
                    else              ns.prof = kResid[(hn >> 3) % 11u];
                    if (priv && ((n == 1 && (hn >> 4) % 100u < 65u) || (n == 2 && (hn >> 12) % 100u < 45u)))
                    { ns.baby = true; ns.prof = PfNone; ns.level = 1; }   // children in a family: up to two
                    npcs->append(ns);
                    occ.insert(dkey(bx, byc, bz));
                    ++npcTotal;
                }
                // PETS of residential houses: in multi-storey buildings (apartment blocks, 44400000) — cats (up to 7 per building, about every second floor, sometimes two),
                // in the private sector — dogs (wolves: 1 per house with an 80% chance, in large houses sometimes a second).
                if (npcs && !lair && (resid || priv))
                {
                    static const char* const kCatVars[] = { "minecraft:tabby", "minecraft:black", "minecraft:red", "minecraft:siamese", "minecraft:british_shorthair",
                                                            "minecraft:calico", "minecraft:persian", "minecraft:ragdoll", "minecraft:white", "minecraft:jellie", "minecraft:all_black" };
                    int nPet = 0;
                    const quint32 hq = finHash(minx + 17, minz + k * 3, miny + lyF, 0x9E7A1u);
                    if (priv)
                    {
                        if (k == 0) nPet = (hq % 100u < 80u) ? ((openN >= 70 && (hq >> 8) % 100u < 35u) ? 2 : 1) : 0;
                    }
                    else if (petTotal < 7)
                        nPet = (hq % 100u < 55u) ? (((hq >> 8) % 100u < 25u) ? 2 : 1) : 0;
                    for (int pn = 0; pn < nPet && petTotal < (priv ? 2 : 7); ++pn)
                    {
                        int lx = 0, lz = 0;
                        const quint32 hpn = finHash(minx + pn * 41, minz + k + 7, miny + lyF, 0x9E7A2u);
                        if (!findCell(hpn, lx, lz, 2)) break;
                        NpcSite pet;
                        pet.kind = 5;
                        pet.animal = priv ? QStringLiteral("minecraft:wolf") : QStringLiteral("minecraft:cat");
                        if (!priv) pet.petVariant = QString::fromLatin1(kCatVars[(hpn >> 12) % 11u]);
                        pet.x = minx + lx; pet.z = minz + lz; pet.y = miny + lyF - sink;
                        pet.yaw = int((hpn >> 20) % 4u) * 90;
                        npcs->append(pet);
                        occ.insert(dkey(pet.x, miny + lyF, pet.z));
                        ++petTotal;
                    }
                }
                if (hospital)
                {   // patients: on beds (by the head part of the bed), up to 6 per floor; without a profession, in ordinary clothes
                    int np = 0;
                    for (int lz = 1; lz < sz - 1 && np < 6; ++lz)
                        for (int lx = 1; lx < sx - 1 && np < 6; ++lx)
                        {
                            const quint16 b0 = store.get(minx + lx, miny + lyF - sink, minz + lz);
                            if (b0 < IdBedBase || b0 >= IdBedMax || ((int(b0) - int(IdBedBase)) & 1) == 0) continue;     // the head part is needed
                            const quint32 hp = finHash(minx + lx, minz + lz, miny + lyF, 0x9A71u);
                            if (hp % 100u >= 75u) continue;
                            NpcSite ns;
                            ns.x = minx + lx; ns.z = minz + lz; ns.y = miny + lyF - sink; ns.yoff = 0.5625;
                            ns.yaw = int((hp >> 8) % 4u) * 90;
                            ns.prof = PfNone; ns.vtype = (hp >> 16) % 100u < 45u ? 0 : int((hp >> 20) % 7u); ns.level = 1;
                            npcs->append(ns);
                            ++np;
                        }
                }
                if (industrialF && !lair && k == 0 && finHash(minx, minz, miny, 0x601E3u) % 100u < 50u)
                {   // the workshop guard: an iron golem on the 1st floor (50% of buildings)
                    int lx = 0, lz = 0;
                    if (findCell(finHash(minx + 5, minz + 9, miny, 0x601E4u), lx, lz, 3))
                    {
                        NpcSite g; g.kind = 3; g.x = minx + lx; g.z = minz + lz; g.y = miny + lyF - sink; g.yaw = 0;
                        npcs->append(g);
                        occ.insert(dkey(g.x, miny + lyF, g.z));
                    }
                }
                if (lair)
                {   // raiders: A PER-FLOOR LIMIT (2..5 by floor area, not per building): pillagers 60%, vindicators 30%, an evoker 10%;
                    // the first raider of a lair is the standard-bearer (a pillager captain with an ominous banner on the head)
                    if (lairCapsLeft < 0)
                    {   // the number of standard-bearers grows with the building area (floor cells of the 1st floor inside the walls): 1 per ~350 cells, up to 8
                        int area = 0;
                        const int rowA = lowF ? lyLow + 1 : lyLow + 2;
                        for (int lz = 0; lz < sz; ++lz)
                            for (int lx = 0; lx < sx; ++lx)
                            {
                                const char g0 = grid[lz * sxy + rowA * sx + lx];
                                if (g0 == char(3) || g0 == char(4)) ++area;
                            }
                        lairCapsLeft = qBound(1, 1 + area / 350, 8);
                    }
                    const int capsPerFloor = std::max(1, (lairCapsLeft + std::max(1, nStories - k) - 1) / std::max(1, nStories - k));   // evenly over the remaining floors
                    const int nM = std::max(qBound(2, openN / 45, 5), capsPerFloor + 1);
                    int capsHere = 0;
                    for (int n = 0; n < nM; ++n)
                    {
                        int lx = 0, lz = 0;
                        const quint32 hn = finHash(minx + n * 37, minz + k * 5, miny + lyF, 0x1A15u);
                        if (!findCell(hn, lx, lz, 3)) break;
                        NpcSite m;
                        m.kind = 4;
                        const int rr = int(hn % 100u);
                        m.variant = rr < 60 ? 4 : (rr < 90 ? 5 : 6);          // pillager / vindicator / evoker
                        if (lairCapsLeft > 0 && capsHere < capsPerFloor) { m.variant = 4; m.captain = true; --lairCapsLeft; ++capsHere; }
                        m.x = minx + lx; m.z = minz + lz; m.y = miny + lyF - sink;
                        m.yaw = int((hn >> 11) % 4u) * 90;
                        npcs->append(m);
                        occ.insert(dkey(m.x, miny + lyF, m.z));
                    }
                }
            }
            // A KINDERGARTEN: the whole free floor is covered with multi-colored carpets (pieces of 2×2 of a random color); occupied cells (furniture, doors, stairs) are not touched.
            // We place it after the residents: they need free cells.
            if (obj.code == 44313000)
                for (const QVector<int>& cs : rooms)
                    for (int idx : cs)
                    {
                        const int lx = idx % sx, lz = idx / sx;
                        if (!cellFree(lx, lz, 0) || nearShaft(lx, lz)) continue;
                        const quint32 hc = finHash(minx + (lx >> 1), minz + (lz >> 1), miny + lyF, 0x4B1D5u);
                        int dm2 = 0;
                        put(lx, lz, 0, quint16(int(IdCarpetBase) + int(hc % quint32(kCarpetsCount))), &dm2);
                    }
        }
    }
}

void ObjectVoxelizer::buildLairBanners()
{
    // THE LAIR BANNERS OUTSIDE: on each compass side one ominous banner on the wall of the 1st floor above the door row
    // (2–3 rows above the floor), not on a window, the cell outside is free; closer to the middle of the facade.
    if (lair && banners)
    {
        static const int sdx[4] = { 0, 1, 0, -1 }, sdz[4] = { -1, 0, 1, 0 };
        const int lyB0 = (lowF ? lyLow + 1 : lyLow + 2) + 2;
        const int ccx = (fx0 + fx1) / 2, ccz = (fz0 + fz1) / 2;
        int bestScore[4] = { 1 << 20, 1 << 20, 1 << 20, 1 << 20 }, bx4[4] = {}, by4[4] = {}, bz4[4] = {};
        bool have[4] = { false, false, false, false };
        for (int cell : surfaceCells)
        {
            const int ly = (cell / sx) % sy;
            if ((ly != lyB0 && ly != lyB0 + 1) || grid[cell] != char(1)) continue;
            const int lx = cell % sx, lz = cell / sxy;
            if (windowDir(lx, ly, lz)) continue;
            for (int d = 0; d < 4; ++d)
            {
                const int ox = lx + sdx[d], oz = lz + sdz[d];
                const char go = (ox < 0 || ox >= sx || oz < 0 || oz >= sz) ? char(0) : grid[oz * sxy + ly * sx + ox];
                if (go != char(0) && go != char(2)) continue;
                if (store.get(minx + ox, miny + ly - sink, minz + oz) != IdAir) continue;
                const int sc2 = std::abs(sdx[d] == 0 ? lx - ccx : lz - ccz) * 4 + (ly - lyB0);
                if (sc2 < bestScore[d]) { bestScore[d] = sc2; bx4[d] = minx + ox; by4[d] = miny + ly; bz4[d] = minz + oz; have[d] = true; }
            }
        }
        for (int d = 0; d < 4; ++d)
            if (have[d])
            {
                addDecor(bx4[d], by4[d], bz4[d], quint16(int(IdWallBannerBase) + d));
                banners->append(BannerSite{ bx4[d], by4[d] - sink, bz4[d], d });
            }
    }
}

void ObjectVoxelizer::buildIsland()
{
    // A FLOATING ISLAND over a kindergarten (44313000): the center above the building, the semi-axes ~1.25 of the building size (the island is 2–3 building sizes), the height — above the roof.
    if ((obj.code == 44313000 || obj.code == 44312000 || obj.code == 44315000) && islands && obj.closed && sx >= 6 && sz >= 6)
    {
        IslandSite is;
        is.kind = obj.code == 44313000 ? 0 : 1;
        is.cx = minx + sx / 2; is.cz = minz + sz / 2;
        if (is.kind == 0) { is.rx = std::max(12, std::min(45, int(sx * 0.95))); is.rz = std::max(12, std::min(45, int(sz * 0.95))); }   // the island is a bit smaller than the building ×1: less shadow
        else { is.rx = sx / 2; is.rz = sz / 2; }                         // a school: the semi-axes of the building itself
        is.yTop = miny + sy - sink;
        is.hash = quint32(is.cx) * 374761393u ^ quint32(is.cz) * 668265263u ^ 0x151A9Du;
        islands->append(is);
    }
}

void ObjectVoxelizer::buildRaiderZone()
{
    // THE RAIDER SPAWN ZONE: only lairs in industrial zones and only in a large building. We take the largest rectangle of "deep" cells
    // of the 1st-floor interior (with a further inset of a cell from the walls), in height — from the floor to a row below the lowest roof above it: the zone is certainly
    // smaller than the building so that raiders do not spawn outside. Smaller than 8×8 or fewer than 300 cells inside — no zone.
    if (lair && zones && obj.code == 44340000 && floors && obj.closed && storyP > 0)
    {
        const int rowF = lowF ? lyLow + 1 : lyLow + 2;
        QVector<char> mask(sx * sz, 0);
        int nIn = 0;
        for (int lz = 2; lz < sz - 2; ++lz)
            for (int lx = 2; lx < sx - 2; ++lx)
            {
                bool all = true;
                for (int dz = -1; dz <= 1 && all; ++dz)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (grid[(lz + dz) * sxy + rowF * sx + lx + dx] != char(3)) { all = false; break; }
                if (all) { mask[lz * sx + lx] = 1; ++nIn; }
            }
        if (nIn >= 300)
        {   // the maximum rectangle by histograms
            QVector<int> hgt(sx, 0);
            int bestA = 0, bx0 = 0, bx1 = -1, bz0 = 0, bz1 = -1;
            for (int lz = 0; lz < sz; ++lz)
            {
                for (int lx = 0; lx < sx; ++lx) hgt[lx] = mask[lz * sx + lx] ? hgt[lx] + 1 : 0;
                QVector<int> stk;
                for (int lx = 0; lx <= sx; ++lx)
                {
                    const int h = (lx < sx) ? hgt[lx] : 0;
                    while (!stk.isEmpty() && hgt[stk.last()] >= h)
                    {
                        const int hh = hgt[stk.takeLast()];
                        const int left = stk.isEmpty() ? 0 : stk.last() + 1;
                        const int width = lx - left;
                        if (hh * width > bestA) { bestA = hh * width; bx0 = left; bx1 = lx - 1; bz0 = lz - hh + 1; bz1 = lz; }
                    }
                    stk.append(lx);
                }
            }
            if (bx1 - bx0 + 1 >= 8 && bz1 - bz0 + 1 >= 8)
            {
                int minTop = sy;
                for (int lz = bz0; lz <= bz1; ++lz)
                    for (int lx = bx0; lx <= bx1; ++lx)
                    {
                        int top = rowF;
                        while (top + 1 < sy && (grid[lz * sxy + (top + 1) * sx + lx] == char(3) || grid[lz * sxy + (top + 1) * sx + lx] == char(4))) ++top;
                        minTop = std::min(minTop, top);
                    }
                const int rowTop = minTop - 2;                                 // below the roof lining
                if (rowTop - rowF >= 3)
                {
                    LairZone z;
                    z.x0 = minx + bx0; z.x1 = minx + bx1; z.z0 = minz + bz0; z.z1 = minz + bz1;
                    z.y0 = miny + rowF - sink; z.y1 = miny + rowTop - sink;
                    for (int k = 0; k < 40; ++k)
                    {   // the floors of the stories (the walking rows) below the top of the zone: a tile 3 high on each
                        const int fy = z.y0 + k * storyP;
                        if (fy + 2 > z.y1) break;
                        z.floorsY.append(fy);
                    }
                    if (!z.floorsY.isEmpty()) zones->append(z);
                }
            }
        }
    }
}

void ObjectVoxelizer::buildBasement()
{
    // A BASEMENT (a request): in ~50% of private houses — a room from 18 cells (5×5, 5×4, 4×5, 6×3, 3×6 — the first that fits) with a ceiling of 2 blocks under the floor of the 1st floor, a descent by a vertical
    // ladder through a hatch opening in the floor. Inside either a chest with valuable loot or a heap of mobs. The place is chosen after the furniture:
    // 9 cells of the room — there is a floor, under it is empty (neither a foundation nor a past basement); the descent cell is free 2 blocks upward,
    // on the north side of it — the ladder holder (the house floor). The room itself is carved out later (the top of the ground is lowered).
    if ((priv || obj.code == 44319000 || obj.code == 44315000 || obj.code == 44312000 || deepRole) && basements && floors && storyP > 0 && sx >= 5 && sz >= 5)
    {
        auto bHash = [](int bx, int bz, int by, quint32 salt) -> quint32
        {
            quint32 h = quint32(bx) * 374761393u ^ quint32(bz) * 668265263u ^ quint32(by) * 2246822519u ^ salt;
            h ^= h >> 15; h *= 2246822519u; return h ^ (h >> 13);
        };
        const quint32 hb = bHash(minx, minz, miny, 0xBA5E7u);
        const bool churchB = (obj.code == 44319000) || deepRole == 1 || deepRole == 2;        // a church: a basement in 85% (private houses 50%), the floor level a row higher; a building chosen by a point on the map — always, whatever its code
        const bool labB = (obj.code == 44315000 || obj.code == 44312000) && (deepRole == 0 || deepRole == 3);       // deepRole 3 — one of the biggest schools: a laboratory is guaranteed            // a scientific building: an underground laboratory (as with a church — a 3×3 descent, everything else is built later)
        const bool deepB = churchB || labB;
        const int ly0 = deepB ? lyLow + 2 : lyLow + 1;
        const int Wy = miny + ly0 - sink;
        auto colOk = [&](int lx, int lz) -> bool
        {
            if (lx < 1 || lx >= sx - 1 || lz < 1 || lz >= sz - 1) return false;
            const char gc = grid[lz * sxy + ly0 * sx + lx];
            if (gc != char(3) && gc != char(4)) return false;
            const int bx = minx + lx, bz = minz + lz;
            if (store.get(bx, Wy - 1, bz) == IdAir) return false;              // there is no floor
            for (int y = Wy - 4; y <= (deepB ? Wy - 3 : Wy - 2); ++y)
                if (store.get(bx, y, bz) != IdAir) return false;               // under the floor is occupied (for a church the row Wy-2 is a foundation, we will cut it)
            return true;
        };
        // A church: the interior area of the 1st floor (to choose the largest ones for secret passages).
        int churchArea = 0;
        if (deepB)
            for (int lz = 0; lz < sz; ++lz)
                for (int lx = 0; lx < sx; ++lx)
                {
                    const char gc = grid[lz * sxy + ly0 * sx + lx];
                    if (gc == char(3) || gc == char(4)) ++churchArea;
                }
        if (deepRole || hb % 100u < (churchB ? 85u : (labB ? (obj.code == 44312000 ? 0u : 100u) : 50u)))       // schools get a laboratory only when they are chosen in advance (deepRole 3, see the reserve for the Ancient City)
        {
            static const int kSzP[5][2] = { { 5, 5 }, { 5, 4 }, { 4, 5 }, { 6, 3 }, { 3, 6 } };   // the width along X, the depth along Z
            static const int kSzC[5][2] = { { 3, 3 }, { 3, 3 }, { 3, 3 }, { 3, 3 }, { 3, 3 } };   // a church: only a descent is needed (the labyrinth is built later)
            const int (*kSz)[2] = deepB ? kSzC : kSzP;
            bool done = false;
            // two passes: first only places where around the hatch >= 8 of 9 cells are free (a 3×3 carpet will lie whole), then any
            for (int pass = 0; pass < 2 && !done; ++pass)
            for (int si = 0; si < 5 && !done; ++si)
            {
                const int rw = kSz[si][0], rd = kSz[si][1];
                const int nx = sx - 1 - rw, nz = sz - 1 - rd;                          // permissible top-left corners 1..n
                if (nx < 1 || nz < 1) continue;
                const int nCells = nx * nz;
                const int st = int((hb >> 8) % quint32(nCells));
                for (int t = 0; t < nCells && !done; ++t)
                {
                    const int idx = (st + t) % nCells;
                    const int lx0 = 1 + idx % nx, lz0 = 1 + idx / nx;
                    bool ok = true;
                    for (int dz = 0; dz < rd && ok; ++dz)
                        for (int dx = 0; dx < rw; ++dx)
                            if (!colOk(lx0 + dx, lz0 + dz)) { ok = false; break; }
                    if (!ok) continue;
                    const int hx = minx + lx0 + rw / 2, hz = minz + lz0;                // the descent: the north edge, the middle
                    if (store.get(hx, Wy, hz) != IdAir || store.get(hx, Wy + 1, hz) != IdAir) continue;
                    if (store.get(hx, Wy - 1, hz - 1) == IdAir) continue;               // the ladder holder (north)
                    bool busy = doorCells.contains(dkey(hx, miny + ly0, hz));           // a door opening — not ours
                    if (npcs) for (const NpcSite& q : *npcs) if (q.x == hx && q.z == hz && std::abs(q.y - Wy) < 3) busy = true;
                    if (busy) continue;
                    if (pass == 0)
                    {
                        int freeN = 0;
                        for (int dz = -1; dz <= 1; ++dz)
                            for (int dx = -1; dx <= 1; ++dx)
                                if (store.get(hx + dx, Wy, hz + dz) == IdAir && store.get(hx + dx, Wy - 1, hz + dz) != IdAir) ++freeN;
                        if (freeN < 8) continue;
                    }
                    BasementSite bs;
                    bs.x0 = minx + lx0; bs.x1 = minx + lx0 + rw - 1;
                    bs.z0 = minz + lz0; bs.z1 = minz + lz0 + rd - 1;
                    bs.cx = (bs.x0 + bs.x1) / 2; bs.cz = (bs.z0 + bs.z1) / 2;
                    bs.wy = Wy; bs.hx = hx; bs.hz = hz;
                    bs.church = churchB;
                    bs.lab = labB;
                    bs.school = labB && obj.code == 44312000;
                    bs.forced = deepRole == 3 ? 0 : deepRole;
                    bs.content = (hb >> 16) % 100u < (churchB ? 80u : 55u) ? 0 : 1;
                    bs.mobType = int((hb >> 24) % 4u);                 // a zombie / skeleton / spider / witch — 25% each
                    bs.area = churchArea;
                    bs.size = qint64(churchArea) * qint64(sy);
                    basements->append(bs);
                    done = true;
                }
            }
        }
    }
}

void ObjectVoxelizer::buildBeacons()
{
    // "Aviation beacons" (a user request): on buildings ABOVE 15 floors, starting with the 15th
    // and then every 5th (15-20-25-30) — on the facade, on a pair of DIAGONAL corners (which pair — by
    // the hash of the building and the tier) we hang a wall_torch near the ceiling of the floor (row +3 from the floor). The holder
    // is a real NON-GLASS wall block; the torch cell is a block OUTWARD and there must be
    // air there (otherwise the server will smear an unsupported wall_torch when loading the chunk). We scan
    // 3 columns from the corner along each face of the corner — a mullion/a corner will do, glass will not.
    if (floors && obj.closed && storyP > 0 && nStories > 15)
    {
        for (int k = 14; k < nStories; k += 5)
        {
            const int lyT = lyLow + 2 + k * storyP + 3;         // the torch tier — at the ceiling of floor k
            if (lyT > lyHigh - 2) continue;
            const int byS = miny + lyT - sink;                  // the store is written with "sink"
            const int c0 = int(lineHash(pairKey(qint64(minx) + k * 31, qint64(minz))) % 4u); // the corner pair c0 and c0^3
            // The building GRID is wider than the facade: on each side an air "margin" (for a 14 m tower the grid is
            // 17 columns: the facade lx=1..sx-2). The former anchors hx=minx/minx+sx-1 fell into the margin —
            // the holder was found on NO tier (zero beacons). We look for a wall by the SIGN of the grid
            // (char 1/4) probing columns 0..2 from the edge — it works with a margin of 0..2.
            auto faceScan = [&](bool xAxis, bool loSide, int latStart, int latDir) -> bool
            {   // xAxis — an X-facing face (the wall is the wall column along X, the scan along Z)
                const int nWall = xAxis ? sx : sz;              // the size along the face normal
                const int nLat = xAxis ? sz : sx;               // the size along the face
                for (int d = 0; d <= 2; ++d)
                {
                    const int wall = loSide ? d : nWall - 1 - d;
                    const int out = loSide ? wall - 1 : wall + 1;   // the torch cell — outward, into the margin
                    if (out < 0 || out >= nWall) continue;
                    for (int off = 0; off <= 2; ++off)
                    {
                        const int lat = latStart + off * latDir;
                        if (lat < 0 || lat >= nLat) continue;
                        const int cw = xAxis ? (lat * sxy + lyT * sx + wall)
                                             : (wall * sxy + lyT * sx + lat);
                        const char g = grid[cw];
                        if (g != char(1) && g != char(4)) continue; // glass/a hole/air — not a holder
                        const int wxo = minx + (xAxis ? out : lat);
                        const int wzo = minz + (xAxis ? lat : out);
                        // The torch cell must be empty BY THE STORAGE RECORD: the grid sign in
                        // the margin lies — the corner "glass" window trim draws char(2) there,
                        // although there are no blocks to write (a torch would hang legally).
                        if (store.get(wxo, byS, wzo) != IdAir) continue;
                        const quint16 tid = xAxis ? (loSide ? IdWallTorchW : IdWallTorchE)
                                                  : (loSide ? IdWallTorchN : IdWallTorchS);
                        addDecor(wxo, miny + lyT, wzo, tid);        // addVoxel would raise the ground with a column
                        ++added;
                        return true;
                    }
                }
                return false;
            };
            for (int cc = 0; cc < 2; ++cc)
            {
                const int corner = cc ? (c0 ^ 3) : c0;          // the corner 0..3: bit0 — the X side, bit1 — Z
                const bool xLo = (corner & 1) == 0, zLo = (corner & 2) == 0;
                faceScan(true,  xLo, zLo ? 1 : sz - 2, zLo ? 1 : -1);
                faceScan(false, zLo, xLo ? 1 : sx - 2, xLo ? 1 : -1);
            }
        }
    }
}

int ObjectVoxelizer::run()
{
    if (!buildGrid())
        return 0;

    floodFillBody();


    calcFloors();

    calcPatch();
    // The private sector has NO flat roof (a user request): the roof cap (lyHigh) and
    // the lining under it (lyHigh-1) in INTERIOR columns are not written — there is air up to the roof
    // slopes (the 2nd floor = the whole remainder, including the attic). The wall ring keeps the full
    // thickness up to the top (the gables/soffit attach to it as before). A column is "interior"
    // if there is a cavity "3" under the cell (by the wall below there is always 1/4: a lining or a window hole).
    // The floor = 1 block: the lining row of the slab (lyLow+1) is not written in the interior either.

    buildEntrances();
    buildWalls();
    buildLining();
    buildStairs();
    buildSlabs();
    buildCeiling();
    // The cells of door openings of partitions (world) — the "Interiors" pass keeps its
    // distance from them so that a cabinet does not grow in a doorway. The key: (x+32768)<<29 | (z+32768)<<12 | y.
    for (qint64 ek : entranceKeys) doorCells.insert(ek);   // entrances: furniture/light do not grow in the passage
    buildAddressPlate();
    buildPartitions();
    buildGableRoof();
    buildLight();
    buildInteriors();
    buildLairBanners();
    buildIsland();
    buildRaiderZone();
    buildBasement();
    buildBeacons();
    return added;
}
