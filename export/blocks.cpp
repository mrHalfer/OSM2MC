#include "export/blocks.h"
#include <QSet>
#include <climits>
#include <cstring>

// The arrays are extern in export/blocks.h: the exporter code indexes them without
// knowing their size, so the counts are constants there — the asserts at the bottom
// of this file catch a forgotten update when an array grows.

const char* kAir = "minecraft:air";
const char* kGrassBlock = "minecraft:grass_block";
const char* kDirt = "minecraft:dirt";
const char* kStone = "minecraft:stone";

const NamedColor kBlockPalette[] = {
    { "minecraft:white_concrete",       240, 240, 242 },
    { "minecraft:light_gray_concrete",  180, 180, 180 },
    { "minecraft:gray_concrete",        110, 110, 110 },
    { "minecraft:black_concrete",        25,  25,  27 },
    { "minecraft:brown_concrete",       120,  80,  55 },
    { "minecraft:red_concrete",         180,  55,  50 },
    { "minecraft:orange_concrete",      235, 130,  30 },
    { "minecraft:yellow_concrete",      240, 205,  65 },
    { "minecraft:lime_concrete",        120, 190,  65 },
    { "minecraft:green_concrete",        70, 130,  50 },
    { "minecraft:cyan_concrete",         35, 155, 150 },
    { "minecraft:light_blue_concrete",   80, 160, 215 },
    { "minecraft:blue_concrete",         60,  70, 165 },
    { "minecraft:purple_concrete",      140,  80, 190 },
    { "minecraft:magenta_concrete",     195,  85, 195 },
    { "minecraft:pink_concrete",        240, 150, 185 },
    { "minecraft:bricks",               150,  97,  84 },
    { "minecraft:stone",                125, 125, 125 },
    // Cave rocks/ores (cave_templates.bin from extract_caves.py) — only for the underground
    // palettes; nearestBlockId does not see them (the "_concrete" filter), so buildings do not turn into "stone".
    { "minecraft:cobblestone",           85,  85,  85 },
    { "minecraft:deepslate",             80,  80,  82 },
    { "minecraft:cobbled_deepslate",     70,  70,  72 },
    { "minecraft:polished_deepslate",    80,  80,  84 },
    { "minecraft:tuff",                 108, 104,  97 },
    { "minecraft:granite",              156,  99,  74 },
    { "minecraft:diorite",              188, 188, 188 },
    { "minecraft:andesite",             135, 135, 135 },
    { "minecraft:smooth_stone",         155, 155, 155 },
    { "minecraft:calcite",              223, 224, 220 },
    { "minecraft:smooth_basalt",         61,  64,  71 },
    { "minecraft:blackstone",            39,  34,  44 },
    { "minecraft:gravel",               131, 127, 126 },
    { "minecraft:sand",                 219, 207, 163 },
    { "minecraft:clay",                 165, 168, 178 },
    { "minecraft:coarse_dirt",          119,  85,  59 },
    { "minecraft:rootdirt",             104,  72,  51 },
    { "minecraft:mud",                  110,  98,  86 },
    { "minecraft:packed_mud",           142, 106,  79 },
    { "minecraft:ice",                  145, 183, 253 },
    { "minecraft:packed_ice",           140, 180, 250 },
    { "minecraft:blue_ice",             115, 118, 229 },
    { "minecraft:obsidian",              20,  17,  31 },
    { "minecraft:bedrock",               85,  85,  85 },
    { "minecraft:glowstone",            190, 160,  90 },
    { "minecraft:magma_block",          140,  80,  40 },
    { "minecraft:netherrack",           110,  54,  53 },
    { "minecraft:gold_block",           246, 208,  61 },
    { "minecraft:dripstone_block",      113,  95,  87 },
    { "minecraft:moss_block",           117, 143,  45 },
    { "minecraft:coal_ore",             116, 114, 110 },
    { "minecraft:iron_ore",             145, 127, 116 },
    { "minecraft:copper_ore",           120, 120, 105 },
    { "minecraft:gold_ore",             155, 145,  85 },
    { "minecraft:redstone_ore",         146,  70,  65 },
    { "minecraft:lapis_ore",            100, 110, 160 },
    { "minecraft:emerald_ore",           95, 150, 120 },
    { "minecraft:diamond_ore",          100, 160, 165 },
    { "minecraft:deepslate_coal_ore",    70,  70,  72 },
    { "minecraft:deepslate_iron_ore",    85,  80,  78 },
    { "minecraft:deepslate_copper_ore",  78,  76,  74 },
    { "minecraft:deepslate_gold_ore",    92,  86,  66 },
    { "minecraft:deepslate_redstone_ore",90,  60,  58 },
    { "minecraft:deepslate_lapis_ore",   66,  72, 100 },
    { "minecraft:deepslate_emerald_ore", 64,  88,  78 },
    { "minecraft:deepslate_diamond_ore", 62,  86,  90 },
    { "minecraft:raw_iron_block",       120, 100,  90 },
    { "minecraft:raw_copper_block",     105,  95,  88 },
    { "minecraft:raw_gold_block",       130, 110,  70 },
    { "minecraft:coal_block",            30,  30,  30 },
    { "minecraft:cobweb",               230, 230, 230 },
    // Floating islands over kindergartens (the tropics): slime, bamboo, a jungle tree, sugar cane, fern, lily pads
    { "minecraft:slime_block",          110, 190,  90 },
    { "minecraft:jungle_log",            85,  67,  25 },
    { "minecraft:bamboo",               100, 160,  50 },
    { "minecraft:sugar_cane",           150, 190, 100 },
    { "minecraft:fern",                  80, 130,  60 },
    { "minecraft:lily_pad",              40, 120,  40 },
    { "minecraft:spruce_log",            58,  37,  16 },
    { "minecraft:birch_log",            216, 215, 210 },
    { "minecraft:poplar_log",           110,  85,  60 },
    { "minecraft:sweet_berry_bush",      70, 110,  50 },
    { "minecraft:brown_mushroom",       145, 110,  80 },
    { "minecraft:red_mushroom",         200,  50,  45 },
    { "minecraft:stone_bricks",         122, 122, 122 },
    { "minecraft:beehive",              190, 150,  80 },
    { "minecraft:kelp",                  60, 130,  50 },
    { "minecraft:kelp_plant",            50, 120,  45 },
    { "minecraft:seagrass",              70, 140,  60 },
    // Climate zones (export/climate.h): acacia of savannas and deserts, the snow cover of tundra, dry bush
    { "minecraft:acacia_log",           103,  96,  86 },
    { "minecraft:snow_block",           249, 254, 254 },
    { "minecraft:dead_bush",            107,  78,  40 },
    // Domes: the spires by the colour of the dome
    { "minecraft:lapis_block",           31,  67, 140 },
    { "minecraft:diamond_block",        98, 237, 228 },
    { "minecraft:redstone_block",       175,  24,   5 },
    // Desert open lands: cacti (age=0 — the default, saved without properties)
    { "minecraft:cactus",                20, 122,  32 },
    // Dark forests (climate zones): the trunk of the dark oak (the leaves are a special id, see IdDarkOakLeaves)
    { "minecraft:dark_oak_log",          66,  43,  20 },
    // Point objects (3.575): the antenna of a communication tower and the weathered stone of memorials
    { "minecraft:quartz_block",         236, 231, 217 },
    { "minecraft:mossy_cobblestone",    112, 120,  88 },
    // White church domes (roof:colour=white — see the dome pass): polished quartz, whiter than any concrete
    { "minecraft:smooth_quartz",         240, 238, 228 },
    // Underground metro (61123100): the mixed masonry of the tunnel walls and floor
    { "minecraft:mossy_stone_bricks",    117, 117, 105 },
    { "minecraft:cracked_stone_bricks",  110, 110, 110 },
    { "minecraft:polished_andesite",     133, 133, 133 },
    { "minecraft:polished_granite",      153,  97,  72 },
    { "minecraft:polished_diorite",      197, 197, 197 },
    // Underground stations (44330000): chisel caps of the pylons and the statue head
    { "minecraft:chiseled_stone_bricks", 170, 170, 172 },
    // Metro vestibule vending machines (a request to furnish them) and the lab interior: polished blackstone
    { "minecraft:polished_blackstone",    38,  32,  43 },
    // Lighting masts (tower:type=lighting, 3.575): the polished calcite of the pole (glowstone is already above)
    { "minecraft:polished_calcite",      222, 222, 220 },
    // Cemeteries (csrpol 53510000): a carved pumpkin on the grass (the lit one is an enum id, see IdJackLanternBase)
    { "minecraft:carved_pumpkin",        196, 124,  18 },
    // Rail platforms (rdcpol 62134000): a half-block step over the stone-brick paving (a user request)
    { "minecraft:stone_brick_slab",      122, 122, 122 },
};



//! The carpet palette for roads — colors repeating the 16 wool colors (roads in the 3D window are gray/brown,
//! the choice is by the same nearest RGB distance as for concrete).
const NamedColor kCarpets[] = {
    { "minecraft:white_carpet",       235, 235, 235 },
    { "minecraft:orange_carpet",      216, 175,  92 },
    { "minecraft:magenta_carpet",     144, 100, 194 },
    { "minecraft:light_blue_carpet",  144, 159, 194 },
    { "minecraft:yellow_carpet",      211, 190,   9 },
    { "minecraft:lime_carpet",        112, 185,  25 },
    { "minecraft:pink_carpet",        237, 141, 172 },
    { "minecraft:gray_carpet",         69,  69,  69 },
    { "minecraft:light_gray_carpet", 155, 161, 161 },
    { "minecraft:cyan_carpet",         69, 124, 161 },
    { "minecraft:purple_carpet",      122,  48,  86 },
    { "minecraft:blue_carpet",         54,  61, 121 },
    { "minecraft:brown_carpet",       110,  71,  39 },
    { "minecraft:green_carpet",        76, 127,  37 },
    { "minecraft:red_carpet",         142,  60,  46 },
    { "minecraft:black_carpet",        20,  20,  20 },
};

//! The wool palette (csrpol plates without height — "instead of turf"). Exactly the carpet one (wool and
//! carpet in Minecraft have the same color), but a separate block: wool is a full block, laid over the ground surface
//! instead of grass turf.
const NamedColor kWools[] = {
    { "minecraft:white_wool",       235, 235, 235 },
    { "minecraft:orange_wool",      216, 175,  92 },
    { "minecraft:magenta_wool",     144, 100, 194 },
    { "minecraft:light_blue_wool",  144, 159, 194 },
    { "minecraft:yellow_wool",      211, 190,   9 },
    { "minecraft:lime_wool",        112, 185,  25 },
    { "minecraft:pink_wool",        237, 141, 172 },
    { "minecraft:gray_wool",         69,  69,  69 },
    { "minecraft:light_gray_wool", 155, 161, 161 },
    { "minecraft:cyan_wool",         69, 124, 161 },
    { "minecraft:purple_wool",      122,  48,  86 },
    { "minecraft:blue_wool",         54,  61, 121 },
    { "minecraft:brown_wool",       110,  71,  39 },
    { "minecraft:green_wool",        76, 127,  37 },
    { "minecraft:red_wool",         142,  60,  46 },
    { "minecraft:black_wool",        20,  20,  20 },
};

//! Meadow flowers (a user request): scattered over grass polygons "without height" instead of
//! part of the short grass; the values of the pot's flower property (a windowsill) are the same names.
//! 1.21+ (flattening 24w21a) and 26.x carry them as separate ids; the names were verified by probing the servers.
const FlowerName kFlowers[] = {
    { "minecraft:dandelion",    "dandelion"    },
    { "minecraft:poppy",        "poppy"        },
    { "minecraft:cornflower",   "cornflower"   },
    { "minecraft:allium",       "allium"       },
    { "minecraft:azure_bluet",  "azure_bluet"  },
    { "minecraft:oxeye_daisy",  "oxeye_daisy"  },
    { "minecraft:red_tulip",    "red_tulip"    },
    { "minecraft:white_tulip",  "white_tulip"  },
    { "minecraft:orange_tulip", "orange_tulip" },
    { "minecraft:pink_tulip",   "pink_tulip"   },
};

//! The bed colors for interiors (IdBedBase + col*8 + dir*2 + part, see the enum).
const char* const kBeds[] = { "minecraft:red_bed", "minecraft:white_bed",
                                     "minecraft:blue_bed" };

//! The painting motifs — only of SIZE 1×1 (kebab/match/pumpkin), so that a painting definitely
//! does not overlap a window/door opening (the user's remark: "they have a size").
//! The 1×1 paintings (the sizes were checked against the server's painting_variant/*.json): other motifs are bigger than a cell and
//! would overlap neighboring blocks.
const char* const kPaintMotifs[] = { "minecraft:kebab", "minecraft:aztec", "minecraft:alban",
                                            "minecraft:aztec2", "minecraft:bomb", "minecraft:plant",
                                            "minecraft:wasteland", "minecraft:meditative" };

//! The armor materials of the mall mannequins (item ids <material>_<part>; "gold" is golden_).
const char* const kArmorMats[] = { "leather", "chainmail", "iron", "golden", "diamond" };


//! The state table of the active CaveSet (set in CaveSet::load, read from the chunk assembly threads).
const QVector<CaveState>* g_caveStates = nullptr;

//! The entrance doors: the type (wood — by the building color, 11 — an iron "locked" one), the compass sides as in dvec: 0=N,1=E,2=S,3=W.
const char* const kDoorNames[12] = {
    "minecraft:oak_door", "minecraft:spruce_door", "minecraft:birch_door", "minecraft:jungle_door",
    "minecraft:acacia_door", "minecraft:dark_oak_door", "minecraft:mangrove_door", "minecraft:cherry_door",
    "minecraft:crimson_door", "minecraft:warped_door", "minecraft:bamboo_door", "minecraft:iron_door" };
const char* const kDirNames[4] = { "north", "east", "south", "west" };
//! The approximate plank color (for matching a door to the building material), the order as in kDoorNames[0..10].
const int kDoorRgb[11][3] = { { 162, 130, 78 }, { 114, 84, 48 }, { 192, 175, 121 }, { 160, 115, 81 },
                                     { 168, 90, 50 }, { 66, 43, 20 }, { 117, 54, 48 }, { 226, 178, 172 },
                                     { 101, 48, 70 }, { 43, 104, 99 }, { 193, 173, 80 } };
QString blockName(quint16 id)
{
    switch (id)
    {
    case IdAir:       return QLatin1String(kAir);
    case IdGrass:     return QLatin1String(kGrassBlock);
    case IdDirt:      return QLatin1String(kDirt);
    case IdStone:     return QLatin1String(kStone);
    case IdOakLog:    return QLatin1String("minecraft:oak_log");
    case IdOakLeaves: return QLatin1String("minecraft:oak_leaves");
    case IdGrassPlant:return QLatin1String("minecraft:short_grass");
    case IdRail:
    case IdRailEW:
    case IdRailNS:    return QLatin1String("minecraft:rail");
    case IdOakPlanks: return QLatin1String("minecraft:oak_planks");
    case IdIronBlock: return QLatin1String("minecraft:iron_block");
    case IdGlass:     return QLatin1String("minecraft:glass");
    case IdWater:     return QLatin1String("minecraft:water");
    case IdWaterFalling: return QLatin1String("minecraft:water");   // level=8 — see blockProperties
    case IdLava:      return QLatin1String("minecraft:lava");
    case IdSpawner:        return QLatin1String("minecraft:spawner");
    case IdTallGrass:
    case IdTallGrassUpper: return QLatin1String("minecraft:tall_grass");
    case IdWheat:     return QLatin1String("minecraft:wheat");
    case IdFarmland:  return QLatin1String("minecraft:farmland");
    case IdCarrots:   return QLatin1String("minecraft:carrots");
    case IdPotatoes:  return QLatin1String("minecraft:potatoes");
    case IdBeetroots: return QLatin1String("minecraft:beetroots");
    case IdChest:
    case IdChestL:
    case IdChestR:    return QLatin1String("minecraft:chest");
    case IdWallTorchN:
    case IdWallTorchE:
    case IdWallTorchS:
    case IdWallTorchW: return QLatin1String("minecraft:wall_torch");
    case IdLanternHung: return QLatin1String("minecraft:lantern");
    case IdGateN:
    case IdGateE:
    case IdGateS:
    case IdGateW:       return QLatin1String("minecraft:oak_fence_gate");
    case IdCobbleWall:  return QLatin1String("minecraft:cobblestone_wall");
    case IdStairsN:
    case IdStairsE:
    case IdStairsS:
    case IdStairsW:       return QLatin1String("minecraft:oak_stairs");
    case IdSlabBottom:
    case IdSlabTop:       return QLatin1String("minecraft:oak_slab");
    case IdBookshelf:     return QLatin1String("minecraft:bookshelf");
    case IdCraftingTable: return QLatin1String("minecraft:crafting_table");
    case IdCartographyTable: return QLatin1String("minecraft:cartography_table");
    case IdBarrel:        return QLatin1String("minecraft:barrel");
    case IdFurnace:       return QLatin1String("minecraft:furnace");
    case IdLanternStanding: return QLatin1String("minecraft:lantern");
    case IdTorch: return QLatin1String("minecraft:torch");
    case IdCauldron:      return QLatin1String("minecraft:cauldron");
    case IdComposter:     return QLatin1String("minecraft:composter");
    case IdLoom:          return QLatin1String("minecraft:loom");
    case IdGrindstone:    return QLatin1String("minecraft:grindstone");
    case IdSmithingTable: return QLatin1String("minecraft:smithing_table");
    case IdPressurePlateOak: return QLatin1String("minecraft:oak_pressure_plate");
    case IdBell: return QLatin1String("minecraft:bell");
    case IdIronTrapdoor: return QLatin1String("minecraft:iron_trapdoor");
    case IdOakTrapdoor: return QLatin1String("minecraft:oak_trapdoor");
    case IdLeverCeil: return QLatin1String("minecraft:lever");
    case IdPressurePlateStone: return QLatin1String("minecraft:stone_pressure_plate");
    case IdObsidian:         return QLatin1String("minecraft:obsidian");
    case IdCryingObsidian:   return QLatin1String("minecraft:crying_obsidian");
    case IdPortalX:
    case IdPortalZ:          return QLatin1String("minecraft:portal");
    case IdStoneStairsN:
    case IdStoneStairsE:
    case IdStoneStairsS:
    case IdStoneStairsW:  return QLatin1String("minecraft:stone_stairs");
    case IdBrickStairsN:
    case IdBrickStairsE:
    case IdBrickStairsS:
    case IdBrickStairsW:  return QLatin1String("minecraft:stone_brick_stairs");
    case IdLadderN:
    case IdLadderE:
    case IdLadderS:
    case IdLadderW:       return QLatin1String("minecraft:ladder");
    default:          break;
    }
    if (id >= IdFence && id < IdFenceMax)          // a fence: IdFence + connection bits (see the enum)
        return QLatin1String("minecraft:oak_fence");
    if (id >= IdStoneFence && id < IdStoneFenceMax)
        return QLatin1String("minecraft:stone_brick_wall");
    if (id == IdStoneBrickWall) return QLatin1String("minecraft:stone_brick_wall");
    if (id >= IdWallBannerBase && id < IdWallBannerMax) return QLatin1String("minecraft:white_wall_banner");
    if (id == IdHayBale) return QLatin1String("minecraft:hay_block");
    if (id == IdBlackGlass) return QLatin1String("minecraft:black_stained_glass");
    if (id >= IdAnvilBase && id < IdAnvilMax) return QLatin1String("minecraft:anvil");
    if (id >= IdBlastFurnaceBase && id < IdBlastFurnaceMax) return QLatin1String("minecraft:blast_furnace");
    if (id >= IdGrindstoneBase && id < IdGrindstoneMax) return QLatin1String("minecraft:grindstone");
    if (id >= IdStonecutterBase && id < IdStonecutterMax) return QLatin1String("minecraft:stonecutter");
    if (id == IdLavaCauldron) return QLatin1String("minecraft:lava_cauldron");
    if (id == IdBlackstone) return QLatin1String("minecraft:blackstone");
    if (id == IdPaleLog) return QLatin1String("minecraft:pale_oak_log");
    if (id == IdPaleLeaves) return QLatin1String("minecraft:pale_oak_leaves");
    if (id == IdPaleMossBlock) return QLatin1String("minecraft:pale_moss_block");
    if (id == IdPaleMossCarpet) return QLatin1String("minecraft:pale_moss_carpet");
    if (id == IdPaleHangingMoss) return QLatin1String("minecraft:pale_hanging_moss");
    if (id == IdCreakingHeart) return QLatin1String("minecraft:creaking_heart");
    if (id == IdEyeOpen) return QLatin1String("minecraft:open_eyeblossom");
    if (id == IdEyeClosed) return QLatin1String("minecraft:closed_eyeblossom");
    if (id == IdJungleLeaves) return QLatin1String("minecraft:jungle_leaves");
    if (id == IdAcaciaLeaves) return QLatin1String("minecraft:acacia_leaves");
    if (id == IdSpruceLeaves) return QLatin1String("minecraft:spruce_leaves");
    if (id == IdBirchLeaves) return QLatin1String("minecraft:birch_leaves");
    if (id == IdDarkOakLeaves) return QLatin1String("minecraft:dark_oak_leaves");
    if (id >= IdJackLanternBase && id < IdJackLanternBase + 4) return QLatin1String("minecraft:jack_o_lantern");
    if (id == IdRedPoplarLeaves) return QLatin1String("minecraft:red_poplar_leaves");
    if (id == IdOrangePoplarLeaves) return QLatin1String("minecraft:orange_poplar_leaves");
    if (id == IdYellowPoplarLeaves) return QLatin1String("minecraft:yellow_poplar_leaves");
    if (id == IdLeafLitter) return QLatin1String("minecraft:leaf_litter");
    if (id == IdVine) return QLatin1String("minecraft:vine");
    if (id >= IdCandleBase && id < IdCandleMax) return QLatin1String("minecraft:candle");
    if (id >= IdLecternBase && id < IdLecternMax) return QLatin1String("minecraft:lectern");
    if (id == IdPodzol) return QLatin1String("minecraft:podzol");
    if (id == IdDirtPath) return QLatin1String("minecraft:dirt_path");
    if (id == IdCoarseDirt) return QLatin1String("minecraft:coarse_dirt");
    if (id >= IdWallSignBase && id < IdWallSignMax)
        return QLatin1String("minecraft:oak_wall_sign");
    if (id >= IdStoneButtonBase && id < IdStoneButtonMax)
        return QLatin1String("minecraft:stone_button");
    if (id >= IdDoorBase && id < IdDoorMax)
        return QLatin1String(kDoorNames[(int(id) - int(IdDoorBase)) / 16]);
    if (id >= IdRailShapeBase && id < IdRailShapeMax)
        return QLatin1String("minecraft:rail");
    if (id >= IdPoweredRailShapeBase && id < IdPoweredRailShapeMax)
        return QLatin1String("minecraft:powered_rail");
    if (id >= IdBedBase && id < IdBedMax)          // a bed: the color from the kBeds table
        return QLatin1String(kBeds[(int(id) - int(IdBedBase)) / 8]);
    if (id >= IdPainting && id < IdPaintingMax)    // a painting: the motif is in the BE, only facing here
        return QLatin1String("minecraft:painting");
    if (id >= IdFlowerPotBase && id < IdFlowerPotMax)   // a pot: the flower in the pot is a separate block potted_<flower>
    {
        static const char* const kPotted[10] = {
            "minecraft:potted_dandelion", "minecraft:potted_poppy", "minecraft:potted_cornflower",
            "minecraft:potted_allium", "minecraft:potted_azure_bluet", "minecraft:potted_oxeye_daisy",
            "minecraft:potted_red_tulip", "minecraft:potted_white_tulip", "minecraft:potted_orange_tulip",
            "minecraft:potted_pink_tulip" };
        return QLatin1String(kPotted[(int(id) - int(IdFlowerPotBase)) % 10]);
    }
    if (id >= IdFlowerBase && id < IdFlowerMax)
        return QLatin1String(kFlowers[int(id) - int(IdFlowerBase)].name);
    if (id >= IdCarpetBase && id < IdCarpetBase + kCarpetsCount)
        return QLatin1String(kCarpets[int(id) - int(IdCarpetBase)].name);
    if (id >= IdWoolBase && id < IdWoolBase + kWoolsCount)
        return QLatin1String(kWools[int(id) - int(IdWoolBase)].name);
    const int i = int(id) - int(IdPaletteStart);
    if (i >= 0 && i < kPaletteCount)
        return QLatin1String(kBlockPalette[i].name);
    if (id >= IdCaveBase && g_caveStates && int(id) - int(IdCaveBase) < g_caveStates->size())
        return (*g_caveStates)[int(id) - int(IdCaveBase)].name;
    return QLatin1String(kAir);
}

//! The block state properties for the palette. Checked against chunks re-saved by the 26.3 and
//! 1.21.11 servers (setblock probes rail/oak_leaves): the values are strings, default blocks are written without
//! Properties. The leaves are persistent=true so that a tree does not fall apart without a "proper" distance;
//! the rails — oriented by the neighboring columns of the track (otherwise all rails lie "idle").
//! The rail shapes: indices 0..9 (N=north -Z, E=east +X, S=south +Z, W=west -X).
const char* const kRailShapes[10] = { "north_south", "east_west", "ascending_east", "ascending_west",
                                             "ascending_north", "ascending_south", "south_east", "south_west",
                                             "north_west", "north_east" };
void blockProperties(quint16 id, QVector<QPair<QString, QString>>& out)
{
    out.clear();
    if (id == IdWaterFalling)
    {   // falling water (the fountain streams in 3.575): level=8 is the "falling straight down" state
        out << qMakePair(QStringLiteral("level"), QStringLiteral("8"));
        return;
    }
    if (id >= IdWallSignBase && id < IdWallSignMax)
    {
        out << qMakePair(QStringLiteral("facing"), QString::fromLatin1(kDirNames[(int(id) - int(IdWallSignBase)) & 3]))
            << qMakePair(QStringLiteral("waterlogged"), QStringLiteral("false"));
        return;
    }
    if (id >= IdStoneButtonBase && id < IdStoneButtonMax)
    {
        out << qMakePair(QStringLiteral("face"), QStringLiteral("wall"))
            << qMakePair(QStringLiteral("facing"), QString::fromLatin1(kDirNames[(int(id) - int(IdStoneButtonBase)) & 3]))
            << qMakePair(QStringLiteral("powered"), QStringLiteral("false"));
        return;
    }
    if (id >= IdWallBannerBase && id < IdWallBannerMax)
    {
        out << qMakePair(QStringLiteral("facing"), QString::fromLatin1(kDirNames[(int(id) - int(IdWallBannerBase)) & 3]));
        return;
    }
    if (id >= IdJackLanternBase && id < IdJackLanternBase + 4)
    {   // a jack o'lantern: the carved face looks to `facing` (a cemetery lantern post)
        out << qMakePair(QStringLiteral("facing"), QString::fromLatin1(kDirNames[int(id) - int(IdJackLanternBase)]));
        return;
    }
    if (id >= IdAnvilBase && id < IdAnvilMax)
    {
        out << qMakePair(QStringLiteral("facing"), QString::fromLatin1(kDirNames[(int(id) - int(IdAnvilBase)) & 3]));
        return;
    }
    if (id >= IdBlastFurnaceBase && id < IdBlastFurnaceMax)
    {
        out << qMakePair(QStringLiteral("facing"), QString::fromLatin1(kDirNames[(int(id) - int(IdBlastFurnaceBase)) & 3]))
            << qMakePair(QStringLiteral("lit"), QStringLiteral("false"));
        return;
    }
    if (id >= IdGrindstoneBase && id < IdGrindstoneMax)
    {
        out << qMakePair(QStringLiteral("face"), QStringLiteral("floor"))
            << qMakePair(QStringLiteral("facing"), QString::fromLatin1(kDirNames[(int(id) - int(IdGrindstoneBase)) & 3]));
        return;
    }
    if (id >= IdStonecutterBase && id < IdStonecutterMax)
    {
        out << qMakePair(QStringLiteral("facing"), QString::fromLatin1(kDirNames[(int(id) - int(IdStonecutterBase)) & 3]));
        return;
    }
    if (id == IdHayBale)
    {
        out << qMakePair(QStringLiteral("axis"), QStringLiteral("y"));
        return;
    }
    if (id >= IdLecternBase && id < IdLecternMax)
    {
        out << qMakePair(QStringLiteral("facing"), QString::fromLatin1(kDirNames[(int(id) - int(IdLecternBase)) & 3]))
            << qMakePair(QStringLiteral("has_book"), QStringLiteral("true"))
            << qMakePair(QStringLiteral("powered"), QStringLiteral("false"));
        return;
    }
    if (id >= IdCandleBase && id < IdCandleMax)
    {
        out << qMakePair(QStringLiteral("candles"), QString::number(int(id) - int(IdCandleBase) + 1))
            << qMakePair(QStringLiteral("lit"), QStringLiteral("true"))
            << qMakePair(QStringLiteral("waterlogged"), QStringLiteral("false"));
        return;
    }
    if (id == IdIronTrapdoor || id == IdOakTrapdoor)
    {
        out << qMakePair(QStringLiteral("facing"), QStringLiteral("north"))
            << qMakePair(QStringLiteral("half"), QStringLiteral("bottom"))
            << qMakePair(QStringLiteral("open"), QStringLiteral("false"))
            << qMakePair(QStringLiteral("powered"), QStringLiteral("false"))
            << qMakePair(QStringLiteral("waterlogged"), QStringLiteral("false"));
        return;
    }
    if (id == IdLeverCeil)
    {
        out << qMakePair(QStringLiteral("face"), QStringLiteral("ceiling"))
            << qMakePair(QStringLiteral("facing"), QStringLiteral("north"))
            << qMakePair(QStringLiteral("powered"), QStringLiteral("false"));
        return;
    }
    if (id == IdBell)
    {   // a bell on the floor, not ringing
        out << qMakePair(QStringLiteral("attachment"), QStringLiteral("floor"))
            << qMakePair(QStringLiteral("facing"), QStringLiteral("north"))
            << qMakePair(QStringLiteral("powered"), QStringLiteral("false"));
        return;
    }
    if (id >= IdDoorBase && id < IdDoorMax)
    {   // a door: facing, half, hinge on the left, closed
        const int k = int(id) - int(IdDoorBase);
        out << qMakePair(QStringLiteral("facing"), QLatin1String(kDirNames[(k / 4) % 4]))
            << qMakePair(QStringLiteral("half"), (k & 1) ? QStringLiteral("upper") : QStringLiteral("lower"))
            << qMakePair(QStringLiteral("hinge"), (k & 2) ? QStringLiteral("right") : QStringLiteral("left"))
            << qMakePair(QStringLiteral("open"), QStringLiteral("false"))
            << qMakePair(QStringLiteral("powered"), QStringLiteral("false"));
        return;
    }
    if (id >= IdRailShapeBase && id < IdRailShapeMax)
    {   // a rail: the shape by index (straight / turns / slopes)
        out << qMakePair(QStringLiteral("shape"), QLatin1String(kRailShapes[int(id) - int(IdRailShapeBase)]))
            << qMakePair(QStringLiteral("waterlogged"), QStringLiteral("false"));
        return;
    }
    if (id >= IdPoweredRailShapeBase && id < IdPoweredRailShapeMax)
    {   // an electrified rail (the metro tunnels): the same shapes as a plain rail, powered=false by default
        out << qMakePair(QStringLiteral("shape"), QLatin1String(kRailShapes[int(id) - int(IdPoweredRailShapeBase)]))
            << qMakePair(QStringLiteral("waterlogged"), QStringLiteral("false"));
        return;
    }
    if (id >= IdCaveBase)
    {   // a state from the cave world: the properties are written in the file itself
        if (g_caveStates && int(id) - int(IdCaveBase) < g_caveStates->size())
            out = (*g_caveStates)[int(id) - int(IdCaveBase)].props;
        return;
    }
    if (id >= IdFence && id < IdFenceMax)
    {   // The fence connections are PERSISTENT properties: north(-Z)/east(+X)/south(+Z)/west(-X) from
        // the bitmask written into the id by pass 3.58. Without them the game loads bare sticks.
        const int conn = int(id) - int(IdFence);
        out << qMakePair(QStringLiteral("north"), (conn & 1) ? QStringLiteral("true") : QStringLiteral("false"))
            << qMakePair(QStringLiteral("east"),  (conn & 2) ? QStringLiteral("true") : QStringLiteral("false"))
            << qMakePair(QStringLiteral("south"), (conn & 4) ? QStringLiteral("true") : QStringLiteral("false"))
            << qMakePair(QStringLiteral("west"),  (conn & 8) ? QStringLiteral("true") : QStringLiteral("false"))
            << qMakePair(QStringLiteral("waterlogged"), QStringLiteral("false"));
        return;
    }
    if (id >= IdStoneFence && id < IdStoneFenceMax)
    {   // a stone fence: the sides low/none by the connection bitmask; the post (up) — everywhere except straight segments
        const int conn = int(id) - int(IdStoneFence);
        out << qMakePair(QStringLiteral("north"), (conn & 1) ? QStringLiteral("low") : QStringLiteral("none"))
            << qMakePair(QStringLiteral("east"),  (conn & 2) ? QStringLiteral("low") : QStringLiteral("none"))
            << qMakePair(QStringLiteral("south"), (conn & 4) ? QStringLiteral("low") : QStringLiteral("none"))
            << qMakePair(QStringLiteral("west"),  (conn & 8) ? QStringLiteral("low") : QStringLiteral("none"))
            << qMakePair(QStringLiteral("up"), (conn == 5 || conn == 10) ? QStringLiteral("false") : QStringLiteral("true"))
            << qMakePair(QStringLiteral("waterlogged"), QStringLiteral("false"));
        return;
    }
    if (id >= IdPainting && id < IdPaintingMax)
    {   // the painting facing is AWAY from the wall (as for wall_torch: which side the viewer is on).
        // the motif (a 1×1 subject) is in block_entities, without it the server takes kebab — also 1×1.
        static const char* const kPaintDir[] = { "north", "east", "south", "west" };
        out << qMakePair(QStringLiteral("facing"),
                        QLatin1String(kPaintDir[int(id) - int(IdPainting)]));
        return;
    }
    if (id >= IdBedBase && id < IdBedBase + kBedsCount * 8)
    {   // A bed is two blocks: BOTH cells with the same facing (from foot to head), part tells the
        // halves apart. occupied=false is the default, not written. Without part the server would turn the pair into two
        // "feet"/"heads" and the head would ride on foot.
        static const char* const kBedDir[] = { "north", "east", "south", "west" };
        const int v = int(id) - int(IdBedBase);
        out << qMakePair(QStringLiteral("facing"), QLatin1String(kBedDir[(v >> 1) & 3]))
            << qMakePair(QStringLiteral("part"), (v & 1) ? QStringLiteral("head")
                                                         : QStringLiteral("foot"));
        return;
    }
    if (id >= IdFlowerPotBase && id < IdFlowerPotMax)
    {   // flower_pot has NO flower property (a probe on the servers: the server
        // loses the "flower" property, the pot loads empty). The id variants = the kind of flower, the flower itself is in the
        // pot's block_entity (display/DisplayBlock, see buildChunkNbt). Without Properties.
        return;
    }
    switch (id)
    {
    case IdRailEW: out << qMakePair(QStringLiteral("shape"), QStringLiteral("east_west"))
                       << qMakePair(QStringLiteral("waterlogged"), QStringLiteral("false")); break;
    case IdRailNS: out << qMakePair(QStringLiteral("shape"), QStringLiteral("north_south"))
                       << qMakePair(QStringLiteral("waterlogged"), QStringLiteral("false")); break;
    case IdOakLeaves: out << qMakePair(QStringLiteral("distance"), QStringLiteral("2"))
                          << qMakePair(QStringLiteral("persistent"), QStringLiteral("true"))
                          << qMakePair(QStringLiteral("waterlogged"), QStringLiteral("false")); break;
    case IdJungleLeaves:
    case IdAcaciaLeaves:
    case IdSpruceLeaves:
    case IdBirchLeaves:
    case IdDarkOakLeaves:
    case IdRedPoplarLeaves:
    case IdOrangePoplarLeaves:
    case IdYellowPoplarLeaves:
    case IdPaleLeaves: out << qMakePair(QStringLiteral("distance"), QStringLiteral("2"))
                           << qMakePair(QStringLiteral("persistent"), QStringLiteral("true"))
                           << qMakePair(QStringLiteral("waterlogged"), QStringLiteral("false")); break;
    case IdPaleMossCarpet: out << qMakePair(QStringLiteral("bottom"), QStringLiteral("true"))
                               << qMakePair(QStringLiteral("east"), QStringLiteral("none"))
                               << qMakePair(QStringLiteral("north"), QStringLiteral("none"))
                               << qMakePair(QStringLiteral("south"), QStringLiteral("none"))
                               << qMakePair(QStringLiteral("west"), QStringLiteral("none")); break;
    case IdPaleHangingMoss: out << qMakePair(QStringLiteral("tip"), QStringLiteral("true")); break;
    case IdLeafLitter: out << qMakePair(QStringLiteral("facing"), QStringLiteral("north"))
                           << qMakePair(QStringLiteral("segment_amount"), QStringLiteral("2")); break;
    case IdCreakingHeart: out << qMakePair(QStringLiteral("axis"), QStringLiteral("y"))
                              << qMakePair(QStringLiteral("creaking_heart_state"), QStringLiteral("dormant"))
                              << qMakePair(QStringLiteral("natural"), QStringLiteral("true")); break;
    case IdTallGrassUpper: out << qMakePair(QStringLiteral("half"), QStringLiteral("upper")); break;
    case IdWheat: out << qMakePair(QStringLiteral("age"), QStringLiteral("7")); break;
    case IdCarrots: out << qMakePair(QStringLiteral("age"), QStringLiteral("7")); break;
    case IdPotatoes: out << qMakePair(QStringLiteral("age"), QStringLiteral("7")); break;
    case IdBeetroots: out << qMakePair(QStringLiteral("age"), QStringLiteral("3")); break;
    case IdFarmland: out << qMakePair(QStringLiteral("moisture"), QStringLiteral("7")); break;
    case IdPortalX: out << qMakePair(QStringLiteral("axis"), QStringLiteral("x")); break;
    case IdPortalZ: out << qMakePair(QStringLiteral("axis"), QStringLiteral("z")); break;
    case IdChestL: out << qMakePair(QStringLiteral("facing"), QStringLiteral("north"))
                       << qMakePair(QStringLiteral("type"), QStringLiteral("left"))
                       << qMakePair(QStringLiteral("waterlogged"), QStringLiteral("false")); break;
    case IdChestR: out << qMakePair(QStringLiteral("facing"), QStringLiteral("north"))
                       << qMakePair(QStringLiteral("type"), QStringLiteral("right"))
                       << qMakePair(QStringLiteral("waterlogged"), QStringLiteral("false")); break;
    case IdWallTorchN: out << qMakePair(QStringLiteral("facing"), QStringLiteral("north")); break;
    case IdWallTorchE: out << qMakePair(QStringLiteral("facing"), QStringLiteral("east")); break;
    case IdWallTorchS: out << qMakePair(QStringLiteral("facing"), QStringLiteral("south")); break;
    case IdWallTorchW: out << qMakePair(QStringLiteral("facing"), QStringLiteral("west")); break;
    case IdLanternHung: out << qMakePair(QStringLiteral("hanging"), QStringLiteral("true"))
                            << qMakePair(QStringLiteral("waterlogged"), QStringLiteral("false")); break;
    case IdGateN: out << qMakePair(QStringLiteral("facing"), QStringLiteral("north")); break;
    case IdGateE: out << qMakePair(QStringLiteral("facing"), QStringLiteral("east")); break;
    case IdGateS: out << qMakePair(QStringLiteral("facing"), QStringLiteral("south")); break;
    case IdGateW: out << qMakePair(QStringLiteral("facing"), QStringLiteral("west")); break;
    case IdStairsN: out << qMakePair(QStringLiteral("facing"), QStringLiteral("north")); break;
    case IdStairsE: out << qMakePair(QStringLiteral("facing"), QStringLiteral("east")); break;
    case IdStairsS: out << qMakePair(QStringLiteral("facing"), QStringLiteral("south")); break;
    case IdStairsW: out << qMakePair(QStringLiteral("facing"), QStringLiteral("west")); break;
    case IdSlabBottom: out << qMakePair(QStringLiteral("type"), QStringLiteral("bottom")); break;
    case IdSlabTop: out << qMakePair(QStringLiteral("type"), QStringLiteral("top")); break;
    case IdLadderN: out << qMakePair(QStringLiteral("facing"), QStringLiteral("north")); break;
    case IdLadderE: out << qMakePair(QStringLiteral("facing"), QStringLiteral("east")); break;
    case IdLadderS: out << qMakePair(QStringLiteral("facing"), QStringLiteral("south")); break;
    case IdLadderW: out << qMakePair(QStringLiteral("facing"), QStringLiteral("west")); break;
    case IdStoneStairsN: out << qMakePair(QStringLiteral("facing"), QStringLiteral("north")); break;
    case IdStoneStairsE: out << qMakePair(QStringLiteral("facing"), QStringLiteral("east")); break;
    case IdStoneStairsS: out << qMakePair(QStringLiteral("facing"), QStringLiteral("south")); break;
    case IdStoneStairsW: out << qMakePair(QStringLiteral("facing"), QStringLiteral("west")); break;
    case IdBrickStairsN: out << qMakePair(QStringLiteral("facing"), QStringLiteral("north")); break;
    case IdBrickStairsE: out << qMakePair(QStringLiteral("facing"), QStringLiteral("east")); break;
    case IdBrickStairsS: out << qMakePair(QStringLiteral("facing"), QStringLiteral("south")); break;
    case IdBrickStairsW: out << qMakePair(QStringLiteral("facing"), QStringLiteral("west")); break;
    default: break;
    }
}

//! The nearest-by-RGB concrete color from the palette (the Euclidean distance over 8-bit components).
quint16 nearestBlockId(const QColor& c)
{
    const int r = c.red(), g = c.green(), b = c.blue();
    int best = 0;
    long bestD = std::numeric_limits<long>::max();
    for (int i = 0; i < kPaletteCount; ++i)
    {
        // Walls (bldpol/csrpol) — ONLY concrete by color: stone/bricks live in the palette for
        // palette indices (brick by a keyword), and in the color matching the stone one
        // (125,125,125) outvoted beige-gray shades ("a stone shed" instead of concrete).
        if (!strstr(kBlockPalette[i].name, "_concrete")) continue;
        const long dr = r - kBlockPalette[i].r;
        const long dg = g - kBlockPalette[i].g;
        const long db = b - kBlockPalette[i].b;
        const long d = dr * dr + dg * dg + db * db;
        if (d < bestD) { bestD = d; best = i; }
    }
    return quint16(IdPaletteStart + best);
}

//! The nearest-by-RGB carpet color (roads) — a separate matching, we do not touch concrete for buildings.
quint16 nearestCarpetId(const QColor& c)
{
    const int r = c.red(), g = c.green(), b = c.blue();
    int best = 0;
    long bestD = std::numeric_limits<long>::max();
    for (int i = 0; i < kCarpetsCount; ++i)
    {
        const long dr = r - kCarpets[i].r;
        const long dg = g - kCarpets[i].g;
        const long db = b - kCarpets[i].b;
        const long d = dr * dr + dg * dg + db * db;
        if (d < bestD) { bestD = d; best = i; }
    }
    return quint16(IdCarpetBase + best);
}

//! The index of a block in kBlockPalette by the full name (-1 if none).
int paletteIndexOf(const char* name)
{
    for (int i = 0; i < kPaletteCount; ++i)
        if (qstrcmp(kBlockPalette[i].name, name) == 0)
            return i;
    return -1;
}

//! The vegpol codes on which a Pale Garden may grow (a user request): tall/stunted forest, forest undergrowth, bushes, industrial crops and meadow vegetation.
//! Parks (71126000), lawns and flowerbeds (71329000), gardens and everything courtyard (vgbpol) — not allowed.
bool isPaleForestCode(int code)
{
    switch (code)
    {
    case 71111110: case 71111120: case 71113000: case 71211100: case 71211200:
    case 71122000: case 71221000: case 71321000: case 71314000:
        return true;
    default:
        return false;
    }
}

//! Forest blocks that are not among the special idents (birch/spruce trunks of single trees, fern, berries, mushrooms, lilies, bamboo, sugar cane, moss):
//! village plots and sign posts count them as "vegetation", not an obstacle.
bool isForestExtraVeg(quint16 bid)
{
    if (bid < IdPaletteStart || bid >= IdPaletteStart + kPaletteCount) return false;
    static const char* const kNames[] = { "minecraft:birch_log", "minecraft:spruce_log", "minecraft:jungle_log", "minecraft:jungle_leaves", "minecraft:acacia_log", "minecraft:dark_oak_log", "minecraft:dead_bush", "minecraft:fern", "minecraft:sweet_berry_bush",
                                           "minecraft:brown_mushroom", "minecraft:red_mushroom", "minecraft:lily_pad", "minecraft:bamboo", "minecraft:sugar_cane", "minecraft:pale_moss_block" };
    static QSet<quint16> ids;
    if (ids.isEmpty())
        for (const char* n : kNames) { const int pi = paletteIndexOf(n); if (pi >= 0) ids.insert(quint16(IdPaletteStart + pi)); }
    return ids.contains(bid);
}

//! A block is vegetation (a tree, leaves, grass, flowers, crops, forest flora): the chamfer of the world edge does not count such columns as "occupied" and lowers them together with them.
bool isVegBlockId(quint16 bid)
{
    if (bid == IdAir) return false;
    if (bid == IdOakLog || bid == IdOakLeaves || bid == IdSpruceLeaves || bid == IdBirchLeaves || bid == IdDarkOakLeaves || bid == IdAcaciaLeaves || bid == IdJungleLeaves || bid == IdRedPoplarLeaves || bid == IdOrangePoplarLeaves
        || bid == IdYellowPoplarLeaves || bid == IdPaleLog || bid == IdPaleLeaves || bid == IdPaleMossCarpet || bid == IdLeafLitter || bid == IdPaleHangingMoss || bid == IdVine
        || bid == IdEyeOpen || bid == IdEyeClosed || bid == IdGrassPlant || bid == IdTallGrass || bid == IdTallGrassUpper || bid == IdWheat
        || (bid >= IdFlowerBase && bid < IdFlowerMax) || isForestExtraVeg(bid)) return true;
    if (bid >= IdCaveBase && g_caveStates)
    {   // a template state (the Pale Garden scatters its leaf litter and grass through the template palette):
        // recognize plant-like names there too; solid blocks and 'grass_block' stay obstacles
        const int off = int(bid) - int(IdCaveBase);
        if (off < g_caveStates->size())
        {
            const QString tail = (*g_caveStates)[off].name.section(QLatin1Char(':'), -1);
            static const char* const kPlantKeys[] = { "grass", "fern", "leaves", "sapling", "vine", "flower", "sprout",
                                                      "litter", "moss_carpet", "hanging_moss", "seedling", "bush", "kelp", "wheat", "berries" };
            if (tail != QLatin1String("grass_block"))
                for (const char* pk : kPlantKeys)
                    if (tail.contains(QLatin1String(pk))) return true;
        }
    }
    return false;
}

//! The building material from the "Material" column (the bldpol layer in the "Material from a column" mode — the same
//! layer settings and object columns that the radio model reads, see MainWindow::ExtractRadioBuildings).
//! The column values are free text, so matching is by keywords; 0 — not recognized,
//! the caller picks a block by the object color.
quint16 materialBlockId(const QString& material, const QColor& color)
{
    const QString s = material.toLower();
    // The "Material" column (MAT) is either a digit of the reference or a word; empty/0/not recognized — the
    // object color from the 3D window. The reference: 1 concrete, 2 reinforced concrete -> light gray concrete; 3 metal,
    // 9 steel -> an iron block; 4 stone -> the same light gray concrete; 7 brick -> brick; 5 wooden -> planks;
    // 6 earth -> dirt; 8 mixed -> calcite.
    {
        bool isNum = false;
        const int code = qRound(material.trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&isNum));
        if (isNum)
        {
            switch (code)
            {
            case 1: case 2: case 4: return quint16(IdPaletteStart + paletteIndexOf("minecraft:light_gray_concrete"));
            case 3: case 9: return IdIronBlock;
            case 7:         return quint16(IdPaletteStart + paletteIndexOf("minecraft:bricks"));
            case 5:         return IdOakPlanks;
            case 6:         return IdDirt;
            case 8:         return quint16(IdPaletteStart + paletteIndexOf("minecraft:calcite"));
            default:        return 0;                       // 0 [EMPTY] and unknown codes — by color
            }
        }
    }
    if (s.contains(QStringLiteral("кирпич")) || s.contains(QLatin1String("brick")))
        return quint16(IdPaletteStart + paletteIndexOf("minecraft:bricks"));
    if (s.contains(QStringLiteral("камен")) || s.contains(QLatin1String("stone"))
        || s.contains(QLatin1String("masonry")))
        return quint16(IdPaletteStart + paletteIndexOf("minecraft:light_gray_concrete"));
    if (s.contains(QStringLiteral("дерев")) || s.contains(QStringLiteral("брус"))
        || s.contains(QStringLiteral("бревен")) || s.contains(QLatin1String("wood")) || s.contains(QLatin1String("timber"))
        || s.contains(QLatin1String("log")))
        return IdOakPlanks;
    if (s.contains(QStringLiteral("земл")) || s.contains(QLatin1String("earth")) || s.contains(QLatin1String("adobe"))
        || s.contains(QLatin1String("mud")) || s.contains(QLatin1String("clay")))
        return IdDirt;
    if (s.contains(QStringLiteral("смешан")) || s.contains(QLatin1String("mixed")))
        return quint16(IdPaletteStart + paletteIndexOf("minecraft:calcite"));
    if (s.contains(QStringLiteral("стекл")) || s.contains(QLatin1String("glass")))
        return IdGlass;
    if (s.contains(QStringLiteral("метал")) || s.contains(QStringLiteral("сталь"))
        || s.contains(QStringLiteral("стальн")) || s.contains(QLatin1String("metal")) || s.contains(QLatin1String("steel"))
        || s.contains(QLatin1String("iron")) || s.contains(QLatin1String("aluminium")) || s.contains(QLatin1String("aluminum")))
        return IdIronBlock;
    if (s.contains(QStringLiteral("железобетон")) || s.contains(QStringLiteral("бетон"))
        || s.contains(QStringLiteral("монолит")) || s.contains(QLatin1String("concrete"))
        || s.contains(QLatin1String("cement")))
        return quint16(IdPaletteStart + paletteIndexOf("minecraft:light_gray_concrete"));
    if (s.contains(QStringLiteral("каркас"))
        || s.contains(QStringLiteral("профнастил")) || s.contains(QStringLiteral("сендвич"))
        || s.contains(QStringLiteral("сэндвич")) || s.contains(QStringLiteral("панел")) || s.contains(QLatin1String("panel"))
        || s.contains(QLatin1String("sandwich")) || s.contains(QLatin1String("plaster")) || s.contains(QLatin1String("frame")))
        return nearestBlockId(color);   // frame/panels/sandwich — concrete of the object color
    return 0;
}

static_assert(int(sizeof(kBlockPalette) / sizeof(kBlockPalette[0])) == kPaletteCount, "update kPaletteCount in export/blocks.h when the array grows");
static_assert(int(sizeof(kCarpets) / sizeof(kCarpets[0])) == kCarpetsCount, "update kCarpetsCount in export/blocks.h when the array grows");
static_assert(int(sizeof(kWools) / sizeof(kWools[0])) == kWoolsCount, "update kWoolsCount in export/blocks.h when the array grows");
static_assert(int(sizeof(kFlowers) / sizeof(kFlowers[0])) == kFlowersCount, "update kFlowersCount in export/blocks.h when the array grows");
static_assert(int(sizeof(kBeds) / sizeof(kBeds[0])) == kBedsCount, "update kBedsCount in export/blocks.h when the array grows");
static_assert(int(sizeof(kPaintMotifs) / sizeof(kPaintMotifs[0])) == kPaintMotifsCount, "update kPaintMotifsCount in export/blocks.h when the array grows");
static_assert(int(sizeof(kArmorMats) / sizeof(kArmorMats[0])) == kArmorMatsCount, "update kArmorMatsCount in export/blocks.h when the array grows");
