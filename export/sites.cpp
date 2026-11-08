#include "export/sites.h"
#include <QVector>
#include <algorithm>
#include <cmath>

// The tables are extern in export/sites.h — the asserts at the bottom catch a forgotten count update.


const char* const kSpawnerTypes[] = { "minecraft:zombie", "minecraft:skeleton",
                                             "minecraft:spider", "minecraft:witch" };   // the witch — only the basement nests
const BookText kBooks[] = {
    { "От Иоанна, 1", "В начале было Слово, и Слово было у Бога, и Слово было Бог. Оно было в начале у Бога. Всё чрез Него начало быть, и без Него ничто не начало быть, что начало быть.",
                      "В Нём была жизнь, и жизнь была свет человеков. И свет во тьме светит, и тьма не объяла его." },
    { "Заповеди блаженства", "Блаженны нищие духом, ибо их есть Царство Небесное. Блаженны плачущие, ибо они утешатся. Блаженны кроткие, ибо они наследуют землю.",
                      "Блаженны алчущие и жаждущие правды, ибо они насытятся. Блаженны милостивые, ибо они помилованы будут. Блаженны чистые сердцем, ибо они Бога узрят." },
    { "Откровение, 21", "И увидел я новое небо и новую землю, ибо прежнее небо и прежняя земля миновали, и моря уже нет. И я, Иоанн, увидел святой город Иерусалим, новый, сходящий от Бога с неба.",
                      "И отрет Бог всякую слезу с очей их, и смерти не будет уже; ни плача, ни вопля, ни болезни уже не будет, ибо прежнее прошло." },
    { "Молитва", "Отче наш, сущий на небесах! Да святится имя Твоё; да приидет Царствие Твоё; да будет воля Твоя и на земле, как на небе.",
                      "Хлеб наш насущный дай нам на сей день; и прости нам долги наши, как и мы прощаем должникам нашим; и не введи нас в искушение, но избавь нас от лукавого." },
    { "Альфа и Омега", "Я есмь Альфа и Омега, начало и конец, говорит Господь, Который есть и был и грядёт, Вседержитель.",
                      "Се, стою у двери и стучу: если кто услышит голос Мой и отворит дверь, войду к нему, и буду вечерять с ним, и он со Мною." },
    { "О любви", "Любовь долготерпит, милосердствует, любовь не завидует, любовь не превозносится, не гордится, не бесчинствует, не ищет своего, не раздражается, не мыслит зла.",
                      "Любовь никогда не перестаёт. А теперь пребывают сии три: вера, надежда, любовь; но любовь из них больше." },
    { "От Иоанна, 3", "Ибо так возлюбил Бог мир, что отдал Сына Своего Единородного, дабы всякий верующий в Него, не погиб, но имел жизнь вечную.",
                      "Ибо не послал Бог Сына Своего в мир, чтобы судить мир, но чтобы мир спасён был чрез Него." },
    { "Откровение, 22", "И показал мне чистую реку воды жизни, светлую, как кристалл, исходящую от престола Бога и Агнца. И ночи не будет там, и не будут нуждаться ни в светильнике, ни в свете солнечном.",
                      "Я есмь Альфа и Омега, первый и последний, начало и конец. Блаженны соблюдающие заповеди Его." },
};
//! The books on the lecterns of research institutes (instead of the teacher's desk): the most famous physics formulas. The index in ChestSite::lectern = 1000 + the number.
const BookText kSciBooks[] = {
    { "Эквивалентность", "E = mc²\n\nЭнергия покоя равна массе, умноженной на квадрат скорости света.\n\nc ≈ 299 792 458 м/с",
                      "E = hν\n\nЭнергия кванта равна постоянной Планка, умноженной на частоту.\n\nλ = h / p" },
    { "Законы Ньютона", "F = ma\n\nСила равна массе, умноженной на ускорение.\n\np = mv\n\nF = dp / dt",
                      "F = G·m₁m₂ / r²\n\nЗакон всемирного тяготения.\n\ng ≈ 9,81 м/с²" },
    { "Электричество", "U = I·R\n\nЗакон Ома.\n\nP = U·I\n\nQ = I²·R·t (закон Джоуля — Ленца)",
                      "F = k·q₁q₂ / r²\n\nЗакон Кулона.\n\nC = q / U" },
    { "Механика", "v = v₀ + at\n\ns = v₀t + at² / 2\n\nEк = mv² / 2\n\nEп = mgh",
                      "A = F·s·cos α\n\nN = A / t\n\nT = 2π√(l / g)" },
    { "Термодинамика", "pV = nRT\n\nУравнение состояния идеального газа.\n\nΔU = Q − A",
                      "Q = cmΔT\n\nS = k·ln W\n\nη = 1 − T₂ / T₁" },
    { "Волны и свет", "c = λ·ν\n\nn = c / v\n\nsin α / sin β = n₂ / n₁",
                      "∇·E = ρ / ε₀\n\n∇×E = −∂B / ∂t\n\n(уравнения Максвелла)" },
};

//! The loot of barrels (a request: "what usually lies in barrels in Minecraft"): vanilla village barrels —
//! the fisherman, butcher, a meat/fruit "house", shepherd, tanner, mason, weaponsmith/toolsmith,
//! plus the bilge barrel of a shipwreck. The game survives tables that are absent in a version (an empty barrel).
const char* const kBarrelLoot[] = {
    "minecraft:chests/village/village_fisher",
    "minecraft:chests/village/village_butcher",
    "minecraft:chests/village/village_plains_house",
    "minecraft:chests/village/village_shepherd",
    "minecraft:chests/village/village_tannery",
    "minecraft:chests/village/village_mason",
    "minecraft:chests/village/village_taiga_house",
    "minecraft:chests/shipwreck_supply",
};

//! The chest loot tables by "expensiveness" (a request: the really valuable only in basements and in house treasures, and not always):
//!  tier 0 (cheap, household) — [0, kLootT0), tier 1 (medium) — [kLootT0, kLootT1), tier 2 (expensive) — the rest.
const char* const kChestLoot[] = {
    "minecraft:chests/village/village_plains_house",
    "minecraft:chests/village/village_taiga_house",
    "minecraft:chests/village/village_savanna_house",
    "minecraft:chests/village/village_desert_house",
    "minecraft:chests/village/village_snowy_house",
    "minecraft:chests/village/village_butcher",
    "minecraft:chests/village/village_fisher",
    "minecraft:chests/village/village_shepherd",
    "minecraft:chests/igloo_chest",
    // medium
    "minecraft:chests/abandoned_mineshaft",
    "minecraft:chests/simple_dungeon",
    "minecraft:chests/village/village_toolsmith",
    "minecraft:chests/village/village_weaponsmith",
    "minecraft:chests/village/village_armorer",
    "minecraft:chests/village/village_temple",
    // expensive
    "minecraft:chests/shipwreck_treasure",
    "minecraft:chests/desert_pyramid",
    "minecraft:chests/woodland_mansion",              // the mansion: diamonds, enchanted books, a totem — the "expensive" loot of the Pale Garden (index kChestLootCount-3)
    "minecraft:chests/ruined_portal",                 // gold and enchanted gear — the chest of a ruined portal (index kChestLootCount-2)
    "minecraft:chests/pillager_outpost",
};
//! The table choice by a random r: pCheap % — the cheap tier, the next pMid % — the medium one, the rest — the expensive one.
int pickChestLoot(quint32 r, int pCheap, int pMid)
{
    const int roll = int(r % 100u);
    const quint32 q = r / 100u;
    if (roll < pCheap) return int(q % quint32(kLootT0));
    if (roll < pCheap + pMid) return kLootT0 + int(q % quint32(kLootT1 - kLootT0));
    return kLootT1 + int(q % quint32(kChestLootCount - kLootT1));
}
//! Whether the point (px,pz) is in the "ragged" zone of the colored forest around a school / research institute / kindergarten: the distance from the building walls is no more than R·(0.55..1.45),
//! the multiplier is a smooth value noise by coordinates (a 5-block cell) with its own salt for each building: the edges of the zone are tongues and bays, not a circle.
bool zoneInside(const IslandSite& is, int px, int pz, int R)
{
    const int dxo = std::max(0, std::abs(px - is.cx) - is.rx), dzo = std::max(0, std::abs(pz - is.cz) - is.rz);
    const double d = std::hypot(double(dxo), double(dzo));
    if (d > R * 1.45) return false;
    if (d <= 0.0) return true;
    auto hv = [&](int gx, int gz) -> double
    {
        quint32 h = quint32(gx) * 374761393u ^ quint32(gz) * 668265263u ^ is.hash;
        h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
        return double(h % 1000u) / 999.0;
    };
    const double fx = px / 5.0, fz = pz / 5.0;
    const int gx = int(std::floor(fx)), gz = int(std::floor(fz));
    double tx = fx - gx, tz = fz - gz;
    tx = tx * tx * (3.0 - 2.0 * tx); tz = tz * tz * (3.0 - 2.0 * tz);
    const double n = (hv(gx, gz) * (1 - tx) + hv(gx + 1, gz) * tx) * (1 - tz) + (hv(gx, gz + 1) * (1 - tx) + hv(gx + 1, gz + 1) * tx) * tz;
    return d <= R * (0.55 + 0.9 * n);
}
//! The maximum durability of tools and swords by material (wood, stone, gold, iron, diamond).
const int kToolMaxDur[5] = { 59, 131, 32, 250, 1561 };
const char* const kToolMatNames[5] = { "wooden", "stone", "golden", "iron", "diamond" };
const char* const kToolKindNames[5] = { "sword", "pickaxe", "axe", "shovel", "hoe" };

//! The professions of villagers (an id without the minecraft: prefix) and the "clothing" by biome.
const char* const kProfs[] = { "armorer", "butcher", "cartographer", "cleric", "farmer", "fisherman",
                                      "fletcher", "leatherworker", "librarian", "mason", "nitwit", "none",
                                      "shepherd", "toolsmith", "weaponsmith" };
const char* const kVTypes[] = { "plains", "desert", "jungle", "savanna", "snow", "swamp", "taiga" };


// The tables are extern in export/sites.h — these asserts catch a forgotten count update.

static_assert(int(sizeof(kSpawnerTypes) / sizeof(kSpawnerTypes[0])) == kSpawnerTypesCount, "update kSpawnerTypesCount in export/sites.h");
static_assert(int(sizeof(kBooks) / sizeof(kBooks[0])) == kBooksCount, "update kBooksCount in export/sites.h");
static_assert(int(sizeof(kSciBooks) / sizeof(kSciBooks[0])) == kSciBooksCount, "update kSciBooksCount in export/sites.h");
static_assert(int(sizeof(kBarrelLoot) / sizeof(kBarrelLoot[0])) == kBarrelLootCount, "update kBarrelLootCount in export/sites.h");
static_assert(int(sizeof(kChestLoot) / sizeof(kChestLoot[0])) == kChestLootCount, "update kChestLootCount in export/sites.h");
