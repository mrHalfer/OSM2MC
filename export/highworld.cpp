#include "export/highworld.h"

// The dimension_type JSON schema of minecraft:overworld changed several times (plain fields in the old games,
// an "attributes" block later, a clock/dragon field in the newest ones), and a wrong field silently drops the
// whole override. The pack therefore carries the base file plus two overlays selected by the pack format, the
// same layout the game itself ships overlays with. All three copies widen the world to kWorldMinY..kWorldMaxY
// (export/worldy.h): height 720 from min_y -128.
namespace
{
const char kPackMeta[] = R"({
  "pack": {
    "description": "OSMC extended build height",
    "pack_format": 61,
    "supported_formats": { "min_inclusive": 61, "max_inclusive": 2147483647 },
    "min_format": [61, 0],
    "max_format": [2147483647, 2147483647]
  },
  "overlays": {
    "entries": [
      {
        "min_format": [90, 0],
        "max_format": [100, 2147483647],
        "directory": "overlay_attributes"
      },
      {
        "min_format": [101, 0],
        "max_format": [2147483647, 2147483647],
        "directory": "overlay_2601"
      }
    ]
  }
})";

const char kBase[] = R"({
  "ambient_light": 0.0,
  "bed_works": true,
  "coordinate_scale": 1.0,
  "effects": "minecraft:overworld",
  "has_ceiling": false,
  "has_raids": true,
  "has_skylight": true,
  "height": 720,
  "infiniburn": "#minecraft:infiniburn_overworld",
  "logical_height": 720,
  "min_y": -64,
  "monster_spawn_block_light_limit": 0,
  "monster_spawn_light_level": {
    "max_inclusive": 7,
    "min_inclusive": 0,
    "type": "minecraft:uniform"
  },
  "natural": true,
  "piglin_safe": false,
  "respawn_anchor_works": false,
  "ultrawarm": false
})";

const char kOverlayAttributes[] = R"({
  "ambient_light": 0.0,
  "attributes": {
    "minecraft:audio/ambient_sounds": {
      "mood": {
        "block_search_extent": 8,
        "offset": 2.0,
        "sound": "minecraft:ambient.cave",
        "tick_delay": 6000
      }
    },
    "minecraft:audio/background_music": {
      "creative": {
        "max_delay": 24000,
        "min_delay": 12000,
        "sound": "minecraft:music.creative"
      },
      "default": {
        "max_delay": 24000,
        "min_delay": 12000,
        "sound": "minecraft:music.game"
      }
    },
    "minecraft:gameplay/bed_rule": {
      "can_set_spawn": "always",
      "can_sleep": "when_dark",
      "error_message": {
        "translate": "block.minecraft.bed.no_sleep"
      }
    },
    "minecraft:gameplay/nether_portal_spawns_piglin": true,
    "minecraft:gameplay/respawn_anchor_works": false,
    "minecraft:visual/cloud_color": "#ccffffff",
    "minecraft:visual/cloud_height": 192.33,
    "minecraft:visual/fog_color": "#c0d8ff",
    "minecraft:visual/sky_color": "#78a7ff"
  },
  "coordinate_scale": 1.0,
  "has_ceiling": false,
  "has_skylight": true,
  "height": 720,
  "infiniburn": "#minecraft:infiniburn_overworld",
  "logical_height": 720,
  "min_y": -64,
  "monster_spawn_block_light_limit": 0,
  "monster_spawn_light_level": {
    "max_inclusive": 7,
    "min_inclusive": 0,
    "type": "minecraft:uniform"
  },
  "timelines": "#minecraft:in_overworld"
})";

const char kOverlay2601[] = R"({
  "ambient_light": 0.0,
  "attributes": {
    "minecraft:audio/ambient_sounds": {
      "mood": {
        "block_search_extent": 8,
        "offset": 2.0,
        "sound": "minecraft:ambient.cave",
        "tick_delay": 6000
      }
    },
    "minecraft:audio/background_music": {
      "creative": {
        "max_delay": 24000,
        "min_delay": 12000,
        "sound": "minecraft:music.creative"
      },
      "default": {
        "max_delay": 24000,
        "min_delay": 12000,
        "sound": "minecraft:music.game"
      }
    },
    "minecraft:gameplay/bed_rule": {
      "can_set_spawn": "always",
      "can_sleep": "when_dark",
      "error_message": {
        "translate": "block.minecraft.bed.no_sleep"
      }
    },
    "minecraft:gameplay/nether_portal_spawns_piglin": true,
    "minecraft:gameplay/respawn_anchor_works": false,
    "minecraft:visual/ambient_light_color": "#0a0a0a",
    "minecraft:visual/cloud_color": "#ccffffff",
    "minecraft:visual/cloud_height": 192.33,
    "minecraft:visual/fog_color": "#c0d8ff",
    "minecraft:visual/sky_color": "#78a7ff"
  },
  "coordinate_scale": 1.0,
  "default_clock": "minecraft:overworld",
  "has_ceiling": false,
  "has_ender_dragon_fight": false,
  "has_skylight": true,
  "height": 720,
  "infiniburn": "#minecraft:infiniburn_overworld",
  "logical_height": 720,
  "min_y": -64,
  "monster_spawn_block_light_limit": 0,
  "monster_spawn_light_level": {
    "max_inclusive": 7,
    "min_inclusive": 0,
    "type": "minecraft:uniform"
  },
  "timelines": "#minecraft:in_overworld"
})";
}

void addTallWorldDatapack(const std::function<void(const QString&, const QByteArray&)>& addEntry)
{
    addEntry(QStringLiteral("datapacks/tmc_tall/pack.mcmeta"), QByteArray(kPackMeta));
    addEntry(QStringLiteral("datapacks/tmc_tall/data/minecraft/dimension_type/overworld.json"), QByteArray(kBase));
    addEntry(QStringLiteral("datapacks/tmc_tall/overlay_attributes/data/minecraft/dimension_type/overworld.json"),
             QByteArray(kOverlayAttributes));
    addEntry(QStringLiteral("datapacks/tmc_tall/overlay_2601/data/minecraft/dimension_type/overworld.json"),
             QByteArray(kOverlay2601));
}
