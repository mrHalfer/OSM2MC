#include "export/encounters.h"
#include "export/encounters_ru.h"
#include "export/blocks.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <QList>

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
    // destructive — the city blows up and burns (a user request)
    "gas_cylinder", "call_01", "fire_deer",
    // the night sky (a user request)
    "bat_swarm",
};
const int kEncCount = int(sizeof(kEncIds) / sizeof(kEncIds[0]));

// The events whose datapack logic shipped; the "City events" window marks the rest "[IN PROGRESS]".
// The list grows release by release (docs/city-encounters.md, docs/encounters-feasibility.md).
bool encImplemented(const char* id)
{
    static const char* const kDone[] = {
        "carousel", "blinking_lamp", "local_rain", "chalk_art", "swings", "pumpkin_faces", "cat_council",
        "flasher", "window_wave", "matron", "scarecrow", "fresh_grave", "clock_0307",
        "taxi", "lurker", "bus13", "dry_fountain", "last_train", "bench_otherworld",
        "floor_half",
        "echo", "upstairs", "third_passenger", "stray_dog",
        "procession", "moths", "hush_field", "window_walker",
        "grave_caretaker", "howling_dog", "night_game", "skip_rope", "auditorium", "timetable",
        "mower", "extra_car", "phantom_station", "breathing_rails",
        "power_hum", "cloud_reflection", "ghost_rolley", "night_market", "hitchhiker",
        "ghost_lift", "long_shadows", "rising_street", "extra_flight",
        "gas_cylinder", "call_01", "fire_deer",
        "bat_swarm",
    };
    for (const char* const s : kDone)
        if (std::strcmp(s, id) == 0)
            return true;
    return false;
}

// The chalk arrow: tip, two wings, two shaft cells pointing north; rotated clockwise per dir step
// (north -> east), which is where the exporter's flat-road bake and the baked draw functions meet.
void encChalkCells(const EncPack::Prop& prop, QPoint out[5])
{
    static const int kArrow[5][2] = { {0, -2}, {-1, -1}, {1, -1}, {0, 0}, {0, 1} };
    for (int c = 0; c < 5; ++c)
    {
        int dx = kArrow[c][0], dz = kArrow[c][1];
        for (int r = 0; r < (prop.dir & 3); ++r)
        {   const int t = -dz; dz = dx; dx = t; }
        out[c] = QPoint(prop.x + dx, prop.z + dz);
    }
}

// The marker tag of every kEp* kind, in the order of the enum (docs/city-encounters.md): a prop
// carries it on its marker entity, the radius searches of the events select by it
const char* const EncPack::kKindTag[20] = {
    "osmc.klamp", "osmc.kstop", "osmc.kplay", "osmc.kchalk", "osmc.kjack", "osmc.kcross",
    "osmc.kwall", "osmc.kscares", "osmc.kgrave", "osmc.kbell", "osmc.kschool", "osmc.kcar",
    "osmc.klurk", "osmc.kfount", "osmc.kmetro", "osmc.kbench", "osmc.khalf", "osmc.kmow",
    "osmc.kroof", "osmc.kdeer"
};

// A prop whose side data is too thin gives the generator nothing to bake — such places leave both
// the markers and the dispatch, so a selected place always has real functions behind it
static bool encPropViable(const EncPack::Prop& p)
{
    int atLeast = 0;
    switch (p.kind)
    {
    case EncPack::kEpWindows: case EncPack::kEpGrave: case EncPack::kEpLamp:
    case EncPack::kEpBench:    atLeast = 1; break;      // a lamp with no light cell cannot blink, the bench without its trigger cell is mute
    case EncPack::kEpSchool:  atLeast = 3; break;                           // the matron walks three cells
    case EncPack::kEpCar:     atLeast = 2; break;                           // a taxi needs the patch and the car
    case EncPack::kEpRoof:    atLeast = 1; break;                           // the fire burns the ridge cell and gives it back
    case EncPack::kEpDeer:    atLeast = 24; break;                          // a whole ring of crowns, not one lonely tree
    case EncPack::kEpHalf:    atLeast = 2; break;                           // the door is a pair of lining cells, both must be real
    case EncPack::kEpLurker:  atLeast = 4; break;
    case EncPack::kEpFountain: atLeast = 17; break;                         // the bowl drains whole or not at all
    case EncPack::kEpMetro:   atLeast = 12; break;                          // the catch cell plus an eleven-cell route
    case EncPack::kEpMow:     atLeast = 6; break;                           // half a walk of stripes is not a walk, it is noise
    default: break;
    }
    return p.cells.size() >= atLeast;
}

void EncPack::capProps(QVector<Prop>& props)
{   // one cap per kind: an event spread over the whole city stops being LOCAL, and the caps bound
    // the size of the generated pack (the caps also decide how many markers a small parcel grows)
    static const int kCap[20] = { 120, 60, 40, 40, 1000000000, 16, 24, 16, 60, 8, 24, 24, 32, 16, 12, 60, 1000000000, 16, 16, 8 };
    // kEpJack (no cap in practice): the watch of the cemetery faces is not a rolled meeting but a
    // place EVERY pumpkin keeps - posts and grass carvings of every graveyard, tens of thousands
    // in a big city; the markers are inert and the tick only looks within twenty-four blocks.
    // kEpHalf uncapped too (a user word): a door of the 9 1/2 floor is a place already earned
    // by climbing ten floors in game - the list must not thin the doors a map actually grew
    QVector<Prop> keep[20];
    for (int k = 0; k < 20; ++k)
    {
        QVector<const Prop*> alive;
        for (const Prop& p : props)
            if (p.kind == k && encPropViable(p))
                alive.append(&p);
        const int n = alive.size();
        const int cap = qMin(n, kCap[k]);
        for (int s = 0; s < cap; ++s)          // the uniform stride keeps the places spread over the city
            keep[k].append(*alive[cap == n ? s : s * n / cap]);
    }
    // the survivors re-enter the list grouped by kind and numbered inside it: build() rebuilds the
    // same byKind lists in the same order, so Prop::idx IS the dispatch number of the place
    props.clear();
    for (int k = 0; k < 20; ++k)
        for (int j = 0; j < keep[k].size(); ++j)
        {
            Prop p = keep[k][j];
            p.idx = j;
            props.append(p);
        }
}

namespace
{
//! The gate of the player-bound rolls (upstairs): the night window, then the roll of 1..100 against
//! the chance and the custom dispatch after it. Place-bound events use the nearRoll frame instead -
//! one roll a night and the marker places of their kind answer near the players.
QString gate(int lo, int hi, int chance)
{
    return QStringLiteral("# the roll of the event\n"
                          "execute unless score #day osmc_enc_t matches %1 run return 0\n"
                          "execute store result score #enc_r osmc_enc_t run random value 1..100\n"
                          "execute unless score #enc_r osmc_enc_t matches 1..%2 run return 0\n")
           .arg(QStringLiteral("%1..%2").arg(lo).arg(hi)).arg(chance);
}

}   // namespace

namespace
{
//! The full block state ("minecraft:oak_slab[type=top]"): baked cells that carry properties (the
//! slab halves of a private roof, the stairs of a gable, the persistent leaves of a crown) must be
//! given back in the very state they were placed in - a plain name would restore a default state
QString blockState(quint16 id)
{
    QString n = blockName(id);
    QVector<QPair<QString, QString>> pr;
    blockProperties(id, pr);
    for (int i = 0; i < pr.size(); ++i)
    {
        n += QLatin1Char(i ? ',' : '[');
        n += pr[i].first;
        n += QLatin1Char('=');
        n += pr[i].second;
    }
    if (!pr.isEmpty()) n += QLatin1Char(']');
    return n;
}

}   // namespace

// The pack generator. Everything a baked event needs (coordinates, counters, schedules) is written
// into literal mcfunction lines here — the runtime pack carries no macros and reads no data files,
// so the same functions behave identically in 1.21.x and 26.x (from data pack format 48 = the 1.21.0
// export to 121 = 26.3; the few JSON files whose schema the two families spell differently are
// written per format, see the legacy121 branches below) -
// except where 26.x retired the mechanic an event is built on, see the legacy121 branches below.
void EncPack::build(const AddFn& addIn, const QSet<QString>& off, const QVector<Prop>& props,
                    const QVector<int>& bookCell, bool legacy121,
                    bool russian)
{
    // the Russian voice of the pack (a user's request): the generator below keeps writing English,
    // one post-pass over every finished entry swaps the player-visible lines (export/encounters_ru.h)
    const AddFn add = russian
        ? AddFn([addIn](const QString& path, const QByteArray& data)
                { addIn(path, encRu::translate(data)); })
        : addIn;
    // The place lists per kind. ExportRun caps and numbers the props through capProps() before the
    // markers are written, so the caps filter below only guards a build() called without that pass;
    // the vector arrives in the stable order of the build stages (the chalk candidates are pre-sorted
    // by coordinates), so the pack is reproducible
    QVector<Prop> byKind[20];
    // the lurker and the ninth-and-a-half floor carry bigger pools since the dread system: at a
    // scared night the lurker comes from more lamps and the door of no plan can stand in more
    // houses (the storm of dread 95 opens every baked one at once, see the hf section); kEpJack
    // is uncapped like capProps(): every cemetery face keeps its marker, kEpHalf uncapped as
    // well - every climbed-to door of the pool stands in the dispatch
    static const int kCap[20] = { 120, 60, 40, 40, 1000000000, 16, 24, 16, 60, 8, 24, 24, 32, 16, 12, 60, 1000000000, 16, 16, 8 };
    for (const Prop& p : props)
        if (p.kind >= 0 && p.kind < 20 && byKind[p.kind].size() < kCap[p.kind] && encPropViable(p))
            byKind[p.kind].append(p);
    const QVector<Prop>& lamps  = byKind[kEpLamp];        // blinking_lamp + the local_rain anchor
    const QVector<Prop>& stops  = byKind[kEpBusStop];     // cat_council + bus13
    const QVector<Prop>& plays  = byKind[kEpPlayground];  // swings
    const QVector<Prop>& chalks = byKind[kEpChalk];       // chalk_art
    const QVector<Prop>& jacks  = byKind[kEpJack];        // pumpkin_faces
    const QVector<Prop>& crosses = byKind[kEpCross];      // flasher
    const QVector<Prop>& walls  = byKind[kEpWindows];     // window_wave
    const QVector<Prop>& scares = byKind[kEpScarecrow];   // scarecrow
    const QVector<Prop>& graves = byKind[kEpGrave];       // fresh_grave
    const QVector<Prop>& bells  = byKind[kEpBell];        // clock_0307
    const QVector<Prop>& schools = byKind[kEpSchool];     // matron
    const QVector<Prop>& cars   = byKind[kEpCar];       // taxi
    const QVector<Prop>& lurks  = byKind[kEpLurker];    // lurker
    const QVector<Prop>& founts = byKind[kEpFountain];  // dry_fountain
    const QVector<Prop>& metros = byKind[kEpMetro];     // last_train
    const QVector<Prop>& benches = byKind[kEpBench];    // bench_otherworld
    const QVector<Prop>& halfs = byKind[kEpHalf];       // floor_half
    const QVector<Prop>& lawns = byKind[kEpMow];        // mower
    const QVector<Prop>& roofs = byKind[kEpRoof];       // call_01 (gas_cylinder wakes the same walls window_wave watches)
    const QVector<Prop>& deerG = byKind[kEpDeer];       // fire_deer

    const QString fn = QStringLiteral("datapacks/osmc_enc/data/osmc_enc/function/");
    const QString ev = fn + QStringLiteral("ev/");           // zip paths for addEntry
    const QString fp = QStringLiteral("osmc_enc:ev/");       // function-name prefix inside commands

    // The dispatcher (as in the first release): the cycle runs every 200 ticks and shares the daytime
    // score with the events; the on/off functions let the player retune the world at run time.
    QString load = QStringLiteral("# generated by OSMC: random city events (docs/city-encounters.md)\n"
                                  "scoreboard objectives add osmc_enc dummy\n"
                                  "scoreboard objectives add osmc_enc_t dummy\n"
                                  "scoreboard objectives add osmc_bat dummy\n"
                                  "scoreboard players set #ca_n osmc_enc_t 0\n"
                                  "scoreboard players set #k24000 osmc_enc_t 24000\n"
                                  // The dread scale and its level memory survive world reloads (the
                                  // city remembers), so they init only when never set; #lv is derived
                                  // from #dread on every cycle and needs no memory
                                  "execute unless score #dread osmc_enc_t matches -2147483648.. run scoreboard players set #dread osmc_enc_t 0\n"
                                  "execute unless score #dread_sent osmc_enc_t matches -2147483648.. run scoreboard players set #dread_sent osmc_enc_t 0\n"
                                  "scoreboard players set #lv osmc_enc_t 0\n"
                                  "scoreboard players set #k100 osmc_enc_t 100\n"
                                  "scoreboard players set #tsync osmc_enc_t 0\n"
                                  // the clock scores carry over reloads (the city keeps counting its days), so they
                                  // init only when never set
                                  "execute unless score #day osmc_enc_t matches -2147483648.. run scoreboard players set #day osmc_enc_t 0\n"
                                  "execute unless score #day_old osmc_enc_t matches -2147483648.. run scoreboard players set #day_old osmc_enc_t 0\n"
                                  "execute unless score #dayn osmc_enc_t matches -2147483648.. run scoreboard players set #dayn osmc_enc_t 0\n");
    QString cycle = QStringLiteral("# one cycle every 200 ticks; each enabled event decides inside whether it comes now\n");
    QString tickFob;                        // the per-tick hooks an event adds to tick.mcfunction
    QString tickJack;                       // (the cemetery faces watch there too, see pumpkin_faces)
    QString tickScare;                      // (and the yard scarecrow swivels on the same two-second beat)
    QString tickGlass;                      // the dread's glass counters are read every tick, not per cycle
    QString tickKill;                       // (and so are the kill statistics, one gate per kind)
    QString tickBats;                       // (the bat swarm watches its own dive on the same beat)
    // The game clock is read in time/sync (one call every 20 ticks, see tick.mcfunction), not on the
    // cycle. The owner's testing caught the pack on the wrong part of the day after /time commands:
    // 26.x moved the day onto the minecraft:day timeline, and gametime stayed a bare tick counter
    // the commands do not move (a server probe: after /time set midnight gametime still counted the
    // server uptime). The timeline itself answers "time query day", so reading IT makes every gate
    // follow /time within a second; the day number counts the timeline's WRAPS, so a commanded dawn
    // opens the activation dice too ("#<id>_d < #dayn": one roll a day, the owner's scheme - a
    // place-bound event is rare, but the places near the player make it meetable). 1.21.x keeps the
    // old reads: its daytime and gametime do obey the commands there.
    QString clock = legacy121
        ? QStringLiteral("execute store result score #day osmc_enc_t run time query daytime\n"
                         "execute store result score #dayn osmc_enc_t run time query gametime\n"
                         "scoreboard players operation #dayn osmc_enc_t /= #k24000 osmc_enc_t\n")
        : QStringLiteral("scoreboard players operation #day_old osmc_enc_t = #day osmc_enc_t\n"
                         "execute store result score #day osmc_enc_t run time query day\n"
                         "execute if score #day osmc_enc_t < #day_old osmc_enc_t run scoreboard players add #dayn osmc_enc_t 1\n");

    // ---- dread (docs/city-encounters.md): the city counts its broken windows. Breaking a window is
    // the easiest way into any house, so glass is what frightens the city. The scoreboard criterion
    // minecraft.mined:<block> is the game's own counter of the glass a player has broken (a user's
    // finding, and the way to go: the baked census of window coordinates this system carried first
    // sampled half the city and missed the breaks the user counted by hand - and a real city holds
    // too many glass blocks to watch them by coordinates; the advancement trigger registry, read
    // from both target servers, has no block-break trigger at all). The pack adds two statistic
    // objectives - plain glass and glass panes - and tick.mcfunction hands every breaker to
    // dread/broke, which moves the counted panes into #dread and zeroes the scores: the counter
    // doubles as the event queue, and /scoreboard players get <player> osmc_glass shows what is
    // owed mid-cycle. Undyed glass only: the stained panes of bandit lairs and the tinted windows
    // of military bases live under other criteria the pack never adds; explosions and weather are
    // not mining either - the city feels a hand. Every counted break taps the neighbours with a
    // soft glass sound and nothing in the chat (the debug whisper was removed at the owner's
    // word: in the game it only irritated. Tiers
    // speak once per climb in the chat (re-armed when the tier falls again), multiply the event
    // rolls (an event function re-rolls its own dice, so an extra call is an extra place tonight),
    // and at the top tier open every baked ninth-and-a-half floor door of the city at once (see
    // the hf section).
    {
        load += QStringLiteral(
            "scoreboard objectives add osmc_glass minecraft.mined:minecraft.glass\n"
            "scoreboard objectives add osmc_pane minecraft.mined:minecraft.glass_pane\n");
        add(fn + QStringLiteral("dread/broke.mcfunction"), QByteArray(
            "# this player broke watched glass since the last tick - the city felt every pane\n"
            "# (run as the breaker at the breaker, so the neighbours of the break hear it)\n"
            "scoreboard players operation #dread osmc_enc_t += @s osmc_glass\n"
            "scoreboard players operation #dread osmc_enc_t += @s osmc_pane\n"
            "scoreboard players set @s osmc_glass 0\n"
            "scoreboard players set @s osmc_pane 0\n"
            "execute if score #dread osmc_enc_t matches 101.. run scoreboard players operation #dread osmc_enc_t = #k100 osmc_enc_t\n"
            "execute as @a[distance=..24] at @s run playsound minecraft:block.glass.break master @s ~ ~ ~ 0.6 0.5\n"));
        tickGlass = QStringLiteral(
            "execute as @a[scores={osmc_glass=1..}] at @s run function osmc_enc:dread/broke\n"
            "execute as @a[scores={osmc_pane=1..}] at @s run function osmc_enc:dread/broke\n");
        // the console knob (a request): function macros died in 26.x, so the levels the night reads
        // get their own one-line functions - "/function osmc_enc:dread/s95" beats a whole scoreboard
        // command; any other value still goes through /scoreboard players set #dread osmc_enc_t N
        for (const int lvl : { 0, 25, 50, 75, 95, 100 })
            add(fn + QStringLiteral("dread/s%1.mcfunction").arg(lvl), QStringLiteral(
                "# the console knob: /function osmc_enc:dread/s%1 sets the dread of the city to %1\n"
                "scoreboard players set #dread osmc_enc_t %1\n").arg(lvl).toUtf8());
        // the four tier voices (the legacy text JSON is the chat shape both formats read)
        struct Tier { const char* color; const char* text; const char* sound; };
        static const Tier kTiers[4] = {
            { "gray",        "Somewhere in the city a window stopped being a window. The night noticed.", "minecraft:block.glass.break" },
            { "dark_gray",   "The doors you passed today are locking by themselves. The city heard the glass.", "minecraft:entity.enderman.stare" },
            { "dark_purple", "The streetlights look at the pavement now. The tall windows look back.", "minecraft:entity.elder_guardian.curse" },
            { "dark_red",    "The city knows your count. Tonight everything walks, and every house has a floor you never found.", "minecraft:entity.warden.heartbeat" },
        };
        for (int t = 0; t < 4; ++t)
            add(fn + QStringLiteral("dread/msg/%1.mcfunction").arg(t + 1),
                QStringLiteral("scoreboard players set #dread_sent osmc_enc_t %1\n"
                               "tellraw @a {\"text\":\"%2\",\"color\":\"%3\",\"italic\":true}\n"
                               "execute as @a at @s run playsound %4 master @s ~ ~ ~ 0.5 0.8\n")
                    .arg(t + 1).arg(QLatin1String(kTiers[t].text)).arg(QLatin1String(kTiers[t].color))
                    .arg(QLatin1String(kTiers[t].sound)).toUtf8());
        cycle += QStringLiteral(
            // the tier ladder; #dread_sent chases it down, so a voice speaks once per climb
            "scoreboard players set #lv osmc_enc_t 0\n"
            "execute if score #dread osmc_enc_t matches 25.. run scoreboard players set #lv osmc_enc_t 1\n"
            "execute if score #dread osmc_enc_t matches 50.. run scoreboard players set #lv osmc_enc_t 2\n"
            "execute if score #dread osmc_enc_t matches 75.. run scoreboard players set #lv osmc_enc_t 3\n"
            "execute if score #dread osmc_enc_t matches 95.. run scoreboard players set #lv osmc_enc_t 4\n"
            "execute if score #lv osmc_enc_t < #dread_sent osmc_enc_t run scoreboard players operation #dread_sent osmc_enc_t = #lv osmc_enc_t\n"
            "execute if score #lv osmc_enc_t matches 1.. if score #dread_sent osmc_enc_t matches ..0 run function osmc_enc:dread/msg/1\n"
            "execute if score #lv osmc_enc_t matches 2.. if score #dread_sent osmc_enc_t matches ..1 run function osmc_enc:dread/msg/2\n"
            "execute if score #lv osmc_enc_t matches 3.. if score #dread_sent osmc_enc_t matches ..2 run function osmc_enc:dread/msg/3\n"
            "execute if score #lv osmc_enc_t matches 4.. if score #dread_sent osmc_enc_t matches ..3 run function osmc_enc:dread/msg/4\n");
    }

    // ---- calming the dread (docs/city-encounters.md): glass frightens the city, ordinary kindness
    // soothes it, so every deed below pays #dread back - the calm side of the rework of a user:
    // glass put back and a night slept through pay NOTHING now. The advancements reward the way the
    // old glass_place did - the reward revokes its own advancement, the revoke goes FIRST so that a
    // guarded return still re-arms the deed, and the cycle revokes every deed for every online hand
    // once per ten seconds as a backstop (a user watched his light deed grant exactly once: the
    // in-function revoke is the road the version may eat). A light pays one point at most once per
    // five minutes per player (osmc_lc keeps a gametime deadline, seeded zero on the first sight -
    // a missing score makes the >= comparison of the deadline fail, and that read wrong once killed
    // the deed); monsters are counted OUTSIDE the advancements, by scoreboard statistics - see below
    // points and a finished trade one, and the two together come at most three times per player
    // per Minecraft day - osmc_tn counts the day's deeds and midnight (daytime 18000) zeroes it;
    // taming a cat or a dog pays three points without limits; the fog park flattens the dread to
    // twenty-five on the ride in. The dawn takes one point back on its own. The tier ladder above
    // needs no bookkeeping: #dread_sent chases #lv down by itself.
    {
        load += QStringLiteral(
            "scoreboard objectives add osmc_lc dummy\n"
            "scoreboard objectives add osmc_tn dummy\n");
        cycle += QStringLiteral(
            "execute if score #day osmc_enc_t matches 18000..18199 run scoreboard players set @a osmc_tn 0\n"
            "execute if score #day osmc_enc_t matches 0..499 unless score #dec_d osmc_enc_t matches 1 unless score #dread osmc_enc_t matches ..0 run scoreboard players remove #dread osmc_enc_t 1\n"
            "execute if score #day osmc_enc_t matches 0..499 run scoreboard players set #dec_d osmc_enc_t 1\n"
            "execute unless score #day osmc_enc_t matches 0..499 run scoreboard players set #dec_d osmc_enc_t 0\n");
        add(fn + QStringLiteral("dread/light.mcfunction"), QByteArray(
            "# a light stands where it was dark - the city breathes easier by one (five minutes apart)\n"
            "advancement revoke @s only osmc_enc:calm_light\n"
            "execute unless score @s osmc_lc matches -2147483648.. run scoreboard players set @s osmc_lc 0\n"
            "execute store result score #deed_t osmc_enc_t run time query gametime\n"
            "execute unless score #deed_t osmc_enc_t >= @s osmc_lc run return 0\n"
            "scoreboard players operation @s osmc_lc = #deed_t osmc_enc_t\n"
            "scoreboard players add @s osmc_lc 6000\n"
            "execute unless score #dread osmc_enc_t matches ..0 run scoreboard players remove #dread osmc_enc_t 1\n"
            "playsound minecraft:block.lantern.place master @s ~ ~ ~ 0.3 1.5\n"));
        add(fn + QStringLiteral("dread/tame.mcfunction"), QByteArray(
            "# a cat or a dog chose a hand: the city trusts people again, by three, as often as once\n"
            "advancement revoke @s only osmc_enc:calm_tame\n"
            "execute unless score #dread osmc_enc_t matches ..0 run scoreboard players remove #dread osmc_enc_t 3\n"
            "tellraw @s {\"text\":\"a warm hand - the night is less sure of itself\",\"color\":\"gray\",\"italic\":true}\n"));
        add(fn + QStringLiteral("dread/rang.mcfunction"), QByteArray(
            "# the city heard its own bell: three points back; the bell and the market together come\n"
            "# at most three times a Minecraft day per hand, and midnight clears the day's count\n"
            "advancement revoke @s only osmc_enc:calm_bell\n"
            "execute if score @s osmc_tn matches 3.. run return 0\n"
            "scoreboard players add @s osmc_tn 1\n"
            "execute unless score #dread osmc_enc_t matches ..0 run scoreboard players remove #dread osmc_enc_t 3\n"
            "tellraw @a {\"text\":\"a bell answered somewhere - the city listens to its towers\",\"color\":\"gray\",\"italic\":true}\n"));
        add(fn + QStringLiteral("dread/traded.mcfunction"), QByteArray(
            "# a finished trade calms the city by one; with the bell it makes three deeds a\n"
            "# Minecraft day per hand, the count zeroed at midnight\n"
            // The vanilla trade STATISTIC would be the direct road, but the criterion of the
            // scoreboard objective broke the whole load function in 26.x (the custom statistics
            // were reshuffled), so the advancement - whose trigger both formats do have -
            // carries the deed instead.
            "advancement revoke @s only osmc_enc:calm_trade\n"
            "execute if score @s osmc_tn matches 3.. run return 0\n"
            "scoreboard players add @s osmc_tn 1\n"
            "execute unless score #dread osmc_enc_t matches ..0 run scoreboard players remove #dread osmc_enc_t 1\n"));
        add(QStringLiteral("datapacks/osmc_enc/data/osmc_enc/advancement/calm_trade.json"), QByteArray(
            "{\"criteria\":{\"trade\":{\"trigger\":\"minecraft:villager_trade\"}},"
            "\"rewards\":{\"function\":\"osmc_enc:dread/traded\"}}"));
        // Monsters calm the city through the game's own kill statistics, one objective per hostile
        // kind: ten counted kills lay the dread back by one (the owner's count, replacing the five
        // minutes of the first shape). The advancement road was this deed's first form - and it
        // never granted at all in the owner's 26.x world while the statistic objectives of the same
        // pack counted his glass to the pane; there is no way to check a grant headless, so the
        // deed moves to the mechanism this project has proven alive. No single criterion counts
        // "every monster": totalKillCount would count the owner's chickens too (his word), so the
        // pack carries the hostile list itself - the ids were checked to live in 1.21 AND 26.x, and
        // a killed: criterion of an id one family lacks would kill the whole load function, so the
        // list stays exactly as vetted (creaking and the other 26.x-only monsters stay out). World
        // loads reset the objectives: the game keeps kill statistics per world, and a pack put into
        // a lived-in world must not cash the player's past kills into tonight's calm.
        static const char* const kMonster[] = {
            "blaze", "bogged", "breeze", "cave_spider", "creeper", "drowned", "elder_guardian",
            "enderman", "endermite", "evoker", "ghast", "guardian", "hoglin", "husk", "magma_cube",
            "phantom", "piglin", "piglin_brute", "pillager", "ravager", "shulker", "silverfish",
            "skeleton", "slime", "spider", "stray", "vex", "vindicator", "warden", "witch",
            "wither", "wither_skeleton", "zoglin", "zombie", "zombie_villager" };
        const int nMon = int(sizeof(kMonster) / sizeof(kMonster[0]));
        load += QStringLiteral("scoreboard players set #k10 osmc_enc_t 10\n");
        for (int i = 0; i < nMon; ++i)
        {
            const QString kind = QLatin1String(kMonster[i]);
            const QString obj = QStringLiteral("osmc_k_%1").arg(kind);
            load += QStringLiteral("scoreboard objectives add %1 minecraft.killed:minecraft.%2\n"
                                   "scoreboard players reset * %1\n")
                        .arg(obj, kind);
            add(fn + QStringLiteral("dread/kill/%1.mcfunction").arg(kind), QStringLiteral(
                "# this kind lost one to this hand since the last count - the tens pool decides\n"
                "scoreboard players operation #kpool osmc_enc_t += @s %1\n"
                "scoreboard players set @s %1 0\n"
                "function osmc_enc:dread/kill10\n").arg(obj).toUtf8());
            tickKill += QStringLiteral(
                "execute as @a[scores={%1=1..}] at @s run function osmc_enc:dread/kill/%2\n")
                            .arg(obj, kind);
        }
        add(fn + QStringLiteral("dread/kill10.mcfunction"), QByteArray(
            "# ten counted monsters lay the dread back by one; the remainder of the pool waits for\n"
            "# the next kills across the nights, and the city spends them whole tens only\n"
            "scoreboard players operation #kt osmc_enc_t = #kpool osmc_enc_t\n"
            "scoreboard players operation #kt osmc_enc_t /= #k10 osmc_enc_t\n"
            "scoreboard players operation #kpool osmc_enc_t %= #k10 osmc_enc_t\n"
            "execute if score #kt osmc_enc_t matches 1.. run scoreboard players operation #dread osmc_enc_t -= #kt osmc_enc_t\n"
            "execute if score #dread osmc_enc_t matches ..-1 run scoreboard players set #dread osmc_enc_t 0\n"
            "execute if score #kt osmc_enc_t matches 1.. run playsound minecraft:entity.experience_orb.pickup master @s ~ ~ ~ 0.3 1.3\n"));
        // the blocks the lighting deed accepts, spelled into both "location" forms below
        static const char* const kLights[] = {
            "torch", "wall_torch", "soul_torch", "soul_wall_torch", "lantern", "soul_lantern",
            "sea_lantern", "glowstone", "candle", "white_candle", "orange_candle", "magenta_candle",
            "light_blue_candle", "yellow_candle", "lime_candle", "pink_candle", "gray_candle",
            "light_gray_candle", "cyan_candle", "purple_candle", "blue_candle", "brown_candle",
            "green_candle", "red_candle", "black_candle" };
        QString lightIds;
        const int nLight = int(sizeof(kLights) / sizeof(kLights[0]));
        for (int i = 0; i < nLight; ++i)
        {
            if (i > 0)
                lightIds += QLatin1Char(',');
            lightIds += QStringLiteral("\"minecraft:%1\"").arg(QLatin1String(kLights[i]));
        }
        // The four calm deeds filter the victim/subject/block inside the trigger's "conditions", and
        // the two format families spell those fields differently: since data pack format 101 (26.1)
        // a field like "entity"/"location" is ONE condition dispatched by "type" with namespaced
        // predicate keys, while 1.21.x (formats 48..100) reads the same field as an ARRAY of
        // conditions dispatched by "condition" with plain keys; an entity tag inside the predicate
        // resolves in both. A wrong spelling fails the whole advancement registry and the world
        // then opens in safe mode, so every export writes the form of its own format family - the
        // shapes below are the ones that very version's vanilla data ships itself (voluntary_exile
        // in 26.3, the same pair plus read_power_of_chiseled_bookshelf for the "location" field in
        // 1.21.0 and 1.21.11). The kill deed is NOT here any more: it lives in the statistics above.
        const QString adv = QStringLiteral("datapacks/osmc_enc/data/osmc_enc/advancement/");
        if (legacy121)
        {
            add(adv + QStringLiteral("calm_tame.json"), QByteArray(
                "{\"criteria\":{\"cat\":{\"trigger\":\"minecraft:tame_animal\",\"conditions\":{\"entity\":"
                "[{\"condition\":\"minecraft:entity_properties\",\"entity\":\"this\",\"predicate\":"
                "{\"type\":\"minecraft:cat\"}}]}},\"dog\":{\"trigger\":\"minecraft:tame_animal\","
                "\"conditions\":{\"entity\":[{\"condition\":\"minecraft:entity_properties\",\"entity\":"
                "\"this\",\"predicate\":{\"type\":\"minecraft:wolf\"}}]}}},\"requirements\":[[\"cat\"],"
                "[\"dog\"]],\"rewards\":{\"function\":\"osmc_enc:dread/tame\"}}"));
            add(adv + QStringLiteral("calm_light.json"), QByteArray(
                "{\"criteria\":{\"light\":{\"trigger\":\"minecraft:placed_block\",\"conditions\":"
                "{\"location\":[{\"condition\":\"minecraft:location_check\",\"predicate\":{\"block\":"
                "{\"blocks\":[") + lightIds.toUtf8()
                + "]}}}]}}},\"rewards\":{\"function\":\"osmc_enc:dread/light\"}}");
            add(adv + QStringLiteral("calm_bell.json"), QByteArray(
                "{\"criteria\":{\"use\":{\"trigger\":\"minecraft:default_block_use\",\"conditions\":"
                "{\"location\":[{\"condition\":\"minecraft:location_check\",\"predicate\":{\"block\":"
                "{\"blocks\":[\"minecraft:bell\"]}}}]}}},\"rewards\":{\"function\":"
                "\"osmc_enc:dread/rang\"}}"));
        }
        else
        {
            add(adv + QStringLiteral("calm_light.json"), QByteArray(
                "{\"criteria\":{\"light\":{\"trigger\":\"minecraft:placed_block\",\"conditions\":"
                "{\"location\":{\"type\":\"minecraft:match_block\",\"blocks\":[") + lightIds.toUtf8()
                + "]}}}},\"rewards\":{\"function\":\"osmc_enc:dread/light\"}}");
            add(adv + QStringLiteral("calm_tame.json"), QByteArray(
                "{\"criteria\":{\"cat\":{\"trigger\":\"minecraft:tame_animal\",\"conditions\":{\"entity\":"
                "{\"type\":\"minecraft:entity_properties\",\"entity\":\"this\",\"predicate\":"
                "{\"minecraft:entity_type\":\"minecraft:cat\"}}}},\"dog\":{\"trigger\":"
                "\"minecraft:tame_animal\",\"conditions\":{\"entity\":{\"type\":"
                "\"minecraft:entity_properties\",\"entity\":\"this\",\"predicate\":{\"minecraft:entity_"
                "type\":\"minecraft:wolf\"}}}}},\"requirements\":[[\"cat\"],[\"dog\"]],\"rewards\":"
                "{\"function\":\"osmc_enc:dread/tame\"}}"));
            add(adv + QStringLiteral("calm_bell.json"), QByteArray(
                "{\"criteria\":{\"use\":{\"trigger\":\"minecraft:default_block_use\",\"conditions\":"
                "{\"location\":{\"type\":\"minecraft:match_block\",\"blocks\":[\"minecraft:bell\"]}}}},"
                "\"rewards\":{\"function\":\"osmc_enc:dread/rang\"}}"));
        }
        // The backstop of the re-arm: a deed's function revokes its own advancement, and the cycle
        // (every ten seconds) revokes it once more for every online hand - a deed whose in-function
        // revoke was eaten by some version re-arms here anyway, and the cooldowns kept inside the
        // functions mean a double re-arm cannot pay twice
        cycle += QStringLiteral(
            "advancement revoke @a only osmc_enc:calm_light\n"
            "advancement revoke @a only osmc_enc:calm_tame\n"
            "advancement revoke @a only osmc_enc:calm_bell\n"
            "advancement revoke @a only osmc_enc:calm_trade\n");
        if (bookCell.size() >= 3)
        {   // the rules of the night lie on a lectern the exporter baked near the spawn; the city
            // repairs its own pedestal on every load (a taken book comes back, a broken lectern
            // stands again): bookCell is the GROUND cell, the glowstone sits one over it and the
            // lectern two (the bake's own heights - a shift here grew a second pedestal one
            // block under the baked one, a user catch). The glowstone keeps the page readable. The book
            // is re-paged on every load, guardlessly: the chunk-baked book is English-only, so a
            // Russian world must overwrite it, and a re-page costs nothing the player can lose.
            // The \n below are SNBT escapes the book GUI renders as line breaks - every item of
            // the rules starts on its own line (the owner's request: not one gray string)
            static const char* const kBookPages[] = {
                R"(The world around you is alive.\nThe city feels broken glass:\nevery window you break raises its dread.\nThe higher the dread,\nthe more often the night's events find you.\nSome are harmless.\nSome can kill you.)",
                R"(At 25, 50, 75 and 95\nthe night changes its voice.\nAt 95 every house\nopens its hidden floor.)",
                R"(The dread calms down over kindness:\n- a torch in the dark: one point,\nat most once in five minutes\n- ten monsters laid to rest: one point\n- a bell answers by three\n- a finished trade by one)",
                R"(Bells and trades together pay\nat most three times a day,\nand midnight renews the count.\nA tamed cat or dog pays three -\nas often as you earn it.)",
                R"(The fog park flattens the dread\nto twenty-five, whatever it was.\nDawn takes one point back\non its own.)",
                R"(Anything here can be switched off on the run:\nfunction osmc_enc:off/<name> turns one away,\nosmc_enc:on/<name> calls it back.\nThe dread answers the console itself:\nosmc_enc:dread/s25 - the same for\ns0, s50, s75, s95, s100.)",
            };
            QString pages;
            for (const char* const pg : kBookPages)
                pages += QLatin1String("{raw:\"") + QString::fromUtf8(pg) + QLatin1String("\"},");
            pages.chop(1);
            load += QStringLiteral("schedule function osmc_enc:dread/book 5s replace\n");
            add(fn + QStringLiteral("dread/book.mcfunction"),
                (QStringLiteral(
                    "# the rules book on the lectern by the spawn: the written_book component form both\n"
                    "# target formats read, set with data and restocked whenever the page goes missing\n"
                    "execute unless block %1 %2 %3 minecraft:glowstone run setblock %1 %2 %3 minecraft:glowstone\n"
                    "execute unless block %1 %4 %3 minecraft:lectern run setblock %1 %4 %3 minecraft:lectern\n"
                    "execute if block %1 %4 %3 minecraft:lectern run data modify block %1 %4 %3 Book set value "
                    "{id:\"minecraft:written_book\",count:1,components:{\"minecraft:written_book_content\":{"
                    "title:{raw:\"Rules of the Night\"},author:\"the city\",pages:[")
                 + pages + QStringLiteral("]}}}\n"))
                .arg(bookCell[0]).arg(bookCell[1] + 1).arg(bookCell[2]).arg(bookCell[1] + 2).toUtf8());   // %2 is the glowstone row, %4 the lectern row (the bake's top+1 / top+2)
            // the five-second pass right after the load catches the page only while the spawn chunk is
            // still loaded from the visit before - the cycle restocks the book whenever a player
            // stands near the pedestal, so a freshly entered world gets its pages on the first walk-in
            cycle += QStringLiteral(
                "# the book on the lectern goes back to its rules of the night near a player\n"
                "execute positioned %1 %2 %3 as @a[distance=..48,limit=1] run function osmc_enc:dread/book\n")
                .arg(bookCell[0]).arg(bookCell[1]).arg(bookCell[2]);
        }
    }

    // ---- the activation frame of the place-bound events (the owner's rework): the dice of an event
    // fall on evenly spaced slots of its hours — the dread tier buys the rolls (#rolls = tier + 1) and
    // the chance (#ch = ten per tier) — and every winning roll wakes exactly ONE place: the nearest
    // free marker around a player answers first, and only when nobody near woke does a second, wider
    // search reach the whole city. The night waits while nobody is online (the slots are counted in
    // world time but consumed only in front of a witness), and a woken place carries the busy score
    // of osmc_enc_b until dawn, so two legends never share a twenty-block corner.
    {
        load += QStringLiteral(
            "scoreboard objectives add osmc_enc_b dummy\n"           // a place taken by tonight's event
            "scoreboard players set #ten osmc_enc_t 10\n");
        cycle += QStringLiteral(
            "#ch: the chance of one roll; #rolls: how many rolls the tier spreads over the hours\n"
            "scoreboard players operation #ch osmc_enc_t = #lv osmc_enc_t\n"
            "scoreboard players add #ch osmc_enc_t 1\n"
            "scoreboard players operation #ch osmc_enc_t *= #ten osmc_enc_t\n"
            "scoreboard players operation #rolls osmc_enc_t = #lv osmc_enc_t\n"
            "scoreboard players add #rolls osmc_enc_t 1\n"
            // the morning hands the city back: every place is free for the next night's dice
            "execute if score #day osmc_enc_t matches 0..499 unless score #busy_l osmc_enc_t matches 1 run scoreboard players reset * osmc_enc_b\n"
            "execute if score #day osmc_enc_t matches 0..499 run scoreboard players set #busy_l osmc_enc_t 1\n"
            "execute unless score #day osmc_enc_t matches 0..499 run scoreboard players set #busy_l osmc_enc_t 0\n");
    }

    const QString noPlace =
        QStringLiteral("# no places of this kind were found in the exported world\nreturn 0\n");

    // The body of one place-bound event plus the three functions it searches through (v is never
    // empty at the call sites — each one answers noPlace itself when its kind found nothing)
    auto nearRoll = [&](const QString& id, int lo, int hi, const QVector<Prop>& v, const QString& p)
    {   // p: the ev/ path prefix of the event's places ("blink/"); the per-j go files bake their
        // own coordinates and stay exactly as the event blocks below write them
        const QString tag = QLatin1String(kKindTag[v.first().kind]);
        const QString fnp = fp + p;
        const int w = hi - lo + 1;                      // the width of the event's hours in ticks
        // the roll counter and the day stamp survive /reload (the city does not forget what it has
        // already rolled today); #<id>_w/#<id>_w2 are the window's widths the slot arithmetic divides by
        load += QStringLiteral(
            "execute unless score #%1_d osmc_enc_t matches -2147483648.. run scoreboard players set #%1_d osmc_enc_t -1\n"
            "scoreboard players set #%1_w osmc_enc_t %2\n"
            "scoreboard players set #%1_w2 osmc_enc_t %3\n")
            .arg(id).arg(w).arg(2 * w);
        add(ev + p + QStringLiteral("scan.mcfunction"), QStringLiteral(
            "# the ten places of this kind nearest to whoever stands in the city\n"
            "execute as @e[type=minecraft:marker,tag=%1,distance=..300,limit=10,sort=nearest] "
            "at @s run function %2near\n")
            .arg(tag).arg(fnp).toUtf8());
        add(ev + p + QStringLiteral("far.mcfunction"), QStringLiteral(
            "# nobody near woke — the search reaches the whole loaded city, so the legend still\n"
            "# happens somewhere tonight even when the player sleeps far from it\n"
            "execute as @e[type=minecraft:marker,tag=%1,distance=..1000,limit=6,sort=nearest] "
            "at @s run function %2near\n")
            .arg(tag).arg(fnp).toUtf8());
        QString near = QStringLiteral(
            "# one roll wakes ONE place: the nearest eligible marker answers, the rest stay asleep\n"
            "execute if score @s osmc_enc_b matches 1.. run return 0\n"
            "execute if entity @e[type=minecraft:marker,tag=osmc.enc,scores={osmc_enc_b=1..},distance=..20] run return 0\n"
            "execute if score #go osmc_enc_t matches 1.. run return 0\n"
            "scoreboard players set #go osmc_enc_t 1\n"
            "scoreboard players add @s osmc_enc_b 1\n");
        for (int j = 0; j < v.size(); ++j)
            near += QStringLiteral("execute if entity @s[tag=osmc.p%1] run function %2\n")
                        .arg(j).arg(fnp + QStringLiteral("%1/go").arg(j));
        add(ev + p + QStringLiteral("near.mcfunction"), near.toUtf8());
        // the slot of the roll: #id_k counts the slots already spent today, the (2*pos+... ) shape
        // puts the N slots at the CENTERS of N equal parts of the window (the first roll lands mid
        // into the hours, not at their opening); the arithmetic truncates toward zero, so the +w bias
        // keeps #pos positive and the -1 restores the first slot before the window's quarter mark
        return QStringLiteral(
                "# the dice of the legend: #rolls slots spread over its hours, the night waits\n"
                "# for a witness, and every winning roll wakes exactly one place near a player\n"
                "execute unless entity @a run return 0\n"
                "execute unless score #day osmc_enc_t matches %1 run return 0\n"
                // a Minecraft day (its #dayn) hands the roll counter back to the legend
                "execute if score #%2_d osmc_enc_t < #dayn osmc_enc_t run scoreboard players set #%2_k osmc_enc_t -1\n"
                "execute if score #%2_d osmc_enc_t < #dayn osmc_enc_t run scoreboard players operation #%2_d osmc_enc_t = #dayn osmc_enc_t\n"
                // #pos = round-down( 2*#rolls*(#day-lo) + w ) / 2w - 1 : the slot of this tick
                "scoreboard players operation #pos osmc_enc_t = #day osmc_enc_t\n"
                "scoreboard players remove #pos osmc_enc_t %7\n"
                "scoreboard players operation #pos osmc_enc_t *= #rolls osmc_enc_t\n"
                "scoreboard players operation #pos osmc_enc_t += #pos osmc_enc_t\n"
                "scoreboard players operation #pos osmc_enc_t += #%2_w osmc_enc_t\n"
                "scoreboard players operation #pos osmc_enc_t /= #%2_w2 osmc_enc_t\n"
                "scoreboard players remove #pos osmc_enc_t 1\n"
                "execute unless score #pos osmc_enc_t > #%2_k osmc_enc_t run return 0\n"
                "scoreboard players operation #%2_k osmc_enc_t = #pos osmc_enc_t\n"
                // the roll; the places answer through their markers
                "execute store result score #enc_r osmc_enc_t run random value 1..100\n"
                "execute unless score #enc_r osmc_enc_t <= #ch osmc_enc_t run return 0\n"
                "scoreboard players set #go osmc_enc_t 0\n"
                "execute as @a unless score #go osmc_enc_t matches 1.. at @s run function %3scan\n"
                "execute as @a unless score #go osmc_enc_t matches 1.. at @s run function %3far\n")
            .arg(QStringLiteral("%1..%2").arg(lo).arg(hi)).arg(id).arg(fnp).arg(lo);
    };

    for (int i = 0; i < kEncCount; ++i)
    {
        const QString id = QLatin1String(kEncIds[i]);
        load += QStringLiteral("scoreboard players set #%1 osmc_enc %2\n").arg(id).arg(off.contains(id) ? 0 : 1);
        cycle += QStringLiteral("execute if score #%1 osmc_enc matches 1 run function osmc_enc:event/%1\n").arg(id);
        // the dread climbs: every tier adds one more call for every enabled event (the player-bound
        // events re-roll their own dice inside, so an extra call is one more meeting tonight; the
        // place-bound ones ignore the extra calls of the same cycle through their slot guard — their
        // growth is the #ch chance and the #rolls slot count spread over the hours)
        cycle += QStringLiteral(
            "execute if score #lv osmc_enc_t matches 1.. if score #%1 osmc_enc matches 1 run function osmc_enc:event/%1\n"
            "execute if score #lv osmc_enc_t matches 2.. if score #%1 osmc_enc matches 1 run function osmc_enc:event/%1\n"
            "execute if score #lv osmc_enc_t matches 3.. if score #%1 osmc_enc matches 1 run function osmc_enc:event/%1\n").arg(id);
        add(fn + QStringLiteral("on/%1.mcfunction").arg(id),
                 QStringLiteral("scoreboard players set #%1 osmc_enc 1\n").arg(id).toUtf8());
        add(fn + QStringLiteral("off/%1.mcfunction").arg(id),
                 QStringLiteral("scoreboard players set #%1 osmc_enc 0\n").arg(id).toUtf8());

        QString body;
        if (id == QLatin1String("carousel"))
            body = QStringLiteral(
                "# a cold shimmer over the marked parks — the park sites carry code 71126000\n"
                "execute as @e[type=minecraft:marker,tag=osmc.site_71126000,limit=2,sort=random] "
                "at @s run particle minecraft:end_rod ~ ~6 ~ 4 2 4 0.01 40\n");
        else if (id == QLatin1String("blinking_lamp"))
        {   // one baked lamp goes dark and comes back beat by beat. The rare OSM lighting mast carries two
            // glowstones on a calcite stand; the exporter's own street and park posts carry a lantern,
            // whose "dark" is the empty cell it hangs in
            if (lamps.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 13000, 23999, lamps, QStringLiteral("blink/"));
                for (int j = 0; j < lamps.size(); ++j)
                {
                    const Prop& p = lamps[j];
                    const QString dark = p.dir == kLightMast ? QStringLiteral("minecraft:calcite")
                                                             : QStringLiteral("minecraft:air");
                    const QString lit = p.dir == kLightHanging ? QStringLiteral("minecraft:lantern[hanging=true]")
                                          : p.dir == kLightStanding ? QStringLiteral("minecraft:lantern")
                                                                    : QStringLiteral("minecraft:glowstone");
                    const int nLight = p.cells.size();
                    const QString pth = ev + QStringLiteral("blink/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("blink/%1/").arg(j);
                    QString step = QStringLiteral(
                        "# beat 1 puts the lamp out; the beats after bring its lights back, one by one\n"
                        "scoreboard players add #bl_%1 osmc_enc_t 1\n"
                        "execute if score #bl_%1 osmc_enc_t matches 1 run playsound minecraft:block.lever.click "
                        "master @a[x=%2,y=%3,z=%4,distance=..32] %2 %3 %4 1.0 1.6\n")
                        .arg(j).arg(p.x).arg(p.y + 3).arg(p.z);
                    for (int c = 0; c < nLight; ++c)
                    {
                        const Cell& lc = p.cells.value(c);
                        step += QStringLiteral(
                            "execute if score #bl_%1 osmc_enc_t matches 1 run setblock %2 %3 %4 %5\n"
                            "execute if score #bl_%1 osmc_enc_t matches %6 run setblock %2 %3 %4 %7\n")
                            .arg(j).arg(lc.x).arg(lc.y).arg(lc.z).arg(dark).arg(c + 2).arg(lit);
                    }
                    for (int b = 1; b <= nLight; ++b)      // the last light is restored by the last beat
                        step += QStringLiteral(
                            "execute if score #bl_%1 osmc_enc_t matches %2 run schedule function %3 2s append\n")
                            .arg(j).arg(b).arg(fnp + QStringLiteral("step"));
                    step += QStringLiteral(
                        "execute if score #bl_%1 osmc_enc_t matches %2.. run scoreboard players set #bl_%1 osmc_enc_t 0\n")
                        .arg(j).arg(nLight + 1);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the lamp's own lights are the cells of the prop; the blink runs only while\n"
                        "# someone walks below, so nobody waits for a lamp that no one sees\n"
                        "execute unless entity @a[x=%1,y=%2,z=%3,distance=..48] run return 0\n"
                        "scoreboard players set #bl_%4 osmc_enc_t 0\n"
                        "schedule function %5 1s replace\n")
                        .arg(p.x).arg(p.y + 3).arg(p.z).arg(j).arg(fnp + QStringLiteral("step")).toUtf8());
                    add(pth + QStringLiteral("step.mcfunction"), step.toUtf8());
                }
            }
        }
        else if (id == QLatin1String("local_rain"))
        {   // a wet column of splash particles and rain sound over one baked street lamp (a stylization:
            // the world weather stays untouched — docs/encounters-feasibility.md)
            if (lamps.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 0, 23999, lamps, QStringLiteral("rain/"));
                for (int j = 0; j < lamps.size(); ++j)
                {
                    const Prop& p = lamps[j];
                    const int top = p.cells.isEmpty() ? p.y + 6 : p.cells.last().y;    // the highest light
                    const QString pth = ev + QStringLiteral("rain/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("rain/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# twelve seconds of rain that falls on about nothing else\n"
                        "execute unless entity @a[x=%1,y=%2,z=%3,distance=..64] run return 0\n"
                        "scoreboard players set #rain_%4 osmc_enc_t 12\n"
                        "schedule function %5 1s replace\n")
                        .arg(p.x).arg(p.y + 3).arg(p.z).arg(j).arg(fnp + QStringLiteral("step")).toUtf8());
                    add(pth + QStringLiteral("step.mcfunction"), QStringLiteral(
                        "# the column over the lamp; seconds counted down by #rain_%1\n"
                        "particle minecraft:splash %2 %3 %4 3 3 3 0.04 180\n"
                        "playsound minecraft:weather.rain master @a[x=%2,y=%3,z=%4,distance=..32] %2 %3 %4 0.7 1.0\n"
                        "scoreboard players remove #rain_%1 osmc_enc_t 1\n"
                        "execute if score #rain_%1 osmc_enc_t matches 1.. run schedule function %5 1s append\n")
                        .arg(j).arg(p.x).arg(top + 2).arg(p.z).arg(fnp + QStringLiteral("step")).toUtf8());
                }
            }
        }
        else if (id == QLatin1String("chalk_art"))
        {   // someone erases the chalk arrow on the asphalt and draws it again in the next color
            if (chalks.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 13000, 23999, chalks, QStringLiteral("chalk/"));
                static const char* const kChalkColor[4] = { "white", "yellow", "red", "light_blue" };
                for (int j = 0; j < chalks.size(); ++j)
                {
                    const Prop& p = chalks[j];
                    QPoint cell[5];
                    encChalkCells(p, cell);
                    const QString pth = ev + QStringLiteral("chalk/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("chalk/%1/").arg(j);
                    QString go = QStringLiteral(
                        "# the pattern erases first (the original road blocks are baked), the redraw comes\n"
                        "# a few seconds later — as if someone had been standing here with a rag\n"
                        "execute unless entity @a[x=%1,y=%2,z=%3,distance=..96] run return 0\n")
                        .arg(p.x).arg(p.y).arg(p.z);
                    for (int c = 0; c < 5; ++c)
                        go += QStringLiteral("setblock %1 %2 %3 %4\n")
                                  .arg(cell[c].x()).arg(p.y).arg(cell[c].y()).arg(blockName(p.blk[c]));
                    go += QStringLiteral(
                        "scoreboard players add #ca_n osmc_enc_t 1\n"
                        "execute if score #ca_n osmc_enc_t matches 4.. run scoreboard players set #ca_n osmc_enc_t 0\n"
                        "schedule function %1 4s replace\n").arg(fnp + QStringLiteral("draw"));
                    add(pth + QStringLiteral("go.mcfunction"), go.toUtf8());
                    QString draw;
                    for (int k = 0; k < 4; ++k)
                        draw += QStringLiteral("execute if score #ca_n osmc_enc_t matches %1 run function %2\n")
                                    .arg(k).arg(fnp + QStringLiteral("c%1").arg(k));
                    add(pth + QStringLiteral("draw.mcfunction"), draw.toUtf8());
                    for (int k = 0; k < 4; ++k)
                    {
                        QString cells;
                        for (int c = 0; c < 5; ++c)
                            cells += QStringLiteral("setblock %1 %2 %3 minecraft:%4_concrete_powder\n")
                                         .arg(cell[c].x()).arg(p.y).arg(cell[c].y())
                                         .arg(QLatin1String(kChalkColor[k]));
                        add(pth + QStringLiteral("c%1.mcfunction").arg(k),
                                 (QStringLiteral("# the arrow redrawn in the %1 chalk\n").arg(QLatin1String(kChalkColor[k]))
                                  + cells).toUtf8());
                    }
                }
            }
        }
        else if (id == QLatin1String("swings"))
        {   // an invisible rider rocks a baked playground: a floating carpet teleports along the arc,
            // eight positions chained through schedules (docs/city-encounters.md flavour)
            if (plays.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 13000, 23999, plays, QStringLiteral("swing/"));
                static const double kArc[8] = { 1.0, 0.55, 0.0, -0.55, -1.0, -0.55, 0.0, 0.55 };
                static const int kAxis[4][2] = { {0, -1}, {1, 0}, {0, 1}, {-1, 0} };   // N/E/S/W
                add(ev + QStringLiteral("swing/stop.mcfunction"), QByteArray(
                    "kill @e[type=minecraft:armor_stand,tag=osmc.swing]\n"));
                for (int j = 0; j < plays.size(); ++j)
                {
                    const Prop& p = plays[j];
                    const QString pth = ev + QStringLiteral("swing/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("swing/%1/").arg(j);
                    const int ax = kAxis[p.dir & 3][0], az = kAxis[p.dir & 3][1];
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# one swing at a time in the whole world; the rider is the invisible stand whose\n"
                        "# hand holds the carpet, so only the carpet rides the arc\n"
                        "execute if entity @e[type=minecraft:armor_stand,tag=osmc.swing] run return 0\n"
                        "execute unless entity @a[x=%1,y=%2,z=%3,distance=..48] run return 0\n"
                        "summon minecraft:armor_stand %4 %5 %6 {Invisible:1b,NoGravity:1b,Silent:1b,"
                        "Tags:[\"osmc.swing\"],HandItems:[{id:\"minecraft:light_blue_carpet\",count:1b},{}]}\n"
                        // the armor/hand item fields of an entity were renamed to "equipment" in 1.21.5, so the
                        // summon NBT above only dresses the stand in 1.21.x worlds; /item is the same command
                        // in both, and setting the slot twice is the same slot
                        "item replace entity @e[type=minecraft:armor_stand,tag=osmc.swing,limit=1] "
                        "weapon.mainhand with minecraft:light_blue_carpet\n"
                        "scoreboard players set #swk_%7 osmc_enc_t 24\n"
                        "scoreboard players set #sw_live osmc_enc 30\n"
                        "schedule function %8 1s replace\n")
                        .arg(p.x).arg(p.y).arg(p.z)
                        .arg(p.x + 0.5 + kArc[0] * ax, 0, 'f', 1).arg(p.y + 1.5, 0, 'f', 1)
                        .arg(p.z + 0.5 + kArc[0] * az, 0, 'f', 1)
                        .arg(j).arg(fnp + QStringLiteral("s0")).toUtf8());
                    for (int k = 0; k < 8; ++k)
                    {
                        const double px = p.x + 0.5 + kArc[k] * ax, pz = p.z + 0.5 + kArc[k] * az;
                        add(pth + QStringLiteral("s%1.mcfunction").arg(k), QStringLiteral(
                            "# position %1 of the arc; the chain lives one second at a time\n"
                            "data modify entity @e[type=minecraft:armor_stand,tag=osmc.swing,limit=1] "
                            "Pos set value [%2d,%3d,%4d]\n"
                            "scoreboard players set #sw_live osmc_enc 30\n"
                            "scoreboard players remove #swk_%5 osmc_enc_t 1\n"
                            "execute if score #swk_%5 osmc_enc_t matches 1.. run playsound minecraft:block.chain.place "
                            "master @a[x=%6,y=%7,z=%8,distance=..24] %6 %7 %8 0.5 0.9\n"
                            "execute if score #swk_%5 osmc_enc_t matches 1.. run schedule function %9 1s append\n"
                            "execute if score #swk_%5 osmc_enc_t matches ..0 run function osmc_enc:ev/swing/stop\n")
                            .arg(k).arg(px, 0, 'f', 1).arg(p.y + 1.5, 0, 'f', 1).arg(pz, 0, 'f', 1)
                            .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(fnp + QStringLiteral("s%1").arg((k + 1) % 8)).toUtf8());
                    }
                }
                cycle += QStringLiteral(
                    "# a swing left mid-arc by a server restart must not haunt the playground\n"
                    "execute if entity @e[type=minecraft:armor_stand,tag=osmc.swing] "
                    "run scoreboard players remove #sw_live osmc_enc 20\n"
                    "execute if entity @e[type=minecraft:armor_stand,tag=osmc.swing] "
                    "if score #sw_live osmc_enc matches ..0 run function osmc_enc:ev/swing/stop\n");
            }
        }
        else if (id == QLatin1String("pumpkin_faces"))
        {   // a cemetery jack turns his carved face to the nearest player (the owner's rework): the
            // dice made the event nearly invisible, so the faces now turn GUARANTEED - every face
            // within twenty-four blocks of a witness re-aims at the closest one every two seconds,
            // no roll and no rest, but only from dusk till dawn (a user word: the day watch was
            // too loud). The markers bake on the posts and the grass carvings alike, so only
            // graveyards ever turn.
            if (jacks.isEmpty()) body = noPlace;
            else
            {
                body = QStringLiteral(
                    "# the faces watch on their own: every pumpkin turns in ev/jack/tick (the tick)\n");
                tickJack += QStringLiteral(
                    "scoreboard players add #jack_t osmc_enc_t 1\n"
                    "execute if score #jack_t osmc_enc_t matches 40.. run function osmc_enc:ev/jack/tick\n"
                    "execute if score #jack_t osmc_enc_t matches 40.. run scoreboard players set #jack_t osmc_enc_t 0\n");
                add(ev + QStringLiteral("jack/tick.mcfunction"), QByteArray(
                    "# every two seconds EVERY face within twenty-four blocks of a player re-aims at\n"
                    "# the closest witness - guaranteed, no dice, no rest, but the faces keep watch\n"
                    "# from dusk till dawn only (the day they sleep; the box is small on purpose: the\n"
                    "# scan must stay cheap on every tick)\n"
                    "execute unless score #pumpkin_faces osmc_enc matches 1 run return 0\n"
                    "execute unless score #day osmc_enc_t matches 13000..23999 run return 0\n"
                    "execute as @a at @s as @e[type=minecraft:marker,tag=osmc.kjack,distance=..24] "
                    "at @s run function osmc_enc:ev/jack/turn\n"));
                add(ev + QStringLiteral("jack/turn.mcfunction"), QByteArray(
                    "# the nearest witness owns the face: the facing's cone is picked by the rotated\n"
                    "# offsets u = dx + dz, v = dx - dz - equal signs of u and v name the axis\n"
                    "execute unless block ~ ~ ~ minecraft:jack_o_lantern unless block ~ ~ ~ minecraft:carved_pumpkin run return 0\n"
                    "execute store result score #jpx osmc_enc_t run data get entity @a[sort=nearest,limit=1,distance=..24] Pos[0]\n"
                    "execute store result score #jpz osmc_enc_t run data get entity @a[sort=nearest,limit=1,distance=..24] Pos[2]\n"
                    "execute store result score #jdx osmc_enc_t run data get entity @s Pos[0]\n"
                    "scoreboard players operation #jpx osmc_enc_t -= #jdx osmc_enc_t\n"
                    "execute store result score #jdx osmc_enc_t run data get entity @s Pos[2]\n"
                    "scoreboard players operation #jpz osmc_enc_t -= #jdx osmc_enc_t\n"
                    "scoreboard players operation #ju osmc_enc_t = #jpx osmc_enc_t\n"
                    "scoreboard players operation #ju osmc_enc_t += #jpz osmc_enc_t\n"
                    "scoreboard players operation #jv osmc_enc_t = #jpx osmc_enc_t\n"
                    "scoreboard players operation #jv osmc_enc_t -= #jpz osmc_enc_t\n"
                    "execute if score #ju osmc_enc_t matches 1.. if score #jv osmc_enc_t matches 1.. run return run function osmc_enc:ev/jack/turned/east\n"
                    "execute if score #ju osmc_enc_t matches ..-1 if score #jv osmc_enc_t matches ..-1 run return run function osmc_enc:ev/jack/turned/west\n"
                    "execute if score #ju osmc_enc_t matches 1.. if score #jv osmc_enc_t matches ..-1 run return run function osmc_enc:ev/jack/turned/south\n"
                    "execute if score #ju osmc_enc_t matches ..-1 if score #jv osmc_enc_t matches 1.. run return run function osmc_enc:ev/jack/turned/north\n"
                    "return 0\n"));
                static const char* const kFace[4] = { "north", "west", "south", "east" };
                for (const char* const f : kFace)
                    add(ev + QStringLiteral("jack/turned/%1.mcfunction").arg(QLatin1String(f)), QStringLiteral(
                        "# %1: the face turns to the nearest witness - the carved pumpkin and the lit one\n"
                        "# alike (the owner's catch: the cemetery carries both, only jacks were turning)\n"
                        "execute if block ~ ~ ~ minecraft:jack_o_lantern[facing=%1] run return 0\n"
                        "execute if block ~ ~ ~ minecraft:carved_pumpkin[facing=%1] run return 0\n"
                        "execute if block ~ ~ ~ minecraft:jack_o_lantern run setblock ~ ~ ~ minecraft:jack_o_lantern[facing=%1] replace\n"
                        "execute if block ~ ~ ~ minecraft:carved_pumpkin run setblock ~ ~ ~ minecraft:carved_pumpkin[facing=%1] replace\n"
                        "playsound minecraft:block.pumpkin.carve master @a[distance=..32] ~ ~ ~ 0.5 0.6\n")
                        .arg(QLatin1String(f)).toUtf8());
            }
        }
        else if (id == QLatin1String("cat_council"))
        {   // cats gather sitting at a baked bus stop node; they scatter at an approaching person and
            // the council goes home at dawn
            if (stops.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 13000, 23999, stops, QStringLiteral("cat/"));
                static const int kSeat[5][2] = { {0, 0}, {1, 0}, {-1, 0}, {0, 1}, {1, 1} };
                QString watch = QStringLiteral(
                    "# the council breaks up when someone comes too close, and at dawn the cats go home\n");
                for (int j = 0; j < stops.size(); ++j)
                {
                    const Prop& p = stops[j];
                    QString go = QStringLiteral(
                        "# five sitting cats on the stop node; the world keeps a bounded council\n"
                        "execute if entity @e[type=minecraft:cat,tag=osmc.cats_%1] run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..64] run return 0\n"
                        "scoreboard players set #nc osmc_enc_t 0\n"
                        "execute as @e[type=minecraft:cat,tag=osmc.cats] run scoreboard players add #nc osmc_enc_t 1\n"
                        "execute if score #nc osmc_enc_t matches 30.. run return 0\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z);
                    for (int c = 0; c < 5; ++c)
                        go += QStringLiteral("summon minecraft:cat %1 %2 %3 {Sitting:1b,Tags:[\"osmc.cats\",\"osmc.cats_%4\"]}\n")
                                  .arg(p.x + kSeat[c][0]).arg(p.y).arg(p.z + kSeat[c][1]).arg(j);
                    add(ev + QStringLiteral("cat/%1/go.mcfunction").arg(j), go.toUtf8());
                    add(ev + QStringLiteral("cat/%1/flee.mcfunction").arg(j), QStringLiteral(
                        "# a poof at the node — the council was not meant to be seen\n"
                        "particle minecraft:poof %1 %2 %3 1 0.6 1 0.1 60\n"
                        "playsound minecraft:entity.cat.hiss master @a[x=%1,y=%2,z=%3,distance=..24] %1 %2 %3 0.7 1.2\n"
                        "kill @e[type=minecraft:cat,tag=osmc.cats_%4]\n")
                        .arg(p.x).arg(p.y).arg(p.z).arg(j).toUtf8());
                    watch += QStringLiteral(
                        "execute if entity @e[type=minecraft:cat,tag=osmc.cats_%1] "
                        "if entity @a[x=%2,y=%3,z=%4,distance=..10] run function %5\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z)
                        .arg(fp + QStringLiteral("cat/%1/flee").arg(j));
                }
                watch += QStringLiteral(
                    "execute if score #day osmc_enc_t matches %1 run kill @e[type=minecraft:cat,tag=osmc.cats]\n")
                    .arg(QStringLiteral("0..11999"));
                add(ev + QStringLiteral("cat/watch.mcfunction"), watch.toUtf8());
                cycle += QStringLiteral(
                    "execute if entity @e[type=minecraft:cat,tag=osmc.cats] run function %1\n")
                    .arg(fp + QStringLiteral("cat/watch"));
            }
        }
        else if (id == QLatin1String("flasher"))
        {   // a wall-post mast lamp at a baked crossroad blinks while nobody watches; a witness within
            // 10 blocks freezes it dark — the light is for those who came to look at nothing
            if (crosses.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 12000, 23999, crosses, QStringLiteral("flash/"));
                for (int j = 0; j < crosses.size(); ++j)
                {
                    const Prop& p = crosses[j];
                    const QString pth = ev + QStringLiteral("flash/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("flash/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the mast is two wall posts with a lamp on top; one flasher lives in the world\n"
                        "execute if score #fl_on osmc_enc matches 1 run return 0\n"
                        "execute unless entity @a[x=%1,y=%2,z=%3,distance=..80] run return 0\n"
                        "setblock %1 %2 %3 minecraft:cobblestone_wall\n"
                        "setblock %1 %4 %3 minecraft:cobblestone_wall\n"
                        "setblock %1 %5 %3 minecraft:calcite\n"
                        "scoreboard players set #fl_on osmc_enc 1\n"
                        "scoreboard players set #flt osmc_enc_t 300\n"
                        "scoreboard players set #fl_h osmc_enc_t 0\n"
                        "playsound minecraft:block.beacon.power_select master @a[x=%1,y=%2,z=%3,distance=..40] %1 %2 %3 0.7 0.8\n"
                        "schedule function %6 1s replace\n")
                        .arg(p.x).arg(p.y).arg(p.z).arg(p.y + 1).arg(p.y + 2)
                        .arg(fnp + QStringLiteral("beat")).toUtf8());
                    add(pth + QStringLiteral("beat.mcfunction"), QStringLiteral(
                        "# beat of the blink: dark near a witness, sea lantern/calcite beat far from one\n"
                        "scoreboard players remove #flt osmc_enc_t 1\n"
                        "execute if score #flt osmc_enc_t matches ..0 run setblock %1 %2 %3 minecraft:air\n"
                        "execute if score #flt osmc_enc_t matches ..0 run setblock %1 %4 %3 minecraft:air\n"
                        "execute if score #flt osmc_enc_t matches ..0 run setblock %1 %6 %3 minecraft:air\n"
                        "execute if score #flt osmc_enc_t matches ..0 run scoreboard players set #fl_on osmc_enc 0\n"
                        "execute if score #flt osmc_enc_t matches ..0 run return 0\n"
                        "execute if score #fl_on osmc_enc matches ..0 run return 0\n"
                        "execute unless block %1 %4 %3 minecraft:cobblestone_wall run scoreboard players set #fl_on osmc_enc 0\n"
                        "execute unless block %1 %4 %3 minecraft:cobblestone_wall run return 0\n"
                        "scoreboard players add #fl_h osmc_enc_t 1\n"
                        "execute if score #fl_h osmc_enc_t matches 4.. run scoreboard players set #fl_h osmc_enc_t 0\n"
                        "execute if entity @a[x=%1,y=%2,z=%3,distance=..10] if block %1 %2 %3 minecraft:sea_lantern run setblock %1 %2 %3 minecraft:calcite\n"
                        "execute if entity @a[x=%1,y=%2,z=%3,distance=..10] if score #fl_h osmc_enc_t matches 0 run playsound minecraft:block.beacon.ambient master @a[x=%1,y=%2,z=%3,distance=..16] %1 %2 %3 0.8 0.6\n"
                        "execute unless entity @a[x=%1,y=%2,z=%3,distance=..10] if block %1 %2 %3 minecraft:calcite run setblock %1 %2 %3 minecraft:sea_lantern\n"
                        "execute unless entity @a[x=%1,y=%2,z=%3,distance=..10] if block %1 %2 %3 minecraft:sea_lantern run setblock %1 %2 %3 minecraft:calcite\n"
                        "execute if score #fl_on osmc_enc matches 1 run schedule function %5 1s append\n")
                         .arg(p.x).arg(p.y + 2).arg(p.z).arg(p.y + 1)
                         .arg(fnp + QStringLiteral("beat")).arg(p.y).toUtf8());
                }
            }
        }
        else if (id == QLatin1String("window_wave"))
        {   // the windows of one baked facade light up row by row bottom to top, then all go dark
            if (walls.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 13000, 23999, walls, QStringLiteral("win/"));
                for (int j = 0; j < walls.size(); ++j)
                {
                    const Prop& p = walls[j];
                    const QString pth = ev + QStringLiteral("win/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("win/%1/").arg(j);
                    // the glass grouped into rows bottom to top (the collector delivers the cells y-sorted)
                    QVector<QVector<Cell>> rows;
                    for (const Cell& c : p.cells)
                    {
                        if (rows.isEmpty() || rows.last().last().y != c.y)
                            rows.append(QVector<Cell>() << c);
                        else
                            rows.last().append(c);
                    }
                    const Cell& a0 = p.cells.first();     // the row chain speaks/sounds at the lowest glass
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# one wave at a time per wall; ~20 seconds of light climbing the house\n"
                        "execute if score #ww_%1 osmc_enc matches 1 run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..96] run return 0\n"
                        "scoreboard players set #ww_%1 osmc_enc 1\n"
                        "schedule function %5 1s replace\n")
                        .arg(j).arg(a0.x).arg(a0.y).arg(a0.z).arg(fnp + QStringLiteral("r0")).toUtf8());
                    for (int r = 0; r < rows.size(); ++r)
                    {
                        QString s = QStringLiteral(
                            "# row %1 of the wave: these windows catch light\n").arg(r);
                        for (const Cell& c : rows[r])
                            s += QStringLiteral("setblock %1 %2 %3 minecraft:sea_lantern\n")
                                     .arg(c.x).arg(c.y).arg(c.z);
                        const double pitch = rows.size() > 1 ? 0.8 + 0.4 * double(r) / double(rows.size() - 1) : 1.0;
                        s += QStringLiteral("playsound minecraft:block.glass.place master @a[x=%1,y=%2,z=%3,distance=..64] %1 %2 %3 0.3 %4\n")
                                 .arg(a0.x).arg(a0.y).arg(a0.z).arg(pitch, 0, 'f', 2);
                        if (r + 1 < rows.size())
                            s += QStringLiteral("schedule function %1 25t append\n")
                                     .arg(fnp + QStringLiteral("r%1").arg(r + 1));
                        else
                            s += QStringLiteral("schedule function %1 2s replace\n")
                                     .arg(fnp + QStringLiteral("off"));
                        add(pth + QStringLiteral("r%1.mcfunction").arg(r), s.toUtf8());
                    }
                    QString off = QStringLiteral(
                        "# and all of it goes dark at once\n");
                    for (const Cell& c : p.cells)
                        off += QStringLiteral("setblock %1 %2 %3 %4\n")
                                   .arg(c.x).arg(c.y).arg(c.z).arg(blockName(c.id));
                    off += QStringLiteral(
                        "playsound minecraft:block.redstone_torch.burnout master @a[x=%1,y=%2,z=%3,distance=..64] %1 %2 %3 0.8 1.2\n"
                        "scoreboard players set #ww_%4 osmc_enc 0\n")
                        .arg(a0.x).arg(a0.y).arg(a0.z).arg(j);
                    add(pth + QStringLiteral("off.mcfunction"), off.toUtf8());
                }
            }
        }
        else if (id == QLatin1String("matron"))
        {   // her cabinet appears by the school door; a lampless silhouette walks to it while watched
            if (schools.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 13000, 23999, schools, QStringLiteral("mat/"));
                static const char* const kFace4[4] = { "north", "east", "south", "west" };
                for (int j = 0; j < schools.size(); ++j)
                {
                    const Prop& p = schools[j];
                    const QString pth = ev + QStringLiteral("mat/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("mat/%1/").arg(j);
                    // the yaw the silhouette is turned with: it faces the cabinet as it walks
                    const Cell& c0 = p.cells[0];
                    const int ddx = p.x - c0.x, ddz = p.z - c0.z;
                    const int yaw = std::abs(ddx) >= std::abs(ddz) ? (ddx > 0 ? -90 : 90) : (ddz > 0 ? 0 : 180);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the cabinet and its plain sign; the story is in the walking, not in the text\n"
                        "execute if score #mat_%1 osmc_enc matches 1 run return 0\n"
                        "execute if entity @e[type=minecraft:armor_stand,tag=osmc_mat_%1] run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..32] run return 0\n"
                        "execute unless block %2 %3 %4 minecraft:trapped_chest run setblock %2 %3 %4 minecraft:trapped_chest[facing=%5]\n"
                        "execute unless block %2 %6 %4 minecraft:oak_wall_sign run setblock %2 %6 %4 minecraft:oak_wall_sign[facing=%5]\n"
                        "summon minecraft:armor_stand %7 %8 %9 {Invisible:1b,NoGravity:1b,Silent:1b,Tags:[\"osmc_mat\",\"osmc_mat_%1\"],Rotation:[%10f,0f]}\n"
                        "title @a[x=%2,y=%3,z=%4,distance=..24] actionbar {\"text\":\"314: her office door stands open\",\"color\":\"gray\",\"italic\":true}\n"
                        "schedule function %11 1s replace\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(QLatin1String(kFace4[p.dir & 3]))
                        .arg(p.y + 1)
                        .arg(c0.x + 0.5, 0, 'f', 1).arg(double(c0.y), 0, 'f', 1).arg(c0.z + 0.5, 0, 'f', 1)
                        .arg(yaw).arg(fnp + QStringLiteral("w0")).toUtf8());
                    for (int w = 0; w < 3; ++w)
                    {
                        const Cell& c = p.cells[w];
                        QString s = QStringLiteral(
                            "# step %1 of her walk to the cabinet\n").arg(w + 1);
                        s += QStringLiteral(
                            "execute unless entity @a[x=%1,y=%2,z=%3,distance=..40] run kill @e[type=minecraft:armor_stand,tag=osmc_mat_%4]\n"
                            "data modify entity @e[type=minecraft:armor_stand,tag=osmc_mat_%4,limit=1] Pos set value [%5d,%6d,%7d]\n"
                            "playsound minecraft:block.wool.place master @a[x=%1,y=%2,z=%3,distance=..32] %1 %2 %3 0.4 0.6\n")
                            .arg(p.x).arg(p.y).arg(p.z).arg(j)
                            .arg(c.x + 0.5, 0, 'f', 1).arg(double(c.y), 0, 'f', 1).arg(c.z + 0.5, 0, 'f', 1);
                        if (w < 2)
                            s += QStringLiteral("schedule function %1 2s append\n")
                                     .arg(fnp + QStringLiteral("w%1").arg(w + 1));
                        else
                            s += QStringLiteral(
                                "playsound minecraft:block.wooden_door.close master @a[x=%1,y=%2,z=%3,distance=..32] %1 %2 %3 1.0 0.7\n"
                                "kill @e[type=minecraft:armor_stand,tag=osmc_mat_%4]\n")
                                .arg(p.x).arg(p.y).arg(p.z).arg(j);
                        add(pth + QStringLiteral("w%1.mcfunction").arg(w), s.toUtf8());
                    }
                    if (p.cells.size() > 3)
                    {   // "her office" lights up every night after eight until someone finds her
                        // cabinet open — then the quarter calms down for good
                        QString lit = QStringLiteral(
                            "# the office she never leaves: two windows that catch light on their own\n");
                        QString dk = QStringLiteral(
                            "# the sun takes the light back; the office looks ordinary again\n");
                        for (int c = 3; c < p.cells.size(); ++c)
                        {
                            const Cell& g = p.cells[c];
                            lit += QStringLiteral("setblock %1 %2 %3 minecraft:sea_lantern\n")
                                       .arg(g.x).arg(g.y).arg(g.z);
                            dk += QStringLiteral("setblock %1 %2 %3 %4\n")
                                      .arg(g.x).arg(g.y).arg(g.z).arg(blockName(g.id));
                        }
                        lit += QStringLiteral(
                            "playsound minecraft:block.beacon.activate master @a[x=%1,y=%2,z=%3,distance=..48] %1 %2 %3 0.5 1.4\n")
                            .arg(p.x).arg(p.y).arg(p.z);
                        dk += QStringLiteral(
                            "playsound minecraft:block.redstone_torch.burnout master @a[x=%1,y=%2,z=%3,distance=..48] %1 %2 %3 0.5 1.4\n")
                            .arg(p.x).arg(p.y).arg(p.z);
                        add(pth + QStringLiteral("light.mcfunction"), lit.toUtf8());
                        add(pth + QStringLiteral("dark.mcfunction"), dk.toUtf8());
                        add(pth + QStringLiteral("calm.mcfunction"), QStringLiteral(
                            "# somebody stood long enough by the open cabinet — she stays in it now\n"
                            "scoreboard players set #mat_%1 osmc_enc 1\n"
                            "scoreboard players set #mat_f_%1 osmc_enc_t 0\n"
                            "kill @e[type=minecraft:armor_stand,tag=osmc_mat_%1]\n"
                            "playsound minecraft:block.wooden_door.close master @a[x=%2,y=%3,z=%4,distance=..32] %2 %3 %4 1.0 0.8\n"
                            "title @a[x=%2,y=%3,z=%4,distance=..24] actionbar {\"text\":\"the corridor goes quiet — 314 stayed closed all night\",\"color\":\"gray\",\"italic\":true}\n"
                            "function %5dark\n")
                            .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(fnp).toUtf8());
                        // a chest has no "open" block state (and no NBT of it), so "found" is a player
                        // standing by the cabinet for a minute: the cycle counts those seconds with #mat_f_
                        cycle += QStringLiteral(
                            "execute unless score #mat_%1 osmc_enc matches 1 if score #day osmc_enc_t matches 18000..18199 run function %2light\n"
                            "execute if score #day osmc_enc_t matches 0..199 run function %2dark\n"
                            "execute unless score #mat_%1 osmc_enc matches 1 unless entity @a[x=%3,y=%4,z=%5,distance=..3] run scoreboard players set #mat_f_%1 osmc_enc_t 0\n"
                            "execute unless score #mat_%1 osmc_enc matches 1 if entity @a[x=%3,y=%4,z=%5,distance=..3] run scoreboard players add #mat_f_%1 osmc_enc_t 1\n"
                            "execute unless score #mat_%1 osmc_enc matches 1 if score #mat_f_%1 osmc_enc_t matches 6.. run function %2calm\n")
                            .arg(j).arg(fnp).arg(p.x).arg(p.y).arg(p.z);
                    }
                }
                cycle += QStringLiteral(
                    "execute if score #day osmc_enc_t matches 0..11999 run kill @e[type=minecraft:armor_stand,tag=osmc_mat]\n");
            }
        }
        else if (id == QLatin1String("scarecrow"))
        {   // the yard scarecrow: by day it looks away from the house, by night it watches it, named
            // (the owner's rework): it also turns by itself - whenever the nearest witness within
            // twenty blocks does NOT look at him (his yaw sector does not point at his cone), he
            // swings around to watch them; the moment eyes rest on him, the day-night flip snaps
            // him back to his post - every two seconds from the tick, like the cemetery faces
            if (scares.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 13000, 23999, scares, QStringLiteral("sc/"));
                static const double kYaw[4] = { 180, -90, 0, 90 };    // the kDirNames order N/E/S/W
                static const double kFaceYaw[4] = { -90, 0, 90, 180 };// the look/* order E/S/W/N
                static const char* const kFaceN[4] = { "east", "south", "west", "north" };
                QString watch = QStringLiteral(
                    "# the flip and the swivel run every two seconds from the tick watch\n");
                for (int j = 0; j < scares.size(); ++j)
                {
                    const Prop& p = scares[j];
                    const double dayYaw = kYaw[p.dir & 3], nightYaw = kYaw[(p.dir + 2) & 3];
                    add(ev + QStringLiteral("sc/%1/go.mcfunction").arg(j), QStringLiteral(
                        "# first night of the season: it turns up facing the house already\n"
                        "execute if entity @e[type=minecraft:armor_stand,tag=osmc_sc_%1] run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..48] run return 0\n"
                        "summon minecraft:armor_stand %5 %6 %7 {Invisible:1b,NoGravity:1b,Silent:1b,Tags:[\"osmc_sc\",\"osmc_sc_%1\"],Rotation:[%8f,0f],CustomNameVisible:1b,ArmorItems:[{},{},{},{id:\"minecraft:carved_pumpkin\",count:1b}]}\n"
                        "item replace entity @e[type=minecraft:armor_stand,tag=osmc_sc_%1,limit=1] armor.head with minecraft:carved_pumpkin\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z)
                        .arg(p.x + 0.5, 0, 'f', 1).arg(double(p.y), 0, 'f', 1).arg(p.z + 0.5, 0, 'f', 1)
                        .arg(nightYaw, 0, 'f', 0).toUtf8());
                    add(ev + QStringLiteral("sc/%1/watch.mcfunction").arg(j), QStringLiteral(
                        "# the flip lives on the stand itself - and he may prefer a witness instead\n"
                        "execute as @e[type=minecraft:armor_stand,tag=osmc_sc_%1,limit=1] at @s run function osmc_enc:ev/sc/%1/watch2\n")
                        .arg(j).toUtf8());
                    QString w = QStringLiteral(
                        "# night: face to the house, carved face, the board's text; day: it looks away\n"
                        "execute unless entity @a[distance=..20] run scoreboard players set #sl osmc_enc_t 0\n"
                        "execute if entity @a[distance=..20] run function osmc_enc:ev/sc/feel\n"
                        "execute if score #sl osmc_enc_t matches 0 run scoreboard players set #se_%1 osmc_enc_t 0\n"
                        "execute if score #sl osmc_enc_t matches 1 run scoreboard players set #se_%1 osmc_enc_t 1\n"
                        "execute if score #se_%1 osmc_enc_t matches 0 if score #day osmc_enc_t matches 13000..23999 run data modify entity @s Rotation set value [%2f,0f]\n"
                        "execute if score #day osmc_enc_t matches 13000..23999 run data modify entity @s ArmorItems[3].id set value \"minecraft:carved_pumpkin\"\n"
                        "execute if score #day osmc_enc_t matches 13000..23999 run item replace entity @s armor.head with minecraft:carved_pumpkin\n"
                        "execute if score #day osmc_enc_t matches 13000..23999 run data modify entity @s CustomName set value \"leave by morning\"\n"
                        "execute if score #se_%1 osmc_enc_t matches 0 if score #day osmc_enc_t matches 0..11999 run data modify entity @s Rotation set value [%3f,0f]\n"
                        "execute if score #day osmc_enc_t matches 0..11999 run data modify entity @s ArmorItems[3].id set value \"minecraft:pumpkin\"\n"
                        "execute if score #day osmc_enc_t matches 0..11999 run item replace entity @s armor.head with minecraft:pumpkin\n"
                        "execute if score #day osmc_enc_t matches 0..11999 run data remove entity @s CustomName\n"
                        "execute if score #se_%1 osmc_enc_t matches 1 run function osmc_enc:ev/sc/look\n")
                        .arg(j).arg(nightYaw, 0, 'f', 0).arg(dayYaw, 0, 'f', 0);
                    add(ev + QStringLiteral("sc/%1/watch2.mcfunction").arg(j), w.toUtf8());
                    watch += QStringLiteral(
                        "execute if entity @e[type=minecraft:armor_stand,tag=osmc_sc_%1] run function %2\n")
                        .arg(j).arg(fp + QStringLiteral("sc/%1/watch").arg(j));
                }
                add(ev + QStringLiteral("sc/watch.mcfunction"), watch.toUtf8());
                add(ev + QStringLiteral("sc/feel.mcfunction"), QByteArray(
                    "# does the nearest witness's gaze rest on him right now? #sl=1 means NOT looking\n"
                    "# (the witness's yaw sector does not point at the cone holding the scarecrow)\n"
                    "scoreboard players set #sl osmc_enc_t 0\n"
                    "execute unless entity @a[distance=..20] run return 0\n"
                    "execute store result score #sy osmc_enc_t run data get entity @a[sort=nearest,limit=1,distance=..20] Rotation[0]\n"
                    "execute store result score #sx osmc_enc_t run data get entity @a[sort=nearest,limit=1,distance=..20] Pos[0]\n"
                    "execute store result score #sz osmc_enc_t run data get entity @a[sort=nearest,limit=1,distance=..20] Pos[2]\n"
                    "execute store result score #sw osmc_enc_t run data get entity @s Pos[0]\n"
                    "scoreboard players operation #sx osmc_enc_t -= #sw osmc_enc_t\n"
                    "execute store result score #sw osmc_enc_t run data get entity @s Pos[2]\n"
                    "scoreboard players operation #sz osmc_enc_t -= #sw osmc_enc_t\n"
                    "scoreboard players add #sy osmc_enc_t 495\n"
                    "scoreboard players operation #sf osmc_enc_t = #sy osmc_enc_t\n"
                    "scoreboard players operation #sf osmc_enc_t /= #ninety osmc_enc_t\n"
                    "scoreboard players operation #sf osmc_enc_t %= #four osmc_enc_t\n"
                    "scoreboard players operation #su osmc_enc_t = #sx osmc_enc_t\n"
                    "scoreboard players operation #su osmc_enc_t += #sz osmc_enc_t\n"
                    "scoreboard players operation #sv osmc_enc_t = #sx osmc_enc_t\n"
                    "scoreboard players operation #sv osmc_enc_t -= #sz osmc_enc_t\n"
                    "execute if score #su osmc_enc_t matches 1.. if score #sv osmc_enc_t matches 1.. if score #sf osmc_enc_t matches 2 run return 0\n"
                    "execute if score #su osmc_enc_t matches 1.. if score #sv osmc_enc_t matches ..-1 if score #sf osmc_enc_t matches 3 run return 0\n"
                    "execute if score #su osmc_enc_t matches ..-1 if score #sv osmc_enc_t matches ..-1 if score #sf osmc_enc_t matches 0 run return 0\n"
                    "execute if score #su osmc_enc_t matches ..-1 if score #sv osmc_enc_t matches 1.. if score #sf osmc_enc_t matches 1 run return 0\n"
                    "scoreboard players set #sl osmc_enc_t 1\n"));
                add(ev + QStringLiteral("sc/look.mcfunction"), QByteArray(
                    "# somebody stands at his back: he swings around to watch them\n"
                    "execute if score #su osmc_enc_t matches 1.. if score #sv osmc_enc_t matches 1.. run return run function osmc_enc:ev/sc/look/east\n"
                    "execute if score #su osmc_enc_t matches ..-1 if score #sv osmc_enc_t matches ..-1 run return run function osmc_enc:ev/sc/look/west\n"
                    "execute if score #su osmc_enc_t matches 1.. if score #sv osmc_enc_t matches ..-1 run return run function osmc_enc:ev/sc/look/south\n"
                    "execute if score #su osmc_enc_t matches ..-1 if score #sv osmc_enc_t matches 1.. run return run function osmc_enc:ev/sc/look/north\n"
                    "return 0\n"));
                for (int f = 0; f < 4; ++f)
                    add(ev + QStringLiteral("sc/look/%1.mcfunction").arg(QLatin1String(kFaceN[f])), QStringLiteral(
                        "# %1: the pumpkin swivels toward the witness; the stick creaks quietly\n"
                        "execute if data entity @s {Rotation:[%2f,0f]} run return 0\n"
                        "data modify entity @s Rotation set value [%2f,0f]\n"
                        "playsound minecraft:entity.armor_stand.hit master @a[distance=..16] ~ ~ ~ 0.4 0.7\n")
                        .arg(QLatin1String(kFaceN[f])).arg(kFaceYaw[f], 0, 'f', 0).toUtf8());
                tickScare += QStringLiteral(
                    "scoreboard players add #sc_t osmc_enc_t 1\n"
                    "execute if score #sc_t osmc_enc_t matches 40.. run function osmc_enc:ev/sc/watch\n"
                    "execute if score #sc_t osmc_enc_t matches 40.. run scoreboard players set #sc_t osmc_enc_t 0\n");
                load += QStringLiteral(
                    "scoreboard players set #ninety osmc_enc_t 90\n"
                    "scoreboard players set #four osmc_enc_t 4\n");
            }
        }
        else if (id == QLatin1String("fresh_grave"))
        {   // a grave of loose soil appears; digging it is undone by itself — the car-TNT family of
            // tricks (baked coordinates + a standing watch) but reading the BLOCK: while a mound is
            // out (#grv_j runs) the cycle looks at its cell in the loaded chunk, and the coarse soil
            // gone means somebody dug — the grave digs itself back up. Both formats feel it the same
            // way, which matters because 26.x deleted the block-breaking trigger outright
            if (graves.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 13000, 23999, graves, QStringLiteral("grave/"));
                for (int j = 0; j < graves.size(); ++j)
                {
                    const Prop& p = graves[j];
                    const QString orig = blockName(p.cells.first().id);   // what the mound covers
                    add(ev + QStringLiteral("grave/%1/go.mcfunction").arg(j), QStringLiteral(
                        "# the fresh mound: coarse soil and a hump of rooted dirt on top (#grv_%1 runs)\n"
                        "execute if score #grv_%1 osmc_enc matches 1.. run return 0\n"
                        "execute if block %2 %3 %4 minecraft:coarse_dirt run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..48] run return 0\n"
                        "scoreboard players set #grv_%1 osmc_enc 1\n"
                        "setblock %2 %3 %4 minecraft:coarse_dirt\n"
                        "setblock %2 %5 %4 minecraft:rooted_dirt\n"
                        "particle minecraft:soul %2.5 %5.5 %4.5 0.4 0.8 0.4 0.06 80\n"
                        "playsound minecraft:block.gravel.fall master @a[x=%2,y=%3,z=%4,distance=..24] %2.5 %5 %4.5 0.6 0.55\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(p.y + 1).toUtf8());
                    add(ev + QStringLiteral("grave/%1/back.mcfunction").arg(j), QStringLiteral(
                        "# somebody dug: the soil gives the grave back, the mound retires till tonight\n"
                        "scoreboard players set #grv_%1 osmc_enc 0\n"
                        "playsound minecraft:block.grass.break master @a[x=%2,y=%3,z=%4,distance=..32] %2 %3 %4 0.8 0.5\n"
                        "setblock %2 %3 %4 %5\n"
                        "execute if block %2 %6 %4 minecraft:rooted_dirt run setblock %2 %6 %4 minecraft:air\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(orig).arg(p.y + 1).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #grv_%1 osmc_enc matches 1 if loaded %2 %3 %4"
                        " unless block %2 %3 %4 minecraft:coarse_dirt run function %5\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z)
                        .arg(fp + QStringLiteral("grave/%1/back").arg(j));
                }
            }
        }
        else if (id == QLatin1String("clock_0307"))
        {   // the cemetery clock always shows 03:07: seven bell strikes at that hour, rebuilt if broken
            if (bells.isEmpty()) body = noPlace;
            else
            {
                body = QStringLiteral(
                    "# the strikes come from the cycle at the 03:07 hour (daytime 21117), not from a roll\n");
                static const char* const kFace5[4] = { "north", "east", "south", "west" };
                static const double kPitch[7] = { 1.0, 0.9, 0.85, 0.8, 0.75, 0.9, 1.1 };
                for (int j = 0; j < bells.size(); ++j)
                {
                    const Prop& p = bells[j];
                    const QString pth = ev + QStringLiteral("bell/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("bell/%1/").arg(j);
                    add(pth + QStringLiteral("ring.mcfunction"), QStringLiteral(
                        "# the hour it is, whatever the clock hands say; the tower rises if someone broke it\n"
                        "scoreboard players operation #bd_%1 osmc_enc_t = #day osmc_enc_t\n"
                        "execute unless block %2 %3 %4 minecraft:bell run fill %2 %5 %4 %2 %6 %4 minecraft:stone_bricks\n"
                        "execute unless block %2 %3 %4 minecraft:bell run setblock %2 %3 %4 minecraft:bell[facing=%7]\n"
                        "schedule function %8 1s replace\n")
                        .arg(j).arg(p.x).arg(p.y + 4).arg(p.z).arg(p.y).arg(p.y + 3)
                        .arg(QLatin1String(kFace5[p.dir & 3]))
                        .arg(fnp + QStringLiteral("r0")).toUtf8());
                    for (int r = 0; r < 7; ++r)
                    {
                        QString s = QStringLiteral(
                            "# strike %1 of seven\n").arg(r + 1);
                        s += QStringLiteral("playsound minecraft:block.bell.use master @a[x=%1,y=%2,z=%3,distance=..80] %1 %2 %3 1.2 %4\n")
                                 .arg(p.x).arg(p.y + 4).arg(p.z).arg(kPitch[r], 0, 'f', 2);
                        if (r < 6)
                            s += QStringLiteral("schedule function %1 1s append\n")
                                     .arg(fnp + QStringLiteral("r%1").arg(r + 1));
                        add(pth + QStringLiteral("r%1.mcfunction").arg(r), s.toUtf8());
                    }
                    cycle += QStringLiteral(
                        "execute if score #clock_0307 osmc_enc matches 1 if score #day osmc_enc_t matches 21117..21317 "
                        "unless score #bd_%1 osmc_enc_t = #day osmc_enc_t run function %2\n")
                        .arg(j).arg(fnp + QStringLiteral("ring"));
                }
            }
        }
        else if (id == QLatin1String("taxi"))
        {   // the night rider: the plate by his car door is the ignition; find the garage and he is gone
            if (cars.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 13000, 23999, cars, QStringLiteral("taxi/"));
                for (int j = 0; j < cars.size(); ++j)
                {
                    const Prop& p = cars[j];
                    const Cell& gar = p.cells[0];     // his "garage": a stain on a far, dark lot
                    const Cell& core = p.cells[1];    // the middle of the parked car
                    const QString pth = ev + QStringLiteral("taxi/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("taxi/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# stage 1: only the ignition plate by the car door is new (#tx_<j> counts stages)\n"
                        "execute if score #tx_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..64] run return 0\n"
                        "setblock %2 %3 %4 minecraft:stone_pressure_plate\n"
                        "scoreboard players set #tx_%1 osmc_enc 1\n"
                        "title @a[x=%2,y=%3,z=%4,distance=..16] actionbar {\"text\":\"a plate lies by the car door, where no pedal belongs\",\"color\":\"gray\",\"italic\":true}\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).toUtf8());
                    add(pth + QStringLiteral("ignite.mcfunction"), QStringLiteral(
                        "# the engine turns over in a car nobody owns; a passenger sits inside already\n"
                        "execute if entity @e[type=minecraft:armor_stand,tag=osmc_tx_%1] run return 0\n"
                        "summon minecraft:armor_stand %2 %3 %4 {Invisible:1b,NoGravity:1b,Silent:1b,Scale:1.4f,Tags:[\"osmc_tx\",\"osmc_tx_%1\"],Rotation:[90f,0f],ArmorItems:[{},{},{},{id:\"minecraft:black_shulker_box\",count:1b}]}\n"
                        "item replace entity @e[type=minecraft:armor_stand,tag=osmc_tx_%1,limit=1] armor.head with minecraft:black_shulker_box\n"
                        "playsound minecraft:entity.minecart.riding master @a[x=%5,y=%6,z=%7,distance=..32] %5 %6 %7 0.9 0.5\n")
                        .arg(j).arg(core.x + 0.5, 0, 'f', 1).arg(double(core.y), 0, 'f', 1)
                        .arg(core.z + 0.5, 0, 'f', 1).arg(p.x).arg(p.y).arg(p.z).toUtf8());
                    add(pth + QStringLiteral("stall.mcfunction"), QStringLiteral(
                        "# off the pedal: the door slams, and somewhere in the industrial flats a stain\n"
                        "# appears where his garage should be\n"
                        "kill @e[type=minecraft:armor_stand,tag=osmc_tx_%1]\n"
                        "playsound minecraft:block.wooden_door.close master @a[x=%2,y=%3,z=%4,distance=..32] %2 %3 %4 1.0 0.6\n"
                        "execute if block %5 %6 %7 minecraft:%8 run setblock %5 %6 %7 minecraft:black_concrete\n"
                        "title @a[x=%2,y=%3,z=%4,distance=..24] actionbar {\"text\":\"motor oil smell — from beyond the quarter, from the dark lot\",\"color\":\"gray\",\"italic\":true}\n"
                        "scoreboard players set #tx_%1 osmc_enc 3\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z)
                        .arg(gar.x).arg(gar.y).arg(gar.z).arg(blockName(gar.id).mid(10)).toUtf8());
                    add(pth + QStringLiteral("gone.mcfunction"), QStringLiteral(
                        "# somebody reached the lot: the garage was always empty, and the car got bored\n"
                        "setblock %2 %3 %4 %5\n"
                        "setblock %6 %7 %8 minecraft:air\n"
                        "summon minecraft:item %6 %9 %8 {Item:{id:\"minecraft:paper\",count:1b},PickupDelay:0,Health:100,CustomName:{\"text\":\"the garage is empty. it was never full\"}}\n"
                        "playsound minecraft:block.beacon.deactivate master @a[x=%6,y=%7,z=%8,distance=..48] %6 %7 %8 0.7 0.6\n"
                        "scoreboard players set #tx_%1 osmc_enc 4\n")
                        .arg(j).arg(gar.x).arg(gar.y).arg(gar.z).arg(blockName(gar.id))
                        .arg(p.x).arg(p.y).arg(p.z).arg(p.y + 1).toUtf8());
                    add(pth + QStringLiteral("watch.mcfunction"), QStringLiteral(
                        "# the plate is the ignition while he is around; the whole arc runs on four stages\n"
                        "execute if score #tx_%1 osmc_enc matches 4.. run return 0\n"
                        "execute unless block %2 %3 %4 minecraft:stone_pressure_plate run scoreboard players set #tx_%1 osmc_enc 0\n"
                        "execute if score #tx_%1 osmc_enc matches 1 if score #day osmc_enc_t matches 0..11999 run setblock %2 %3 %4 minecraft:air\n"
                        "execute if score #tx_%1 osmc_enc matches 1 if score #day osmc_enc_t matches 0..11999 run scoreboard players set #tx_%1 osmc_enc 0\n"
                        "execute if score #tx_%1 osmc_enc matches 2 if score #day osmc_enc_t matches 0..11999 run kill @e[type=minecraft:armor_stand,tag=osmc_tx_%1]\n"
                        "execute if score #tx_%1 osmc_enc matches 2 if score #day osmc_enc_t matches 0..11999 run scoreboard players set #tx_%1 osmc_enc 0\n"
                        "execute if score #tx_%1 osmc_enc matches 1 if block %2 %3 %4 minecraft:stone_pressure_plate[powered=true] run function %9ignite\n"
                        "execute if score #tx_%1 osmc_enc matches 1 if block %2 %3 %4 minecraft:stone_pressure_plate[powered=true] run scoreboard players set #tx_%1 osmc_enc 2\n"
                        "execute if score #tx_%1 osmc_enc matches 2 unless block %2 %3 %4 minecraft:stone_pressure_plate[powered=true] run function %9stall\n"
                        "execute if score #tx_%1 osmc_enc matches 3 unless block %5 %6 %7 minecraft:black_concrete run setblock %5 %6 %7 minecraft:black_concrete\n"
                        "execute if score #tx_%1 osmc_enc matches 3 if entity @a[x=%5,y=%6,z=%7,distance=..4] run function %9gone\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z)
                        .arg(gar.x).arg(gar.y).arg(gar.z)
                        .arg(fnp).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #tx_%1 osmc_enc matches 1.. run function %2watch\n")
                        .arg(j).arg(fnp).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("lurker"))
        {   // the tall one circles the lamp's light; he lets go only when the sun takes the street
            if (lurks.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 13000, 23999, lurks, QStringLiteral("lurk/"));
                static const double kFaceC[4] = { 0, 90, 180, -90 };   // the stand is invisible; the yaw only
                // keeps the four ring spots (the first four accepted rays of N,NE,E,SE,S,SW,W,NW) distinct
                for (int j = 0; j < lurks.size(); ++j)
                {
                    const Prop& p = lurks[j];
                    const QString pth = ev + QStringLiteral("lurk/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("lurk/%1/").arg(j);
                    const QString sel = QStringLiteral("@e[type=minecraft:armor_stand,tag=osmc_lk_%1,limit=1]").arg(j);
                    QString go = QStringLiteral(
                        "# he was standing there before the street noticed he is too tall for it\n"
                        "execute if entity @e[type=minecraft:armor_stand,tag=osmc_lk_%1] run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..56] run return 0\n"
                        "scoreboard players set #lk_s_%1 osmc_enc_t %5\n"
                        "summon minecraft:armor_stand %6 %7 %8 {Invisible:1b,NoGravity:1b,Silent:1b,Scale:1.7f,Tags:[\"osmc_lk\",\"osmc_lk_%1\"],Rotation:[%9f,0f]}\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(j % 4)
                        .arg(p.cells[j % 4].x + 0.5, 0, 'f', 1)
                        .arg(double(p.cells[j % 4].y), 0, 'f', 1)
                        .arg(p.cells[j % 4].z + 0.5, 0, 'f', 1)
                        .arg(kFaceC[j % 4], 0, 'f', 0);
                    add(pth + QStringLiteral("go.mcfunction"), go.toUtf8());
                    QString step = QStringLiteral(
                        "# he trades places while you look at the lamp; each trade is one more step\n"
                        "scoreboard players add #lk_s_%1 osmc_enc_t 1\n"
                        "execute if score #lk_s_%1 osmc_enc_t matches 4.. run scoreboard players set #lk_s_%1 osmc_enc_t 0\n"
                        "scoreboard players add #lk_n_%1 osmc_enc_t 1\n")
                        .arg(j);
                    for (int a = 0; a < 4; ++a)
                        step += QStringLiteral(
                            "execute if score #lk_s_%1 osmc_enc_t matches %2 run tp %3 %4 %5 %6 %7 0\n")
                            .arg(j).arg(a).arg(sel)
                            .arg(p.cells[a].x + 0.5, 0, 'f', 1).arg(double(p.cells[a].y), 0, 'f', 1)
                            .arg(p.cells[a].z + 0.5, 0, 'f', 1).arg(kFaceC[a], 0, 'f', 0);
                    step += QStringLiteral(
                        "particle minecraft:dust{color:[0.02,0.02,0.04],scale:1.5} ~ ~1.5 ~ 0.5 1.5 0.5 0.02 30\n"
                        "playsound minecraft:block.wool.place master @a[x=%2,y=%3,z=%4,distance=..40] ~ ~ ~ 0.5 0.45\n"
                        "execute if score #lk_n_%1 osmc_enc_t matches 5.. run title @a[distance=..32] actionbar {\"text\":\"something tall keeps pace with your steps\",\"color\":\"dark_gray\",\"italic\":true}\n"
                        "execute if score #lk_n_%1 osmc_enc_t matches 5.. run scoreboard players set #lk_n_%1 osmc_enc_t 0\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z);
                    add(pth + QStringLiteral("step.mcfunction"), step.toUtf8());
                    add(pth + QStringLiteral("gone.mcfunction"), QStringLiteral(
                        "# daybreak: he walks into the lamp light and simply is not there anymore\n"
                        "tp %1 %2 %3 %4 0 0\n"
                        "particle minecraft:portal %2 %3 %4 0.4 1.5 0.4 0.1 250\n"
                        "playsound minecraft:entity.enderman.teleport master @a[x=%2,y=%3,z=%4,distance=..48] %2 %3 %4 0.9 0.7\n"
                        "kill %1\n"
                        "title @a[x=%2,y=%3,z=%4,distance=..32] actionbar {\"text\":\"the lamp light holds; the extra shadow went with the night\",\"color\":\"gray\",\"italic\":true}\n")
                        .arg(sel)
                        .arg(p.x + 0.5, 0, 'f', 1).arg(p.y + 9).arg(p.z + 0.5, 0, 'f', 1).toUtf8());
                    add(pth + QStringLiteral("watch.mcfunction"), QStringLiteral(
                        "# the ring tightens when you come closer to his light\n"
                        "execute unless entity @e[type=minecraft:armor_stand,tag=osmc_lk_%1] run return 0\n"
                        "execute if score #day osmc_enc_t matches 0..11999 run function %2gone\n"
                        "execute as %3 at @s if entity @a[distance=..10] run function %2step\n")
                        .arg(j).arg(fnp).arg(sel).toUtf8());
                    cycle += QStringLiteral(
                        "execute if entity @e[type=minecraft:armor_stand,tag=osmc_lk_%1] run function %2watch\n")
                        .arg(j).arg(fnp).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("bus13"))
        {   // the 13 never really comes: at a stop without route 13 it only flickers past twice, with
            // the roar of a passing engine — the owner's rule is a couple of flickers, no real bus
            // (docs/encounters-feasibility.md). Whoever stands there very still with a clock in hand
            // turns the fear into a photographed legend, and that stop stays quiet for good
            if (stops.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 13000, 23999, stops, QStringLiteral("bus/"));
                static const int kAx4[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };   // N E S W
                for (int j = 0; j < stops.size(); ++j)
                {
                    const Prop& p = stops[j];
                    const int pa = (p.dir + 1) & 3;    // the shelter looks at the street; the 13 rushes along it
                    const int px = kAx4[pa][0], pz = kAx4[pa][1];
                    const int yaw = px > 0 ? -90 : (px < 0 ? 90 : (pz > 0 ? 0 : 180));       // the rush direction
                    const QString pth = ev + QStringLiteral("bus/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("bus/%1/").arg(j);
                    const QString killAll = QStringLiteral("kill @e[type=minecraft:armor_stand,tag=osmc_b13_%1]\n").arg(j);
                    auto flicker = [&](int at)
                    {   // the glow is given by the command, not in the summon NBT: the effect field of an
                        // entity changed its shape between 1.21.x and 26.x, while "effect give" is the same
                        QString s = QStringLiteral(
                            "summon minecraft:armor_stand %1 %2 %3 {Invisible:1b,NoGravity:1b,Silent:1b,"
                            "Invulnerable:1b,PersistenceRequired:1b,Tags:[\"osmc_b13\",\"osmc_b13_%4\"],"
                            "Rotation:[%5f,0f]}\n"
                            "effect give @e[type=minecraft:armor_stand,tag=osmc_b13_%4] minecraft:glowing 2 0 true\n")
                            .arg(p.x + px * at + 0.5, 0, 'f', 1).arg(p.y).arg(p.z + pz * at + 0.5, 0, 'f', 1)
                            .arg(j).arg(yaw);
                        if (at > 2)      // the 13 is already further along the street: kill the previous flicker
                            s = QStringLiteral("kill @e[type=minecraft:armor_stand,tag=osmc_b13_%1]\n").arg(j) + s;
                        return s;
                    };
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the 13 rushes past a stop that never had it (#bs_%1: 0 idle, 1 rushing, 2 photographed)\n"
                        "execute if score #bs_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..48] run return 0\n"
                        "scoreboard players set #bs_%1 osmc_enc 1\n"
                        "playsound minecraft:entity.minecart.riding master @a[x=%2,y=%3,z=%4,distance=..64] %2 %3 %4 1.0 0.5\n"
                        "title @a[x=%2,y=%3,z=%4,distance=..32] actionbar {\"text\":\"route 13 rushes past a stop that never had it\",\"color\":\"gray\",\"italic\":true}\n"
                        "schedule function %5blink1 1s replace\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(fnp).toUtf8());
                    add(pth + QStringLiteral("blink1.mcfunction"), QStringLiteral(
                        "# the first flicker: something tall and long glows for a beat at the curb\n"
                        "particle minecraft:end_rod %1 %2 %3 1 0.5 1 0.02 25\n"
                        "%5"
                        "playsound minecraft:entity.enderman.teleport master @a[x=%1,y=%2,z=%3,distance=..48] %1 %2 %3 0.6 1.4\n"
                        "schedule function %4blink2 1s replace\n")
                        .arg(p.x).arg(p.y).arg(p.z).arg(fnp).arg(flicker(2)).toUtf8());
                    add(pth + QStringLiteral("blink2.mcfunction"), QStringLiteral(
                        "# the second flicker is already further along the street — the 13 does not stop here\n"
                        "particle minecraft:end_rod %1 %2 %3 1 0.5 1 0.02 25\n"
                        "%5"
                        "playsound minecraft:entity.enderman.teleport master @a[x=%1,y=%2,z=%3,distance=..48] %1 %2 %3 0.6 1.1\n"
                        "schedule function %4gone 1s replace\n")
                        .arg(p.x).arg(p.y).arg(p.z).arg(fnp).arg(flicker(5)).toUtf8());
                    add(pth + QStringLiteral("gone.mcfunction"), QStringLiteral(
                        "# the roar fades towards the depot that never existed\n"
                        "%5"
                        "playsound minecraft:block.metal_pressure_plate.click_off master @a[x=%1,y=%2,z=%3,distance=..48] %1 %2 %3 0.8 0.5\n"
                        "execute unless score #bs_%4 osmc_enc matches 2 run scoreboard players set #bs_%4 osmc_enc 0\n")
                        .arg(p.x).arg(p.y).arg(p.z).arg(j).arg(killAll).toUtf8());
                    add(pth + QStringLiteral("watch.mcfunction"), QStringLiteral(
                        "# the stop remembers the 13: whoever stands very still with a clock in hand\n"
                        "# turns the fear into a photographed legend\n"
                        "execute if score #bs_%1 osmc_enc matches 2 run return 0\n"
                        "execute if score #day osmc_enc_t matches 0..11999 if entity @e[type=minecraft:armor_stand,tag=osmc_b13_%1] run function %5gone\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..3,nbt={SelectedItem:{id:\"minecraft:clock\"}}] run scoreboard players set #pt_%1 osmc_enc_t 0\n"
                        "execute if entity @a[x=%2,y=%3,z=%4,distance=..3,nbt={SelectedItem:{id:\"minecraft:clock\"}}] run scoreboard players add #pt_%1 osmc_enc_t 1\n"
                        "execute if score #pt_%1 osmc_enc_t matches 18.. run function %5legend\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(fnp).toUtf8());
                    add(pth + QStringLiteral("legend.mcfunction"), QStringLiteral(
                        "# the photo came out — the 13 becomes a story people tell, not a fear\n"
                        "scoreboard players set #bs_%1 osmc_enc 2\n"
                        "scoreboard players set #pt_%1 osmc_enc_t 0\n"
                        "%2"
                        "particle minecraft:end_rod %3 %4 %5 1 1.5 1 0.02 60\n"
                        "playsound minecraft:entity.player.levelup master @a[x=%3,y=%4,z=%5,distance=..48] %3 %4 %5 0.7 1.2\n"
                        "title @a[x=%3,y=%4,z=%5,distance=..48] actionbar {\"text\":\"you photographed the 13. the city keeps it as a legend\",\"color\":\"gold\",\"italic\":true}\n")
                        .arg(j).arg(killAll).arg(p.x).arg(p.y).arg(p.z).toUtf8());
                    cycle += QStringLiteral(
                        "execute unless score #bs_%1 osmc_enc matches 2 run function %2watch\n")
                        .arg(j).arg(fnp).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("dry_fountain"))
        {   // the night drinks the fountain: the whole bowl drains in one tick (a half-empty bowl
            // would keep flowing into the world until morning), the pool keeps a dark "mouth", and
            // after half a minute the water climbs back — colder
            if (founts.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 13000, 23999, founts, QStringLiteral("fount/"));
                for (int j = 0; j < founts.size(); ++j)
                {
                    const Prop& p = founts[j];
                    const Cell& mouth = p.cells.first();
                    const QString pth = ev + QStringLiteral("fount/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("fount/%1/").arg(j);
                    // the two setblock loops are literal text, not templates: every coordinate is
                    // the exporter's own, the %N macros would only invite renumbering mistakes
                    QString drained, filled;
                    for (const Cell& c : p.cells)
                    {
                        drained += QStringLiteral("setblock %1 %2 %3 minecraft:air\n")
                                       .arg(c.x).arg(c.y).arg(c.z);
                        filled += QStringLiteral("setblock %1 %2 %3 %4\n")
                                      .arg(c.x).arg(c.y).arg(c.z).arg(blockName(c.id));
                    }
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# one dry-out per night, and only while somebody stands by the bowl (#df_%1)\n"
                        "execute if score #df_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..48] run return 0\n"
                        "scoreboard players set #df_%1 osmc_enc 1\n"
                        "title @a[x=%2,y=%3,z=%4,distance=..32] actionbar {\"text\":\"somewhere under the bowl the stone begins to drink\",\"color\":\"gray\",\"italic\":true}\n"
                        "schedule function %5 2s replace\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(fnp + QStringLiteral("empty")).toUtf8());
                    add(pth + QStringLiteral("empty.mcfunction"), (drained + QStringLiteral(
                        "# the bowl is empty and the mouth stayed in the pool\n"
                        "setblock %1 %2 %3 minecraft:black_concrete\n"
                        "particle minecraft:splash %1 %4 %3 0.9 0.3 0.9 0.05 40\n"
                        "playsound minecraft:block.water.ambient master @a[x=%1,y=%2,z=%3,distance=..40] %1 %2 %3 0.8 0.55\n"
                        "title @a[x=%1,y=%2,z=%3,distance=..32] actionbar {\"text\":\"the fountain went silent — the water left through the bottom\",\"color\":\"gray\",\"italic\":true}\n"
                        "schedule function %5 30s replace\n"))
                        .arg(mouth.x).arg(mouth.y).arg(mouth.z).arg(mouth.y + 1)
                        .arg(fnp + QStringLiteral("full")).toUtf8());
                    add(pth + QStringLiteral("full.mcfunction"), (filled + QStringLiteral(
                        "# the water climbs back into the very same cells and closes the mouth under it\n"
                        "particle minecraft:splash %1 %4 %3 0.9 0.5 0.9 0.08 80\n"
                        "playsound minecraft:entity.generic.splash master @a[x=%1,y=%2,z=%3,distance=..40] %1 %2 %3 0.9 1.25\n"
                        "title @a[x=%1,y=%2,z=%3,distance=..32] actionbar {\"text\":\"the fountain runs again — and the water is colder than it was\",\"color\":\"gray\",\"italic\":true}\n"
                        "scoreboard players set #df_%5 osmc_enc 0\n"))
                        .arg(mouth.x).arg(mouth.y).arg(mouth.z).arg(mouth.y + 1)
                        .arg(j).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #df_%1 osmc_enc matches 1 run particle minecraft:portal %2 %3 %4 0.15 0.7 0.15 0.08 25\n"
                        "execute if score #df_%1 osmc_enc matches 1 if score #day osmc_enc_t matches 0..11999 run function %5\n")
                        .arg(j).arg(mouth.x).arg(mouth.y + 1).arg(mouth.z)
                        .arg(fnp + QStringLiteral("full")).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("last_train"))
        {   // the last train: the train the exporter dug himself walks its tunnel one cell every two
            // seconds, sweeps the platform clear, and past the hall it carries whoever stood on the
            // platform to a secret room at the end of the line (the route and the room are baked)
            if (metros.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 23000, 23999, metros, QStringLiteral("lt/"));
                static const char* const kChased[5] = { "zombie", "skeleton", "spider", "cave_spider", "creeper" };
                for (int j = 0; j < metros.size(); ++j)
                {
                    const Prop& p = metros[j];
                    const int n = p.cells.size() - 1;             // the route cells after the catch cell
                    int hallIdx = 0, hallD = 1 << 30;             // where the platform touches the route
                    for (int s = 0; s < n; ++s)
                    {
                        const Cell& c = p.cells[s + 1];
                        const int d = qMax(qAbs(c.x - p.x), qAbs(c.z - p.z));
                        if (d < hallD) { hallD = d; hallIdx = s; }
                    }
                    const int catchStep = qMin(n - 1, hallIdx + 2);   // just past the platform
                    const Cell& loot = p.cells.first();
                    const QString pth = ev + QStringLiteral("lt/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("lt/%1/").arg(j);
                    // every line is literal: the route coordinates are the exporter's own, %N macros
                    // here would only invite the renumbering mistakes (see dry_fountain above)
                    QString go = QStringLiteral(
                        "# one train per night, for whoever waits on the platform (#lt_%1 runs)\n")
                        .arg(j);
                    go += QStringLiteral("execute if score #lt_%1 osmc_enc matches 1.. run return 0\n").arg(j);
                    go += QStringLiteral("execute unless entity @a[x=%1,y=%2,z=%3,distance=..48] run return 0\n")
                              .arg(p.x).arg(p.y).arg(p.z);
                    go += QStringLiteral("scoreboard players set #lt_%1 osmc_enc 1\n").arg(j);
                    go += QStringLiteral("title @a[x=%1,y=%2,z=%3,distance=..32] actionbar "
                                         "{\"text\":\"a rumble in the tunnel — the last train tonight\","
                                         "\"color\":\"gray\",\"italic\":true}\n").arg(p.x).arg(p.y).arg(p.z);
                    go += QStringLiteral("playsound minecraft:block.note_block.pling master "
                                         "@a[x=%1,y=%2,z=%3,distance=..40] %1 %2 %3 1.0 0.5\n")
                              .arg(p.x).arg(p.y).arg(p.z);
                    for (int k = 0; k < 3; ++k)
                    {   // three invisible stands wearing minecarts: a floating minecart is the car;
                        // /item is the belt that dresses them in both 1.21.x and 26.x
                        go += QStringLiteral("summon minecraft:armor_stand %1 %2 %3 {Invisible:1b,NoGravity:1b,"
                                             "Silent:1b,Tags:[\"osmc_lt\",\"osmc_lt_%4\",\"osmc_lt_%4_%5\"],"
                                             "ArmorItems:[{},{},{},{id:\"minecraft:minecart\",count:1b}]}\n")
                                  .arg(p.cells[1].x + 0.5, 0, 'f', 1).arg(double(p.cells[1].y), 0, 'f', 1)
                                  .arg(p.cells[1].z + 0.5, 0, 'f', 1).arg(j).arg(k);
                        go += QStringLiteral("item replace entity "
                                             "@e[type=minecraft:armor_stand,tag=osmc_lt_%1_%2,limit=1] "
                                             "armor.head with minecraft:minecart\n").arg(j).arg(k);
                    }
                    go += QStringLiteral("schedule function %1 2s replace\n").arg(fnp + QStringLiteral("r0"));
                    add(pth + QStringLiteral("go.mcfunction"), go.toUtf8());
                    for (int s = 0; s < n; ++s)
                    {
                        const Cell& cp = p.cells[s + 1];
                        QString step = QStringLiteral("# step %1 of the nightly run; the train moves a cell\n")
                                           .arg(s);
                        step += QStringLiteral("execute unless score #lt_%1 osmc_enc matches 1.. run return 0\n")
                                    .arg(j);
                        for (int k = 0; k < 3; ++k)   // the tail cars wait at the first cell until pulled
                        {
                            const Cell& ck = p.cells[qMax(0, s - k) + 1];
                            step += QStringLiteral("tp @e[type=minecraft:armor_stand,tag=osmc_lt_%1_%2,limit=1] "
                                                   "%3 %4 %5\n")
                                        .arg(j).arg(k)
                                        .arg(ck.x + 0.5, 0, 'f', 1).arg(double(ck.y), 0, 'f', 1)
                                        .arg(ck.z + 0.5, 0, 'f', 1);
                        }
                        step += QStringLiteral("particle minecraft:electric_spark %1 %2 %3 0.4 0.3 0.4 0.02 14\n")
                                    .arg(cp.x + 0.5, 0, 'f', 1).arg(cp.y).arg(cp.z + 0.5, 0, 'f', 1);
                        step += QStringLiteral("playsound minecraft:entity.minecart.riding master "
                                               "@a[x=%1,y=%2,z=%3,distance=..48] %1 %2 %3 0.6 0.9\n")
                                    .arg(cp.x).arg(cp.y).arg(cp.z);
                        if (hallIdx - 3 <= s && s <= hallIdx + 3)   // the headlights sweep the platform
                            for (int m = 0; m < 5; ++m)
                                step += QStringLiteral("kill @e[type=minecraft:%1,x=%2,y=%3,z=%4,distance=..5]\n")
                                            .arg(QLatin1String(kChased[m])).arg(cp.x).arg(cp.y).arg(cp.z);
                        if (s == catchStep)
                            step += QStringLiteral("function %1\n").arg(fnp + QStringLiteral("catch"));
                        if (s + 1 < n)
                            step += QStringLiteral("schedule function %1 2s replace\n")
                                        .arg(fnp + QStringLiteral("r%1").arg(s + 1));
                        else
                            step += QStringLiteral("schedule function %1 40s replace\n")
                                        .arg(fnp + QStringLiteral("gone"));
                        add(pth + QStringLiteral("r%1.mcfunction").arg(s), step.toUtf8());
                    }
                    add(pth + QStringLiteral("catch.mcfunction"), QStringLiteral(
                        "# the cars walk through the platform and nobody boards them the normal way: the\n"
                        "# riders simply find themselves at the end of the line, beside somebody's chest\n"
                        "tp @a[x=%1,y=%2,z=%3,distance=..4] %4 %5 %6\n"
                        "particle minecraft:end_rod %4 %5 %6 0.5 0.5 0.5 0.02 40\n"
                        "playsound minecraft:entity.enderman.teleport master "
                        "@a[x=%4,y=%5,z=%6,distance=..16] %4 %5 %6 0.8 0.7\n"
                        "title @a[x=%4,y=%5,z=%6,distance=..12] actionbar "
                        "{\"text\":\"the last train carried its riders to the end of the line\","
                        "\"color\":\"gray\",\"italic\":true}\n")
                        .arg(p.x).arg(p.y).arg(p.z)
                        .arg(loot.x + 0.5, 0, 'f', 1).arg(double(loot.y), 0, 'f', 1)
                        .arg(loot.z + 0.5, 0, 'f', 1).toUtf8());
                    const Cell& last = p.cells.last();
                    add(pth + QStringLiteral("gone.mcfunction"), QStringLiteral(
                        "# the rails fall quiet; the train was never on any timetable\n"
                        "kill @e[type=minecraft:armor_stand,tag=osmc_lt_%1]\n"
                        "playsound minecraft:block.beacon.deactivate master "
                        "@a[x=%2,y=%3,z=%4,distance=..64] %2 %3 %4 0.5 0.5\n"
                        "scoreboard players set #lt_%1 osmc_enc 0\n")
                        .arg(j).arg(last.x).arg(last.y).arg(last.z).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #lt_%1 osmc_enc matches 1.. if score #day osmc_enc_t "
                        "matches 1000..11999 run function %2\n")
                        .arg(j).arg(fnp + QStringLiteral("gone")).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("bench_otherworld"))
        {   // the bench not for everyone (a user request): about every three minutes the fog walks the
            // city and with a chance takes ONE oak bench within 50 blocks of a player - its seat turns
            // dark oak (seen from afar) while the former chosen one becomes ordinary oak again, so the
            // fog always sits on exactly one bench. Standing a second in the clearing around the dark
            // bench (the seat plus two blocks) sinks the sitter into the fog park; the way home glows
            // underfoot before the middle park seat. The
            // choice is let go at dawn and when somebody is taken.
            if (benches.isEmpty()) body = noPlace;
            else
            {
                const int nB = benches.size();
                // The body runs once per cycle (200 t): a countdown of 18 cycles is the three minutes.
                // Its first two lines let the bench go when the dawn ends the fog's walk.
                body = QStringLiteral(
                    "# the fog walks the city: about every three minutes it may choose a bench near a person\n"
                    "execute if score #fob_cur osmc_enc_t matches 0.. unless score #day osmc_enc_t"
                    " matches 13000..23999 run function osmc_enc:ev/fob/quiet\n"
                    "execute if score #fob_cur osmc_enc_t matches 0.. unless score #day osmc_enc_t"
                    " matches 13000..23999 run scoreboard players set #fob_cur osmc_enc_t -1\n"
                    "scoreboard players remove #fob_cd osmc_enc_t 1\n"
                    "execute unless score #fob_cd osmc_enc_t matches ..0 run return 0\n"
                    "scoreboard players set #fob_cd osmc_enc_t 18\n"
                    "execute unless score #day osmc_enc_t matches 13000..23999 run return 0\n"
                    "execute store result score #enc_r osmc_enc_t run random value 1..100\n"
                    "execute unless score #enc_r osmc_enc_t matches 1..40 run return 0\n"
                    "# the former bench becomes ordinary before the new one goes dark\n"
                    "execute if score #fob_cur osmc_enc_t matches 0.. run function osmc_enc:ev/fob/quiet\n"
                    "scoreboard players set #fob_cur osmc_enc_t -1\n"
                    "scoreboard players set #fob_h osmc_enc_t 0\n"
                    "scoreboard players set #fob_ok osmc_enc_t 0\n"
                    // the walk of the place list starts at a random bench and closes over on itself:
                    // the first one near a person wins, a fixed order would always choose the same
                    "execute store result score #fob_c osmc_enc_t run random value 0..%1\n").arg(nB - 1);
                for (int k = 0; k < nB; ++k)
                    body += QStringLiteral(
                        "execute if score #fob_ok osmc_enc_t matches 0 run function osmc_enc:ev/fob/pick\n"
                        "execute if score #fob_ok osmc_enc_t matches 0 run scoreboard players add #fob_c osmc_enc_t 1\n"
                        "execute if score #fob_ok osmc_enc_t matches 0 if score #fob_c osmc_enc_t matches %1"
                        " run scoreboard players set #fob_c osmc_enc_t 0\n").arg(nB);
                QString pick = QStringLiteral("# the bench #fob_c points at; #fob_ok says it has been taken\n");
                QString quietAll = QStringLiteral("# the fog leaves its bench: the wood becomes ordinary oak again\n");
                QString watch = QStringLiteral("# the fog's bench watched every tick: standing in its clearing fills #fob_h\n");
                QString take = QStringLiteral(
                    "# the way in: a second in the clearing around the fog's bench, and the city falls silent\n"
                    "scoreboard players set #fob_h osmc_enc_t 0\n");
                QString back = QStringLiteral("# the way out of the fog park: back to the bench this person stood at\n");
                // the dimension is pure data — a void flat world of one biome under a frozen dusk;
                // the fields the target formats do not know are ignored, the required ones are there
                add(QStringLiteral("datapacks/osmc_enc/data/osmc_enc/dimension/fogpark.json"), QByteArray(
                    "{\"type\":\"osmc_enc:fog\",\"generator\":{\"type\":\"minecraft:flat\",\"settings\":{"
                    "\"biome\":\"minecraft:the_void\",\"layers\":[],\"features\":false,\"lakes\":false}}}")
                    );
                // the park is rebuilt idempotently on every world load; its nine chunks stay force-
                // loaded so the bake below always lands on real ground (nine void chunks, no cost)
                add(ev + QStringLiteral("fob/build.mcfunction"), QByteArray(
                    "# the fog park: rebuilt on every load, its lawn spans -9..9 at y=-61\n"
                    "execute in osmc_enc:fogpark run forceload add -16 -16 16 16\n"
                    "schedule function osmc_enc:ev/fob/bake 2s replace\n"));
                add(ev + QStringLiteral("fob/bake.mcfunction"), QByteArray(
                    "# the bedrock floor: what lies under the park is nobody's business\n"
                    "execute in osmc_enc:fogpark run fill -10 -62 -10 10 -62 10 minecraft:bedrock\n"
                    "execute in osmc_enc:fogpark run fill -9 -61 -9 9 -61 9 minecraft:moss_block\n"
                    "execute in osmc_enc:fogpark run fill -1 -61 -9 1 -61 9 minecraft:dirt_path\n"
                    "# the way home glows underfoot (a user request): sea lanterns at lawn level right\n"
                    "# before the middle seat - the fog cannot hide the exit from its guest any more\n"
                    "execute in osmc_enc:fogpark run fill -1 -61 -4 1 -61 0 minecraft:sea_lantern\n"
                    "# the lawn line is swept first: stray water of an old bake goes back to air\n"
                    "execute in osmc_enc:fogpark run fill -9 -60 -9 9 -60 9 minecraft:air\n"
                    "# the pond sits IN the lawn at its own level (the moss walls hold it); a pond\n"
                    "# poured OVER the ground walked its water across the park to the seats and the\n"
                    "# exit, and the guest of the fog never got back (a user catch)\n"
                    "execute in osmc_enc:fogpark run fill -6 -61 -6 -4 -61 -3 minecraft:water\n"
                    "# the railing: nobody should walk off the edge of the world while looking for a way out\n"
                    "execute in osmc_enc:fogpark run fill -9 -60 -9 9 -60 -9 minecraft:oak_fence\n"
                    "execute in osmc_enc:fogpark run fill -9 -60 9 9 -60 9 minecraft:oak_fence\n"
                    "execute in osmc_enc:fogpark run fill -9 -60 -9 -9 -60 9 minecraft:oak_fence\n"
                    "execute in osmc_enc:fogpark run fill 9 -60 -9 9 -60 9 minecraft:oak_fence\n"
                    "# the three seats by the path face the pond; the exit waits before the middle one\n"
                    "execute in osmc_enc:fogpark run setblock 0 -60 -3 minecraft:oak_stairs[facing=south]\n"
                    "execute in osmc_enc:fogpark run setblock -3 -60 -3 minecraft:oak_stairs[facing=south]\n"
                    "execute in osmc_enc:fogpark run setblock 3 -60 -3 minecraft:oak_stairs[facing=south]\n"
                    "execute in osmc_enc:fogpark run setblock -8 -60 -8 minecraft:sea_lantern\n"
                    "execute in osmc_enc:fogpark run setblock 8 -60 -8 minecraft:sea_lantern\n"
                    "execute in osmc_enc:fogpark run setblock -8 -60 8 minecraft:sea_lantern\n"
                    "execute in osmc_enc:fogpark run setblock 8 -60 8 minecraft:sea_lantern\n"
                    "# the trees: trunks of three kinds, crowns of one gray — the fog does not tear\n"
                    "execute in osmc_enc:fogpark run fill -6 -60 4 -6 -56 4 minecraft:dark_oak_log\n"
                    "execute in osmc_enc:fogpark run fill -8 -55 2 -4 -53 6 minecraft:light_gray_wool\n"
                    "execute in osmc_enc:fogpark run fill 5 -60 5 5 -56 5 minecraft:oak_log\n"
                    "execute in osmc_enc:fogpark run fill 3 -55 3 7 -53 7 minecraft:light_gray_wool\n"
                    "execute in osmc_enc:fogpark run fill 4 -60 -7 4 -56 -7 minecraft:birch_log\n"
                    "execute in osmc_enc:fogpark run fill 2 -55 -9 6 -53 -5 minecraft:light_gray_wool\n"));
                load += QStringLiteral("scoreboard players set #fob_h osmc_enc_t 0\n"
                                       "scoreboard players set #fob_cur osmc_enc_t -1\n"
                                       "scoreboard players set #fob_home osmc_enc_t -1\n"
                                       "scoreboard players set #fob_cd osmc_enc_t 18\n"
                                       "schedule function osmc_enc:ev/fob/build 5s replace\n");
                // the ride in; both moves across the dimension border are the map makers' way:
                // a tag in one context, the tp in the other
                add(ev + QStringLiteral("fob/enter.mcfunction"), QByteArray(
                    "# the sitting one sinks into the park; the city keeps the bench and every sound\n"
                    "# and the fog flattens the dread of the city: whatever it was, twenty-five stays (a request)\n"
                    "scoreboard players set #dread osmc_enc_t 25\n"
                    "execute in osmc_enc:fogpark positioned 0.5 -60 0.5 run tp @a[tag=osmc_fog] ~ ~ ~\n"
                    "execute in osmc_enc:fogpark positioned 0.5 -60 0.5 run particle minecraft:cloud"
                    " 0.5 -59 0.5 2 1 2 0.03 60\n"
                    "execute in osmc_enc:fogpark positioned 0.5 -60 0.5 run playsound minecraft:ambient.cave"
                    " master @a[tag=osmc_fog] ~ ~ ~ 0.5 0.7\n"
                    "execute in osmc_enc:fogpark positioned 0.5 -60 0.5 run title @a[tag=osmc_fog] actionbar"
                    " {\"text\":\"the city fell silent - this park is on no map; the way home glows "
                    "underfoot by the middle seat\",\"color\":\"gray\",\"italic\":true}\n"
                    "tag @a[tag=osmc_fog] remove osmc_fog\n"));
                static const char* const kFaceN[4] = { "north", "east", "south", "west" };   // the kDirNames order of the stair ids
                for (int j = 0; j < nB; ++j)
                {
                    const Prop& p = benches[j];
                    const Cell& t = p.cells.first();     // the standing cell before the seat
                    // the doorway of the fog (a user request): the seat's own cells plus two blocks
                    // all around - a clearing a person sees and stands in, not one exact tile; the
                    // feet test reaches one row below the seat and one row on top of it
                    int znX0 = p.cells.at(1).x, znX1 = znX0, znZ0 = p.cells.at(1).z, znZ1 = znZ0;
                    for (int c = 2; c < p.cells.size(); ++c)
                    {
                        znX0 = qMin(znX0, p.cells.at(c).x);   znX1 = qMax(znX1, p.cells.at(c).x);
                        znZ0 = qMin(znZ0, p.cells.at(c).z);   znZ1 = qMax(znZ1, p.cells.at(c).z);
                    }
                    const QString zone = QStringLiteral("x=%1,y=%2,z=%3,dx=%4,dy=2,dz=%5")
                        .arg(znX0 - 2).arg(p.cells.at(1).y - 1).arg(znZ0 - 2)
                        .arg(znX1 - znX0 + 5).arg(znZ1 - znZ0 + 5);
                    const QString pth = ev + QStringLiteral("fob/%1/").arg(j);
                    QString dark, restock;                                   // what the fog does to the seat, and back
                    for (int c = 1; c < p.cells.size(); ++c)
                    {   // the seat cells carry their baked oak_stairs id, its low bits are the facing
                        const Cell& s = p.cells[c];
                        const QString face = QLatin1String(kFaceN[(int(s.id) - int(IdStairsN)) & 3]);
                        dark += QStringLiteral("setblock %1 %2 %3 minecraft:dark_oak_stairs[facing=%4]\n")
                                    .arg(s.x).arg(s.y).arg(s.z).arg(face);
                        restock += QStringLiteral("setblock %1 %2 %3 minecraft:oak_stairs[facing=%4]\n")
                                     .arg(s.x).arg(s.y).arg(s.z).arg(face);
                    }
                    add(pth + QStringLiteral("try.mcfunction"), (QStringLiteral(
                        "# a candidate for tonight: the fog sits where a person stands close enough to see\n"
                        "# the bench change (50 blocks), the first such bench of the walk is the chosen one\n"
                        "execute in minecraft:overworld positioned %1 %2 %3 unless entity @a[distance=..50] run return 0\n"
                        "scoreboard players set #fob_ok osmc_enc_t 1\n"
                        "scoreboard players set #fob_cur osmc_enc_t %4\n")
                        .arg(t.x).arg(t.y).arg(t.z).arg(j)
                        + dark + QStringLiteral(
                        "particle minecraft:cloud %1 %2 %3 1 0.5 1 0.02 30\n"
                        "playsound minecraft:block.wood.place master @a[x=%1,y=%2,z=%3,distance=..24] %1 %2 %3 0.4 0.6\n")
                        .arg(t.x).arg(t.y).arg(t.z)).toUtf8());
                    add(pth + QStringLiteral("quiet.mcfunction"), restock.toUtf8());
                    // the fog took this bench: whoever stood in its clearing is in the park now, the
                    // wood forgets its darkness, and #fob_home knows where the way out must lead back to
                    add(pth + QStringLiteral("take.mcfunction"), QStringLiteral(
                        "tag @a[%1] add osmc_fog\n"
                        "execute unless entity @a[tag=osmc_fog] run return 0\n"
                        "scoreboard players set #fob_home osmc_enc_t %2\n"
                        "function osmc_enc:ev/fob/enter\n"
                        "function osmc_enc:ev/fob/%2/quiet\n"
                        "scoreboard players set #fob_cur osmc_enc_t -1\n")
                        .arg(zone).arg(j).toUtf8());
                    add(pth + QStringLiteral("leave.mcfunction"), QStringLiteral(
                        "# up from the park bench - the city takes this person back to the seat\n"
                        "execute in osmc_enc:fogpark run tag @a[x=-1,y=-60,z=-4,dx=2,dy=1,dz=4] add osmc_back\n"
                        "execute if entity @a[tag=osmc_back] in minecraft:overworld positioned %1 %2 %3"
                        " run tp @a[tag=osmc_back] ~ ~ ~\n"
                        "execute if entity @a[tag=osmc_back] in minecraft:overworld positioned %1 %2 %3"
                        " run playsound minecraft:block.grass.break master @a[distance=..16]"
                        " ~ ~ ~ 1.0 0.6\n"
                        "execute if entity @a[tag=osmc_back] in minecraft:overworld positioned %1 %2 %3"
                        " run title @a[tag=osmc_back] actionbar"
                        " {\"text\":\"the traffic came back - the bench here is cold, as it should be\","
                        "\"color\":\"gray\",\"italic\":true}\n"
                        "tag @a[tag=osmc_back] remove osmc_back\n"
                        "scoreboard players set #fob_home osmc_enc_t -1\n")
                        .arg(t.x).arg(t.y).arg(t.z).toUtf8());
                    pick += QStringLiteral("execute if score #fob_c osmc_enc_t matches %1"
                                           " run function osmc_enc:ev/fob/%1/try\n").arg(j);
                    quietAll += QStringLiteral("execute if score #fob_cur osmc_enc_t matches %1"
                                               " run function osmc_enc:ev/fob/%1/quiet\n").arg(j);
                    watch += QStringLiteral(
                        "execute if score #fob_cur osmc_enc_t matches %1 in minecraft:overworld"
                        " if entity @a[%2] run scoreboard players add #fob_h osmc_enc_t 1\n"
                        "execute if score #fob_cur osmc_enc_t matches %1 in minecraft:overworld"
                        " unless entity @a[%2] run scoreboard players set #fob_h osmc_enc_t 0\n")
                        .arg(j).arg(zone);
                    take += QStringLiteral(
                        "execute if score #fob_cur osmc_enc_t matches %1 in minecraft:overworld"
                        " run function osmc_enc:ev/fob/%1/take\n")
                        .arg(j);
                    back += QStringLiteral("execute if score #fob_home osmc_enc_t matches %1"
                                           " run function osmc_enc:ev/fob/%1/leave\n").arg(j);
                }
                add(ev + QStringLiteral("fob/pick.mcfunction"), pick.toUtf8());
                add(ev + QStringLiteral("fob/quiet.mcfunction"), quietAll.toUtf8());
                add(ev + QStringLiteral("fob/watch.mcfunction"), watch.toUtf8());
                add(ev + QStringLiteral("fob/take.mcfunction"), take.toUtf8());
                add(ev + QStringLiteral("fob/back.mcfunction"), back.toUtf8());
                // the tick hooks of the fog's bench: a second (20 ticks) standing in the clearing of
                // the chosen one is the way in; standing on the glowing tile is the way back
                tickFob = QStringLiteral(
                    "execute if score #fob_cur osmc_enc_t matches 0.. run function osmc_enc:ev/fob/watch\n"
                    "execute if score #fob_h osmc_enc_t matches 20.. run function osmc_enc:ev/fob/take\n"
                    "execute if score #fob_home osmc_enc_t matches 0.. run function osmc_enc:ev/fob/back\n");
                cycle += QStringLiteral(
                    "execute in osmc_enc:fogpark positioned 0.5 -60 0.5 if entity @a[distance=..40]"
                    " run particle minecraft:cloud 0.5 -59 0.5 9 2 9 0.03 80\n"
                    "execute in osmc_enc:fogpark positioned 0.5 -60 -2 if entity @a[distance=..40]"
                    " run particle minecraft:end_rod 0.5 -58.5 -2 1 1 1 0.005 6\n");
            }
        }
        else if (id == QLatin1String("floor_half"))
        {   // the floor between the floors: after midnight the stairwell of a tall house grows a
            // door that is on no plan of the house; whoever opens it from the flight is standing on
            // the same landing mirrored - windows into the void, doors onto nothing - and only ONE
            // of the four doors of that floor lets the guest back, one door per guest, chosen by
            // the floor at the trade (the owner's rule). The floor receives each guest
            // fifty-fifty: a chest left in the hall, or company that did not come from the city
            // above (docs/city-encounters.md)
            if (halfs.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 13000, 23999, halfs, QStringLiteral("hf/"));
                // the mirrored floor stands in its own void dimension (the dusk type is shared
                // with the fog park and written below the loop)
                add(QStringLiteral("datapacks/osmc_enc/data/osmc_enc/dimension/half.json"), QByteArray(
                    "{\"type\":\"osmc_enc:fog\",\"generator\":{\"type\":\"minecraft:flat\",\"settings\":{"
                    "\"biome\":\"minecraft:the_void\",\"layers\":[],\"features\":false,\"lakes\":false}}}"));
                // the floor is rebuilt idempotently on every load; its nine chunks stay force-loaded
                add(ev + QStringLiteral("hf/build.mcfunction"), QByteArray(
                    "# the floor of no plan: rebuilt on every load, the hall spans -6..6 at y=-40\n"
                    "execute in osmc_enc:half run forceload add -16 -16 16 16\n"
                    "schedule function osmc_enc:ev/hf/bake 2s replace\n"));
                add(ev + QStringLiteral("hf/bake.mcfunction"), QByteArray(
                    "execute in osmc_enc:half run fill -6 -41 -3 6 -41 3 minecraft:polished_andesite\n"
                    "execute in osmc_enc:half run fill -7 -35 -4 7 -35 4 minecraft:polished_andesite\n"
                    "execute in osmc_enc:half run fill -6 -40 -3 6 -36 -3 minecraft:stone_bricks\n"
                    "execute in osmc_enc:half run fill -6 -40 3 6 -36 3 minecraft:stone_bricks\n"
                    "execute in osmc_enc:half run fill -6 -40 -3 -6 -36 3 minecraft:stone_bricks\n"
                    "execute in osmc_enc:half run fill 6 -40 -3 6 -36 3 minecraft:stone_bricks\n"
                    "# the long walls are windows: the city outside this floor never was\n"
                    "execute in osmc_enc:half run fill -4 -40 -3 4 -36 -3 minecraft:glass\n"
                    "execute in osmc_enc:half run fill -4 -40 3 4 -36 3 minecraft:glass\n"
                    "# behind the doors that open onto nothing stands nobody's ground\n"
                    "execute in osmc_enc:half run fill 7 -40 -3 7 -36 3 minecraft:barrier\n"
                    "execute in osmc_enc:half run fill -7 -40 -3 -7 -36 3 minecraft:barrier\n"
                    "execute in osmc_enc:half run setblock -6 -40 -1 minecraft:oak_door[facing=east,half=lower]\n"
                    "execute in osmc_enc:half run setblock -6 -39 -1 minecraft:oak_door[facing=east,half=upper]\n"
                    "execute in osmc_enc:half run setblock -6 -40 1 minecraft:oak_door[facing=east,half=lower]\n"
                    "execute in osmc_enc:half run setblock -6 -39 1 minecraft:oak_door[facing=east,half=upper]\n"
                    "execute in osmc_enc:half run setblock 6 -40 -2 minecraft:oak_door[facing=west,half=lower]\n"
                    "execute in osmc_enc:half run setblock 6 -39 -2 minecraft:oak_door[facing=west,half=upper]\n"
                    "execute in osmc_enc:half run setblock 6 -40 2 minecraft:oak_door[facing=west,half=lower]\n"
                    "execute in osmc_enc:half run setblock 6 -39 2 minecraft:oak_door[facing=west,half=upper]\n"
                    "# the lights of a floor nobody drew\n"
                    "execute in osmc_enc:half run setblock -3 -35 0 minecraft:sea_lantern\n"
                    "execute in osmc_enc:half run setblock 3 -35 0 minecraft:sea_lantern\n"));
                // the floor receives each guest fifty-fifty: half the visits find a chest in the
                // hall (nine of ten plain, one of ten from a mansion), half meet company - five to
                // fifteen ordinary horrors, or the one thing that hears hearts. No creepers (the
                // hall would not outlive them), no bosses; the morning wipes both halves away.
                add(ev + QStringLiteral("hf/roll.mcfunction"), QByteArray(
                    "# what did the floor prepare for this guest: a gift, or company\n"
                    "execute store result score #hf_kind osmc_enc run random value 1..2\n"
                    "execute if score #hf_kind osmc_enc matches 1 in osmc_enc:half"
                        " run function osmc_enc:ev/hf/prize\n"
                    "execute if score #hf_kind osmc_enc matches 2 in osmc_enc:half"
                        " run function osmc_enc:ev/hf/punish\n"));
                add(ev + QStringLiteral("hf/prize.mcfunction"), QByteArray(
                    "# a chest grows in the empty hall; the expensive one is the rare one\n"
                    "setblock 0 -40 -2 minecraft:chest[facing=south]\n"
                    "execute store result score #hf_loot osmc_enc run random value 1..10\n"
                    "execute if score #hf_loot osmc_enc matches 1 run loot replace block 0 -40 -2"
                        " contents loot minecraft:chests/woodland_mansion\n"
                    "execute if score #hf_loot osmc_enc matches 2.. run loot replace block 0 -40 -2"
                        " contents loot minecraft:chests/simple_dungeon\n"
                    "execute if score #hf_loot osmc_enc matches 1 run playsound minecraft:block.chest.open"
                        " master @a[tag=osmc_half] 0.5 -39 -1.5 1.0 0.7\n"
                    "execute if score #hf_loot osmc_enc matches 2.. run playsound minecraft:block.chest.open"
                        " master @a[tag=osmc_half] 0.5 -39 -1.5 1.0 1.3\n"
                    "title @a[tag=osmc_half] actionbar"
                    " {\"text\":\"a chest stood in the hall where a moment ago there was none\","
                    "\"color\":\"gold\",\"italic\":true}\n"));
                add(ev + QStringLiteral("hf/punish.mcfunction"), QByteArray(
                    "# or the hall is not empty: a crowd of ordinary horrors, or one very old one\n"
                    "execute store result score #hf_mob osmc_enc run random value 1..10\n"
                    "execute if score #hf_mob osmc_enc matches 1 run summon minecraft:warden 2.5 -40 0.5\n"
                    "execute if score #hf_mob osmc_enc matches 1 run playsound minecraft:entity.warden.agitated"
                        " master @a[tag=osmc_half] 2.5 -40 0.5 1.0 1.0\n"
                    "execute if score #hf_mob osmc_enc matches 1 run title @a[tag=osmc_half] actionbar"
                    " {\"text\":\"something under the floor stopped digging and started listening\","
                    "\"color\":\"dark_purple\",\"italic\":true}\n"
                    "execute if score #hf_mob osmc_enc matches 2.. run function osmc_enc:ev/hf/count\n"
                    "execute if score #hf_mob osmc_enc matches 2.. run playsound"
                        " minecraft:entity.enderman.scream master @a[tag=osmc_half] 3.5 -40 2.5 1.5 0.5\n"
                    "execute if score #hf_mob osmc_enc matches 2.. run title @a[tag=osmc_half] actionbar"
                    " {\"text\":\"the hall is not empty any more\","
                    "\"color\":\"dark_red\",\"italic\":true}\n"));
                add(ev + QStringLiteral("hf/count.mcfunction"), QByteArray(
                    "# five to fifteen of them, as the floor counts\n"
                    "execute store result score #hf_n osmc_enc run random value 5..15\n"
                    "function osmc_enc:ev/hf/spawn1\n"));
                add(ev + QStringLiteral("hf/spawn1.mcfunction"), QByteArray(
                    "# one guest at a time, of a kind the floor picks; no creepers, no bosses,\n"
                    "# and the warm places are allowed to lend their horrors too\n"
                    "execute if score #hf_n osmc_enc matches ..0 run return 0\n"
                    "scoreboard players remove #hf_n osmc_enc 1\n"
                    "execute store result score #hf_m osmc_enc run random value 1..18\n"
                    "execute if score #hf_m osmc_enc matches 1 run summon minecraft:zombie 3.5 -40 2.5\n"
                    "execute if score #hf_m osmc_enc matches 2 run summon minecraft:husk 3.5 -40 2.5\n"
                    "execute if score #hf_m osmc_enc matches 3 run summon minecraft:drowned 3.5 -40 2.5\n"
                    "execute if score #hf_m osmc_enc matches 4 run summon minecraft:skeleton 3.5 -40 2.5\n"
                    "execute if score #hf_m osmc_enc matches 5 run summon minecraft:stray 3.5 -40 2.5\n"
                    "execute if score #hf_m osmc_enc matches 6 run summon minecraft:bogged 3.5 -40 2.5\n"
                    "execute if score #hf_m osmc_enc matches 7 run summon minecraft:spider 3.5 -40 2.5\n"
                    "execute if score #hf_m osmc_enc matches 8 run summon minecraft:cave_spider 3.5 -40 2.5\n"
                    "execute if score #hf_m osmc_enc matches 9 run summon minecraft:enderman 3.5 -40 2.5\n"
                    "execute if score #hf_m osmc_enc matches 10 run summon minecraft:slime 3.5 -40 2.5\n"
                    "execute if score #hf_m osmc_enc matches 11 run summon minecraft:silverfish 3.5 -40 2.5\n"
                    "execute if score #hf_m osmc_enc matches 12 run summon minecraft:witch 3.5 -40 2.5\n"
                    "execute if score #hf_m osmc_enc matches 13 run summon minecraft:blaze 3.5 -40 2.5\n"
                    "execute if score #hf_m osmc_enc matches 14 run summon minecraft:wither_skeleton 3.5 -40 2.5\n"
                    "execute if score #hf_m osmc_enc matches 15 run summon minecraft:magma_cube 3.5 -40 2.5\n"
                    "execute if score #hf_m osmc_enc matches 16 run summon minecraft:piglin 3.5 -40 2.5\n"
                    "execute if score #hf_m osmc_enc matches 17 run summon minecraft:piglin_brute 3.5 -40 2.5\n"
                    "execute if score #hf_m osmc_enc matches 18 run summon minecraft:breeze 3.5 -40 2.5\n"
                    "execute if score #hf_n osmc_enc matches 1.. run function osmc_enc:ev/hf/spawn1\n"));
                add(ev + QStringLiteral("hf/wipe.mcfunction"), QByteArray(
                    "# morning: the floor forgets its gift and its company alike. The kill is pinned\n"
                    "# to the hall on purpose: an unpositioned @e sees every dimension, and this\n"
                    "# line was quietly killing every creature of the city each day (a user catch)\n"
                    "kill @e[type=!minecraft:player,distance=..96]\n"
                    "setblock 0 -40 -2 minecraft:air\n"));
                load += QStringLiteral("scoreboard objectives add osmc_enc_p dummy\n"
                                       "scoreboard objectives add osmc_enc_g dummy\n"
                                       "schedule function osmc_enc:ev/hf/build 6s replace\n");
                static const char* const kHFDoor[4][3] = {
                    { "-6", "-40", "-1" }, { "-6", "-40", "1" }, { "6", "-40", "-2" }, { "6", "-40", "2" } };
                static const char* const kFacing[4] = { "north", "east", "south", "west" };
                static const int kFaceX[4] = { 0, 1, 0, -1 }, kFaceZ[4] = { -1, 0, 1, 0 };
                // the storm of the top tier: at dread 95+ every night cell sets doors at EVERY tall
                // house at once (idempotent, silent) - the night the owner asked for, "a floor you
                // never found" in almost every house; the morning hush lines of the places repair them
                QString storm = QStringLiteral(
                    "# dread 95: the whole city grew the extra floor - doors at every baked stairwell\n");
                for (int j = 0; j < halfs.size(); ++j)
                {
                    const Prop& p = halfs[j];
                    const Cell& low = p.cells.first();
                    const Cell& up = p.cells.at(1);
                    const int stx = p.x + kFaceX[p.dir & 3], stz = p.z + kFaceZ[p.dir & 3];  // the step the door looks at
                    const QString pth = ev + QStringLiteral("hf/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("hf/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the plan of this stairwell grew a door overnight (#hf_%1 runs)\n"
                        "execute if score #hf_%1 osmc_enc matches 1.. run return 0\n"
                        "scoreboard players set #hf_%1 osmc_enc 1\n"
                        "setblock %2 %3 %4 minecraft:oak_door[facing=%6,half=lower,open=false]\n"
                        "setblock %2 %5 %4 minecraft:oak_door[facing=%6,half=upper,open=false]\n"
                        "playsound minecraft:block.wood.place master @a[x=%2,y=%3,z=%4,distance=..16] %2 %3 %4 0.6 0.7\n"
                        "particle minecraft:poof %7 %3 %8 0.15 0.5 0.15 0.01 20\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(up.y)
                        .arg(QLatin1String(kFacing[p.dir & 3]))
                        .arg(double(p.x + 0.5), 0, 'f', 1).arg(double(p.z + 0.5), 0, 'f', 1).toUtf8());
                    add(pth + QStringLiteral("enter.mcfunction"), QStringLiteral(
                        "# the stairwell trades its guest for the floor of no plan\n"
                        "tag @a[distance=..2.5] add osmc_half\n"
                        "scoreboard players set @a[distance=..2.5] osmc_enc_p %1\n"
                        "execute as @a[distance=..2.5] run function osmc_enc:ev/hf/gate\n"
                        "execute in osmc_enc:half run tp @a[tag=osmc_half] 0.5 -40 0.5\n"
                        "execute in osmc_enc:half positioned 0.5 -40 0.5 run particle minecraft:portal"
                        " 0.5 -39 0.5 1.5 1 1.5 0.05 80\n"
                        "execute in osmc_enc:half positioned 0.5 -40 0.5 run playsound minecraft:block.beacon.deactivate"
                        " master @a[tag=osmc_half] ~ ~ ~ 0.4 0.5\n"
                        "execute in osmc_enc:half positioned 0.5 -40 0.5 run title @a[tag=osmc_half] actionbar"
                        " {\"text\":\"the landing is here, the city outside it is not\","
                        "\"color\":\"gray\",\"italic\":true}\n"
                        "function osmc_enc:ev/hf/roll\n")
                        .arg(j).toUtf8());
                    add(pth + QStringLiteral("back.mcfunction"), QStringLiteral(
                        "# through the one door the floor promised this guest - back on their flight\n"
                        "execute in minecraft:overworld run tp @a[scores={osmc_enc_p=%1},tag=osmc_half] %2 %3 %4\n"
                        "execute if entity @a[x=%2,y=%3,z=%4,distance=..4,scores={osmc_enc_p=%1}]"
                        " in minecraft:overworld run playsound minecraft:block.grass.break"
                        " master @a[x=%2,y=%3,z=%4,distance=..16] %2 %3 %4 1.0 0.6\n"
                        "execute if entity @a[x=%2,y=%3,z=%4,distance=..4,scores={osmc_enc_p=%1}]"
                        " in minecraft:overworld run title @a[x=%2,y=%3,z=%4,distance=..8] actionbar"
                        " {\"text\":\"the stairwell took its guest back; the wall is a wall again\","
                        "\"color\":\"gray\",\"italic\":true}\n"
                        "tag @a[scores={osmc_enc_p=%1},tag=osmc_half] remove osmc_half\n"
                        "scoreboard players reset @a[scores={osmc_enc_p=%1}] osmc_enc_g\n"
                        "scoreboard players reset @a[scores={osmc_enc_p=%1}] osmc_enc_p\n")
                        .arg(j).arg(double(stx + 0.5), 0, 'f', 1).arg(p.y)
                        .arg(double(stz + 0.5), 0, 'f', 1).toUtf8());
                    add(pth + QStringLiteral("hush.mcfunction"), QStringLiteral(
                        "# morning: the plan is repaired, the wall remembers its own blocks\n"
                        "setblock %2 %3 %4 %5\n"
                        "setblock %2 %6 %4 %7\n"
                        "scoreboard players set #hf_%1 osmc_enc 0\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(blockName(low.id))
                        .arg(up.y).arg(blockName(up.id)).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #hf_%1 osmc_enc matches 1 in minecraft:overworld positioned %2 %3 %4"
                        " if block %2 %3 %4 minecraft:oak_door[half=lower,open=true]"
                        " if entity @a[distance=..2.5] run function %5\n"
                        "execute if score #hf_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 0..11999 run function %6\n"
                        "execute in osmc_enc:half if entity @a[scores={osmc_enc_p=%1}]"
                        " if score #day osmc_enc_t matches 0..11999 run function %7\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z)
                        .arg(fnp + QStringLiteral("enter")).arg(fnp + QStringLiteral("hush"))
                        .arg(fnp + QStringLiteral("back")).toUtf8();
                    storm += QStringLiteral(
                        "scoreboard players set #hf_%1 osmc_enc 1\n"
                        "setblock %2 %3 %4 minecraft:oak_door[facing=%5,half=lower,open=false]\n"
                        "setblock %2 %6 %4 minecraft:oak_door[facing=%5,half=upper,open=false]\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z)
                        .arg(QLatin1String(kFacing[p.dir & 3])).arg(up.y);
                }
                // the morning wipe of the floor once per cycle (it used to ride the per-stairwell
                // block - eight identical lines); positioned pins its kill to the hall, never the city
                cycle += QStringLiteral(
                    "execute in osmc_enc:half positioned 0 -40 0 if score #day osmc_enc_t"
                    " matches 0..11999 run function osmc_enc:ev/hf/wipe\n");
                add(ev + QStringLiteral("hf/storm.mcfunction"), storm.toUtf8());
                cycle += QStringLiteral(
                    "execute if score #lv osmc_enc_t matches 4 if score #day osmc_enc_t matches 13000..23999"
                    " if score #floor_half osmc_enc matches 1 run function osmc_enc:ev/hf/storm\n");
                // the way out of the mirrored floor is the ONE door the floor promised each guest
                // on the trade (osmc_enc_g); the other three open onto nobody's ground and keep
                // the guest a little longer in the hall of windows into the void
                add(ev + QStringLiteral("hf/gate.mcfunction"), QByteArray(
                    "# the floor promises this guest one of its four doors; only it lets back\n"
                    "execute store result score @s osmc_enc_g run random value 1..4\n"));
                QString exits = QStringLiteral(
                    "# the promised door steps the guest standing at it back; the other three keep\n"
                    "execute as @a[distance=..2.5,scores={osmc_enc_p=0..}] run function osmc_enc:ev/hf/guestback\n");
                add(ev + QStringLiteral("hf/exit.mcfunction"), exits.toUtf8());
                QString guestBack = QStringLiteral(
                    "# whose visit is this: back to the flight that traded for it\n");
                for (int j = 0; j < halfs.size(); ++j)
                    guestBack += QStringLiteral("execute if entity @s[scores={osmc_enc_p=%1}]"
                                               " run function %2\n")
                                     .arg(j).arg(fp + QStringLiteral("hf/%1/back").arg(j));
                add(ev + QStringLiteral("hf/guestback.mcfunction"), guestBack.toUtf8());
                for (int d = 0; d < 4; ++d)
                    cycle += QStringLiteral(
                        "execute in osmc_enc:half positioned %1 %2 %3"
                        " if block %1 %2 %3 minecraft:oak_door[half=lower,open=true]"
                        " if entity @a[distance=..2.5,scores={osmc_enc_g=%4}]"
                        " run function osmc_enc:ev/hf/exit\n")
                        .arg(QLatin1String(kHFDoor[d][0])).arg(QLatin1String(kHFDoor[d][1]))
                        .arg(QLatin1String(kHFDoor[d][2])).arg(d + 1);
            }
        }
        else if (id == QLatin1String("echo"))
        {   // the tunnel gives the walker's own steps back a breath late. no roll: the walk itself is
            // the event (the standing-watch family). Three cells of every baked run listen; on finding
            // a walker the echo remembers where they stood one cycle ago, and a step is heard there.
            // Standing still returns only silence; a fast walker only outruns the sound by less than
            // it feels — and one more scared night of dread cannot stack a second step in a cycle
            if (metros.isEmpty()) body = noPlace;
            else
            {
                load += QStringLiteral(
                    "execute unless entity @e[type=minecraft:marker,tag=osmc_echo] run"
                    " summon minecraft:marker 0 -60 0 {Tags:[\"osmc_echo\"]}\n");
                body = QStringLiteral(
                    "# one tracking per cycle however many times the dread calls this file\n"
                    "scoreboard players set #ech_c osmc_enc_t 0\n");
                for (int j = 0; j < metros.size(); ++j)
                {   // the early, the middle and the late cell of the run listen for a walker
                    const Prop& p = metros[j];
                    const int n = p.cells.size() - 1;
                    const int pick[3] = { 1, n / 2, n - 2 };
                    for (int q = 0; q < 3; ++q)
                    {
                        const Cell& c = p.cells[qMax(1, qMin(pick[q], n - 1)) + 1];
                        body += QStringLiteral(
                            "execute if score #ech_c osmc_enc_t matches 0 in minecraft:overworld"
                            " positioned %1 %2 %3 if entity @a[distance=..1.5]"
                            " as @a[distance=..1.5,limit=1,sort=nearest] run function %4\n")
                            .arg(c.x).arg(c.y).arg(c.z).arg(fp + QStringLiteral("echo/track"));
                    }
                }
                add(ev + QStringLiteral("echo/track.mcfunction"), QByteArray(
                    "# the echo keeps the place the walker stood one cycle ago, and steps there a beat late\n"
                    "scoreboard players set #ech_c osmc_enc_t 1\n"
                    "scoreboard players operation #ex0 osmc_enc = #ex osmc_enc\n"
                    "scoreboard players operation #ey0 osmc_enc = #ey osmc_enc\n"
                    "scoreboard players operation #ez0 osmc_enc = #ez osmc_enc\n"
                    "execute store result score #ex osmc_enc run data get entity @s Pos[0]\n"
                    "execute store result score #ey osmc_enc run data get entity @s Pos[1]\n"
                    "execute store result score #ez osmc_enc run data get entity @s Pos[2]\n"
                    "# the echo of standing still is only silence: a returned step needs two blocks of walking\n"
                    "scoreboard players operation #edx osmc_enc = #ex osmc_enc\n"
                    "scoreboard players operation #edx osmc_enc -= #ex0 osmc_enc\n"
                    "scoreboard players operation #edz osmc_enc = #ez osmc_enc\n"
                    "scoreboard players operation #edz osmc_enc -= #ez0 osmc_enc\n"
                    "scoreboard players set #ech_go osmc_enc 0\n"
                    "execute if score #edx osmc_enc matches 2.. run scoreboard players set #ech_go osmc_enc 1\n"
                    "execute if score #edx osmc_enc matches ..-2 run scoreboard players set #ech_go osmc_enc 1\n"
                    "execute if score #edz osmc_enc matches 2.. run scoreboard players set #ech_go osmc_enc 1\n"
                    "execute if score #edz osmc_enc matches ..-2 run scoreboard players set #ech_go osmc_enc 1\n"
                    "execute unless score #ech_go osmc_enc matches 1 run return 0\n"
                    "execute store result entity @e[type=minecraft:marker,tag=osmc_echo,limit=1] Pos[0] double 1 run scoreboard players get #ex0 osmc_enc\n"
                    "execute store result entity @e[type=minecraft:marker,tag=osmc_echo,limit=1] Pos[1] double 1 run scoreboard players get #ey0 osmc_enc\n"
                    "execute store result entity @e[type=minecraft:marker,tag=osmc_echo,limit=1] Pos[2] double 1 run scoreboard players get #ez0 osmc_enc\n"
                    "schedule function osmc_enc:ev/echo/step 2s replace\n").constData());
                add(ev + QStringLiteral("echo/step.mcfunction"), QByteArray(
                    "# one breath behind: a step from the place the walker already left\n"
                    "execute at @e[type=minecraft:marker,tag=osmc_echo,limit=1] run"
                    " playsound minecraft:entity.skeleton.step master @a[distance=..24] ~ ~ ~ 0.6 0.75\n"
                    "execute at @e[type=minecraft:marker,tag=osmc_echo,limit=1] run"
                    " particle minecraft:crit ~ ~0.1 ~ 0.15 0.05 0.15 0.02 5\n").constData());
            }
        }
        else if (id == QLatin1String("upstairs"))
        {   // the neighbours above: the exporter cannot know whose ceiling is somebody's floor, so the
            // event lives over whoever is around at the late hour — furniture moved seven blocks up,
            // or three knocks with a door behind them that is on no plan of this floor
            body = gate(18000, 23999, 14) + QStringLiteral(
                "execute if score #enc_r osmc_enc_t matches 1..7 as @a[limit=1,sort=random] at @s"
                " run function osmc_enc:ev/up/thump\n"
                "execute if score #enc_r osmc_enc_t matches 8..14 as @a[limit=1,sort=random] at @s"
                " run function osmc_enc:ev/up/knock\n");
            add(ev + QStringLiteral("up/thump.mcfunction"), QByteArray(
                "# the furniture upstairs is rearranged by somebody who never comes down\n"
                "playsound minecraft:block.wood.place master @s ~ ~7 ~ 0.5 0.35\n"
                "playsound minecraft:block.wool.place master @s ~2 ~7 ~-1 0.35 0.5\n"
                "particle minecraft:dust{color:[0.35,0.3,0.25],scale:1.0} ~ ~5.5 ~ 0.8 0.6 0.8 0.01 14\n").constData());
            add(ev + QStringLiteral("up/knock.mcfunction"), QByteArray(
                "# three knocks on the ceiling — the door behind them is on no plan of this floor\n"
                "playsound minecraft:block.wood.hit master @s ~ ~3 ~ 0.7 0.6\n"
                "playsound minecraft:block.wooden_door.close master @s ~ ~7 ~ 0.3 0.6\n").constData());
        }
        else if (id == QLatin1String("third_passenger"))
        {   // the tunnel walks with one more than it can count. whoever a baked run finds is followed
            // by two shadows standing where the walker stood one and two cycles ago; a third set of
            // steps is heard behind them and belongs to nobody. the count is announced once per walk,
            // the shadows dissolve at dawn (the standing-watch family, the echo's score tricks)
            if (metros.isEmpty()) body = noPlace;
            else
            {
                body = QStringLiteral(
                    "# one pass per cycle however many times the dread calls this file\n"
                    "scoreboard players set #t3_c osmc_enc_t 0\n");
                for (int j = 0; j < metros.size(); ++j)
                {   // the middle of the run: the deepest place of the tunnel, where counting fails
                    const Prop& p = metros[j];
                    const Cell& c = p.cells[p.cells.size() / 2];
                    body += QStringLiteral(
                        "execute if score #t3_c osmc_enc_t matches 0 in minecraft:overworld"
                        " positioned %1 %2 %3 if entity @a[distance=..2]"
                        " as @a[distance=..2,limit=1,sort=nearest] run function %4\n")
                        .arg(c.x).arg(c.y).arg(c.z).arg(fp + QStringLiteral("tp3/follow"));
                }
                add(ev + QStringLiteral("tp3/follow.mcfunction"), QByteArray(
                    "# the party is one more than the number of the living walking it\n"
                    "scoreboard players set #t3_c osmc_enc_t 1\n"
                    "execute unless entity @s[tag=osmc_tp_seen] run title @s actionbar"
                    " {\"text\":\"you count the footsteps behind you and one is not yours\","
                    "\"color\":\"dark_gray\",\"italic\":true}\n"
                    "execute unless entity @s[tag=osmc_tp_seen] run playsound minecraft:entity.warden.listening"
                    " master @s ~ ~ ~ 0.35 1.4\n"
                    "tag @s add osmc_tp_seen\n"
                    "execute unless entity @e[type=minecraft:armor_stand,tag=osmc_tp1] run"
                    " summon minecraft:armor_stand ~ ~ ~ {Invisible:1b,NoGravity:1b,Silent:1b,"
                    "Invulnerable:1b,Tags:[\"osmc_tp\",\"osmc_tp1\"]}\n"
                    "execute unless entity @e[type=minecraft:armor_stand,tag=osmc_tp2] run"
                    " summon minecraft:armor_stand ~ ~ ~ {Invisible:1b,NoGravity:1b,Silent:1b,"
                    "Invulnerable:1b,Tags:[\"osmc_tp\",\"osmc_tp2\"]}\n"
                    "# the places of one and two cycles ago are remembered as the echo remembers\n"
                    "scoreboard players operation #t3x2 osmc_enc = #t3x1 osmc_enc\n"
                    "scoreboard players operation #t3y2 osmc_enc = #t3y1 osmc_enc\n"
                    "scoreboard players operation #t3z2 osmc_enc = #t3z1 osmc_enc\n"
                    "scoreboard players operation #t3x1 osmc_enc = #t3x osmc_enc\n"
                    "scoreboard players operation #t3y1 osmc_enc = #t3y osmc_enc\n"
                    "scoreboard players operation #t3z1 osmc_enc = #t3z osmc_enc\n"
                    "execute store result score #t3x osmc_enc run data get entity @s Pos[0]\n"
                    "execute store result score #t3y osmc_enc run data get entity @s Pos[1]\n"
                    "execute store result score #t3z osmc_enc run data get entity @s Pos[2]\n"
                    "scoreboard players operation #t3dx osmc_enc = #t3x osmc_enc\n"
                    "scoreboard players operation #t3dx osmc_enc -= #t3x1 osmc_enc\n"
                    "scoreboard players operation #t3dz osmc_enc = #t3z osmc_enc\n"
                    "scoreboard players operation #t3dz osmc_enc -= #t3z1 osmc_enc\n"
                    "scoreboard players set #t3_go osmc_enc 0\n"
                    "execute if score #t3dx osmc_enc matches 2.. run scoreboard players set #t3_go osmc_enc 1\n"
                    "execute if score #t3dx osmc_enc matches ..-2 run scoreboard players set #t3_go osmc_enc 1\n"
                    "execute if score #t3dz osmc_enc matches 2.. run scoreboard players set #t3_go osmc_enc 1\n"
                    "execute if score #t3dz osmc_enc matches ..-2 run scoreboard players set #t3_go osmc_enc 1\n"
                    "execute unless score #t3_go osmc_enc matches 1 run return 0\n"
                    "execute store result entity @e[type=minecraft:armor_stand,tag=osmc_tp1,limit=1] Pos[0] double 1 run scoreboard players get #t3x1 osmc_enc\n"
                    "execute store result entity @e[type=minecraft:armor_stand,tag=osmc_tp1,limit=1] Pos[1] double 1 run scoreboard players get #t3y1 osmc_enc\n"
                    "execute store result entity @e[type=minecraft:armor_stand,tag=osmc_tp1,limit=1] Pos[2] double 1 run scoreboard players get #t3z1 osmc_enc\n"
                    "execute store result entity @e[type=minecraft:armor_stand,tag=osmc_tp2,limit=1] Pos[0] double 1 run scoreboard players get #t3x2 osmc_enc\n"
                    "execute store result entity @e[type=minecraft:armor_stand,tag=osmc_tp2,limit=1] Pos[1] double 1 run scoreboard players get #t3y2 osmc_enc\n"
                    "execute store result entity @e[type=minecraft:armor_stand,tag=osmc_tp2,limit=1] Pos[2] double 1 run scoreboard players get #t3z2 osmc_enc\n"
                    "# the third set of steps belongs to nobody who is here\n"
                    "execute at @e[type=minecraft:armor_stand,tag=osmc_tp2,limit=1] run"
                    " playsound minecraft:entity.skeleton.step master @a[distance=..20] ~ ~ ~ 0.45 0.6\n").constData());
                cycle += QStringLiteral(
                    "execute if score #day osmc_enc_t matches 1000..11999"
                    " run kill @e[type=minecraft:armor_stand,tag=osmc_tp]\n"
                    "execute if score #day osmc_enc_t matches 1000..11999 run tag @a remove osmc_tp_seen\n").toUtf8();
            }
        }
        else if (id == QLatin1String("stray_dog"))
        {   // a dog with no address joins whoever walks at night — one animal of the world with no
            // roll of its own: it simply keeps its walk once it came, and a new stray falls in on
            // the roll of the cycle. it belongs to the walker (the map-makers' Owner trick), slips
            // away among people, and is gone by dawn; it never makes a sound on its own
            body = QStringLiteral(
                "# the stray keeps his night walk without any roll; a new one falls in at most ONCE a\n"
                "# night - the same one-die-a-day rhythm as the place-bound legends (the old dice woke\n"
                "# him every few cycles, so he was a fixture of every night, not a find)\n"
                "execute unless score #day osmc_enc_t matches 18000..23999 run return 0\n"
                "execute if entity @e[type=minecraft:wolf,tag=osmc_dog] run function osmc_enc:ev/dog/keep\n"
                "execute if entity @e[type=minecraft:wolf,tag=osmc_dog] run return 0\n"
                "execute unless entity @a run return 0\n"
                "execute if score #dog_d osmc_enc_t < #dayn osmc_enc_t run scoreboard players set #dog_k osmc_enc_t 0\n"
                "execute if score #dog_d osmc_enc_t < #dayn osmc_enc_t run scoreboard players operation #dog_d osmc_enc_t = #dayn osmc_enc_t\n"
                "execute if score #dog_k osmc_enc_t matches 1 run return 0\n"
                "scoreboard players set #dog_k osmc_enc_t 1\n"
                "function osmc_enc:ev/dog/roll\n");
            add(ev + QStringLiteral("dog/roll.mcfunction"), QByteArray(
                "# not every street is a road a stray takes\n"
                "execute store result score #enc_r osmc_enc_t run random value 1..100\n"
                "execute unless score #enc_r osmc_enc_t matches 1..10 run return 0\n"
                "execute as @a[limit=1,sort=random] at @s unless entity @e[type=minecraft:villager,distance=..32]"
                " run function osmc_enc:ev/dog/join\n").constData());
            add(ev + QStringLiteral("dog/join.mcfunction"), QByteArray(
                "# a dog with no collar falls in ahead of the walker, on the same path\n"
                "summon minecraft:wolf ~ ~ ~ {Tags:[\"osmc_dog\"],Silent:1b}\n"
                "tp @e[type=minecraft:wolf,tag=osmc_dog,distance=..2,limit=1] ^ ^ ^16\n"
                "execute rotated as @s positioned ^ ^ ^16 run title @a[distance=..20] actionbar"
                " {\"text\":\"a dog with no collar falls in beside you\","
                "\"color\":\"gray\",\"italic\":true}\n"
                "execute rotated as @s positioned ^ ^ ^16 run playsound minecraft:entity.wolf.whine"
                " master @a[distance=..20] ~ ~ ~ 0.4 1.2\n").constData());
            add(ev + QStringLiteral("dog/keep.mcfunction"), QByteArray(
                "# he belongs to whoever is out tonight — the owner is copied, never asked\n"
                "execute unless data entity @e[type=minecraft:wolf,tag=osmc_dog,limit=1] Owner"
                " run function osmc_enc:ev/dog/own\n"
                "# among people he is nobody's, and there he does not stay\n"
                "execute as @e[type=minecraft:wolf,tag=osmc_dog] at @s"
                " if entity @e[type=minecraft:villager,distance=..15] run function osmc_enc:ev/dog/leave\n"
                "execute as @e[type=minecraft:wolf,tag=osmc_dog] at @s"
                " unless entity @a[distance=..80] run kill @s\n").constData());
            add(ev + QStringLiteral("dog/own.mcfunction"), QByteArray(
                "execute as @a[limit=1,sort=random] at @s run data modify"
                " entity @e[type=minecraft:wolf,tag=osmc_dog,limit=1,distance=..64]"
                " Owner set from entity @s UUID\n").constData());
            add(ev + QStringLiteral("dog/leave.mcfunction"), QByteArray(
                "# a puff of nothing where the dog was\n"
                "particle minecraft:poof ~ ~1 ~ 0.3 0.3 0.3 0.1 20\n"
                "playsound minecraft:entity.wolf.whine master @a[distance=..24] ~ ~ ~ 0.35 0.9\n"
                "kill @s\n").constData());
            load += QStringLiteral(
                "execute unless score #dog_d osmc_enc_t matches -2147483648.. run scoreboard players set #dog_d osmc_enc_t -1\n");
            cycle += QStringLiteral(
                "execute if score #day osmc_enc_t matches 1000..11999"
                " run kill @e[type=minecraft:wolf,tag=osmc_dog]\n").toUtf8();
        }
        else if (id == QLatin1String("procession"))
        {   // the procession walks the alley of a graveyard on a rolled night: soul fire marching
            // from this grave toward the next one with cold steps, two seconds to a cell, seen by
            // whoever stands in the alleys. One procession walks the city at a time (#proc_go),
            // the march only continues while somebody could see it, the dawn releases the flag
            if (graves.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 18000, 23999, graves, QStringLiteral("proc/"));
                for (int j = 0; j < graves.size(); ++j)
                {   // the route: this grave, then two waypoints toward the next one only when that
                    // next grave is a neighbour of the same graveyard, not another cemetery
                    const Prop& p = graves[j];
                    QVector<QPoint> route;
                    route << QPoint(p.x, p.z);
                    const Prop& nx = graves[(j + 1) % graves.size()];
                    if (j + 1 < graves.size()
                        && qMax(qAbs(nx.x - p.x), qAbs(nx.z - p.z)) <= 32)
                        for (int w = 1; w <= 2; ++w)
                            route << QPoint(p.x + (nx.x - p.x) * w / 3, p.z + (nx.z - p.z) * w / 3);
                    const QString pth = ev + QStringLiteral("proc/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("proc/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# one procession walks the city at a time; nobody watches it alone\n"
                        "execute if score #proc_go osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%1,y=%2,z=%3,distance=..64] run return 0\n"
                        "scoreboard players set #proc_go osmc_enc 1\n"
                        "title @a[x=%1,y=%2,z=%3,distance=..48] actionbar"
                        " {\"text\":\"the alley of the graveyard fills with cold blue light\","
                        "\"color\":\"aqua\",\"italic\":true}\n"
                        "schedule function %4 1s replace\n")
                        .arg(p.x).arg(p.y).arg(p.z).arg(fnp + QStringLiteral("r0")).toUtf8());
                    for (int s = 0; s < route.size(); ++s)
                    {
                        const QPoint& w = route[s];
                        QString step = QStringLiteral("# waypoint %1 of the march; the column passes through\n").arg(s);
                        step += QStringLiteral("execute unless score #proc_go osmc_enc matches 1 run return 0\n"
                                              "particle minecraft:soul_fire_flame %1 %2 %3 0.7 0.6 0.7 0.03 26\n"
                                              "particle minecraft:soul %1 %2 %3 0.7 1.0 0.7 0.02 18\n")
                                    .arg(w.x() + 0.5, 0, 'f', 1).arg(p.y + 1).arg(w.y() + 0.5, 0, 'f', 1);
                        step += QStringLiteral("playsound minecraft:entity.skeleton.step master"
                                              " @a[x=%1,y=%2,z=%3,distance=..32] %1 %2 %3 0.4 0.45\n")
                                    .arg(w.x()).arg(p.y).arg(w.y());
                        if (s + 1 < route.size())
                            step += QStringLiteral("execute if entity @a[x=%1,y=%2,z=%3,distance=..80] run schedule"
                                                   " function %4 2s replace\n"
                                                   "execute unless entity @a[x=%1,y=%2,z=%3,distance=..80] run"
                                                   " scoreboard players set #proc_go osmc_enc 0\n")
                                        .arg(w.x()).arg(p.y).arg(w.y())
                                        .arg(fnp + QStringLiteral("r%1").arg(s + 1));
                        else
                            step += QStringLiteral("scoreboard players set #proc_go osmc_enc 0\n"
                                                   "playsound minecraft:entity.skeleton.death master"
                                                   " @a[x=%1,y=%2,z=%3,distance=..32] %1 %2 %3 0.4 0.5\n")
                                        .arg(w.x()).arg(p.y).arg(w.y());
                        add(pth + QStringLiteral("r%1.mcfunction").arg(s), step.toUtf8());
                    }
                }
                cycle += QStringLiteral(
                    "execute if score #day osmc_enc_t matches 1000..11999 run scoreboard players set #proc_go osmc_enc 0\n").toUtf8();
            }
        }
        else if (id == QLatin1String("moths"))
        {   // one lamp gets moths tonight: a white circle turns around the light for a minute, and
            // whoever stands in the middle of it finds the world gone quiet (the orbit chain rides
            // schedule, eight baked offsets, the lamp keeps its own counter)
            if (lamps.isEmpty()) body = noPlace;
            else
            {
                load += QStringLiteral("scoreboard players set #k8 osmc_enc_t 8\n");
                body = nearRoll(id, 19000, 23999, lamps, QStringLiteral("moth/"));
                static const int kOrbit[8][2] = { {2, 0}, {1, 1}, {0, 2}, {-1, 1}, {-2, 0}, {-1, -1}, {0, -2}, {1, -1} };
                for (int j = 0; j < lamps.size(); ++j)
                {
                    const Prop& p = lamps[j];
                    const Cell& light = p.cells.first();
                    const QString pth = ev + QStringLiteral("moth/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("moth/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the moths came to this lamp tonight\n"
                        "execute if score #moth_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..40] run return 0\n"
                        "scoreboard players set #moth_%1 osmc_enc 1\n"
                        "scoreboard players set #mc_%1 osmc_enc 0\n"
                        "schedule function %5 10t replace\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(fnp + QStringLiteral("orbit")).toUtf8());
                    QString orbit = QStringLiteral("# the circle of the moths, one eighth of a turn per half-second\n"
                                                   "execute unless score #moth_%1 osmc_enc matches 1 run return 0\n"
                                                   "scoreboard players add #mc_%1 osmc_enc 1\n"
                                                   "scoreboard players operation #mi_%1 osmc_enc = #mc_%1 osmc_enc\n"
                                                   "scoreboard players operation #mi_%1 osmc_enc %= #k8 osmc_enc_t\n")
                                        .arg(j);
                    for (int k = 0; k < 8; ++k)
                        orbit += QStringLiteral("execute if score #mi_%1 osmc_enc matches %2 run particle"
                                               " minecraft:dust{color:[1.0,1.0,1.0],scale:1.0} %3 %4 %5 0.08 0.08 0.08 0.01 7\n")
                                     .arg(j).arg(k)
                                     .arg(light.x + kOrbit[k][0] + 0.5, 0, 'f', 1)
                                     .arg(light.y + 0.5, 0, 'f', 1)
                                     .arg(light.z + kOrbit[k][1] + 0.5, 0, 'f', 1);
                    orbit += QStringLiteral(
                        "# whoever stands in the middle of the circle hears the world stop\n"
                        "execute if entity @a[x=%1,y=%2,z=%3,distance=..2] run stopsound @a[x=%1,y=%2,z=%3,distance=..12]\n"
                        "execute if score #mc_%4 osmc_enc matches ..119 run schedule function %5 10t replace\n"
                        "execute if score #mc_%4 osmc_enc matches 120.. run scoreboard players set #moth_%4 osmc_enc 0\n")
                        .arg(light.x).arg(light.y).arg(light.z).arg(j).arg(fnp + QStringLiteral("orbit"));
                    add(pth + QStringLiteral("orbit.mcfunction"), orbit.toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #moth_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 0..11999 run scoreboard players set #moth_%1 osmc_enc 0\n")
                        .arg(j).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("hush_field"))
        {   // the pocket of silence: a lamp, ten blocks around it the sound falls out of the air for
            // a quarter of a minute — three sweeps of /stopsound, and the ambience comes back by itself
            if (lamps.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 0, 23999, lamps, QStringLiteral("hush/"));
                for (int j = 0; j < lamps.size(); ++j)
                {
                    const Prop& p = lamps[j];
                    const QString pth = ev + QStringLiteral("hush/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("hush/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the air around this lamp forgets how to carry sound\n"
                        "execute if score #hush_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..10] run return 0\n"
                        "scoreboard players set #hush_%1 osmc_enc 1\n"
                        "stopsound @a[x=%2,y=%3,z=%4,distance=..12]\n"
                        "title @a[x=%2,y=%3,z=%4,distance=..12] actionbar"
                        " {\"text\":\"the air here holds no sound\",\"color\":\"gray\",\"italic\":true}\n"
                        "schedule function %5 5s replace\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(fnp + QStringLiteral("s2")).toUtf8());
                    add(pth + QStringLiteral("s2.mcfunction"), QStringLiteral(
                        "# the silence holds\n"
                        "stopsound @a[x=%1,y=%2,z=%3,distance=..12]\n"
                        "schedule function %4 5s replace\n")
                        .arg(p.x).arg(p.y).arg(p.z).arg(fnp + QStringLiteral("s3")).toUtf8());
                    add(pth + QStringLiteral("s3.mcfunction"), QStringLiteral(
                        "# the last sweep; the ambience is on its own again\n"
                        "stopsound @a[x=%1,y=%2,z=%3,distance=..12]\n"
                        "scoreboard players set #hush_%4 osmc_enc 0\n")
                        .arg(p.x).arg(p.y).arg(p.z).arg(j).toUtf8());
                }
            }
        }
        else if (id == QLatin1String("window_walker"))
        {   // at the late hour somebody walks a lit row of windows from the inside: a floating lantern
            // (an invisible stand wearing one in its hand) crosses one row of the baked facade, one
            // window per second and a half. The row is the second from the top when it is wide
            // enough, the widest one otherwise; a facade without a walkable row honestly stays dark
            if (walls.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 22000, 23999, walls, QStringLiteral("ww/"));
                for (int j = 0; j < walls.size(); ++j)
                {
                    const Prop& p = walls[j];
                    QList<int> ys;
                    for (const Cell& c : p.cells) if (!ys.contains(c.y)) ys << c.y;
                    std::sort(ys.begin(), ys.end());
                    int rowY = ys.isEmpty() ? 0 : ys.last();
                    for (int i = ys.size() - 2; i >= 0; --i)
                    {
                        int cnt = 0;
                        for (const Cell& c : p.cells) if (c.y == ys[i]) ++cnt;
                        if (cnt >= 3) { rowY = ys[i]; break; }
                    }
                    QVector<const Cell*> row;
                    for (const Cell& c : p.cells) if (c.y == rowY) row << &c;
                    std::sort(row.begin(), row.end(), [](const Cell* a, const Cell* b)
                              { return a->x != b->x ? a->x < b->x : a->z < b->z; });
                    while (row.size() > 10) row.removeLast();
                    const QString pth = ev + QStringLiteral("ww/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("ww/%1/").arg(j);
                    if (row.size() < 3)
                    {   // nothing to walk here — the roll of this facade goes nowhere
                        add(pth + QStringLiteral("go.mcfunction"), QByteArray(
                            "# no row wide enough on this facade\nreturn 0\n"));
                        continue;
                    }
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# behind the glass of this row a lantern is walking\n"
                        "execute if score #ww_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..48] run return 0\n"
                        "scoreboard players set #ww_%1 osmc_enc 1\n"
                        "summon minecraft:armor_stand %5 %6 %7 {Invisible:1b,NoGravity:1b,Silent:1b,"
                        "Invulnerable:1b,Tags:[\"osmc_ww\",\"osmc_ww_%1\"]}\n"
                        "item replace entity @e[type=minecraft:armor_stand,tag=osmc_ww_%1,limit=1]"
                        " weapon.mainhand with minecraft:lantern\n"
                        "schedule function %8 1s replace\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z)
                        .arg(row.first()->x + 0.5, 0, 'f', 1).arg(double(row.first()->y))
                        .arg(row.first()->z + 0.5, 0, 'f', 1)
                        .arg(fnp + QStringLiteral("w0")).toUtf8());
                    for (int s = 0; s < row.size(); ++s)
                    {
                        const Cell* c = row[s];
                        QString step = QStringLiteral("# the lantern passes window %1 of the row\n").arg(s);
                        step += QStringLiteral("execute unless score #ww_%1 osmc_enc matches 1 run return 0\n"
                                              "tp @e[type=minecraft:armor_stand,tag=osmc_ww_%1,limit=1]"
                                              " %2 %3 %4\n")
                                    .arg(j).arg(c->x + 0.5, 0, 'f', 1).arg(double(c->y))
                                    .arg(c->z + 0.5, 0, 'f', 1);
                        step += QStringLiteral("playsound minecraft:block.lantern.place master"
                                              " @a[x=%1,y=%2,z=%3,distance=..24] %1 %2 %3 0.25 0.7\n")
                                    .arg(c->x).arg(c->y).arg(c->z);
                        if (s + 1 < row.size())
                            step += QStringLiteral("schedule function %1 30t replace\n")
                                        .arg(fnp + QStringLiteral("w%1").arg(s + 1));
                        else
                            step += QStringLiteral("schedule function %1 40s replace\n")
                                        .arg(fnp + QStringLiteral("gone"));
                        add(pth + QStringLiteral("w%1.mcfunction").arg(s), step.toUtf8());
                    }
                    add(pth + QStringLiteral("gone.mcfunction"), QStringLiteral(
                        "# the row is dark again; the lamp behind it was nobody's\n"
                        "kill @e[type=minecraft:armor_stand,tag=osmc_ww_%1]\n"
                        "scoreboard players set #ww_%1 osmc_enc 0\n").arg(j).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #ww_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 0..11999 run function %2\n")
                        .arg(j).arg(fnp + QStringLiteral("gone")).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("grave_caretaker"))
        {   // the caretaker of the graveyard: a bone in the alley who does not walk on his own —
            // when the walker comes to one side of the mound, the caretaker is already standing on
            // the opposite one (four baked side cells, whoever is nearer picks the side); he leaves
            // at dawn, and nobody remembers inviting him
            if (graves.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 18000, 23999, graves, QStringLiteral("gc/"));
                for (int j = 0; j < graves.size(); ++j)
                {
                    const Prop& p = graves[j];
                    const QString pth = ev + QStringLiteral("gc/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("gc/%1/").arg(j);
                    const int nx = p.x, sx = p.x;
                    const int nz = p.z - 3, sz = p.z + 3;
                    const int ex = p.x + 3, wx = p.x - 3;
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the caretaker came back to his mound\n"
                        "execute if score #gc_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..40] run return 0\n"
                        "scoreboard players set #gc_%1 osmc_enc 1\n"
                        "summon minecraft:skeleton %5 %6 %7 {NoAI:1b,Silent:1b,Invulnerable:1b,"
                        "PersistenceRequired:1b,Tags:[\"osmc_gc\",\"osmc_gc_%1\"]}\n"
                        "playsound minecraft:block.sand.break master @a[x=%2,y=%3,z=%4,distance=..24]"
                        " %2 %3 %4 0.5 0.5\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(ex).arg(p.y).arg(p.z).toUtf8());
                    add(pth + QStringLiteral("watch.mcfunction"), (QStringLiteral(
                        "# he keeps the far side of the grave: the walker always finds the caretaker\n"
                        "execute positioned %1 %2 %3 if entity @a[distance=..8] run"
                        " tp @e[type=minecraft:skeleton,tag=osmc_gc_%4,limit=1] %5 %2 %6\n"
                        "execute positioned %5 %2 %6 if entity @a[distance=..8] run"
                        " tp @e[type=minecraft:skeleton,tag=osmc_gc_%4,limit=1] %1 %2 %3\n"
                        "execute positioned %7 %2 %8 if entity @a[distance=..8] run"
                        " tp @e[type=minecraft:skeleton,tag=osmc_gc_%4,limit=1] %9 %2 %8\n"
                        "execute positioned %9 %2 %8 if entity @a[distance=..8] run"
                        " tp @e[type=minecraft:skeleton,tag=osmc_gc_%4,limit=1] %7 %2 %8\n")
                        .arg(nx).arg(p.y).arg(nz).arg(j).arg(sx).arg(sz).arg(ex).arg(p.z).arg(wx)
                        + QStringLiteral(
                        "execute unless entity @a[x=%1,y=%2,z=%3,distance=..40] run"
                        " kill @e[type=minecraft:skeleton,tag=osmc_gc_%4]\n"
                        "execute unless entity @a[x=%1,y=%2,z=%3,distance=..40] run"
                        " scoreboard players set #gc_%4 osmc_enc 0\n")
                        .arg(p.x).arg(p.y).arg(p.z).arg(j)).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #gc_%1 osmc_enc matches 1 in minecraft:overworld positioned %2 %3 %4"
                        " if entity @a[distance=..40] run function %5\n"
                        "execute if score #gc_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 1000..11999 run kill @e[type=minecraft:skeleton,tag=osmc_gc_%1]\n"
                        "execute if score #gc_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 1000..11999 run scoreboard players set #gc_%1 osmc_enc 0\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(fnp + QStringLiteral("watch")).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("howling_dog"))
        {   // past the edge of the streets a dog howls — never at the stop itself, always from the
            // open field beyond it, once, and an answer from further away six seconds later; whose
            // dog it is, and what it is tied to out there, the city does not say
            if (stops.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 18000, 23999, stops, QStringLiteral("hd/"));
                static const int kAx[4][2] = { {0, -1}, {1, 0}, {0, 1}, {-1, 0} };   // the kDirNames order
                for (int j = 0; j < stops.size(); ++j)
                {
                    const Prop& p = stops[j];
                    const int ax = p.dir & 3;
                    const int hx = p.x + kAx[ax][0] * 20, hz = p.z + kAx[ax][1] * 20;
                    const QString pth = ev + QStringLiteral("hd/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the howl starts beyond the last house\n"
                        "execute unless entity @a[x=%1,y=%2,z=%3,distance=..48] run return 0\n"
                        "playsound minecraft:entity.wolf.growl master @a[x=%1,y=%2,z=%3,distance=..64]"
                        " %4 %5 %6 0.7 0.8\n"
                        "title @a[x=%1,y=%2,z=%3,distance=..40] actionbar"
                        " {\"text\":\"the howl comes from past the last house\","
                        "\"color\":\"gray\",\"italic\":true}\n"
                        "schedule function %7 6s replace\n")
                        .arg(p.x).arg(p.y).arg(p.z).arg(hx).arg(p.y).arg(hz)
                        .arg(QStringLiteral("osmc_enc:ev/hd/%1/answer").arg(j)).toUtf8());
                    add(pth + QStringLiteral("answer.mcfunction"), QStringLiteral(
                        "# the answer from further out — and then the night keeps its own\n"
                        "playsound minecraft:entity.wolf.growl master @a[x=%1,y=%2,z=%3,distance=..80]"
                        " %4 %5 %6 0.5 0.6\n")
                        .arg(p.x).arg(p.y).arg(p.z).arg(hx + kAx[ax][0] * 24).arg(p.y)
                        .arg(hz + kAx[ax][1] * 24).toUtf8());
                }
            }
        }
        else if (id == QLatin1String("night_game"))
        {   // the gym left a ball inside: a snowball hops between the three cells the silhouette
            // walks, the school playground plays alone; the ball lies still again before morning
            if (schools.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 19000, 23999, schools, QStringLiteral("ng/"));
                for (int j = 0; j < schools.size(); ++j)
                {
                    const Prop& p = schools[j];
                    const QString pth = ev + QStringLiteral("ng/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("ng/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# somebody left a ball in the gym, and the gym is still playing\n"
                        "execute if score #ng_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..48] run return 0\n"
                        "scoreboard players set #ng_%1 osmc_enc 1\n"
                        "summon minecraft:item %5 %6 %7 {Item:{id:\"minecraft:snowball\",count:1b},"
                        "Tags:[\"osmc_ng\",\"osmc_ng_%1\"],Invulnerable:1b,PickupDelay:1200,Health:32767}\n"
                        "playsound minecraft:block.note_block.pling master @a[x=%2,y=%3,z=%4,distance=..32]"
                        " %2 %3 %4 0.4 1.2\n"
                        "schedule function %8 1s replace\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z)
                        .arg(p.cells[0].x + 0.5, 0, 'f', 1).arg(p.cells[0].y + 0.5)
                        .arg(p.cells[0].z + 0.5, 0, 'f', 1).arg(fnp + QStringLiteral("h0")).toUtf8());
                    static const int kHop[6][2] = { {0, 1}, {1, 1}, {1, 2}, {2, 1}, {1, 1}, {0, 0} };
                    for (int s = 0; s < 6; ++s)
                    {
                        const Cell& c = p.cells[qMin(kHop[s][1], p.cells.size() - 1)];
                        const bool up = kHop[s][0] == 1;
                        QString hop = QStringLiteral("# the ball %1 over cell %2 of the yard\n")
                                          .arg(up ? QStringLiteral("lifts") : QStringLiteral("drops"))
                                          .arg(kHop[s][1]);
                        hop += QStringLiteral("execute unless score #ng_%1 osmc_enc matches 1 run return 0\n"
                                             "tp @e[type=minecraft:item,tag=osmc_ng_%1,limit=1]"
                                             " %2 %3 %4\n")
                                   .arg(j).arg(c.x + 0.5, 0, 'f', 1)
                                   .arg(up ? QStringLiteral("%1").arg(c.y + 1.6) : QStringLiteral("%1").arg(c.y + 0.4))
                                   .arg(c.z + 0.5, 0, 'f', 1);
                        hop += QStringLiteral("playsound minecraft:entity.snowball.throw master"
                                             " @a[x=%1,y=%2,z=%3,distance=..24] %1 %2 %3 0.35 0.9\n")
                                   .arg(c.x).arg(c.y).arg(c.z);
                        if (s + 1 < 6)
                            hop += QStringLiteral("schedule function %1 %2 replace\n")
                                       .arg(fnp + QStringLiteral("h%1").arg(s + 1))
                                       .arg(up ? QStringLiteral("10t") : QStringLiteral("14t"));
                        else
                            hop += QStringLiteral("schedule function %1 20s replace\n")
                                       .arg(fnp + QStringLiteral("gone"));
                        add(pth + QStringLiteral("h%1.mcfunction").arg(s), hop.toUtf8());
                    }
                    add(pth + QStringLiteral("gone.mcfunction"), QStringLiteral(
                        "# the ball lies still; the gym keeps playing without it\n"
                        "kill @e[type=minecraft:item,tag=osmc_ng_%1]\n"
                        "scoreboard players set #ng_%1 osmc_enc 0\n").arg(j).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #ng_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 0..11999 run function %2\n")
                        .arg(j).arg(fnp + QStringLiteral("gone")).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("skip_rope"))
        {   // two children who are not here jump rope in the yard: the rhythm of a note block, the
            // rope itself a line of crit particles crossing the dark, and two small silhouettes
            // rising and sitting down again on every beat
            if (schools.isEmpty()) body = noPlace;
            else
            {
                load += QStringLiteral("scoreboard players set #k2 osmc_enc_t 2\n");
                body = nearRoll(id, 19000, 23999, schools, QStringLiteral("sr/"));
                for (int j = 0; j < schools.size(); ++j)
                {
                    const Prop& p = schools[j];
                    const QString pth = ev + QStringLiteral("sr/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("sr/%1/").arg(j);
                    const Cell& a = p.cells[1];
                    const Cell& b = p.cells[2];
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the yard is skipping rope tonight\n"
                        "execute if score #sr_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..40] run return 0\n"
                        "scoreboard players set #sr_%1 osmc_enc 1\n"
                        "scoreboard players set #sr_n_%1 osmc_enc 0\n"
                        "summon minecraft:armor_stand %5 %6 %7 {Invisible:1b,NoGravity:1b,Silent:1b,"
                        "Invulnerable:1b,Scale:0.6f,Tags:[\"osmc_sr\",\"osmc_sr_%1_a\"]}\n"
                        "summon minecraft:armor_stand %8 %9 %10 {Invisible:1b,NoGravity:1b,Silent:1b,"
                        "Invulnerable:1b,Scale:0.6f,Tags:[\"osmc_sr\",\"osmc_sr_%1_b\"]}\n"
                        "schedule function %11 10t replace\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z)
                        .arg(a.x + 0.5, 0, 'f', 1).arg(double(a.y)).arg(a.z + 0.5, 0, 'f', 1)
                        .arg(b.x + 0.5, 0, 'f', 1).arg(double(b.y)).arg(b.z + 0.5, 0, 'f', 1)
                        .arg(fnp + QStringLiteral("beat")).toUtf8());
                    add(pth + QStringLiteral("beat.mcfunction"), (QStringLiteral(
                        "# one beat of the rope: the children up, the children down, on alternate beats\n"
                        "execute unless score #sr_%1 osmc_enc matches 1 run return 0\n"
                        "scoreboard players add #sr_n_%1 osmc_enc 1\n"
                        "scoreboard players operation #sr_p_%1 osmc_enc = #sr_n_%1 osmc_enc\n"
                        "scoreboard players operation #sr_p_%1 osmc_enc %= #k2 osmc_enc_t\n"
                        "execute if score #sr_p_%1 osmc_enc matches 0 run"
                        " tp @e[type=minecraft:armor_stand,tag=osmc_sr_%1_a,limit=1] %2 %3 %4\n"
                        "execute if score #sr_p_%1 osmc_enc matches 0 run"
                        " tp @e[type=minecraft:armor_stand,tag=osmc_sr_%1_b,limit=1] %5 %6 %7\n"
                        "execute if score #sr_p_%1 osmc_enc matches 1 run"
                        " tp @e[type=minecraft:armor_stand,tag=osmc_sr_%1_a,limit=1] %2 %8 %4\n"
                        "execute if score #sr_p_%1 osmc_enc matches 1 run"
                        " tp @e[type=minecraft:armor_stand,tag=osmc_sr_%1_b,limit=1] %5 %9 %7\n"
                        "particle minecraft:crit %10 %11 %12 0.3 0.5 0.3 0.1 4\n"
                        "execute if score #sr_n_%1 osmc_enc matches 1..32 run"
                        " playsound minecraft:block.note_block.hat master @a[x=%2,y=%3,z=%4,distance=..24]"
                        " %2 %3 %4 0.3 1.6\n")
                        .arg(j).arg(a.x + 0.5, 0, 'f', 1).arg(double(a.y)).arg(a.z + 0.5, 0, 'f', 1)
                        .arg(b.x + 0.5, 0, 'f', 1).arg(double(b.y)).arg(b.z + 0.5, 0, 'f', 1)
                        .arg(a.y + 1).arg(b.y + 1)
                        .arg((a.x + b.x) / 2.0 + 0.5, 0, 'f', 1).arg(a.y + 1)
                        .arg((a.z + b.z) / 2.0 + 0.5, 0, 'f', 1)
                        + QStringLiteral(
                        "execute if score #sr_n_%1 osmc_enc matches ..63 run schedule function %2 10t replace\n"
                        "execute if score #sr_n_%1 osmc_enc matches 64.. run function %3\n")
                        .arg(j).arg(fnp + QStringLiteral("beat")).arg(fnp + QStringLiteral("gone"))).toUtf8());
                    add(pth + QStringLiteral("gone.mcfunction"), QStringLiteral(
                        "# the rope lies in the grass again; nobody was holding its ends\n"
                        "kill @e[type=minecraft:armor_stand,tag=osmc_sr_%1_a]\n"
                        "kill @e[type=minecraft:armor_stand,tag=osmc_sr_%1_b]\n"
                        "scoreboard players set #sr_%1 osmc_enc 0\n").arg(j).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #sr_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 0..11999 run function %2\n")
                        .arg(j).arg(fnp + QStringLiteral("gone")).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("auditorium"))
        {   // the spotlight over the empty chair: a pillar of end rod light over the yard three
            // slow pulses, and the outline of a graduate drawn once in white sparks on the ground
            if (schools.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 20000, 23999, schools, QStringLiteral("au/"));
                static const int kRing[8][2] = { {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1} };
                for (int j = 0; j < schools.size(); ++j)
                {
                    const Prop& p = schools[j];
                    const Cell& c = p.cells[1];
                    const QString pth = ev + QStringLiteral("au/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("au/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the auditorium rehearsed for nobody; the light stayed\n"
                        "execute if score #au_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..40] run return 0\n"
                        "scoreboard players set #au_%1 osmc_enc 1\n"
                        "title @a[x=%2,y=%3,z=%4,distance=..24] actionbar"
                        " {\"text\":\"a spotlight stands over one empty chair\","
                        "\"color\":\"yellow\",\"italic\":true}\n"
                        "schedule function %5 1s replace\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(fnp + QStringLiteral("p1")).toUtf8());
                    QString pulse = QStringLiteral("# the pillar, and the outline of the graduate\n"
                                                   "execute unless score #au_%1 osmc_enc matches 1 run return 0\n")
                                        .arg(j);
                    for (int k = 0; k < 8; ++k)
                        pulse += QStringLiteral("particle minecraft:end_rod %1 %2 %3 0.05 1.2 0.05 0.02 10\n")
                                     .arg(c.x + kRing[k][0] + 0.5, 0, 'f', 1).arg(c.y + 1.0)
                                     .arg(c.z + kRing[k][1] + 0.5, 0, 'f', 1);
                    pulse += QStringLiteral("particle minecraft:portal %1 %2 %3 0.9 0.05 0.9 0.2 60\n"
                                            "playsound minecraft:block.beacon.power_select master"
                                            " @a[x=%4,y=%5,z=%6,distance=..24] %4 %5 %6 0.3 1.7\n")
                                 .arg(c.x + 0.5, 0, 'f', 1).arg(c.y).arg(c.z + 0.5, 0, 'f', 1)
                                 .arg(p.x).arg(p.y).arg(p.z);
                    add(pth + QStringLiteral("p1.mcfunction"),
                        (pulse + QStringLiteral("schedule function %1 2s replace\n")
                             .arg(fnp + QStringLiteral("p2"))).toUtf8());
                    add(pth + QStringLiteral("p2.mcfunction"), (pulse + QStringLiteral(
                        "schedule function %1 2s replace\n").arg(fnp + QStringLiteral("p3"))).toUtf8());
                    add(pth + QStringLiteral("p3.mcfunction"), (pulse + QStringLiteral(
                        "schedule function %1 2s replace\n").arg(fnp + QStringLiteral("gone"))).toUtf8());
                    add(pth + QStringLiteral("gone.mcfunction"), QStringLiteral(
                        "# the light goes out mid-applause\n"
                        "playsound minecraft:block.beacon.deactivate master @a[x=%1,y=%2,z=%3,distance=..24]"
                        " %1 %2 %3 0.3 0.7\n"
                        "scoreboard players set #au_%4 osmc_enc 0\n")
                        .arg(p.x).arg(p.y).arg(p.z).arg(j).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #au_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 0..11999 run scoreboard players set #au_%1 osmc_enc 0\n")
                        .arg(j).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("timetable"))
        {   // the board was rewritten in the night: dust over the school's chest, a scrap left on
            // its lid, and the last line of the scrap reads a name the city does not have
            if (schools.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 1000, 5999, schools, QStringLiteral("tt/"));
                static const char* const kNames[6] = { "V. Nezhina", "A. Voltinskaya", "K. Surov",
                                                       "M. Golubeva", "I. Tish", "L. Morozova" };
                for (int j = 0; j < schools.size(); ++j)
                {
                    const Prop& p = schools[j];
                    add(ev + QStringLiteral("tt/%1/go.mcfunction").arg(j), QStringLiteral(
                        "# the board remembered somebody else's schedule\n"
                        "execute unless entity @a[x=%1,y=%2,z=%3,distance=..32] run return 0\n"
                        "particle minecraft:dust{color:[1.0,1.0,1.0],scale:1.0} %4 %5 %6 0.3 0.4 0.3 0.01 40\n"
                        "summon minecraft:item %4 %5 %6 {Item:{id:\"minecraft:paper\",count:1b},"
                        "CustomName:{\"text\":\"the timetable, rewritten\"},"
                        "Tags:[\"osmc_tt\"],PickupDelay:600,Health:32767}\n"
                        "playsound minecraft:block.wool.place master @a[x=%1,y=%2,z=%3,distance=..24]"
                        " %4 %5 %6 0.4 0.8\n"
                        "title @a[x=%1,y=%2,z=%3,distance=..24] actionbar"
                        " {\"text\":\"the board was rewritten; the last line reads %7\","
                        "\"color\":\"dark_gray\",\"italic\":true}\n")
                        .arg(p.x).arg(p.y).arg(p.z)
                        .arg(p.cells.first().x + 0.5, 0, 'f', 1).arg(p.cells.first().y + 0.6)
                        .arg(p.cells.first().z + 0.5, 0, 'f', 1)
                        .arg(QLatin1String(kNames[j % 6])).toUtf8());
                }
            }
        }
        else if (id == QLatin1String("mower"))
        {   // the night mower: one stripe per night, three cells of the baked walk go trodden under
            // nobody's feet with the hum of something small and low, and the dawn gives the whole
            // lawn back — whoever walks the park early sees the stripe of last night only
            if (lawns.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 18000, 23999, lawns, QStringLiteral("mow/"));
                for (int j = 0; j < lawns.size(); ++j)
                {
                    const Prop& p = lawns[j];
                    const int steps = (int(p.cells.size()) + 2) / 3;
                    const QString pth = ev + QStringLiteral("mow/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("mow/%1/").arg(j);
                    QString go = QStringLiteral(
                        "# one stripe per night; the mower passes the cells the exporter measured~N~"
                        "execute if score #mow_done_%1 osmc_enc matches 1 run return 0~N~"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..48] run return 0~N~"
                        "scoreboard players add #mow_i_%1 osmc_enc 1~N~")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z);
                    for (int k = 0; k < steps; ++k)
                        go += QStringLiteral("execute if score #mow_i_%1 osmc_enc matches %2 run function %3~N~")
                                .arg(j).arg(k + 1).arg(fnp + QStringLiteral("s%1").arg(k));
                    go += QStringLiteral(
                        "execute if score #mow_i_%1 osmc_enc matches %2.. run scoreboard players set #mow_done_%1 osmc_enc 1~N~")
                        .arg(j).arg(steps + 1);
                    add(pth + QStringLiteral("go.mcfunction"), go.toUtf8());
                    for (int k = 0; k < steps; ++k)
                    {
                        QString step = QStringLiteral("# stripe %1 of the lawn goes trodden~N~").arg(k + 1);
                        for (int i = k * 3; i < qMin(k * 3 + 3, int(p.cells.size())); ++i)
                        {
                            const Cell& c = p.cells[i];
                            step += QStringLiteral("setblock %1 %2 %3 minecraft:dirt_path~N~"
                                                   "playsound minecraft:block.grass.break master"
                                                   " @a[x=%1,y=%2,z=%3,distance=..24] %1 %2 %3 0.5 0.4~N~")
                                        .arg(c.x).arg(c.y).arg(c.z);
                        }
                        step += QStringLiteral("particle minecraft:cloud %1 %2 %3 1.5 0.2 1.5 0.03 40~N~"
                                               "playsound minecraft:entity.bee.loop master"
                                               " @a[x=%1,y=%2,z=%3,distance=..32] %1 %2 %3 0.4 0.35~N~")
                                    .arg(p.x).arg(p.y).arg(p.z);
                        add(pth + QStringLiteral("s%1.mcfunction").arg(k), step.toUtf8());
                    }
                    QString quiet = QStringLiteral(
                        "# the dawn: the lawn is whole again, the mower was never here~N~"
                        "scoreboard players set #mow_i_%1 osmc_enc 0~N~"
                        "scoreboard players set #mow_done_%1 osmc_enc 0~N~").arg(j);
                    for (const Cell& c : p.cells)
                        quiet += QStringLiteral("setblock %1 %2 %3 %4~N~")
                                    .arg(c.x).arg(c.y).arg(c.z).arg(blockName(c.id));
                    add(pth + QStringLiteral("quiet.mcfunction"), quiet.toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #mow_i_%1 osmc_enc matches 1.. if score #day osmc_enc_t"
                        " matches 1000..11999 run function %2~N~")
                        .arg(j).arg(fnp + QStringLiteral("quiet")).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("extra_car"))
        {   // a radio is playing beside a car that was not here yesterday: three taps of a note
            // block like a broadcast without words, a line on the actionbar, and silence by morning
            if (cars.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 19000, 23999, cars, QStringLiteral("ec/"));
                for (int j = 0; j < cars.size(); ++j)
                {
                    const Prop& p = cars[j];
                    add(ev + QStringLiteral("ec/%1/go.mcfunction").arg(j), QStringLiteral(
                        "# the car that stands too well plays the news to nobody\n"
                        "execute unless entity @a[x=%1,y=%2,z=%3,distance=..40] run return 0\n"
                        "playsound minecraft:block.note_block.bit master @a[x=%1,y=%2,z=%3,distance=..20]"
                        " %4 %5 %6 0.3 0.6\n"
                        "title @a[x=%1,y=%2,z=%3,distance=..20] actionbar"
                        " {\"text\":\"the parked car is playing the news\","
                        "\"color\":\"gray\",\"italic\":true}\n"
                        "schedule function osmc_enc:ev/ec/%7/t2 2s replace\n")
                        .arg(p.x).arg(p.y).arg(p.z)
                        .arg(p.cells[1].x).arg(p.cells[1].y).arg(p.cells[1].z).arg(j).toUtf8());
                    add(ev + QStringLiteral("ec/%1/t2.mcfunction").arg(j), QStringLiteral(
                        "execute positioned %1 %2 %3 run playsound minecraft:block.note_block.bit"
                        " master @a[distance=..20] ~ ~ ~ 0.3 0.5\n"
                        "schedule function osmc_enc:ev/ec/%4/t3 2s replace\n")
                        .arg(p.cells[1].x).arg(p.cells[1].y).arg(p.cells[1].z).arg(j).toUtf8());
                    add(ev + QStringLiteral("ec/%1/t3.mcfunction").arg(j), QStringLiteral(
                        "execute positioned %1 %2 %3 run playsound minecraft:block.note_block.bit"
                        " master @a[distance=..20] ~ ~ ~ 0.3 0.4\n"
                        "execute positioned %1 %2 %3 run playsound minecraft:block.note_block.bass"
                        " master @a[distance=..20] ~ ~ ~ 0.3 0.5\n")
                        .arg(p.cells[1].x).arg(p.cells[1].y).arg(p.cells[1].z).toUtf8());
                }
            }
        }
        else if (id == QLatin1String("phantom_station"))
        {   // the station that is not on any map lights up for a minute at the platform the exporter
            // knows: a lantern and a bell stand in the hall only while nobody is looking closely, and
            // the morning sweep removes them from the hall whether or not the minute kept its word
            if (metros.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 19000, 23999, metros, QStringLiteral("ps/"));
                for (int j = 0; j < metros.size(); ++j)
                {
                    const Prop& p = metros[j];
                    const QString pth = ev + QStringLiteral("ps/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("ps/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# a lantern, a bell, and no station behind either\n"
                        "execute if score #ps_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..40] run return 0\n"
                        "scoreboard players set #ps_%1 osmc_enc 1\n"
                        "execute if block %5 %6 %7 minecraft:air run setblock %5 %6 %7 minecraft:lantern\n"
                        "execute if block %8 %9 %10 minecraft:air run setblock %8 %9 %10 minecraft:bell\n"
                        "particle minecraft:end_rod %2 %3 %4 2 2 2 0.02 60\n"
                        "playsound minecraft:block.bell.use master @a[x=%2,y=%3,z=%4,distance=..32]"
                        " %2 %3 %4 0.7 0.8\n"
                        "title @a[x=%2,y=%3,z=%4,distance=..24] actionbar"
                        " {\"text\":\"the platform is lit on a map that lists no such station\","
                        "\"color\":\"aqua\",\"italic\":true}\n"
                        "schedule function %11 60s replace\n")
                        .arg(j).arg(p.x).arg(p.y + 1).arg(p.z)
                        .arg(p.x + 1).arg(p.y + 1).arg(p.z)
                        .arg(p.x - 1).arg(p.y + 1).arg(p.z)
                        .arg(fnp + QStringLiteral("gone")).toUtf8());
                    add(pth + QStringLiteral("gone.mcfunction"), QStringLiteral(
                        "# the minute kept; the hall remembers nothing\n"
                        "execute if block %1 %2 %3 minecraft:lantern run setblock %1 %2 %3 minecraft:air\n"
                        "execute if block %4 %5 %6 minecraft:bell run setblock %4 %5 %6 minecraft:air\n"
                        "scoreboard players set #ps_%7 osmc_enc 0\n")
                        .arg(p.x + 1).arg(p.y + 1).arg(p.z)
                        .arg(p.x - 1).arg(p.y + 1).arg(p.z).arg(j).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #ps_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 1000..11999 run function %2\n")
                        .arg(j).arg(fnp + QStringLiteral("gone")).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("breathing_rails"))
        {   // the tunnel breathes: a heartbeat walks the baked run, a red pulse thins and thickens
            // above the rails, and at the middle a low listening sound leans in to hear the sleeper.
            // The rails themselves do not move — the light and the sound do (the honest part of the
            // idea: nobody must ever be sealed inside the pulse, so it never becomes a wall)
            if (metros.isEmpty()) body = noPlace;
            else
            {
                load += QStringLiteral("scoreboard players set #k4 osmc_enc_t 4\n");
                body = nearRoll(id, 18000, 23999, metros, QStringLiteral("br/"));
                for (int j = 0; j < metros.size(); ++j)
                {
                    const Prop& p = metros[j];
                    const QString pth = ev + QStringLiteral("br/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("br/%1/").arg(j);
                    const int mid = p.cells.size() / 2;
                    const Cell& c = p.cells[mid];
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the heartbeat starts at the far end of the run\n"
                        "execute if score #br_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..48] run return 0\n"
                        "scoreboard players set #br_%1 osmc_enc 1\n"
                        "scoreboard players set #br_n_%1 osmc_enc 0\n"
                        "title @a[x=%2,y=%3,z=%4,distance=..32] actionbar"
                        " {\"text\":\"the tunnel breathes in\",\"color\":\"dark_red\",\"italic\":true}\n"
                        "schedule function %5 10t replace\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(fnp + QStringLiteral("beat")).toUtf8());
                    add(pth + QStringLiteral("beat.mcfunction"), (QStringLiteral(
                        "# one beat: the red pulse at the rails, one in four times leaning closer\n"
                        "execute unless score #br_%1 osmc_enc matches 1 run return 0\n"
                        "scoreboard players add #br_n_%1 osmc_enc 1\n"
                        "scoreboard players operation #br_q_%1 osmc_enc = #br_n_%1 osmc_enc\n"
                        "scoreboard players operation #br_q_%1 osmc_enc %= #k4 osmc_enc_t\n"
                        "particle minecraft:dust{color:[0.8,0.05,0.05],scale:1.0} %2 %3 %4 0.9 0.2 0.9 0.02 18\n"
                        "playsound minecraft:block.sculk_sensor.clicking master"
                        " @a[x=%2,y=%3,z=%4,distance=..40] %2 %3 %4 0.4 0.6\n"
                        "execute if score #br_q_%1 osmc_enc matches 0 run playsound minecraft:entity.warden.heartbeat"
                        " master @a[x=%2,y=%3,z=%4,distance=..40] %2 %3 %4 0.6 0.8\n")
                        .arg(j).arg(c.x).arg(c.y).arg(c.z)
                        + QStringLiteral(
                        "execute if score #br_n_%1 osmc_enc matches 1..40 run schedule function %2 40t replace\n"
                        "execute if score #br_n_%1 osmc_enc matches 41.. run scoreboard players set #br_%1 osmc_enc 0\n")
                        .arg(j).arg(fnp + QStringLiteral("beat"))).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #br_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 0..11999 run scoreboard players set #br_%1 osmc_enc 0\n")
                        .arg(j).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("power_hum"))
        {   // the grid hums: one lamp tonight carries the fifty hertz of wires nobody can see —
            // three slow pulses of a low sound under the light and the ordinary night returns;
            // the lines themselves stay where the city put them, underground
            if (lamps.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 20000, 23999, lamps, QStringLiteral("ph/"));
                for (int j = 0; j < lamps.size(); ++j)
                {
                    const Prop& p = lamps[j];
                    const Cell& light = p.cells.first();
                    const QString pth = ev + QStringLiteral("ph/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("ph/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the hum starts under this lamp\n"
                        "execute if score #ph_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..32] run return 0\n"
                        "scoreboard players set #ph_%1 osmc_enc 1\n"
                        "title @a[x=%2,y=%3,z=%4,distance=..24] actionbar"
                        " {\"text\":\"the wires hum fifty hertz to nobody\","
                        "\"color\":\"dark_gray\",\"italic\":true}\n"
                        "schedule function %5 1s replace\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(fnp + QStringLiteral("p1")).toUtf8());
                    for (int s = 1; s <= 3; ++s)
                    {
                        QString pulse = QStringLiteral("# the hum holds its pulse %1\n").arg(s);
                        pulse += QStringLiteral("execute unless score #ph_%1 osmc_enc matches 1 run return 0\n"
                                                "playsound minecraft:entity.elder_guardian.curse master"
                                                " @a[x=%2,y=%3,z=%4,distance=..28] %5 %6 %7 0.4 0.45\n"
                                                "particle minecraft:smoke %5 %6 %7 0.2 0.3 0.2 0.01 6\n")
                                     .arg(j).arg(p.x).arg(p.y).arg(p.z)
                                     .arg(light.x).arg(light.y).arg(light.z);
                        if (s < 3)
                            pulse += QStringLiteral("schedule function %1 3s replace\n")
                                         .arg(fnp + QStringLiteral("p%1").arg(s + 1));
                        else
                            pulse += QStringLiteral("schedule function %1 4s replace\n")
                                         .arg(fnp + QStringLiteral("gone"));
                        add(pth + QStringLiteral("p%1.mcfunction").arg(s), pulse.toUtf8());
                    }
                    add(pth + QStringLiteral("gone.mcfunction"), QStringLiteral(
                        "# the grid keeps its own business again\n"
                        "scoreboard players set #ph_%1 osmc_enc 0\n").arg(j).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #ph_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 0..11999 run scoreboard players set #ph_%1 osmc_enc 0\n")
                        .arg(j).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("cloud_reflection"))
        {   // the bowl keeps a cloud the sky does not have: white softness drifting over the water
            // in three sweeps, and whoever stands by the fountain sees it pass under their own
            // reflection; the morning sweep only clears the counter — the particles were never there
            if (founts.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 19000, 23999, founts, QStringLiteral("cr/"));
                static const int kDrift[3][2] = { { 0, 0 }, { 1, 1 }, { -1, -1 } };   // the cloud's path
                for (int j = 0; j < founts.size(); ++j)
                {
                    const Prop& p = founts[j];
                    const Cell& mouth = p.cells.first();
                    const QString pth = ev + QStringLiteral("cr/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("cr/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the water holds something the sky never sent\n"
                        "execute if score #cr_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..32] run return 0\n"
                        "scoreboard players set #cr_%1 osmc_enc 1\n"
                        "title @a[x=%2,y=%3,z=%4,distance=..24] actionbar"
                        " {\"text\":\"the bowl reflects a cloud this sky does not have\","
                        "\"color\":\"white\",\"italic\":true}\n"
                        "schedule function %5 1s replace\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(fnp + QStringLiteral("d1")).toUtf8());
                    for (int s = 1; s <= 3; ++s)
                    {
                        QString drift = QStringLiteral("# the cloud crosses the bowl, sweep %1\n").arg(s);
                        drift += QStringLiteral("execute unless score #cr_%1 osmc_enc matches 1 run return 0\n"
                                                "particle minecraft:cloud %2 %3 %4 1.1 0.15 1.1 0.015 22\n"
                                                "particle minecraft:bubble %2 %3 %4 0.9 0.1 0.9 0.02 12\n")
                                     .arg(j).arg(mouth.x + kDrift[s - 1][0] + 0.5, 0, 'f', 1)
                                     .arg(mouth.y + 1).arg(mouth.z + kDrift[s - 1][1] + 0.5, 0, 'f', 1);
                        if (s < 3)
                            drift += QStringLiteral("schedule function %1 4s replace\n")
                                         .arg(fnp + QStringLiteral("d%1").arg(s + 1));
                        else
                            drift += QStringLiteral("schedule function %1 5s replace\n")
                                         .arg(fnp + QStringLiteral("gone"));
                        add(pth + QStringLiteral("d%1.mcfunction").arg(s), drift.toUtf8());
                    }
                    add(pth + QStringLiteral("gone.mcfunction"), QStringLiteral(
                        "# the cloud leaves with the night; the bowl keeps only water\n"
                        "scoreboard players set #cr_%1 osmc_enc 0\n").arg(j).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #cr_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 0..11999 run scoreboard players set #cr_%1 osmc_enc 0\n")
                        .arg(j).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("ghost_rolley"))
        {   // the rorolley comes when waited for: whoever stands on the tunnel floor gets it —
            // a real cart (it existed long before the rails left) rolls the baked run toward the
            // sleeper, sparks along, and at the far end it is simply not there anymore
            if (metros.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 18000, 23999, metros, QStringLiteral("gr/"));
                for (int j = 0; j < metros.size(); ++j)
                {
                    const Prop& p = metros[j];
                    const int n = p.cells.size() - 1;
                    const QString pth = ev + QStringLiteral("gr/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("gr/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the cart comes only to whoever waits on the floor\n"
                        "execute if score #gr_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,dx=6,dy=6,dz=6] run return 0\n"
                        "scoreboard players set #gr_%1 osmc_enc 1\n"
                        "title @a[x=%2,y=%3,z=%4,dx=6,dy=6,dz=6] actionbar"
                        " {\"text\":\"somewhere down the tunnel wheels begin to turn\","
                        "\"color\":\"gray\",\"italic\":true}\n"
                        "schedule function %5 3s replace\n")
                        .arg(j).arg(p.cells[n].x - 3).arg(p.cells[n].y).arg(p.cells[n].z - 3)
                        .arg(fnp + QStringLiteral("h0")).toUtf8());
                    for (int s = 0; s < 8; ++s)
                    {   // the cart enters from the far end and walks eight cells toward the middle
                        const int idx = qMax(1, n - 1 - s * qMax(1, (n - 1) / 8));
                        const Cell& c = p.cells[idx];
                        QString hop = QStringLiteral("# the rorolley at cell %1 of the run\n").arg(s + 1);
                        hop += QStringLiteral("execute unless score #gr_%1 osmc_enc matches 1 run return 0\n"
                                              "summon minecraft:minecart %2 %3 %4"
                                              " {Tags:[\"osmc_gr_%1\"]}\n"
                                              "particle minecraft:crit %2 %3 %4 0.3 0.2 0.3 0.1 10\n"
                                              "playsound minecraft:entity.minecart.riding master"
                                              " @a[x=%5,y=%6,z=%7,dx=12,dy=12,dz=12] %2 %3 %4 0.5 0.55\n")
                                   .arg(j).arg(c.x + 0.5, 0, 'f', 1).arg(c.y + 1).arg(c.z + 0.5, 0, 'f', 1)
                                   .arg(p.cells[n].x - 6).arg(p.cells[n].y - 6).arg(p.cells[n].z - 6);
                        hop += QStringLiteral("kill @e[type=minecraft:minecart,tag=osmc_gr_%1,distance=..16,limit=1]\n")
                                   .arg(j);   // the cart behind itself was never here either
                        if (s + 1 < 8)
                            hop += QStringLiteral("schedule function %1 20t replace\n")
                                       .arg(fnp + QStringLiteral("h%1").arg(s + 1));
                        else
                            hop += QStringLiteral("schedule function %1 3s replace\n")
                                       .arg(fnp + QStringLiteral("gone"));
                        add(pth + QStringLiteral("h%1.mcfunction").arg(s), hop.toUtf8());
                    }
                    add(pth + QStringLiteral("gone.mcfunction"), QStringLiteral(
                        "# the rails remember the cart without keeping it\n"
                        "kill @e[type=minecraft:minecart,tag=osmc_gr_%1]\n"
                        "playsound minecraft:block.metal.pressure_plate.off master"
                        " @a[x=%2,y=%3,z=%4,dx=12,dy=12,dz=12] %2 %3 %4 0.4 0.5\n"
                        "scoreboard players set #gr_%1 osmc_enc 0\n")
                        .arg(j).arg(p.cells[n].x - 6).arg(p.cells[n].y - 6).arg(p.cells[n].z - 6).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #gr_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 0..11999 run kill @e[type=minecraft:minecart,tag=osmc_gr_%1]\n"
                        "execute if score #gr_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 0..11999 run scoreboard players set #gr_%1 osmc_enc 0\n")
                        .arg(j).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("night_market"))
        {   // the night market sets up where the field lay all day: stalls of lantern light with no
            // wood in them, the murmur of a hundred buyers made of one villager's whisper, and by
            // the time the walker reaches the stop the row is already packing up
            if (stops.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 19000, 23999, stops, QStringLiteral("nm/"));
                static const int kRow[5] = { -6, -3, 0, 3, 6 };   // the five stalls of the row
                for (int j = 0; j < stops.size(); ++j)
                {
                    const Prop& p = stops[j];
                    const int ax = p.dir & 3;
                    const int ox = (ax == 1) ? 1 : (ax == 3) ? -1 : 0;   // the row across the road
                    const int oz = (ax == 0) ? -1 : (ax == 2) ? 1 : 0;
                    const QString pth = ev + QStringLiteral("nm/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("nm/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the market opens without a single plank\n"
                        "execute if score #nm_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..24] run return 0\n"
                        "scoreboard players set #nm_%1 osmc_enc 1\n"
                        "title @a[x=%2,y=%3,z=%4,distance=..24] actionbar"
                        " {\"text\":\"a market is open here — where the field lay all day\","
                        "\"color\":\"gold\",\"italic\":true}\n"
                        "schedule function %5 1s replace\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(fnp + QStringLiteral("o1")).toUtf8());
                    for (int s = 1; s <= 3; ++s)
                    {
                        QString open = QStringLiteral("# the stalls stand brighter, sweep %1\n").arg(s);
                        open += QStringLiteral("execute unless score #nm_%1 osmc_enc matches 1"
                                               " run return 0\n").arg(j);
                        for (int k = 0; k < 5; ++k)
                            open += QStringLiteral("particle minecraft:dragon_breath %1 %2 %3 0.2 0.4 0.2"
                                                   " 0.02 8\n")
                                         .arg(p.x + ox * kRow[k] + 0.5, 0, 'f', 1)
                                         .arg(p.y + 1).arg(p.z + oz * kRow[k] + 0.5, 0, 'f', 1);
                        open += QStringLiteral("playsound minecraft:entity.villager.ambient master"
                                               " @a[x=%1,y=%2,z=%3,distance=..28] %1 %2 %3 0.3 %4\n")
                                    .arg(p.x).arg(p.y).arg(p.z).arg(0.8 + s * 0.15, 0, 'f', 2);
                        if (s < 3)
                            open += QStringLiteral("schedule function %1 4s replace\n")
                                        .arg(fnp + QStringLiteral("o%1").arg(s + 1));
                        else
                            open += QStringLiteral("schedule function %1 6s replace\n")
                                        .arg(fnp + QStringLiteral("gone"));
                        add(pth + QStringLiteral("o%1.mcfunction").arg(s), open.toUtf8());
                    }
                    add(pth + QStringLiteral("gone.mcfunction"), QStringLiteral(
                        "# the market packs up with nobody buying; the bell of the last stall\n"
                        "playsound minecraft:block.bell.use master @a[x=%1,y=%2,z=%3,distance=..28]"
                        " %1 %2 %3 0.35 0.7\n"
                        "scoreboard players set #nm_%4 osmc_enc 0\n")
                        .arg(p.x).arg(p.y).arg(p.z).arg(j).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #nm_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 0..11999 run scoreboard players set #nm_%1 osmc_enc 0\n")
                        .arg(j).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("hitchhiker"))
        {   // somebody waits at the road's edge with a lantern; whoever comes close enough to offer
            // a ride finds the lantern burning on empty asphalt and footsteps walking away past the
            // car. By morning a note lies where the lantern stood — nobody remembers riding with him
            if (cars.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 21000, 23999, cars, QStringLiteral("hh/"));
                for (int j = 0; j < cars.size(); ++j)
                {
                    const Prop& p = cars[j];
                    if (p.cells.size() < 2)
                    {   // a car with no door cell has no road edge to wait at
                        add(ev + QStringLiteral("hh/%1/go.mcfunction").arg(j), QByteArray(
                            "# this car stands where nobody can stand\nreturn 0\n"));
                        continue;
                    }
                    const Cell& door = p.cells[1];
                    const QString pth = ev + QStringLiteral("hh/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("hh/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# a lantern waits at the edge of the road\n"
                        "execute if score #hh_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..40] run return 0\n"
                        "execute unless block %2 %3 %4 minecraft:air run return 0\n"
                        "scoreboard players set #hh_%1 osmc_enc 1\n"
                        "setblock %2 %3 %4 minecraft:lantern\n"
                        "title @a[x=%2,y=%3,z=%4,distance=..24] actionbar"
                        " {\"text\":\"somebody waits at the road edge with a lantern\","
                        "\"color\":\"yellow\",\"italic\":true}\n"
                        "schedule function %5 2s replace\n")
                        .arg(j).arg(door.x).arg(door.y).arg(door.z).arg(fnp + QStringLiteral("wait")).toUtf8());
                    add(pth + QStringLiteral("wait.mcfunction"), QStringLiteral(
                        "# the wait: he steps closer to the road as whoever approaches\n"
                        "execute unless score #hh_%1 osmc_enc matches 1 run return 0\n"
                        "execute positioned %2 %3 %4 if block %2 %3 %4 minecraft:lantern"
                        " if entity @a[distance=..6] run function %5\n"
                        "execute if score #hh_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 0..11999 run function %6\n"
                        "execute if score #hh_%1 osmc_enc matches 1 run schedule function %7 2s replace\n")
                        .arg(j).arg(door.x).arg(door.y).arg(door.z)
                        .arg(fnp + QStringLiteral("offer")).arg(fnp + QStringLiteral("gone"))
                        .arg(fnp + QStringLiteral("wait")).toUtf8());
                    add(pth + QStringLiteral("offer.mcfunction"), QStringLiteral(
                        "# the offer was one block too late: the lantern burns on empty asphalt\n"
                        "setblock %1 %2 %3 minecraft:air\n"
                        "particle minecraft:poof %1 %4 %3 0.3 0.4 0.3 0.08 24\n"
                        "playsound minecraft:entity.player.splash master @a[x=%1,y=%2,z=%3,distance=..20]"
                        " %1 %2 %3 0.2 0.6\n"
                        "playsound minecraft:entity.skeleton.step master @a[x=%1,y=%2,z=%3,distance=..20]"
                        " %5 %6 %7 0.4 0.5\n"
                        "title @a[x=%1,y=%2,z=%3,distance=..20] actionbar"
                        " {\"text\":\"he was already behind you, and then he was not\","
                        "\"color\":\"gray\",\"italic\":true}\n"
                        "scoreboard players set #hh_%8 osmc_enc 2\n")
                        .arg(door.x).arg(door.y).arg(door.z).arg(door.y + 1)
                        .arg(door.x + 4).arg(door.y).arg(door.z + 4).arg(j).toUtf8());
                    add(pth + QStringLiteral("gone.mcfunction"), QStringLiteral(
                        "# the dawn: the note where the lantern stood, and no other proof\n"
                        "execute if score #hh_%1 osmc_enc matches 2 if block %2 %3 %4 minecraft:air run"
                        " summon minecraft:item %2 %3 %4 {Item:{id:\"minecraft:paper\",count:1b},"
                        "CustomName:{\"text\":\"I did not ride. thank you anyway\"},"
                        "Tags:[\"osmc_hh_%1\"],PickupDelay:6000,Health:32767}\n"
                        "execute if block %2 %3 %4 minecraft:lantern run setblock %2 %3 %4 minecraft:air\n"
                        "scoreboard players set #hh_%1 osmc_enc 0\n")
                        .arg(j).arg(door.x).arg(door.y).arg(door.z).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #hh_%1 osmc_enc matches 1..2 if score #day osmc_enc_t"
                        " matches 1000..11999 run function %2\n")
                        .arg(j).arg(fnp + QStringLiteral("gone")).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("ghost_lift"))
        {   // the lift answers on a floor the stairs do not list: a call button clicked where no
            // button is, the shaft hum behind the stairwell wall, and doors that slide across
            // plain plaster. The cabin never comes — the honest part of the idea; the sound does
            if (halfs.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 18000, 23999, halfs, QStringLiteral("gl/"));
                for (int j = 0; j < halfs.size(); ++j)
                {
                    const Prop& p = halfs[j];
                    const Cell& low = p.cells.first();
                    const QString pth = ev + QStringLiteral("gl/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("gl/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the call was pressed by whoever passed too slowly\n"
                        "execute if score #gl_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,dx=8,dy=8,dz=8] run return 0\n"
                        "scoreboard players set #gl_%1 osmc_enc 1\n"
                        "playsound minecraft:block.note_block.pling master @a[x=%2,y=%3,z=%4,dx=8,dy=8,dz=8]"
                        " %5 %6 %7 0.5 0.7\n"
                        "schedule function %8 2s replace\n")
                        .arg(j).arg(p.x - 4).arg(p.y - 4).arg(p.z - 4)
                        .arg(low.x).arg(low.y).arg(low.z).arg(fnp + QStringLiteral("hum")).toUtf8());
                    add(pth + QStringLiteral("hum.mcfunction"), QStringLiteral(
                        "# the shaft hum, and doors sliding over plaster\n"
                        "execute unless score #gl_%1 osmc_enc matches 1 run return 0\n"
                        "playsound minecraft:block.beacon.power_select master @a[x=%2,y=%3,z=%4,dx=8,dy=8,dz=8]"
                        " %5 %6 %7 0.3 0.5\n"
                        "playsound minecraft:block.polished_blackstone.pressure_plate.off master"
                        " @a[x=%2,y=%3,z=%4,dx=8,dy=8,dz=8] %5 %6 %7 0.6 0.8\n"
                        "particle minecraft:dust{color:[0.5,0.5,0.55],scale:1.0} %5 %9 %7 0.05 1.1 0.05 0.02 30\n"
                        "title @a[x=%2,y=%3,z=%4,dx=8,dy=8,dz=8] actionbar"
                        " {\"text\":\"the lift answered on a floor the stairs do not list\","
                        "\"color\":\"gray\",\"italic\":true}\n"
                        "schedule function %10 3s replace\n")
                        .arg(j).arg(p.x - 4).arg(p.y - 4).arg(p.z - 4)
                        .arg(low.x).arg(low.y).arg(low.z).arg(low.y + 1)
                        .arg(fnp + QStringLiteral("gone")).toUtf8());
                    add(pth + QStringLiteral("gone.mcfunction"), QStringLiteral(
                        "# the cabin stayed where it was: nowhere in this stairwell\n"
                        "playsound minecraft:block.note_block.bass master @a[x=%1,y=%2,z=%3,dx=8,dy=8,dz=8]"
                        " %4 %5 %6 0.4 0.5\n"
                        "scoreboard players set #gl_%7 osmc_enc 0\n")
                        .arg(p.x - 4).arg(p.y - 4).arg(p.z - 4)
                        .arg(low.x).arg(low.y).arg(low.z).arg(j).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #gl_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 0..11999 run scoreboard players set #gl_%1 osmc_enc 0\n")
                        .arg(j).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("long_shadows"))
        {   // the dusk stretches what nobody casts: a dark seam of particles creeping along the
            // ground away from the lamp in three lengths, thin and longer every sweep, gone when
            // the sky is fully dark — the shadow is the only thing that was ever here
            if (lamps.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 12000, 13999, lamps, QStringLiteral("ls/"))
                    + QStringLiteral("# (dusk only: this is an event of the last light, the window above is tight)\n");
                static const int kLen[3] = { 6, 12, 18 };   // the three lengths of the seam
                for (int j = 0; j < lamps.size(); ++j)
                {
                    const Prop& p = lamps[j];
                    const int ax = p.dir & 3;
                    const int ox = (ax == 1) ? 1 : (ax == 3) ? -1 : 0;   // away from the road edge
                    const int oz = (ax == 0) ? -1 : (ax == 2) ? 1 : 0;
                    const QString pth = ev + QStringLiteral("ls/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("ls/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the lamp keeps somebody else's evening\n"
                        "execute if score #ls_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..24] run return 0\n"
                        "scoreboard players set #ls_%1 osmc_enc 1\n"
                        "title @a[x=%2,y=%3,z=%4,distance=..24] actionbar"
                        " {\"text\":\"the dusk stretches a shadow nothing here casts\","
                        "\"color\":\"dark_gray\",\"italic\":true}\n"
                        "schedule function %5 1s replace\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(fnp + QStringLiteral("s1")).toUtf8());
                    for (int s = 1; s <= 3; ++s)
                    {
                        QString sweep = QStringLiteral("# the seam reaches %1 blocks\n").arg(kLen[s - 1]);
                        sweep += QStringLiteral("execute unless score #ls_%1 osmc_enc matches 1 run return 0\n")
                                     .arg(j);
                        for (int b = 1; b <= kLen[s - 1]; ++b)
                            sweep += QStringLiteral("particle minecraft:soul %1 %2 %3 0.12 0.02 0.12 0.0 2\n")
                                         .arg(p.x + ox * b + 0.5, 0, 'f', 1).arg(p.y + 1)
                                         .arg(p.z + oz * b + 0.5, 0, 'f', 1);
                        if (s < 3)
                            sweep += QStringLiteral("schedule function %1 3s replace\n")
                                         .arg(fnp + QStringLiteral("s%1").arg(s + 1));
                        else
                            sweep += QStringLiteral("schedule function %1 5s replace\n")
                                         .arg(fnp + QStringLiteral("gone"));
                        add(pth + QStringLiteral("s%1.mcfunction").arg(s), sweep.toUtf8());
                    }
                    add(pth + QStringLiteral("gone.mcfunction"), QStringLiteral(
                        "# the last light goes; the seam went under the pavement before it\n"
                        "scoreboard players set #ls_%1 osmc_enc 0\n").arg(j).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #ls_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 14000..23999 run scoreboard players set #ls_%1 osmc_enc 0\n")
                        .arg(j).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("rising_street"))
        {   // the street climbs a floor while nobody looks: a slow gravel groan under the asphalt,
            // dust working up through the seams, and the lamp standing a little higher over the
            // road than it did at dusk. The blocks do not move — nothing the walker owns moves
            if (lamps.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 18000, 23999, lamps, QStringLiteral("rs/"));
                for (int j = 0; j < lamps.size(); ++j)
                {
                    const Prop& p = lamps[j];
                    const QString pth = ev + QStringLiteral("rs/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("rs/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the groan starts somewhere under the road\n"
                        "execute if score #rs_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,distance=..24] run return 0\n"
                        "scoreboard players set #rs_%1 osmc_enc 1\n"
                        "title @a[x=%2,y=%3,z=%4,distance=..24] actionbar"
                        " {\"text\":\"the street is climbing a floor nobody will ever see\","
                        "\"color\":\"gray\",\"italic\":true}\n"
                        "schedule function %5 1s replace\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(fnp + QStringLiteral("g1")).toUtf8());
                    for (int s = 1; s <= 3; ++s)
                    {
                        QString groan = QStringLiteral("# the asphalt settles, seam %1\n").arg(s);
                        groan += QStringLiteral("execute unless score #rs_%1 osmc_enc matches 1 run return 0\n")
                                     .arg(j);
                        groan += QStringLiteral("playsound minecraft:block.gravel.fall master"
                                               " @a[x=%1,y=%2,z=%3,distance=..24] %1 %4 %3 0.6 0.35\n")
                                     .arg(p.x).arg(p.y).arg(p.z).arg(p.y - 1);
                        groan += QStringLiteral("particle minecraft:cloud %1 %2 %3 1.2 0.1 1.2 0.02 14\n")
                                     .arg(p.x).arg(p.y + 1).arg(p.z);
                        if (s < 3)
                            groan += QStringLiteral("schedule function %1 4s replace\n")
                                         .arg(fnp + QStringLiteral("g%1").arg(s + 1));
                        else
                            groan += QStringLiteral("schedule function %1 6s replace\n")
                                         .arg(fnp + QStringLiteral("gone"));
                        add(pth + QStringLiteral("g%1.mcfunction").arg(s), groan.toUtf8());
                    }
                    add(pth + QStringLiteral("gone.mcfunction"), QStringLiteral(
                        "# the street stops climbing; it will never admit going up\n"
                        "playsound minecraft:block.deepslate.fall master @a[x=%1,y=%2,z=%3,distance=..24]"
                        " %1 %4 %3 0.5 0.4\n"
                        "scoreboard players set #rs_%5 osmc_enc 0\n")
                        .arg(p.x).arg(p.y).arg(p.z).arg(p.y - 1).arg(j).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #rs_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 0..11999 run scoreboard players set #rs_%1 osmc_enc 0\n")
                        .arg(j).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("extra_flight"))
        {   // one train more than the schedule: the run rumbles through the tunnel without any
            // cart ever being missed — dust from the rails six times and a count nobody finishes
            // twice the same way. The map lists seven flights tonight; the walker heard eight
            if (metros.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 21000, 23999, metros, QStringLiteral("ef/"));
                for (int j = 0; j < metros.size(); ++j)
                {
                    const Prop& p = metros[j];
                    const int n = p.cells.size() - 1;
                    const Cell& mid = p.cells[qMax(1, n / 2)];
                    const QString pth = ev + QStringLiteral("ef/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("ef/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the schedule said seven; somebody signed an eighth\n"
                        "execute if score #ef_%1 osmc_enc matches 1.. run return 0\n"
                        "execute unless entity @a[x=%2,y=%3,z=%4,dx=8,dy=8,dz=8] run return 0\n"
                        "scoreboard players set #ef_%1 osmc_enc 1\n"
                        "title @a[x=%2,y=%3,z=%4,dx=8,dy=8,dz=8] actionbar"
                        " {\"text\":\"the map lists seven flights tonight — you heard eight\","
                        "\"color\":\"dark_aqua\",\"italic\":true}\n"
                        "schedule function %5 2s replace\n")
                        .arg(j).arg(mid.x - 4).arg(mid.y - 4).arg(mid.z - 4)
                        .arg(fnp + QStringLiteral("r1")).toUtf8());
                    for (int s = 1; s <= 6; ++s)
                    {
                        QString rum = QStringLiteral("# the extra flight passes cell %1\n").arg(s);
                        rum += QStringLiteral("execute unless score #ef_%1 osmc_enc matches 1 run return 0\n")
                                   .arg(j);
                        rum += QStringLiteral("particle minecraft:cloud %1 %4 %3 0.9 0.3 0.9 0.02 16\n"
                                              "playsound minecraft:entity.minecart.riding master"
                                              " @a[x=%1,y=%2,z=%3,dx=10,dy=10,dz=10] %1 %4 %3 0.5 0.45\n")
                                   .arg(mid.x).arg(mid.y).arg(mid.z).arg(mid.y + 1);
                        if (s < 6)
                            rum += QStringLiteral("schedule function %1 16t replace\n")
                                       .arg(fnp + QStringLiteral("r%1").arg(s + 1));
                        else
                            rum += QStringLiteral("schedule function %1 4s replace\n")
                                       .arg(fnp + QStringLiteral("gone"));
                        add(pth + QStringLiteral("r%1.mcfunction").arg(s), rum.toUtf8());
                    }
                    add(pth + QStringLiteral("gone.mcfunction"), QStringLiteral(
                        "# the eighth flight leaves the way it came: off the schedule\n"
                        "playsound minecraft:block.metal.pressure_plate.off master"
                        " @a[x=%1,y=%2,z=%3,dx=10,dy=10,dz=10] %1 %4 %3 0.4 0.6\n"
                        "scoreboard players set #ef_%5 osmc_enc 0\n")
                        .arg(mid.x).arg(mid.y).arg(mid.z).arg(mid.y - 1).arg(j).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #ef_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 0..11999 run scoreboard players set #ef_%1 osmc_enc 0\n")
                        .arg(j).toUtf8();
                }
            }
        }
        else if (id == QLatin1String("gas_cylinder"))
        {   // "The Gas Cylinder" (a user request): one window of a tall house hisses for a while (the smell
            // reaches twenty blocks), then the glass becomes a primed cylinder — the window blows out, and
            // the morning glazier gives the frame its window back. The markers are the apartment walls of
            // window_wave: the same glass, the other kind of mischief.
            if (walls.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 14000, 22000, walls, QStringLiteral("gas/"));
                for (int j = 0; j < walls.size(); ++j)
                {
                    const Prop& p = walls[j];
                    const Cell& w = p.cells.at(p.cells.size() / 2);   // a window of the wall's middle row
                    const QString glass = blockName(w.id);
                    const QString pth = ev + QStringLiteral("gas/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("gas/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the stairwell smells of gas: the window hisses for a few seconds\n"
                        "execute unless entity @a[distance=..48] run return 0\n"
                        "playsound minecraft:block.fuse.ambient master @a[distance=..20] %1 %2 %3 1.0 1.4\n"
                        "particle minecraft:mycelium %1 %2 %3 0.3 0.4 0.3 0.02 24\n"
                        "schedule function %4 4s replace\n")
                        .arg(w.x).arg(w.y).arg(w.z).arg(fnp + QStringLiteral("boom")).toUtf8());
                    add(pth + QStringLiteral("boom.mcfunction"), QStringLiteral(
                        "# the cylinder goes off in the window — the blast is allowed to ruin the neighbours,\n"
                        "# this trio is called destructive (TNT needs mobGriefing; without it only the glass falls)\n"
                        "setblock %1 %2 %3 minecraft:air\n"
                        "summon minecraft:tnt %1.5 %2.5 %3.5 {Fuse:5}\n"
                        "playsound minecraft:entity.generic.explode master @a[distance=..64] %1 %2 %3 1.0 0.8\n"
                        "schedule function %4 180s replace\n")
                        .arg(w.x).arg(w.y).arg(w.z).arg(fnp + QStringLiteral("fix")).toUtf8());
                    add(pth + QStringLiteral("fix.mcfunction"), QStringLiteral(
                        "# the morning glazier: the frame gets its window back if the wall itself survived\n"
                        "execute if block %1 %2 %3 minecraft:air run setblock %1 %2 %3 %4\n")
                        .arg(w.x).arg(w.y).arg(w.z).arg(glass).toUtf8());
                }
            }
        }
        else if (id == QLatin1String("call_01"))
        {   // "Call 01" (a user request): an alarm bell and smoke over a private roof, the smell reaches
            // thirty blocks — and in thirty cases out of a hundred the ridge really catches; by morning the
            // firemen have been there (the setblock puts the roof back while the fire did not eat further).
            if (roofs.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 13000, 23000, roofs, QStringLiteral("c01/"));
                for (int j = 0; j < roofs.size(); ++j)
                {
                    const Prop& p = roofs[j];
                    const Cell& r = p.cells.first();
                    const QString ridge = blockState(r.id);
                    const QString pth = ev + QStringLiteral("c01/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("c01/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# first the far bell and the smell of smoke, then the dice of the fire\n"
                        "execute unless entity @a[distance=..64] run return 0\n"
                        "playsound minecraft:block.bell.use master @a[distance=..30] %1 %4 %3 1.0 0.6\n"
                        "particle minecraft:large_smoke %1 %4 %3 1.5 1.0 1.5 0.02 40\n"
                        "schedule function %5 3s replace\n")
                        .arg(r.x).arg(r.y).arg(r.z).arg(r.y + 2).arg(fnp + QStringLiteral("burn")).toUtf8());
                    add(pth + QStringLiteral("burn.mcfunction"), QStringLiteral(
                        "# thirty chances out of a hundred — the rest is a false alarm\n"
                        "execute store result score #c01_r osmc_enc_t run random value 1..100\n"
                        "execute unless score #c01_r osmc_enc_t matches 1..30 run return 0\n"
                        "setblock %1 %2 %3 minecraft:fire\n"
                        "particle minecraft:flame %1 %4 %3 1.2 0.6 1.2 0.04 30\n"
                        "playsound minecraft:block.camp_fire.crackle master @a[distance=..30] %1 %2 %3 1.0 0.8\n"
                        "schedule function %5 120s replace\n")
                        .arg(r.x).arg(r.y).arg(r.z).arg(r.y + 2).arg(fnp + QStringLiteral("fix")).toUtf8());
                    add(pth + QStringLiteral("fix.mcfunction"), QStringLiteral(
                        "# the firemen were quick: the burned-out hollow counts as fire gone too,\n"
                        "# so the ridge goes back either way\n"
                        "execute if block %1 %2 %3 minecraft:fire run setblock %1 %2 %3 %4\n"
                        "execute if block %1 %2 %3 minecraft:air run setblock %1 %2 %3 %4\n")
                        .arg(r.x).arg(r.y).arg(r.z).arg(ridge).toUtf8());
                }
            }
        }
        else if (id == QLatin1String("fire_deer"))
        {   // "The Fire Deer" (a user request): heat and hoofbeats thirty blocks around a clearing of the
            // dense forest — three beats, and the RING of crowns fifty blocks around the clearing lights
            // up at once, the trees inside the ring standing (a user walk-through: the whole disc burned,
            // the deer was nowhere to see). The fire itself walks to the inner trees later, random ticks
            // do that; the pack's foresters put the still-burning crowns back before dawn.
            if (deerG.isEmpty()) body = noPlace;
            else
            {
                body = nearRoll(id, 14000, 23000, deerG, QStringLiteral("deer/"));
                for (int j = 0; j < deerG.size(); ++j)
                {
                    const Prop& p = deerG[j];
                    const QString pth = ev + QStringLiteral("deer/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("deer/%1/").arg(j);
                    const QString hoof = QStringLiteral(
                        "# the hoofbeats come closer: %1 of three\n"
                        "execute unless entity @a[distance=..64] run return 0\n"
                        "playsound minecraft:entity.horse.gallop master @a[distance=..30] %1 %2 %3 1.0 0.5\n"
                        "particle minecraft:flame %1 %2 %3 3 0.3 3 0.02 18\n");
                    QString beat2 = hoof.arg(2);
                    beat2 += QStringLiteral("schedule function %1 2s replace\n")
                                 .arg(fnp + QStringLiteral("ignite"));
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the grass dries up around the clearing — the deer is walking this grove tonight\n"
                        "execute unless entity @a[distance=..64] run return 0\n"
                        "playsound minecraft:entity.horse.gallop master @a[distance=..30] %1 %2 %3 1.0 0.5\n"
                        "particle minecraft:flame %1 %2 %3 2 0.3 2 0.02 12\n"
                        "schedule function %4 2s replace\n")
                        .arg(p.x).arg(p.y).arg(p.z).arg(fnp + QStringLiteral("beat2")).toUtf8());
                    add(pth + QStringLiteral("beat2.mcfunction"),
                        beat2.arg(p.x).arg(p.y).arg(p.z).toUtf8());
                    QString ig = QStringLiteral(
                        "# the grove lights up along its rim; the fire walks into the trees themselves\n"
                        "playsound minecraft:entity.lightning_bolt.thunder master @a[distance=..40] %1 %2 %3 0.4 0.6\n"
                        "playsound minecraft:block.camp_fire.crackle master @a[distance=..30] %1 %2 %3 1.0 0.6\n")
                        .arg(p.x).arg(p.y).arg(p.z);
                    QString fx = QStringLiteral(
                        "# the pack's foresters replant what the fire still holds\n");
                    for (const Cell& c : p.cells)
                    {
                        const QString leaf = blockState(c.id);
                        ig += QStringLiteral("execute if block %1 %2 %3 %4 run setblock %1 %2 %3 minecraft:fire\n")
                                  .arg(c.x).arg(c.y).arg(c.z).arg(leaf);
                        fx += QStringLiteral("execute if block %1 %2 %3 minecraft:fire run setblock %1 %2 %3 %4\n"
                                             "execute if block %1 %2 %3 minecraft:air run setblock %1 %2 %3 %4\n")
                                  .arg(c.x).arg(c.y).arg(c.z).arg(leaf);
                    }
                    ig += QStringLiteral("schedule function %1 240s replace\n").arg(fnp + QStringLiteral("fix"));
                    add(pth + QStringLiteral("ignite.mcfunction"), ig.toUtf8());
                    add(pth + QStringLiteral("fix.mcfunction"), fx.toUtf8());
                }
            }
        }
        else if (id == QLatin1String("bat_swarm"))
        {   // "The Bat Swarm" (a user request): a screech over the head and six to twelve phantoms
            // wheeling above whoever the dice picked — the swarm dives, and it is a horror with
            // a sting, not a death sentence: whoever gets five blocks close bursts into dust a
            // breath later, so some dives bite and others pop a couple of blocks short of the
            // victim. The scheduled quiet scatters the stragglers, sunrise burns what a roof hid;
            // open roofs only — two cells above the witness must be sky for the sky to open
            body = gate(13000, 23999, 2) + QStringLiteral(
                "execute as @a[limit=1,sort=random] at @s run function osmc_enc:ev/bats/go\n");
            QString go = QStringLiteral(
                "# a closed roof above the witness is no sky: the swarm needs two air cells\n"
                "execute unless block ~ ~6 ~ minecraft:air run return 0\n"
                "execute unless block ~ ~12 ~ minecraft:air run return 0\n"
                "execute store result score #bat_n osmc_enc_t run random value 6..12\n"
                "playsound minecraft:entity.bat.takeoff master @a[distance=..40] ~ ~6 ~ 1.0 0.7\n"
                "playsound minecraft:entity.phantom.ambient master @a[distance=..40] ~ ~14 ~ 0.5 1.3\n");
            static const int kPerch[12][3] = {      // the wheel of the swarm, some fifteen up
                {-3, 16, -2}, {2, 18, 1}, {-1, 15, 3}, {4, 17, -3},
                {-4, 19, 2}, {1, 14, -4}, {3, 16, 4}, {-2, 20, -1},
                {0, 17, 2}, {-5, 15, -3}, {4, 19, -2}, {-1, 18, 4},
            };
            for (int k = 0; k < 12; ++k)            // the k-th bat joins at a roll of k+1 or higher
                go += QStringLiteral("execute if score #bat_n osmc_enc_t matches %1.. "
                                     "run summon minecraft:phantom ~%2 ~%3 ~%4\n")
                          .arg(k + 1).arg(kPerch[k][0]).arg(kPerch[k][1]).arg(kPerch[k][2]);
            // 26.x ignores the Tags NBT of a summon, so the swarm is tagged right after it is
            // born, from the witness's own cell (docs/.agents knowledge)
            go += QStringLiteral(
                "tag @e[type=minecraft:phantom,distance=..26] add osmc_bats\n"
                "schedule function osmc_enc:ev/bats/quiet 240s replace\n");
            add(ev + QStringLiteral("bats/go.mcfunction"), go.toUtf8());
            add(ev + QStringLiteral("bats/strike.mcfunction"), QByteArray(
                "# a breath near the witness is the last breath of the phantom (the user asked\n"
                "# for a scare that can sting, not for a doomed swarm)\n"
                "execute as @e[tag=osmc_bats] at @s if entity @a[distance=..5] run"
                " scoreboard players add @s osmc_bat 1\n"
                "execute at @e[tag=osmc_bats,scores={osmc_bat=14..}] run"
                " particle minecraft:poof ~ ~ ~ 0.5 0.5 0.5 0.15 24\n"
                "execute at @e[tag=osmc_bats,scores={osmc_bat=14..}] run"
                " playsound minecraft:entity.phantom.death master @a[distance=..24] ~ ~ ~ 1.0 1.5\n"
                "kill @e[tag=osmc_bats,scores={osmc_bat=14..}]\n"));
            add(ev + QStringLiteral("bats/quiet.mcfunction"), QByteArray(
                "# what no dive spent scatters into the dark\n"
                "execute at @e[tag=osmc_bats] run particle minecraft:poof ~ ~ ~ 0.5 0.5 0.5 0.15 18\n"
                "kill @e[tag=osmc_bats]\n"));
            tickBats = QStringLiteral(
                "execute if entity @e[tag=osmc_bats] run function osmc_enc:ev/bats/strike\n");
        }
        else
            body = QStringLiteral("# the event logic is coming (docs/city-encounters.md)\n");
        add(fn + QStringLiteral("event/%1.mcfunction").arg(id), body.toUtf8());
    }

    add(fn + QStringLiteral("load.mcfunction"), load.toUtf8());
    add(fn + QStringLiteral("tick.mcfunction"),
            QByteArray("scoreboard players add #tick osmc_enc_t 1\n"
                       "execute if score #tick osmc_enc_t matches 200.. run function osmc_enc:cycle\n"
                       "execute if score #tick osmc_enc_t matches 200.. run scoreboard players set #tick osmc_enc_t 0\n"
                       "scoreboard players add #tsync osmc_enc_t 1\n"
                       "execute if score #tsync osmc_enc_t matches 20.. run function osmc_enc:time/sync\n"
                       "execute if score #tsync osmc_enc_t matches 20.. run scoreboard players set #tsync osmc_enc_t 0\n")
            + tickGlass.toUtf8() + tickKill.toUtf8() + tickFob.toUtf8() + tickJack.toUtf8() + tickBats.toUtf8()
            + tickScare.toUtf8());
    add(fn + QStringLiteral("cycle.mcfunction"), cycle.toUtf8());
    add(fn + QStringLiteral("time/sync.mcfunction"),
        QByteArray("# the game clock as the pack sees it, one call every 20 ticks: /time commands\n"
                   "# move the day timeline, and the timeline is what the pack follows\n")
            + clock.toUtf8());
    // the void dimension type shared by the fog park and the mirrored floor: written once, an
    // unused one changes nothing in the game (the fields the target formats do not know are ignored)
    add(QStringLiteral("datapacks/osmc_enc/data/osmc_enc/dimension_type/fog.json"), QByteArray(
        "{\"logical_height\":64,\"min_y\":-64,\"height\":64,\"effects\":\"minecraft:overworld\","
        "\"fixed_time\":13500,"
        "\"has_skylight\":true,\"has_ceiling\":false,\"ambient_light\":0.1,\"natural\":false,"
        "\"bed_works\":false,\"respawn_anchor_works\":false,\"piglin_safe\":false,\"has_raids\":false,"
        // 26.x made these REQUIRED of every dimension_type (1.21.x takes them as the same defaults):
        // no dragon, no scaling, the overworld's own fire line; the void biome has no spawn entries
        // and the two light limits are 0 on top of that - the fog never bites
        "\"coordinate_scale\":1.0,\"has_ender_dragon_fight\":false,"
        "\"infiniburn\":\"#minecraft:infiniburn_overworld\","
        "\"monster_spawn_light_level\":0,\"monster_spawn_block_light_limit\":0}"));
    add(QStringLiteral("datapacks/osmc_enc/data/minecraft/tags/function/load.json"),
             QByteArray("{\"values\":[\"osmc_enc:load\"]}"));
    add(QStringLiteral("datapacks/osmc_enc/data/minecraft/tags/function/tick.json"),
             QByteArray("{\"values\":[\"osmc_enc:tick\"]}"));
    add(QStringLiteral("datapacks/osmc_enc/pack.mcmeta"), legacy121
        // every key of both naming styles (the game itself writes its overlays this way since 1.21,
        // see the tmc_tall pack): 1.21.x reads "supported_formats", 26.x reads "min/max_format"
        ? QByteArray("{\"pack\":{\"description\":\"OSMC: random city events over the exported places\","
                     "\"pack_format\":48,\"supported_formats\":{\"min_inclusive\":48,\"max_inclusive\":100},"
                     "\"min_format\":[48,0],\"max_format\":[100,2147483647]}}")
        : QByteArray("{\"pack\":{\"description\":\"OSMC: random city events over the exported places\","
                     "\"pack_format\":101,\"supported_formats\":{\"min_inclusive\":101,\"max_inclusive\":121},"
                     "\"min_format\":[101,0],\"max_format\":[121,2147483647]}}"));
}
