#ifndef WDFMCEXPORTER_H
#define WDFMCEXPORTER_H

#include <atomic>
#include <functional>
#include <QColor>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QVector>
#include <QVector3D>

/*!
 * \file   mcworldexporter.h
 * \brief  Export of loaded 3D objects into a Minecraft Java Edition save (a zip archive).
 *
 * Writes a standalone, fully self-sufficient save (a world folder inside a zip archive) in the NATIVE
 * Minecraft 26.x format (DataVersion 5023): level.dat + data/minecraft/world_gen_settings.dat +
 * dimensions/minecraft/overworld/region/*.mca (chunks in zlib, palettes as string lists). No
 * external libraries — gzip/zlib through qCompress(), NBT/zip/region are our own writers in the .cpp.
 *
 * The exported objects are simply triangle meshes in WORLD map coordinates with an HONEST height
 * (without zExaggeration — the calling code divides the vertices by it before passing them here). Each mesh
 * is voxelized (1 world unit = 1 block) into a block grid: the triangle surfaces are rasterized
 * into a voxel grid (sliding over Y layers + a convex clip), and if the mesh is closed (an extruded polygon with
 * walls/lid/bottom) the interior is completed by a flood fill from the outside. The object color
 * (the average over the vertices) is matched to the nearest concrete color of the Minecraft palette.
 *
 * The coordinates are centered to (0,0), the heights are shifted to fit into the range
 * 0..255 (Z is compressed if needed; a 26.x world extends to -64..319). The player spawn point
 * is placed above the center of mass of the object voxels — in the middle of the exported scene.
 * Under the affected objects the ground is completed column by column using heightSampler (terrain), and if there is no
 * terrain or the object is raised above it — down to the bottom of the object, and further with stone down to the world bottom (-64),
 * so that the models do not "hang".
 *
 * Export() is pure math/file I/O without QWidget/Qt3D — it is safe to run in a background
 * thread (see the tqt 3D window, exportMinecraftSave()).
 */
class McWorldExporter
{
public:
    //! One exported object — a triangle mesh in world coordinates (X,Y as on the map, Z is the
    //! honest height in the same units, without zExaggeration) + the average color + a closedness flag.
    struct McObject
    {
        //! The role of the object in the world (by "default" determined by the name of the source layer, see
        //! the tqt 3D window, exportMinecraftSave):
        //!  Building   (bldpol)          — a structure: a solid of the nearest concrete/brick by color;
        //!  Vegetation (vegpol/vgbpol)   — vegetation: with a height (REL) — a forest of trees, without — grass;
        //!                               the grass type by polygon area: >50000 m² — overgrown
        //!                               (tall_grass), >100000 m² — ripe wheat (wheat age=7);
        //!  Road       (rdslin)          — a road: the top face of the ribbon -> colored carpet by the layer color;
        //!  Railway    (rrslin)          — a railway: the top face of the ribbon -> rail;
        //!  Plate      (csrpol)          — with a height (REL) — a brick structure (like bldpol, but always
        //!                               brick); without a height — a "plate": over the polygon patch the top
        //!                               ground layer (turf) is replaced by wool of a random color;
        //!  Water      (hdrpol/hdrlin)   — hydrography: a pit with water over the polygon/ribbon patch,
        //!                               the depth is proportional to the area (1/6), but 3..15 blocks;
        //!                               for ribbons (hdrlin, linear) — three times shallower (1/18, 1..15);
        //!  Generic    (other layers)    — as before: a solid for closed ones, otherwise a surface.
        enum Kind { Generic = 0, Building = 1, Vegetation = 2, Road = 3, Railway = 4, Plate = 5, Water = 6,
                    ParkingLot = 7,
                    BlockCover = 8 };   //!< BlockCover (blkpol, only CODE 62231000 asphalt without markings and 45300000 square): a surface instead of turf — gray concrete / stone bricks
        //! ParkingLot (rdcpol) — a surface: dark gray concrete instead of turf + parking place markings; the outlines are in paths

        QVector<QVector3D> vertices;
        QVector<quint32> indices;       //!< triangle list
        QColor color;                   //!< the average object color (0..1 RGB) — matched to the concrete palette
        //! true if the mesh is a closed body (a polygon with walls/lid/bottom): then the interior
        //! is completed by a flood fill. Flat ribbons/lids ("On the ground", roads) — false: for them
        //! the flood fill would "close" the whole volume under the single face.
        bool closed = false;
        Kind kind = Generic;
        bool intraBlock = false;        //!< Vegetation from vgbpol (courtyard vegetation): without inserted structures (camps, outposts)
        //! For Vegetation — the honest extrusion height from the 3D window (meters, REL): if it is enough for
        //! a tree (>= ~3 blocks), trees are planted over the polygon patch, otherwise — grass.
        double extrudeH = 0.0;
        //! For Building — the material string from the "Material" column (the "Material from a column" mode,
        //! ModelAbsorptionMode::MaterialColumn). If non-empty and recognized — the block is taken from it;
        //! if empty/unknown — the nearest concrete by the object color is picked.
        QString material;
        //! For Water — the object is linear (hdrlin, a river/canal), not areal (hdrpol, a pond):
        //! the pit depth is taken three times smaller (ribbons are long and narrow, "area/6" would give a canyon).
        bool linear = false;
        //! For Building — the numeric value of the CODE column from the attribute (0 — no column/not a number).
        //! Some codes (44400002/44400100/44400110) ask for a GABLED roof made of stairs.
        int code = 0;
        //! For Building — the LIFT column == 1: walls 1 block thick instead of 2 (a request
        //! of the user; NOT the roof and NOT the foundation — only the walls, see the lining pass).
        bool lift = false;
        //! For Building — the base of the object is raised above the ground by this many meters (OSM building:part with min_height / building:min_level:
        //! a part hanging over a passage or standing on a lower part). 0 — an ordinary building from the ground. Such a part is built as a floating body: no foundation
        //! in the ground, no entrance, no basement, and the ground under it is not raised to it.
        double raised = 0.0;
        //! For Building — the dome of a church (OSM roof:shape=onion/dome): the export puts a yellow concrete onion dome with a golden spire on the roof.
        bool onionRoof = false;
        double roofH = 0.0;             //!< the height of the roof shape in meters (OSM roof:height): the dome, the rise of a skillion roof; 0 — by the radius / no slope
        //! For Building — a monopitch roof (OSM roof:shape=skillion): a wedge on top of the flat body, the roof falls toward the compass bearing roofDir (degrees clockwise from the north).
        bool skillionRoof = false;
        double roofDir = 0.0;
        QString roofColour, roofMaterial;   //!< OSM roof:colour / roof:material of a dome (the block, the spire, a glass hemisphere)
        //! For Building — the object is an OSM building:part (usually small next to the main building) / its walls are 1 block thick (small parts; set by the export).
        bool part = false;
        bool thin = false;
        //! For a glued complex (merged) — the columns (pairKey of the block x, z) that only raised parts cover: the ground under them is not raised to the body.
        QSet<qint64> floatCols;
        //! true — the object was assembled by the export from several touching industrial buildings (44340000): the inner walls between
        //! the parts are removed, the floors and partitions are shared by the whole complex.
        bool merged = false;
        //! For Building — the address: the STREET column (line 1 of the plate) and NUMBR+ADDNUM (line 2); empty — no plate.
        QString streetType, street, number;   //!< TYSTR (line 1), STREET (line 2), NUMBR+ADDNUM (line 3)
        //! For Railway — the AXIS lines of the track in map coordinates (the parts of a MultiLineString). The export lays a single
        //! rail line along them with correct shapes (straight, turns, slopes) and connections; for 61111000 — two
        //! parallel lines. Empty — the axis line is derived from the mesh ribbon (a straight line along the long axis).
        QVector<QVector<QPointF>> paths;
    };

    //! The terrain height at a world point (X,Y) — honest meters, -9999 for "no data" (the same contract
    //! as ModelTerrain::heightAt). An empty std::function — no terrain (flat ground).
    using HeightSampler = std::function<double(double, double)>;
    //! Export progress: done/total + the phase (0 — objects, 1 — ground and vegetation,
    //! 2 — chunk assembly, 3 — zip writing).
    //! Called from a background thread — the receiving side decides how to bring it to the GUI.
    using ProgressFn = std::function<void(int done, int total, int stage)>;

    //! The world OUTSIDE the exported area (our chunks are marked full and the game does not touch them,
    //! the rest is generated with these settings): normalSurroundings=false — the familiar superflat
    //! generator (the neighbors join our "ground crust" without steps); true — a normal
    //! seed-based noise world (terrain, caves, villages), but there will be cliffs at the export border.
    struct WorldOptions
    {
        quint64 seed = 1234567890ull;   //!< world seed (the shape of the surroundings and the future caves under the export)
        bool normalSurroundings = false;//!< true — minecraft:noise + structures, false — superflat
        //! The thickness of the "apron" — chunks of our ground around the rectangle of objects, beyond which
        //! the game-generated terrain begins (normalSurroundings only): 0 — the joint is right at the city
        //! (the generator completes everything itself, the export is noticeably smaller/faster), 8 — thin, 16 — full.
        int apronChunks = 16;
        //! Caves (a user request): fill the underground part of ALL generated chunks
        //! with "casts" from the game world (extract_caves.py) — repeats and crooked joints are acceptable,
        //! the main thing is the variety of caves underground.
        bool caves = false;
        QString cavesFile;                  //!< cave_templates.bin; empty — next to the exe
        //! Churches for the secret entrances to the stronghold and the Ancient City — by chunks (F3 format: "x y z; x y z" or "x z; x z"); empty — the two largest
        //! by "size" (area × height). The entrance is placed in the labyrinth of the church nearest to the chunk center.
        QString lairChurchChunks;
        //! Chunks (F3 format "x y z" or "x z") separately: the church with the entrance to the stronghold, the church with the entrance to the Ancient City,
        //! the Pale Garden (the forest nearest to the chunk center instead of the largest one). Empty — automatic.
        QString strongholdChunk, cityChunk, paleForestChunk;
        //! No forest/meadow with an allowed code (>= 2000 columns) — allocate an empty 15×15 chunk plot near the data for the Pale Garden
        //! (added in tMC: OSM data may contain no large forests). Not used in tqt by default.
        bool paleForestFallback = true;
        //! A datapack in the world (datapacks/tmc_lairs): separate structure sets so that /locate and the eye of ender find the stronghold, the Ancient City,
        //! the trial chambers and the ruined portals that the exporter wrote into the chunks. The structures themselves do not depend on it. Needed only for searching, but with
        //! custom structure sets the game shows an experimental features warning — hence it is optional.
        bool locateDatapack = true;
        //! Meters per one map coordinate unit along the X and Y axes (at the scene center). 1.0 — the units are meters
        //! (local CRSs, UTM). For Mercator EPSG:3395 at 56° N it is ≈0.565: without the correction the horizontal
        //! sizes are inflated 1.77 times (areas — 3.1 times), while the heights stay real meters. The 3D window computes
        //! the values from the project projection (the tqt 3D window, exportMinecraftSave) and puts them here.
        double unitsToMetersX = 1.0;
        double unitsToMetersY = 1.0;
        //! The player spawn point — in map coordinates (the view center of the 3D window/map). Without it the spawn is placed
        //! above the center of mass of all buildings (the middle of the whole area, which in large projects is far from where one works).
        bool hasSpawnPoint = false;
        double spawnMapX = 0.0;
        double spawnMapY = 0.0;
        //! The export boundary — a polygon in map coordinates (the data projection): only the chunks it touches are written into the world
        //! (the game generates the rest itself). Fewer than 3 vertices — no restriction. Buildings on the boundary stay whole in their chunks.
        QVector<QPointF> clipPolygon;
        //! The climate zone of the export area (export/climate.h, Climate::Zone): tree species, surface and chunk biome. 0 — temperate (as before).
        int climateZone = 0;
        //! The attribution of the source data (for OpenStreetMap — the ODbL line): if not empty, it is written into the root of the zip as ATTRIBUTION.txt
        QString attribution;
        //! The JSON info about this export (app/exportmeta.h): if not empty, it is written into the root of the zip (or of the unpacked saves folder) as export_info.json
        QString exportInfo;
        //! true — the world is written UNPACKED into the folder given as the output path (a saves folder of the launcher/an instance), no zip archive
        bool unpackedFolder = false;
    };

    struct Result
    {
        bool ok = false;
        bool cancelled = false;
        QString error;
        int objectCount = 0;    //!< how many objects were actually voxelized
        int skippedCount = 0;   //!< how many were skipped (an enormous extent, etc.)
        int blockCount = 0;     //!< how many blocks (including ground) were written
        int regionCount = 0;    //!< how many region files are in the archive
        qint64 zipBytes = 0;
        QString lairInfo;       //!< the result of the secret passages of the churches (stronghold / Ancient City) or the reason why there are none
    };

    //! The target world format: native 26.x or the old 1.21.x (before the version reform).
    enum class Format
    {
        Native26x = 0,  //!< Minecraft 26.x: DataVersion 5023, version=Int 19133, generation in
                        //!< data/minecraft/world_gen_settings.dat, chunks in dimensions/minecraft/...
        Legacy121x = 1, //!< Minecraft 1.18–1.21.x (checked against a 1.21.11 world): DataVersion 4671,
                        //!< WorldGenSettings inside level.dat, block palettes — List<Compound>{Name},
                        //!< chunks in region/ at the world root.
    };

    //! Builds the save archive at path (zip). levelName — the world caption in level.dat. cancel —
    //! an optional cancellation flag (checked between objects/regions). A synchronous blocking call
    //! — run it in a background thread. format — the world layout and DataVersion (see Format).
    static Result Export(const QVector<McObject>& objects,
                         const HeightSampler& heightAt,
                         const QString& path,
                         const QString& levelName,
                         const std::atomic_bool* cancel = nullptr,
                         Format format = Format::Native26x,
                         const ProgressFn& progress = ProgressFn(),
                         const WorldOptions& world = WorldOptions());
};

#endif // WDFMCEXPORTER_H
