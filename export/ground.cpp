#include "export/ground.h"

// The ground globals live here; groundBlockAt is inline in the header (hot).

int g_zone = 0;
const char* g_biomeName[10] = { "minecraft:plains", "minecraft:pale_garden", "minecraft:taiga", "minecraft:forest",
                                      "minecraft:birch_forest", "minecraft:dappled_forest", "minecraft:flower_forest", "minecraft:meadow",
                                      "minecraft:swamp", "minecraft:dark_forest" };   // 8 — a swamp in the game (the OSM "natural=wetland" on mrspol, value 8),
                                                                                      // 9 — a dark forest (a cemetery, csrpol 53510000); both the same in every zone
quint16 g_zoneSurface = 1;                                   // the top block of natural ground (1 = IdGrass; desert — sand, tundra — a snow block)
quint16 g_sandId = 0, g_clayId = 0;                      // the sand and clay ids (palette ones; set in Export before the chunk assembly)

