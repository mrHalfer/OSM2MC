#pragma once
// A simplified model of climate zones: the vegetation type (tree species, surface, chunk biome) is chosen from the longitude/latitude of the data center.
// This is a rough map "by rectangles and latitude belts", not an exact Köppen classification: for disputable places (mountains, coasts) pick the zone manually.
#include <QCoreApplication>
#include <QCoreApplication>
#include <QString>
#include <cmath>

namespace Climate
{
enum Zone { Temperate = 0, Boreal, Tundra, Steppe, Mediterranean, Desert, Savanna, Tropical, Monsoon, SemiArid, Rainforest, DarkForest, Count };

inline QString name(int z)
{
    static const char* const kNames[Count] = { QT_TRANSLATE_NOOP("Climate", "temperate (mixed forests)"), QT_TRANSLATE_NOOP("Climate", "boreal (taiga)"), QT_TRANSLATE_NOOP("Climate", "tundra (arctic)"), QT_TRANSLATE_NOOP("Climate", "steppe"), QT_TRANSLATE_NOOP("Climate", "Mediterranean"), QT_TRANSLATE_NOOP("Climate", "desert"), QT_TRANSLATE_NOOP("Climate", "savanna"), QT_TRANSLATE_NOOP("Climate", "tropical (jungle)"), QT_TRANSLATE_NOOP("Climate", "monsoon (palms and oaks)"), QT_TRANSLATE_NOOP("Climate", "semi-arid (palms and cacti)"), QT_TRANSLATE_NOOP("Climate", "temperate rainforest (big oaks)"), QT_TRANSLATE_NOOP("Climate", "dark forest (dark oaks)") };
    return QCoreApplication::translate("Climate", kNames[z >= 0 && z < Count ? z : 0]);
}

//! Short keys for the command line: temperate, boreal, tundra, steppe, mediterranean, desert, savanna, tropical, monsoon, semi-arid, rainforest, dark-forest; -1 — not recognized
inline int fromKey(const QString& key)
{
    static const char* const kKeys[Count] = { "temperate", "boreal", "tundra", "steppe", "mediterranean", "desert", "savanna", "tropical", "monsoon", "semi-arid", "rainforest", "dark-forest" };
    for (int i = 0; i < Count; ++i) if (key.compare(QLatin1String(kKeys[i]), Qt::CaseInsensitive) == 0) return i;
    return -1;
}

namespace detail
{
struct Rect { double lon0, lat0, lon1, lat1; };
inline bool in(const Rect& r, double lon, double lat) { return lon >= r.lon0 && lon <= r.lon1 && lat >= r.lat0 && lat <= r.lat1; }
}

//! The zone by coordinates (degrees). Order of checks: the two "rectangle" forest zones, then deserts, tropical forests, savannas, the Mediterranean, steppes, then latitude belts.
inline Zone fromLonLat(double lon, double lat)
{
    using detail::Rect;
    using detail::in;
    static const Rect kRainforest[] = {
        { -124.7, 42.0, -117.0, 49.5 },   // Oregon and Washington — the big-oak and conifer rainforests of the Pacific Northwest
        { -139.0, 48.3, -120.0, 58.5 },   // the coast of British Columbia and south-east Alaska (the Great Bear Rainforest)
        { -76.5, -56.0, -70.3, -37.8 }    // south Chile and the foothills east of the Andes (Valdivian and Magellanic rainforests)
    };
    static const Rect kDarkForest[] = {
        { 139.7, 41.4, 145.9, 45.8 },     // Hokkaido
        { 145.9, 43.0, 153.5, 48.0 },     // the southern Kurils
        { 37.2, 40.9, 41.2, 43.6 }        // the western Caucasus: the Colchic forests about Sochi and Abkhazia
    };
    static const Rect kDesert[] = {
        { -17, 15, 35, 31 },    // Sahara
        { 35, 16, 60, 31 },     // Arabia
        { 52, 36, 66, 45 },     // Karakum, Kyzylkum
        { 76, 36, 112, 45 },    // Taklamakan, Gobi
        { 68, 24, 74, 29 },     // Thar
        { 118, -30, 142, -20 }, // central Australia
        { 11, -27, 24, -17 },   // Namib, Kalahari
        { -72, -28, -68, -17 }, // Atacama
        { -118, 27, -108, 36 }  // Mojave, Sonoran
    };
    static const Rect kTropical[] = {
        { -75, -10, -50, 4 },   // Amazonia
        { 9, -5, 30, 4 },       // Congo
        { 95, -10, 150, 10 },   // Southeast Asia, Indonesia, New Guinea
        { -12, 4, 10, 8 },      // Gulf of Guinea coast
        { -92, 7, -77, 18 }     // Central America
    };
    static const Rect kSavanna[] = {
        { -17, 8, 40, 15 },     // Sahel
        { 15, -20, 40, -5 },    // East and Southern Africa
        { -60, -20, -40, -5 },  // cerrado
        { 124, -20, 145, -11 }, // northern Australia
        { 72, 10, 88, 24 },     // Hindustan
        { -72, 3, -60, 9 }      // llanos
    };
    static const Rect kMediterranean[] = {
        { -10, 30, 40, 45 },    // the Mediterranean
        { -125, 32, -117, 40 }, // California
        { -74, -38, -70, -30 }, // central Chile
        { 113, -35, 120, -30 }, // southwestern Australia
        { 17, -35, 21, -32 }    // Cape Town
    };
    static const Rect kSteppe[] = {
        { 30, 44, 120, 51 },    // the Eurasian steppe (from the Black Sea region to Mongolia)
        { -105, 35, -95, 50 },  // the Great Plains
        { -65, -38, -58, -30 }, // the Pampas
        { -72, -50, -65, -38 }, // Patagonia
        { 32, 36, 50, 40 }      // the Anatolian and Iranian plateaus
    };
    static const Rect kMonsoon[] = {
        { -82.8, 24.2, -79.9, 27.6 },  // southern Florida (Miami): humid subtropics — palmettos and oaks
        { -85.2, 30.0, -78.5, 35.3 },  // Georgia and South Carolina (the US Atlantic coast south of ~35N)
        { 105, 18, 122.5, 27 },        // southern China (Hainan, Guangdong, Guangxi, Fujian) and Taiwan
        { 127.5, 24, 132.0, 34.2 }     // southern Japan: Okinawa and Kyushu
    };
    static const Rect kSemiArid[] = {
        { -98.0, 25.8, -89.0, 31.2 }   // the Gulf coast: southern Texas and coastal Louisiana — sand, cacti, palms and oaks
    };
    for (const Rect& r : kRainforest) if (in(r, lon, lat)) return Rainforest;   // before the Mediterranean and the steppes: the Chilean rainforest is west of their rectangles
    for (const Rect& r : kDarkForest) if (in(r, lon, lat)) return DarkForest;    // before them too: the Colchic forests sit inside the Mediterranean rectangle
    for (const Rect& r : kMonsoon) if (in(r, lon, lat)) return Monsoon;
    for (const Rect& r : kSemiArid) if (in(r, lon, lat)) return SemiArid;
    for (const Rect& r : kDesert) if (in(r, lon, lat)) return Desert;
    for (const Rect& r : kTropical) if (in(r, lon, lat)) return Tropical;
    for (const Rect& r : kSavanna) if (in(r, lon, lat)) return Savanna;
    for (const Rect& r : kMediterranean) if (in(r, lon, lat)) return Mediterranean;
    for (const Rect& r : kSteppe) if (in(r, lon, lat)) return Steppe;
    const double a = std::abs(lat);
    if (a >= 68.0) return Tundra;
    if (a >= 58.0) return Boreal;
    if (a < 12.0) return Tropical;
    if (a < 23.0) return Savanna;
    return Temperate;
}
}
