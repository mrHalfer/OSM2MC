#pragma once
// The block registry: palettes, special identifiers, names and block states
// (the head of the old mcworldexporter.cpp, shared by all exporter stages).
#include <QColor>
#include <QPair>
#include <QString>
#include <QVector>

extern const char* kAir;
extern const char* kGrassBlock;
extern const char* kDirt;
extern const char* kStone;

struct NamedColor { const char* name; int r, g, b; };

//! Block identifiers in the "registry": 0=air, 1..3 ground, 4.. — the palette above.
enum : quint16 { IdAir = 0, IdGrass = 1, IdDirt = 2, IdStone = 3, IdPaletteStart = 4 };

extern const NamedColor kBlockPalette[];
extern const NamedColor kCarpets[];
extern const NamedColor kWools[];
struct FlowerName { const char* name; const char* flower; };
extern const FlowerName kFlowers[];
extern const char* const kBeds[];
extern const char* const kPaintMotifs[];
extern const char* const kArmorMats[];
extern const char* const kDoorNames[12];
extern const char* const kDirNames[4];
extern const int kDoorRgb[11][3];
extern const char* const kRailShapes[10];

// Entry counts. New blocks join kBlockPalette AT THE END — update kPaletteCount there too.
constexpr int kPaletteCount = 109;
constexpr int kRoadSlabsCount = 32;   // 16 concrete slabs + 16 wool slabs (the road step slabs)
constexpr int kCarpetsCount = 16;
constexpr int kWoolsCount = 16;
constexpr int kFlowersCount = 10;
constexpr int kBedsCount = 3;
constexpr int kPaintMotifsCount = 8;
constexpr int kArmorMatsCount = 5;

//! Special idents after the object palette: 16 carpets, then single vegetation/road blocks.
//! All are >= IdPaletteStart, i.e. they count as "object" blocks (they do not turn into ground, and they do get into
//! the spawn center of mass, though — roads/forests, which is correct on the whole).
enum : quint16
{
    IdCarpetBase = IdPaletteStart + kPaletteCount,   // IdCarpetBase + i = kCarpets[i]
    IdOakLog     = IdCarpetBase + kCarpetsCount,
    IdOakLeaves,
    IdGrassPlant,       //!< 'minecraft:short_grass' — a tuft of grass (in 1.21.2+ and 26.x 'grass' was renamed)
    IdRail,             //!< a rail without neighbors (the default shape none)
    IdRailEW,           //!< a rail along X (shape=east_west)
    IdRailNS,           //!< a rail along Z (shape=north_south)
    IdOakPlanks,
    IdGlass,
    IdIronBlock,          //!< minecraft:iron_block — metal/steel by the MAT column
    IdWoolBase,           // IdWoolBase + i = kWools[i]
    IdWater = int(IdWoolBase) + kWoolsCount,   // water (hydrography pits) — without properties: level=0 by default
    IdTallGrass,          // 'minecraft:tall_grass' the LOWER half (half=lower — the default, without Properties)
    IdTallGrassUpper,     // 'minecraft:tall_grass' the upper half (Properties half=upper)
    IdWheat,              // 'minecraft:wheat' ripe (Properties age=7) — giant fields (>100000 m²)
    IdChest,              // 'minecraft:chest' — a chest with loot inside buildings (default Properties)
    IdChestL,             // the same chest, type=left — the left half of a double chest (joined by vanilla)
    IdChestR,             // type=right — the right half (the vanilla connection: the right neighbor = east for facing=north)
    IdWallTorchN,         // 'minecraft:wall_torch' facing=north/east/south/west — a torch FROM a wall
    IdWallTorchE,         // (facing looks away from the holder wall, the holder is the neighbor on the opposite side)
    IdWallTorchS,
    IdWallTorchW,
    IdLanternHung,        // 'minecraft:lantern' hanging=true — a lamp under a ceiling and a lantern under the arm of a pole
    IdGateN,              // 'minecraft:oak_fence_gate' facing=north/east/south/west — a gate (closed — the default)
    IdGateE,
    IdGateS,
    IdGateW,
    IdCobbleWall,         // 'minecraft:cobblestone_wall' — the base of a lamp post (as in villages); up=true by default
    IdStairsN,            // 'minecraft:oak_stairs' facing — the steps of a pitched roof; THE PROPERTY IS MANDATORY:
    IdStairsE,            // the default is facing=east, slopes along the Z axis without the property would lie "the wrong way"
    IdStairsS,
    IdStairsW,            // (half=bottom/shape=straight/waterlogged — defaults, omitted)
    IdFence,              // 'minecraft:oak_fence' — the BASE: id = IdFence + conn, conn is a bitmask of neighbors
    IdFenceMax = int(IdFence) + 16,
                          // connections: north=1 (-Z), east=2 (+X), south=4 (+Z), west=8 (-X). The fence
                          // connections are a PERSISTENT block state property: without it the fence loads as "sticks"
                          // and only break+place recomputes them. The mask bit is set by the final pass
                          // (3.58) by the neighbors of the already written store.
    IdLadderN = int(IdFenceMax),  // 'minecraft:ladder' facing — a VERTICAL ladder on floors
    IdLadderE,                    // where a stepped flight did not fit; facing looks AWAY from the wall,
    IdLadderS,                    // the holder is a solid neighbor on the opposite side
    IdLadderW,
    IdStoneStairsN = int(IdLadderW) + 1,  // 'minecraft:stone_stairs' — inter-floor steps
    IdStoneStairsE,                       // (a user request) in all buildings EXCEPT
    IdStoneStairsS,                       // the private sector (the pitched roof codes — oak there)
    IdStoneStairsW,
    IdFlowerPotBase,                      // 'minecraft:flower_pot' flower=kFlowers[i] — a pot
    IdFlowerPotMax = int(IdFlowerPotBase) + kFlowersCount,   // on a windowsill behind glass (30%)
    IdFlowerBase = int(IdFlowerPotMax),   // 'minecraft:<color>' meadow flowers: +i = kFlowers[i]
    IdFlowerMax = int(IdFlowerBase) + kFlowersCount,
    IdLava = int(IdFlowerMax),            // 'minecraft:lava' — donor lava lakes in caves
                                          // (cave_templates.bin); without properties: level=0 (a source)
    IdSpawner,                            // 'minecraft:spawner' — monster spawners in buildings
                                          //  (a user request: every 15th "city" floor,
                                          //  50/50; the zombie/skeleton/spider type — in the block_entity, see
                                          //  SpawnerSite. ONE BLOCK: there are no zombie/skeleton_spawner names
                                          //  in the game — the server turned them into air)
    IdSlabBottom,                         // 'minecraft:oak_slab' type=bottom/top — the slopes of a gable
    IdSlabTop,                            // roof of the private sector (a request: "half blocks instead of stairs
                                          //  — a roof half as tall"; oak_stairs remained
                                          //  only on the inter-floor oak flights; the table top
                                          //  since v2 is an oak_pressure_plate, not a slab)
    IdBookshelf,                          // 'minecraft:bookshelf' — bookshelves (interiors)
    IdCraftingTable,                      // 'minecraft:crafting_table' — a crafting table
    IdCartographyTable,                   // 'minecraft:cartography_table' — a cartography table
    IdBarrel,                             // 'minecraft:barrel' — a barrel cabinet (facing=up — the default)
    IdFurnace,                            // 'minecraft:furnace' — a stove (facing/lit — defaults)
    IdLanternStanding,                    // 'minecraft:lantern' ON THE FLOOR/on a post (without properties)
    IdBedBase,                            // 'minecraft:<color>_bed': id = base + col*8 + dir*2 +
    IdBedMax = int(IdBedBase) + kBedsCount * 8,   //  part (col — the kBeds table, dir 0..3 = north/east/
                                          //  south/west, part 0=foot/1=head; occupied=false — the default)
    IdCauldron,                           // 'minecraft:cauldron' — water/dishes (layer/water — defaults)
    IdComposter,                          // 'minecraft:composter' (level=0 — the default)
    IdLoom,                               // 'minecraft:loom' (facing=north — the default)
    IdGrindstone,                         // 'minecraft:grindstone' (face=floor/attached — defaults)
    IdSmithingTable,                      // 'minecraft:smithing_table' (without properties)
    IdPressurePlateStone,                 // 'minecraft:stone_pressure_plate' — at the entrances of large malls
    IdPressurePlateOak,                   // 'minecraft:oak_pressure_plate' — the table top ON
                                          //  a fence post (a request: "instead of an oak slab — an oak
                                          //  pressure plate"; powered=false — the default)
    IdPainting,                           // 'minecraft:painting' +0..3 facing — a 1×1 painting on a wall
    IdPaintingMax = int(IdPainting) + 4,  //  (the motif is in block_entities, see PaintingSite)
    IdObsidian = int(IdPaintingMax),      // 'minecraft:obsidian' — a Nether portal frame (csrpol,
                                          //  a request: 10% of csrpol objects get a portal)
    IdCryingObsidian,                     // 'minecraft:crying_obsidian' — "ruined" portals
                                          //  (vanilla ruins mix in crying obsidian)
    IdPortalX,                            // 'minecraft:portal' axis=x — an active portal: axis
    IdPortalZ,                            //  = the direction of the frame's EXTENT (wiki): along X →
                                          //  axis=x, along Z → axis=z
    IdOakTrapdoor,                        // 'minecraft:oak_trapdoor' facing=north half=bottom open=false — a wooden basement hatch (under a carpet)
    IdIronTrapdoor,                       // 'minecraft:iron_trapdoor' facing=north half=bottom open=false — a basement hatch
    IdLeverCeil,                          // 'minecraft:lever' face=ceiling facing=north powered=false — opens the hatch from inside
    IdStoneBrickWall,                     // 'minecraft:stone_brick_wall' (default properties) — a street sign post
    IdAnvilBase,                          // 'minecraft:anvil' facing = N/E/S/W (+0..3) — anvils of industrial zones
    IdAnvilMax = int(IdAnvilBase) + 4,
    IdBlastFurnaceBase,                   // 'minecraft:blast_furnace' facing (+0..3), lit=false
    IdBlastFurnaceMax = int(IdBlastFurnaceBase) + 4,
    IdGrindstoneBase,                     // 'minecraft:grindstone' face=floor, facing (+0..3)
    IdGrindstoneMax = int(IdGrindstoneBase) + 4,
    IdStonecutterBase,                    // 'minecraft:stonecutter' facing (+0..3)
    IdStonecutterMax = int(IdStonecutterBase) + 4,
    IdLavaCauldron,                       // 'minecraft:lava_cauldron' — a cauldron with lava (without properties)
    IdBlackGlass,                         // 'minecraft:black_stained_glass' — dark windows of bandit lairs
    IdWallBannerBase,                     // 'minecraft:white_wall_banner' facing = N/E/S/W (+0..3); the ominous banner pattern is in the block entity
    IdWallBannerMax = int(IdWallBannerBase) + 4,
    IdHayBale,                            // 'minecraft:hay_block' axis=y — bandit lairs
    IdBlackstone,                         // 'minecraft:blackstone' (black) — a school blackboard
    IdCandleBase,                         // 'minecraft:candle' candles=1..4 lit=true: id = base + (candles-1) — candles on altars
    IdCandleMax = int(IdCandleBase) + 4,
    IdLecternBase,                        // 'minecraft:lectern' facing = N/E/S/W (+0..3), has_book=true — a lectern in a church with a book (the BE Book is in ChestSite::lectern)
    IdLecternMax = int(IdLecternBase) + 4,
    IdPodzol,                             // 'minecraft:podzol' — dirt/field/forest roads (instead of wool)
    IdDirtPath,                           // 'minecraft:dirt_path' — trampled ground under village squares
    IdCoarseDirt,                         // 'minecraft:coarse_dirt'
    IdWallSignBase,                       // 'minecraft:oak_wall_sign' facing = N/E/S/W (+0..3) — a house address plate
    IdWallSignMax = int(IdWallSignBase) + 4,
    IdStoneButtonBase,                    // 'minecraft:stone_button' face=wall powered=false, facing = N/E/S/W (+0..3) — a button by an iron door
    IdStoneButtonMax = int(IdStoneButtonBase) + 4,
    IdBell,                               // 'minecraft:bell' attachment=floor facing=north — the center of a "village" (a raid point)
    IdDoorBase,                           // doors: IdDoorBase + (type*4 + facing)*2 + half; type 0..11 (kDoorTypes), facing N/E/S/W
    IdDoorMax = int(IdDoorBase) + 12 * 4 * 2 * 2,         // id = Base + ((type*4 + facing)*2 + hinge)*2 + half
    IdRailShapeBase,                      // IdRailShapeBase + k, k=0..9: 'minecraft:rail' with the shape kRailShapes[k]
    IdRailShapeMax = int(IdRailShapeBase) + 10,
    IdPaleLog,                            // 'minecraft:pale_oak_log' (axis=y by default) — the Pale Garden forest
    IdPaleLeaves,                         // 'minecraft:pale_oak_leaves' distance=2 persistent=true
    IdPaleMossBlock,                      // 'minecraft:pale_moss_block'
    IdPaleMossCarpet,                     // 'minecraft:pale_moss_carpet' bottom=true, sides none
    IdPaleHangingMoss,                    // 'minecraft:pale_hanging_moss' tip=true — hangs from the crowns
    IdCreakingHeart,                      // 'minecraft:creaking_heart' axis=y, state=dormant, natural=true — in the trunks
    IdEyeOpen,                            // 'minecraft:open_eyeblossom'
    IdEyeClosed,                          // 'minecraft:closed_eyeblossom'
    IdJungleLeaves,                       // 'minecraft:jungle_leaves' distance=2 persistent=true — the tropics of the floating islands
    IdAcaciaLeaves,                       // 'minecraft:acacia_leaves' distance=2 persistent=true — trees of savannas and deserts (climate zones)
    IdSpruceLeaves,                       // 'minecraft:spruce_leaves' distance=2 persistent=true — spruces of the 71111110 forests
    IdBirchLeaves,                        // 'minecraft:birch_leaves' — birches around schools
    IdDarkOakLeaves,                      // 'minecraft:dark_oak_leaves' distance=2 persistent=true — dark forests (climate zones)
    IdRedPoplarLeaves,                    // 'minecraft:red_poplar_leaves' (26.x) — the "motley forest" around kindergartens
    IdOrangePoplarLeaves,
    IdYellowPoplarLeaves,
    IdLeafLitter,                         // 'minecraft:leaf_litter' facing=north segment_amount=2 — the litter of the motley forest
    IdFarmland,                           // 'minecraft:farmland' moisture=7 — greenhouse beds
    IdCarrots,                            // 'minecraft:carrots' age=7
    IdPotatoes,                           // 'minecraft:potatoes' age=7
    IdBeetroots,                          // 'minecraft:beetroots' age=3
    IdStoneFence,                         // 'minecraft:stone_brick_wall' — the BASE of a stone fence: id = IdStoneFence + conn
                                          //  (north=1, east=2, south=4, west=8; bit 16 — another wall stands on top, the fence
                                          //  is two storeys high: the sides become "tall" and upside_down=true), pass 3.58
    IdStoneFenceMax = int(IdStoneFence) + 32,
    IdPoweredRailShapeBase,               // IdPoweredRailShapeBase + k, k=0..9: 'minecraft:powered_rail'
                                          //  with the shape kRailShapes[k] (electrified rails of the metro tunnels)
    IdPoweredRailShapeMax = int(IdPoweredRailShapeBase) + 10,
    IdVine,                               // 'minecraft:vine' without properties (the game connects the faces itself) — jungle trees
    IdWaterFalling,                       // 'minecraft:water' level=8 (falling) — the streams running down a fountain post (3.575)
    IdBrickStairsN,                       // 'minecraft:stone_brick_stairs' facing — the flights of the metro
    IdBrickStairsE,                       //   entrance stair (stone under foot; the oak IdStairs* are the
    IdBrickStairsS,                       //   pitched-roof ones). Kept out of the IdStairsN..IdStairsW and
    IdBrickStairsW,                       //   IdStoneStairsN..IdStoneStairsW ranges used by the roof checks
    IdTorch,                              // 'minecraft:torch' ON THE FLOOR (without properties) — the metro
                                          //   stairwell ledges (a request: "hang torches on the column")
    IdJackLanternBase,                    // IdJackLanternBase + i = 'minecraft:jack_o_lantern' facing north/east/
                                          //   south/west — a lit jack o'lantern on a post by a grave cross (3.5)
    IdJackLanternMax = int(IdJackLanternBase) + 4,
    IdFurnaceBase = IdJackLanternMax,     // 'minecraft:furnace' facing = N/E/S/W (+0..3), lit=false — the stove of a
                                          //   private house turned to the room (fuel — in the block entity, see ChestSite::stove).
                                          //   ⚠ Ranges of multi-variant blocks (Base + i) never overlap: every Base gets
                                          //   its explicit Max line, the next Base starts AT the Max (a lost gap turned
                                          //   3 of 4 cemetery jack o'lanterns into furnaces — blockName checks ranges in order)
    IdFurnaceMax = int(IdFurnaceBase) + 4,
    IdBerryRipe,                          // 'minecraft:sweet_berry_bush' age=3 — a berry bush of the taiga undergrowth
                                          //   (the palette entry writes age=0, the game shows an empty green bush there)
    IdRoadSlabBase,                       // IdRoadSlabBase + i = kRoadSlabs[i] (0..15 — concrete slabs, 16..31 —
                                          // wool slabs): the half-block steps of the roads (type=bottom — the default).
                                          // A separate range, NOT kBlockPalette: nearestBlockId must not turn walls
                                          // into slabs (it matches only "_concrete" palette names)
    IdRoadSlabMax = int(IdRoadSlabBase) + kRoadSlabsCount,
    IdCaveBase,                           // IdCaveBase + i = the i-th state of the cave world palette (the name +
                                          //  properties from cave_templates.bin, see CaveSet) — MUST COME LAST
};

//! A block state from the cave world palette (donor structures: rails, fences, stairs... with properties).
struct CaveState
{
    QString name;
    QVector<QPair<QString, QString>> props;
};
// The state table of the active CaveSet (set in CaveSet::load, read from the chunk assembly threads).
extern const QVector<CaveState>* g_caveStates;

QString blockName(quint16 id);
void blockProperties(quint16 id, QVector<QPair<QString, QString>>& out);
quint16 nearestBlockId(const QColor& c);
quint16 nearestCarpetId(const QColor& c);
int paletteIndexOf(const char* name);
quint16 roadSlabForSurface(quint16 id);   // a concrete/wool roadway block -> the slab of the same color (0 — none)
quint16 roadSlabForCarpet(quint16 id);    // a marking/curb carpet -> the wool slab of the same color (0 — none)
bool isPaleForestCode(int code);
bool isForestExtraVeg(quint16 bid);
bool isVegBlockId(quint16 bid);
quint16 materialBlockId(const QString& material, const QColor& color);

