#include "export/encounters.h"

// The ids double as the datapack function names (data/osmc_enc/function/event/<id>.mcfunction),
// so they stay lowercase ascii. Group comments mirror the dialog sections; the titles and the
// full idea texts live in docs/city-encounters.md.
const char* const kEncIds[] = {
    // pursuers — the long "it follows you" arcs
    "lurker", "taxi", "echo", "matron", "mower", "upstairs", "third_passenger", "bus13",
    // parks, yards, playgrounds
    "carousel", "swings", "chalk_art", "twin_island", "dry_fountain", "bench_otherworld", "stray_dog",
    // graveyards
    "pumpkin_faces", "fresh_grave", "procession", "grave_caretaker", "clock_0307",
    // metro, tunnels, underpasses
    "last_train", "phantom_station", "extra_flight", "breathing_rails", "dark_wave",
    // tower living, entrances, roofs
    "floor_half", "ghost_lift", "window_wave", "roof_garden", "boarded_door",
    // streets, parking lots, interchanges
    "extra_car", "flasher", "loop_interchange", "roadworks", "hitchhiker",
    // schools, kindergartens
    "night_game", "skip_rope", "auditorium", "timetable",
    // private sector, dachas
    "banya", "unlocked_gate", "scarecrow", "howling_dog",
    // woods and industry
    "moths", "power_hum", "dead_camp", "conveyor", "forest_window",
    // water
    "lake_lightpath", "bridge_offerings", "cloud_reflection",
    // caves and the underground
    "fourth_wall", "rune_circle", "ghost_rolley",
    // military base and prison
    "base_radio", "lone_sentry", "refugee_camp",
    // micro wonders (sound/blocks, no mobs)
    "blinking_lamp", "hush_field", "local_rain", "window_walker", "long_shadows", "cat_council",
    // bonus legends
    "phone_booth", "night_market", "rising_street", "warning_sign",
};
const int kEncCount = int(sizeof(kEncIds) / sizeof(kEncIds[0]));
