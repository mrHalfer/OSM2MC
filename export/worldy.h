#pragma once
// The vertical extent of the exported world. Vanilla clamps the overworld to -64..319, which used to force the
// exporter to squash a 700-metre DSM into a 150-block bump (a user request: Rio de Janeiro came out as small
// hills). Our worlds carry the "tmc_tall" dimension_type datapack (see highworld.cpp) which widens the vanilla
// dimension the same way custom dimensions are widened, so the server accepts the range below and the mountain
// stays a mountain. Only the TOP moves: the floor stays the vanilla -64, which is also the bedrock bottom row
// the cave template was cut out with (a user decision). Everything height-related is clamped by these constants,
// never by the literals 255/319.
constexpr int kWorldMinY = -64;             // the datapack: "min_y" (vanilla)
constexpr int kWorldMaxY = 655;             // the datapack: "height" 720 from kWorldMinY
constexpr int kSecMin = kWorldMinY >> 4;    // the first chunk section of a chunk (Y = -4)
constexpr int kSecMax = kWorldMaxY >> 4;    // the last one (Y = 40)
constexpr int kGroundTopMax = 540;          // the highest ground column: a reserve for towers and trees above it
