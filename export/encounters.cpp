#include "export/encounters.h"
#include "export/blocks.h"
#include <cstdlib>
#include <cstring>

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

// The events whose datapack logic shipped; the "City events" window marks the rest "[IN PROGRESS]".
// The list grows release by release (docs/city-encounters.md, docs/encounters-feasibility.md).
bool encImplemented(const char* id)
{
    static const char* const kDone[] = {
        "carousel", "blinking_lamp", "local_rain", "chalk_art", "swings", "pumpkin_faces", "cat_council",
        "flasher", "window_wave", "matron", "scarecrow", "fresh_grave", "clock_0307",
        "taxi", "lurker", "bus13", "dry_fountain", "last_train", "bench_otherworld",
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

namespace
{
//! The gate every baked event starts with: the night window, then the roll of 1..100 against the
//! chance. Each winning number dispatches to one baked place through rollLines().
QString gate(int lo, int hi, int chance)
{
    return QStringLiteral("# the roll of the event: the places are baked into the ev/ functions\n"
                          "execute unless score #day osmc_enc_t matches %1 run return 0\n"
                          "execute store result score #enc_r osmc_enc_t run random value 1..100\n"
                          "execute unless score #enc_r osmc_enc_t matches 1..%2 run return 0\n")
           .arg(QStringLiteral("%1..%2").arg(lo).arg(hi)).arg(chance);
}

//! The dispatch of the rolled number to one of n baked places ("<fnPrefix><i>/go").
QString rollLines(int chance, int n, const QString& fnPrefix)
{
    QString s;
    for (int r = 1; r <= chance; ++r)
        s += QStringLiteral("execute if score #enc_r osmc_enc_t matches %1 run function %2\n")
                 .arg(r).arg(fnPrefix + QStringLiteral("%1/go").arg((r - 1) % n));
    return s;
}
}   // namespace

// The pack generator. Everything a baked event needs (coordinates, counters, schedules) is written
// into literal mcfunction lines here — the runtime pack carries no macros and reads no data files,
// so the same functions behave identically in 1.21.x and 26.x (the formats min_format 82 covers) -
// except where 26.x retired the mechanic an event is built on, see the legacy121 branches below.
void EncPack::build(const AddFn& add, const QSet<QString>& off, const QVector<Prop>& props,
                    bool legacy121)
{
    // The place lists per kind with caps: an event spread over the whole city stops being LOCAL, and
    // the caps bound the size of the generated pack. The vector arrives in the stable order of the
    // build stages (the chalk candidates are pre-sorted by coordinates), so the pack is reproducible.
    QVector<Prop> byKind[17];
    static const int kCap[17] = { 40, 24, 24, 24, 96, 8, 6, 8, 8, 4, 6, 6, 4, 8, 3, 6, 2 };
    for (const Prop& p : props)
        if (p.kind >= 0 && p.kind < 17 && byKind[p.kind].size() < kCap[p.kind])
            byKind[p.kind].append(p);
    // props whose generator would have nothing to bake without their side data are dropped here, so
    // the roll/dispatch below always addresses a place that really has functions
    auto needCells = [](QVector<Prop>& v, int atLeast)
    {   for (int i = v.size() - 1; i >= 0; --i) if (v[i].cells.size() < atLeast) v.remove(i); };
    needCells(byKind[kEpWindows], 1);
    needCells(byKind[kEpGrave], 1);
    needCells(byKind[kEpLamp], 1);                                        // a lamp with no light cell cannot blink
    needCells(byKind[kEpSchool], 3);
    needCells(byKind[kEpCar], 2);
    needCells(byKind[kEpLurker], 4);
    needCells(byKind[kEpFountain], 17);                                   // the bowl drains whole or not at all
    needCells(byKind[kEpMetro], 12);                        // the catch cell plus an eleven-cell route
    needCells(byKind[kEpBench], 1);                          // the bench without its trigger cell is mute
    needCells(byKind[kEpHalf], 2);            // the door is a pair of lining cells, both must be real
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

    const QString fn = QStringLiteral("datapacks/osmc_enc/data/osmc_enc/function/");
    const QString ev = fn + QStringLiteral("ev/");           // zip paths for addEntry
    const QString fp = QStringLiteral("osmc_enc:ev/");       // function-name prefix inside commands

    // The dispatcher (as in the first release): the cycle runs every 200 ticks and shares the daytime
    // score with the events; the on/off functions let the player retune the world at run time.
    QString load = QStringLiteral("# generated by OSMC: random city events (docs/city-encounters.md)\n"
                                  "scoreboard objectives add osmc_enc dummy\n"
                                  "scoreboard objectives add osmc_enc_t dummy\n"
                                  "scoreboard players set #ca_n osmc_enc_t 0\n"
                                  "scoreboard players set #k24000 osmc_enc_t 24000\n");
    QString cycle = QStringLiteral("# one cycle every 200 ticks; each enabled event decides inside whether it comes now\n");
    if (legacy121)
    {
        cycle += QStringLiteral("execute store result score #day osmc_enc_t run time query daytime\n");
    }
    else
    {   // 26.x retired "time query daytime" with the clock/timeline rework (the day is a 24000-tick
        // timeline on the world clock now); gametime is still the whole tick counter and the clock
        // runs with it, so the old 0..23999 daytime score is the remainder. The % constant is set in load.
        cycle += QStringLiteral("execute store result score #day osmc_enc_t run time query gametime\n"
                                "scoreboard players operation #day osmc_enc_t %= #k24000 osmc_enc_t\n");
    }

    const QString noPlace =
        QStringLiteral("# no places of this kind were found in the exported world\nreturn 0\n");

    for (int i = 0; i < kEncCount; ++i)
    {
        const QString id = QLatin1String(kEncIds[i]);
        load += QStringLiteral("scoreboard players set #%1 osmc_enc %2\n").arg(id).arg(off.contains(id) ? 0 : 1);
        cycle += QStringLiteral("execute if score #%1 osmc_enc matches 1 run function osmc_enc:event/%1\n").arg(id);
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
                body = gate(13000, 23999, 8) + rollLines(8, lamps.size(), fp + QStringLiteral("blink/"));
                for (int j = 0; j < lamps.size(); ++j)
                {
                    const Prop& p = lamps[j];
                    const QString dark = p.dir == kLightMast ? QStringLiteral("minecraft:polished_calcite")
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
                body = gate(0, 23999, 8) + rollLines(8, lamps.size(), fp + QStringLiteral("rain/"));
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
                body = gate(13000, 23999, 40) + rollLines(40, chalks.size(), fp + QStringLiteral("chalk/"));
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
                body = gate(13000, 23999, 25) + rollLines(25, plays.size(), fp + QStringLiteral("swing/"));
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
        {   // a cemetery jack turns its carved face to whoever stands near — but never while too close
            if (jacks.isEmpty()) body = noPlace;
            else
            {
                body = gate(18000, 23999, 60) + rollLines(60, jacks.size(), fp + QStringLiteral("jack/"));
                static const char* const kFace[4] = { "north", "east", "south", "west" };
                for (int j = 0; j < jacks.size(); ++j)
                {
                    const Prop& p = jacks[j];
                    QString go = QStringLiteral(
                        "# the face turns to the far witness standing in front of it; up close the jack\n"
                        "# stays as it was — the player is the one who should step back\n"
                        "execute unless entity @a[x=%1,y=%2,z=%3,distance=..48] run return 0\n"
                        "execute if entity @a[x=%1,y=%2,z=%3,distance=..14] run return 0\n")
                        .arg(p.x).arg(p.y).arg(p.z);
                    for (int f = 0; f < 4; ++f)
                    {   // the witness box 16..48 blocks in front of that face (32 wide, the kFace order)
                        const int wx = (f == 0 || f == 2) ? p.x - 16 : (f == 1 ? p.x + 16 : p.x - 48);
                        const int wz = (f == 1 || f == 3) ? p.z - 16 : (f == 0 ? p.z - 48 : p.z + 16);
                        go += QStringLiteral("execute if entity @a[x=%1,y=%2,z=%3,dx=32,dy=32,dz=32] "
                                             "run setblock %4 %5 %6 minecraft:jack_o_lantern[facing=%7]\n")
                                 .arg(wx).arg(p.y - 8).arg(wz)
                                 .arg(p.x).arg(p.y).arg(p.z).arg(QLatin1String(kFace[f]));
                    }
                    go += QStringLiteral(
                        "playsound minecraft:block.pumpkin.carve master @a[x=%1,y=%2,z=%3,distance=..32] "
                        "%1 %2 %3 0.5 0.6\n")
                        .arg(p.x).arg(p.y).arg(p.z);
                    add(ev + QStringLiteral("jack/%1/go.mcfunction").arg(j), go.toUtf8());
                }
            }
        }
        else if (id == QLatin1String("cat_council"))
        {   // cats gather sitting at a baked bus stop node; they scatter at an approaching person and
            // the council goes home at dawn
            if (stops.isEmpty()) body = noPlace;
            else
            {
                body = gate(13000, 23999, 35) + rollLines(35, stops.size(), fp + QStringLiteral("cat/"));
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
                body = gate(12000, 23999, 10) + rollLines(10, crosses.size(), fp + QStringLiteral("flash/"));
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
                        "setblock %1 %5 %3 minecraft:polished_calcite\n"
                        "scoreboard players set #fl_on osmc_enc 1\n"
                        "scoreboard players set #flt osmc_enc_t 300\n"
                        "scoreboard players set #fl_h osmc_enc_t 0\n"
                        "playsound minecraft:block.redstone_torch.on master @a[x=%1,y=%2,z=%3,distance=..40] %1 %2 %3 0.7 0.8\n"
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
                        "execute if entity @a[x=%1,y=%2,z=%3,distance=..10] if block %1 %2 %3 minecraft:sea_lantern run setblock %1 %2 %3 minecraft:polished_calcite\n"
                        "execute if entity @a[x=%1,y=%2,z=%3,distance=..10] if score #fl_h osmc_enc_t matches 0 run playsound minecraft:block.beacon.ambient master @a[x=%1,y=%2,z=%3,distance=..16] %1 %2 %3 0.8 0.6\n"
                        "execute unless entity @a[x=%1,y=%2,z=%3,distance=..10] if block %1 %2 %3 minecraft:polished_calcite run setblock %1 %2 %3 minecraft:sea_lantern\n"
                        "execute unless entity @a[x=%1,y=%2,z=%3,distance=..10] if block %1 %2 %3 minecraft:sea_lantern run setblock %1 %2 %3 minecraft:polished_calcite\n"
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
                body = gate(13000, 23999, 8) + rollLines(8, walls.size(), fp + QStringLiteral("win/"));
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
                        "playsound minecraft:block.redstone_torch.buzzoff master @a[x=%1,y=%2,z=%3,distance=..64] %1 %2 %3 0.8 1.2\n"
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
                body = gate(13000, 23999, 10) + rollLines(10, schools.size(), fp + QStringLiteral("mat/"));
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
                            "playsound minecraft:block.beacon.power master @a[x=%1,y=%2,z=%3,distance=..48] %1 %2 %3 0.5 1.4\n")
                            .arg(p.x).arg(p.y).arg(p.z);
                        dk += QStringLiteral(
                            "playsound minecraft:block.redstone_torch.buzzoff master @a[x=%1,y=%2,z=%3,distance=..48] %1 %2 %3 0.5 1.4\n")
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
            if (scares.isEmpty()) body = noPlace;
            else
            {
                body = gate(13000, 23999, 18) + rollLines(18, scares.size(), fp + QStringLiteral("sc/"));
                static const double kYaw[4] = { 180, -90, 0, 90 };    // the kDirNames order N/E/S/W
                QString watch = QStringLiteral(
                    "# the flip itself runs from the cycle while any scarecrow stands\n");
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
                    QString w = QStringLiteral(
                        "# night: face to the house, carved face, the board's text; day: it looks away\n"
                        "execute unless entity @e[type=minecraft:armor_stand,tag=osmc_sc_%1] run return 0\n")
                        .arg(j);
                    const QString sel = QStringLiteral("@e[type=minecraft:armor_stand,tag=osmc_sc_%1,limit=1]").arg(j);
                    // the pumpkin head is swapped twice over: "ArmorItems" is the field of 1.21.x, "item
                    // replace" is the command both formats read (in 26.x the data modify simply misses)
                    w += QStringLiteral(
                        "execute if score #day osmc_enc_t matches 13000..23999 run data modify entity %1 Rotation set value [%2f,0f]\n"
                        "execute if score #day osmc_enc_t matches 13000..23999 run data modify entity %1 ArmorItems[3].id set value \"minecraft:carved_pumpkin\"\n"
                        "execute if score #day osmc_enc_t matches 13000..23999 run item replace entity %1 armor.head with minecraft:carved_pumpkin\n"
                        "execute if score #day osmc_enc_t matches 13000..23999 run data modify entity %1 CustomName set value \"leave by morning\"\n"
                        "execute if score #day osmc_enc_t matches 0..11999 run data modify entity %1 Rotation set value [%3f,0f]\n"
                        "execute if score #day osmc_enc_t matches 0..11999 run data modify entity %1 ArmorItems[3].id set value \"minecraft:pumpkin\"\n"
                        "execute if score #day osmc_enc_t matches 0..11999 run item replace entity %1 armor.head with minecraft:pumpkin\n"
                        "execute if score #day osmc_enc_t matches 0..11999 run data remove entity %1 CustomName\n")
                        .arg(sel).arg(nightYaw, 0, 'f', 0).arg(dayYaw, 0, 'f', 0);
                    add(ev + QStringLiteral("sc/%1/watch.mcfunction").arg(j), w.toUtf8());
                    watch += QStringLiteral(
                        "execute if entity @e[type=minecraft:armor_stand,tag=osmc_sc_%1] run function %2\n")
                        .arg(j).arg(fp + QStringLiteral("sc/%1/watch").arg(j));
                }
                add(ev + QStringLiteral("sc/watch.mcfunction"), watch.toUtf8());
                cycle += QStringLiteral(
                    "execute if entity @e[type=minecraft:armor_stand,tag=osmc_sc] run function %1\n")
                    .arg(fp + QStringLiteral("sc/watch"));
            }
        }
        else if (id == QLatin1String("fresh_grave"))
        {   // a grave of loose soil appears; digging it is undone by itself (in the 1.21.x format, below)
            if (graves.isEmpty()) body = noPlace;
            else
            {
                body = gate(13000, 23999, 10) + rollLines(10, graves.size(), fp + QStringLiteral("grave/"));
                // 26.x deleted the block-breaking trigger outright (its whole trigger registry was
                // dumped and checked): the dig cannot be detected there, so the mound simply stays
                // dug in those worlds and the restore path is emitted for the legacy format only
                QString back;
                const bool digs = legacy121;
                if (digs) back = QStringLiteral(
                    "# somebody dug: the soil gives the grave back, one revoke so this runs once\n"
                    "advancement revoke @s only osmc_enc:dug_dirt\n");
                for (int j = 0; j < graves.size(); ++j)
                {
                    const Prop& p = graves[j];
                    const QString orig = blockName(p.cells.first().id);   // what the mound covers
                    add(ev + QStringLiteral("grave/%1/go.mcfunction").arg(j), QStringLiteral(
                        "# the fresh mound: coarse soil and a hump of rooted dirt on top\n"
                        "execute if block %1 %2 %3 minecraft:coarse_dirt run return 0\n"
                        "execute unless entity @a[x=%1,y=%2,z=%3,distance=..48] run return 0\n"
                        "setblock %1 %2 %3 minecraft:coarse_dirt\n"
                        "setblock %1 %4 %3 minecraft:rooted_dirt\n"
                        "particle minecraft:soul %1.5 %4.5 %3.5 0.4 0.8 0.4 0.06 80\n"
                        "playsound minecraft:block.gravel.fall master @a[x=%1,y=%2,z=%3,distance=..24] %1.5 %4 %3.5 0.6 0.55\n")
                        .arg(p.x).arg(p.y).arg(p.z).arg(p.y + 1).toUtf8());
                    if (digs) back += QStringLiteral(
                        "execute if block %1 %2 %3 minecraft:coarse_dirt run playsound minecraft:block.grass.break master @a[x=%1,y=%2,z=%3,distance=..32] %1 %2 %3 0.8 0.5\n"
                        "execute if block %1 %2 %3 minecraft:coarse_dirt run setblock %1 %2 %3 %4\n"
                        "execute if block %1 %5 %3 minecraft:rooted_dirt run setblock %1 %5 %3 minecraft:air\n")
                        .arg(p.x).arg(p.y).arg(p.z).arg(orig).arg(p.y + 1);
                }
                if (digs)
                {   // the 1.21.x advancement is the only sensor for the digging; 26.x has none,
                    // so it emits no files and no tick line (a dead predicate would risk a
                    // per-tick evaluation error - cleaner to not ask the question at all)
                    add(ev + QStringLiteral("grave/back.mcfunction"), back.toUtf8());
                    cycle += QStringLiteral(
                        "execute as @a[predicate=osmc_enc:dug_dirt] run function %1\n")
                        .arg(fp + QStringLiteral("grave/back"));
                    add(QStringLiteral("datapacks/osmc_enc/data/osmc_enc/advancement/dug_dirt.json"),
                             QByteArray("{\"criteria\":{\"dug\":{\"trigger\":\"minecraft:destroyed_block\","
                                        "\"conditions\":{\"item\":{\"items\":[\"minecraft:grass_block\",\"minecraft:dirt\","
                                        "\"minecraft:coarse_dirt\",\"minecraft:rooted_dirt\",\"minecraft:dirt_path\"]}}}},"
                                        "\"rewards\":{}}"));
                    add(QStringLiteral("datapacks/osmc_enc/data/osmc_enc/predicate/dug_dirt.json"),
                             QByteArray("{\"player\":{\"advancements\":{\"osmc_enc:dug_dirt\":true}}}"));
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
                body = gate(13000, 23999, 10) + rollLines(10, cars.size(), fp + QStringLiteral("taxi/"));
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
                body = gate(13000, 23999, 10) + rollLines(10, lurks.size(), fp + QStringLiteral("lurk/"));
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
                        "particle minecraft:dust 0.02 0.02 0.04 1.5 ~ ~1.5 ~ 0.5 1.5 0.5 0.02 30\n"
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
                body = gate(13000, 23999, 12) + rollLines(12, stops.size(), fp + QStringLiteral("bus/"));
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
                            "effect give @e[type=minecraft:armor_stand,tag=osmc_b13_%4] minecraft:glowing 2s 0 true\n")
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
                        "playsound minecraft:block.metal_pressure_plate.click master @a[x=%1,y=%2,z=%3,distance=..48] %1 %2 %3 0.8 0.5\n"
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
                body = gate(13000, 23999, 10) + rollLines(10, founts.size(), fp + QStringLiteral("fount/"));
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
                body = gate(23000, 23999, 10) + rollLines(10, metros.size(), fp + QStringLiteral("lt/"));
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
        {   // the bench not for everyone: sit long on a bench the fog chose tonight and the city
            // keeps its sound — you are sitting in the fog park of your own void dimension already;
            // stand up from its bench and the city takes you back (docs/city-encounters.md)
            if (benches.isEmpty()) body = noPlace;
            else
            {
                body = gate(13000, 23999, 10) + rollLines(10, benches.size(), fp + QStringLiteral("fob/"));
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
                    "execute in osmc_enc:fogpark run fill -6 -61 -6 -3 -61 -3 minecraft:clay\n"
                    "execute in osmc_enc:fogpark run fill -6 -60 -6 -3 -60 -3 minecraft:water\n"
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
                                       "schedule function osmc_enc:ev/fob/build 5s replace\n");
                // the ride in and the ride back; both move players across the dimension border the
                // way the map makers do: a tag in one context, the tp in the other
                add(ev + QStringLiteral("fob/enter.mcfunction"), QByteArray(
                    "# the sitting one sinks into the park; the city keeps the bench and every sound\n"
                    "tag @a[distance=..2] add osmc_fog\n"
                    "execute in osmc_enc:fogpark positioned 0.5 -60 0.5 run tp @a[tag=osmc_fog] ~ ~ ~\n"
                    "execute in osmc_enc:fogpark positioned 0.5 -60 0.5 run particle minecraft:moisture"
                    " 0.5 -59 0.5 2 1 2 0.03 60\n"
                    "execute in osmc_enc:fogpark positioned 0.5 -60 0.5 run playsound minecraft:ambient.cave"
                    " master @a[tag=osmc_fog] ~ ~ ~ 0.5 0.7\n"
                    "execute in osmc_enc:fogpark positioned 0.5 -60 0.5 run title @a[tag=osmc_fog] actionbar"
                    " {\"text\":\"the city fell silent - this park is on no map\",\"color\":\"gray\",\"italic\":true}\n"
                    "tag @a[tag=osmc_fog] remove osmc_fog\n"
                    "scoreboard players set #fob_h osmc_enc_t 0\n"));
                for (int j = 0; j < benches.size(); ++j)
                {
                    const Prop& p = benches[j];
                    const Cell& t = p.cells.first();     // the standing cell before the seat
                    const QString pth = ev + QStringLiteral("fob/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the fog chooses this bench for tonight (#fob_%1 watches who sits)\n"
                        "execute if score #fob_%1 osmc_enc matches 1.. run return 0\n"
                        "scoreboard players set #fob_%1 osmc_enc 1\n"
                        "particle minecraft:cloud %2 %3 %4 1 0.5 1 0.02 30\n")
                        .arg(j).arg(t.x).arg(t.y + 1).arg(t.z).toUtf8());
                    add(pth + QStringLiteral("leave.mcfunction"), QStringLiteral(
                        "# up from the park bench - the city takes this person back to the seat\n"
                        "tag @a[distance=..1.5] add osmc_back\n"
                        "execute if entity @a[tag=osmc_back] in minecraft:overworld positioned %1 %2 %3"
                        " run tp @a[tag=osmc_back] ~ ~ ~\n"
                        "execute if entity @a[tag=osmc_back] in minecraft:overworld positioned %1 %2 %3"
                        " run playsound minecraft:block.grass.break master @a[distance=..16]"
                        " ~ ~ ~ 1.0 0.6\n"
                        "execute if entity @a[tag=osmc_back] in minecraft:overworld positioned %1 %2 %3"
                        " run title @a[tag=osmc_back] actionbar"
                        " {\"text\":\"the traffic came back - the bench here is cold, as it should be\","
                        "\"color\":\"gray\",\"italic\":true}\n"
                        "tag @a[tag=osmc_back] remove osmc_back\n")
                        .arg(t.x).arg(t.y).arg(t.z).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #fob_%1 osmc_enc matches 1 in minecraft:overworld positioned %2 %3 %4"
                        " if entity @a[distance=..1.1] run scoreboard players add #fob_h osmc_enc_t 1\n"
                        "execute if score #fob_%1 osmc_enc matches 1 in minecraft:overworld positioned %2 %3 %4"
                        " unless entity @a[distance=..1.1] run scoreboard players set #fob_h osmc_enc_t 0\n"
                        "execute if score #fob_%1 osmc_enc matches 1 if score #day osmc_enc_t"
                        " matches 0..11999 run scoreboard players set #fob_%1 osmc_enc 0\n"
                        "execute in osmc_enc:fogpark positioned 0.5 -60 -1.5 run function %5\n")
                        .arg(j).arg(t.x).arg(t.y).arg(t.z)
                        .arg(fp + QStringLiteral("fob/%1/leave").arg(j)).toUtf8();
                }
                cycle += QStringLiteral(
                    "execute in minecraft:overworld run function osmc_enc:ev/fob/dispatch\n"
                    "execute in osmc_enc:fogpark positioned 0.5 -60 0.5 if entity @a[distance=..40]"
                    " run particle minecraft:moisture 0.5 -59 0.5 9 2 9 0.03 80\n");
                // the ride in: the per-bench watch above fills #fob_h while somebody stands; the
                // dispatch rides at the cell of the chosen bench that really has a sitter now
                QString disp = QStringLiteral(
                    "# the ride in: whoever stood at a chosen bench for two cycles sits in the park\n"
                    "execute unless score #fob_h osmc_enc_t matches 2.. run return 0\n");
                for (int j = 0; j < benches.size(); ++j)
                {
                    const Cell& t = benches[j].cells.first();
                    disp += QStringLiteral(
                        "execute if score #fob_%1 osmc_enc matches 1 in minecraft:overworld positioned %2 %3 %4"
                        " if entity @a[distance=..2] run function osmc_enc:ev/fob/enter\n")
                        .arg(j).arg(t.x).arg(t.y).arg(t.z);
                }
                disp += QStringLiteral("scoreboard players set #fob_h osmc_enc_t 0\n");
                add(ev + QStringLiteral("fob/dispatch.mcfunction"), disp.toUtf8());
            }
        }
        else if (id == QLatin1String("floor_half"))
        {   // the floor between the floors: after midnight the stairwell of a tall house grows a
            // door that is on no plan of the house; whoever opens it from the flight is standing on
            // the same landing mirrored - windows into the void, doors onto nothing - and any door
            // opened there brings the guest back to their own flight (docs/city-encounters.md)
            if (halfs.isEmpty()) body = noPlace;
            else
            {
                body = gate(13000, 23999, 8) + rollLines(8, halfs.size(), fp + QStringLiteral("hf/"));
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
                load += QStringLiteral("scoreboard objectives add osmc_enc_p dummy\n"
                                       "schedule function osmc_enc:ev/hf/build 6s replace\n");
                static const char* const kHFDoor[4][3] = {
                    { "-6", "-40", "-1" }, { "-6", "-40", "1" }, { "6", "-40", "-2" }, { "6", "-40", "2" } };
                static const char* const kFacing[4] = { "north", "east", "south", "west" };
                static const int kFaceX[4] = { 0, 1, 0, -1 }, kFaceZ[4] = { -1, 0, 1, 0 };
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
                        "execute in osmc_enc:half run tp @a[tag=osmc_half] 0.5 -40 0.5\n"
                        "execute in osmc_enc:half positioned 0.5 -40 0.5 run particle minecraft:portal"
                        " 0.5 -39 0.5 1.5 1 1.5 0.05 80\n"
                        "execute in osmc_enc:half positioned 0.5 -40 0.5 run playsound minecraft:block.beacon.deactivate"
                        " master @a[tag=osmc_half] ~ ~ ~ 0.4 0.5\n"
                        "execute in osmc_enc:half positioned 0.5 -40 0.5 run title @a[tag=osmc_half] actionbar"
                        " {\"text\":\"the landing is here, the city outside it is not\","
                        "\"color\":\"gray\",\"italic\":true}\n")
                        .arg(j).toUtf8());
                    add(pth + QStringLiteral("back.mcfunction"), QStringLiteral(
                        "# through any door of that floor - and the guest is back on their own flight\n"
                        "execute in minecraft:overworld run tp @a[scores={osmc_enc_p=%1},tag=osmc_half] %2 %3 %4\n"
                        "execute if entity @a[x=%2,y=%3,z=%4,distance=..4,scores={osmc_enc_p=%1}]"
                        " in minecraft:overworld run playsound minecraft:block.grass.break"
                        " master @a[x=%2,y=%3,z=%4,distance=..16] %2 %3 %4 1.0 0.6\n"
                        "execute if entity @a[x=%2,y=%3,z=%4,distance=..4,scores={osmc_enc_p=%1}]"
                        " in minecraft:overworld run title @a[x=%2,y=%3,z=%4,distance=..8] actionbar"
                        " {\"text\":\"the stairwell took its guest back; the wall is a wall again\","
                        "\"color\":\"gray\",\"italic\":true}\n"
                        "scoreboard players reset @a[scores={osmc_enc_p=%1},tag=osmc_half] osmc_enc_p\n"
                        "tag @a[scores={osmc_enc_p=%1},tag=osmc_half] remove osmc_half\n")
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
                }
                // the way out of the mirrored floor is any of its doors (they open onto nothing,
                // and behind them the guest's own stairwell is already waiting)
                QString exits = QStringLiteral(
                    "# the door the guest picked on that floor is the way back\n");
                for (int j = 0; j < halfs.size(); ++j)
                    exits += QStringLiteral("execute if entity @a[scores={osmc_enc_p=%1},tag=osmc_half]"
                                            " run function %2\n")
                                 .arg(j).arg(fp + QStringLiteral("hf/%1/back").arg(j));
                add(ev + QStringLiteral("hf/exit.mcfunction"), exits.toUtf8());
                for (int d = 0; d < 4; ++d)
                    cycle += QStringLiteral(
                        "execute in osmc_enc:half positioned %1 %2 %3"
                        " if block %1 %2 %3 minecraft:oak_door[half=lower,open=true]"
                        " if entity @a[distance=..2.5,scores={osmc_enc_p=0..}]"
                        " run function osmc_enc:ev/hf/exit\n")
                        .arg(QLatin1String(kHFDoor[d][0])).arg(QLatin1String(kHFDoor[d][1]))
                        .arg(QLatin1String(kHFDoor[d][2]));
            }
        }
        else
            body = QStringLiteral("# the event logic is coming (docs/city-encounters.md)\n");
        add(fn + QStringLiteral("event/%1.mcfunction").arg(id), body.toUtf8());
    }

    add(fn + QStringLiteral("load.mcfunction"), load.toUtf8());
    add(fn + QStringLiteral("tick.mcfunction"),
            QByteArray("scoreboard players add #tick osmc_enc_t 1\n"
                       "execute if score #tick osmc_enc_t matches 200.. run function osmc_enc:cycle\n"
                       "execute if score #tick osmc_enc_t matches 200.. run scoreboard players set #tick osmc_enc_t 0\n"));
    add(fn + QStringLiteral("cycle.mcfunction"), cycle.toUtf8());
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
    add(QStringLiteral("datapacks/osmc_enc/pack.mcmeta"),
             QByteArray("{\"pack\":{\"description\":\"OSMC: random city events over the exported places\",\"min_format\":82,\"max_format\":121}}"));
}
