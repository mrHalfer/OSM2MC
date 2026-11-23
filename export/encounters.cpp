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
// so the same functions behave identically in 1.21.x and 26.x (the formats min_format 82 covers).
void EncPack::build(const AddFn& add, const QSet<QString>& off, const QVector<Prop>& props)
{
    // The place lists per kind with caps: an event spread over the whole city stops being LOCAL, and
    // the caps bound the size of the generated pack. The vector arrives in the stable order of the
    // build stages (the chalk candidates are pre-sorted by coordinates), so the pack is reproducible.
    QVector<Prop> byKind[14];
    static const int kCap[14] = { 40, 24, 24, 24, 96, 8, 6, 8, 8, 4, 6, 6, 3, 4 };
    for (const Prop& p : props)
        if (p.kind >= 0 && p.kind < 14 && byKind[p.kind].size() < kCap[p.kind])
            byKind[p.kind].append(p);
    // props whose generator would have nothing to bake without their side data are dropped here, so
    // the roll/dispatch below always addresses a place that really has functions
    auto needCells = [](QVector<Prop>& v, int atLeast)
    {   for (int i = v.size() - 1; i >= 0; --i) if (v[i].cells.size() < atLeast) v.remove(i); };
    needCells(byKind[kEpWindows], 1);
    needCells(byKind[kEpGrave], 1);
    needCells(byKind[kEpSchool], 3);
    needCells(byKind[kEpCar], 2);
    needCells(byKind[kEpBus], 1);
    needCells(byKind[kEpLurker], 4);
    const QVector<Prop>& lamps  = byKind[kEpLamp];        // blinking_lamp + the local_rain anchor
    const QVector<Prop>& stops  = byKind[kEpBusStop];     // cat_council
    const QVector<Prop>& plays  = byKind[kEpPlayground];  // swings
    const QVector<Prop>& chalks = byKind[kEpChalk];       // chalk_art
    const QVector<Prop>& jacks  = byKind[kEpJack];        // pumpkin_faces
    const QVector<Prop>& crosses = byKind[kEpCross];      // flasher
    const QVector<Prop>& walls  = byKind[kEpWindows];     // window_wave
    const QVector<Prop>& scares = byKind[kEpScarecrow];   // scarecrow
    const QVector<Prop>& graves = byKind[kEpGrave];       // fresh_grave
    const QVector<Prop>& bells  = byKind[kEpBell];        // clock_0307
    const QVector<Prop>& schools = byKind[kEpSchool];     // matron
    const QVector<Prop>& cars   = byKind[kEpCar];         // taxi
    const QVector<Prop>& buses  = byKind[kEpBus];         // bus13
    const QVector<Prop>& lurks  = byKind[kEpLurker];      // lurker

    const QString fn = QStringLiteral("datapacks/osmc_enc/data/osmc_enc/function/");
    const QString ev = fn + QStringLiteral("ev/");           // zip paths for addEntry
    const QString fp = QStringLiteral("osmc_enc:ev/");       // function-name prefix inside commands

    // The dispatcher (as in the first release): the cycle runs every 200 ticks and shares the daytime
    // score with the events; the on/off functions let the player retune the world at run time.
    QString load = QStringLiteral("# generated by OSMC: random city events (docs/city-encounters.md)\n"
                                  "scoreboard objectives add osmc_enc dummy\n"
                                  "scoreboard objectives add osmc_enc_t dummy\n"
                                  "scoreboard players set #ca_n osmc_enc_t 0\n");
    QString cycle = QStringLiteral("# one cycle every 200 ticks; each enabled event decides inside whether it comes now\n"
                                   "execute store result score #day osmc_enc_t run time query daytime\n");

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
        {   // the glowstone lights of one baked mast go dark and come back beat by beat
            if (lamps.isEmpty()) body = noPlace;
            else
            {
                body = gate(13000, 23999, 8) + rollLines(8, lamps.size(), fp + QStringLiteral("blink/"));
                for (int j = 0; j < lamps.size(); ++j)
                {
                    const Prop& p = lamps[j];
                    const QString pth = ev + QStringLiteral("blink/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("blink/%1/").arg(j);
                    add(pth + QStringLiteral("go.mcfunction"), QStringLiteral(
                        "# the mast lights are the glowstone above the base; the blink runs only while\n"
                        "# someone walks below, so nobody waits for a lamp that no one sees\n"
                        "execute unless entity @a[x=%1,y=%2,z=%3,distance=..48] run return 0\n"
                        "scoreboard players set #bl_%4 osmc_enc_t 0\n"
                        "schedule function %5 1s replace\n")
                        .arg(p.x).arg(p.y + 3).arg(p.z).arg(j).arg(fnp + QStringLiteral("step")).toUtf8());
                    add(pth + QStringLiteral("step.mcfunction"), QStringLiteral(
                        "# beat 1: both lights go dark; beats 2 and 3 bring them back one by one\n"
                        "scoreboard players add #bl_%1 osmc_enc_t 1\n"
                        "execute if score #bl_%1 osmc_enc_t matches 1 run setblock %2 %3 %4 minecraft:polished_calcite\n"
                        "execute if score #bl_%1 osmc_enc_t matches 1 run setblock %2 %5 %4 minecraft:polished_calcite\n"
                        "execute if score #bl_%1 osmc_enc_t matches 1 run playsound minecraft:block.lever.click "
                        "master @a[x=%2,y=%3,z=%4,distance=..32] %2 %3 %4 1.0 1.6\n"
                        "execute if score #bl_%1 osmc_enc_t matches 1 run schedule function %6 2s append\n"
                        "execute if score #bl_%1 osmc_enc_t matches 2 run setblock %2 %3 %4 minecraft:glowstone\n"
                        "execute if score #bl_%1 osmc_enc_t matches 2 run schedule function %6 2s append\n"
                        "execute if score #bl_%1 osmc_enc_t matches 3 run setblock %2 %5 %4 minecraft:glowstone\n"
                        "execute if score #bl_%1 osmc_enc_t matches 3 run scoreboard players set #bl_%1 osmc_enc_t 0\n")
                        .arg(j).arg(p.x).arg(p.y + 3).arg(p.z).arg(p.y + 6).arg(fnp + QStringLiteral("step")).toUtf8());
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
                        .arg(j).arg(p.x).arg(p.y + 8).arg(p.z).arg(fnp + QStringLiteral("step")).toUtf8());
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
                            "# the cabinet stood open and nobody minded — she stays in it now\n"
                            "scoreboard players set #mat_%1 osmc_enc 1\n"
                            "kill @e[type=minecraft:armor_stand,tag=osmc_mat_%1]\n"
                            "playsound minecraft:block.wooden_door.close master @a[x=%2,y=%3,z=%4,distance=..32] %2 %3 %4 1.0 0.8\n"
                            "title @a[x=%2,y=%3,z=%4,distance=..24] actionbar {\"text\":\"the corridor goes quiet — 314 stayed closed all night\",\"color\":\"gray\",\"italic\":true}\n"
                            "function %5dark\n")
                            .arg(j).arg(p.x).arg(p.y).arg(p.z).arg(fnp).toUtf8());
                        cycle += QStringLiteral(
                            "execute unless score #mat_%1 osmc_enc matches 1 if score #day osmc_enc_t matches 18000..18199 run function %2light\n"
                            "execute if score #day osmc_enc_t matches 0..199 run function %2dark\n"
                            "execute unless score #mat_%1 osmc_enc matches 1 if block %3 %4 %5 minecraft:trapped_chest[open=true] run function %2calm\n")
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
                        "summon minecraft:armor_stand %5 %6 %7 {Invisible:1b,NoGravity:1b,Silent:1b,Tags:[\"osmc_sc\",\"osmc_sc_%1\"],Rotation:[%8f,0f],CustomNameVisible:1b,ArmorItems:[{},{},{},{id:\"minecraft:carved_pumpkin\",count:1b}]}\n")
                        .arg(j).arg(p.x).arg(p.y).arg(p.z)
                        .arg(p.x + 0.5, 0, 'f', 1).arg(double(p.y), 0, 'f', 1).arg(p.z + 0.5, 0, 'f', 1)
                        .arg(nightYaw, 0, 'f', 0).toUtf8());
                    QString w = QStringLiteral(
                        "# night: face to the house, carved face, the board's text; day: it looks away\n"
                        "execute unless entity @e[type=minecraft:armor_stand,tag=osmc_sc_%1] run return 0\n")
                        .arg(j);
                    const QString sel = QStringLiteral("@e[type=minecraft:armor_stand,tag=osmc_sc_%1,limit=1]").arg(j);
                    w += QStringLiteral(
                        "execute if score #day osmc_enc_t matches 13000..23999 run data modify entity %1 Rotation set value [%2f,0f]\n"
                        "execute if score #day osmc_enc_t matches 13000..23999 run data modify entity %1 ArmorItems[3].id set value \"minecraft:carved_pumpkin\"\n"
                        "execute if score #day osmc_enc_t matches 13000..23999 run data modify entity %1 CustomName set value \"leave by morning\"\n"
                        "execute if score #day osmc_enc_t matches 0..11999 run data modify entity %1 Rotation set value [%3f,0f]\n"
                        "execute if score #day osmc_enc_t matches 0..11999 run data modify entity %1 ArmorItems[3].id set value \"minecraft:pumpkin\"\n"
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
        {   // a grave of loose soil appears; digging it is undone by itself (see dug_dirt.json)
            if (graves.isEmpty()) body = noPlace;
            else
            {
                body = gate(13000, 23999, 10) + rollLines(10, graves.size(), fp + QStringLiteral("grave/"));
                QString back = QStringLiteral(
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
                    back += QStringLiteral(
                        "execute if block %1 %2 %3 minecraft:coarse_dirt run playsound minecraft:block.grass.break master @a[x=%1,y=%2,z=%3,distance=..32] %1 %2 %3 0.8 0.5\n"
                        "execute if block %1 %2 %3 minecraft:coarse_dirt run setblock %1 %2 %3 %4\n"
                        "execute if block %1 %5 %3 minecraft:rooted_dirt run setblock %1 %5 %3 minecraft:air\n")
                        .arg(p.x).arg(p.y).arg(p.z).arg(orig).arg(p.y + 1);
                }
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
                        "setblock %1 %2 %3 %4\n"
                        "setblock %5 %6 %7 minecraft:air\n"
                        "summon minecraft:item %5 %8 %7 {Item:{id:\"minecraft:paper\",count:1b},PickupDelay:0,Health:100,CustomName:{\"text\":\"the garage is empty. it was never full\"}}\n"
                        "playsound minecraft:block.beacon.deactivate master @a[x=%5,y=%6,z=%7,distance=..48] %5 %6 %7 0.7 0.6\n"
                        "scoreboard players set #tx_%1 osmc_enc 4\n")
                        .arg(j).arg(gar.x).arg(gar.y).arg(gar.z).arg(blockName(gar.id))
                        .arg(p.x).arg(p.y).arg(p.z).arg(p.y + 1).toUtf8());
                    add(pth + QStringLiteral("watch.mcfunction"), QStringLiteral(
                        "# the plate is the ignition while he is around; the whole arc runs on four stages\n"
                        "execute if score #tx_%1 osmc_enc matches 4.. run return 0\n"
                        "execute unless block %2 %3 %4 minecraft:stone_pressure_plate run scoreboard players set #tx_%1 osmc_enc 0\n"
                        "execute if score #tx_%1 osmc_enc matches 1 if score #day osmc_enc_t matches 0..11999 run setblock %2 %3 %4 minecraft:air\n"
                        "execute if score #tx_%1 osmc_enc matches 1 if score #day osmc_enc_t matches 0..11999 run scoreboard players set #tx_%1 osmc_enc 0\n"
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
                static const double kFaceC[4] = { 0, 90, 180, -90 };   // the N/E/S/W anchor looks at the lamp
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
                        "playsound minecraft:block.wool.place master @a[x=%1,y=%2,z=%3,distance=..40] ~ ~ ~ 0.5 0.45\n"
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
        {   // the 13 arrives at stops that never had it; the doors stay open for nobody
            if (buses.isEmpty()) body = noPlace;
            else
            {
                body = gate(13000, 23999, 12) + rollLines(12, buses.size(), fp + QStringLiteral("bus/"));
                for (int j = 0; j < buses.size(); ++j)
                {
                    const Prop& p = buses[j];
                    const bool ns = p.dir != 0;
                    const int x1 = p.x + (ns ? 2 : 11), z1 = p.z + (ns ? 11 : 2);
                    const int gy = p.y;
                    const Cell& shot = p.cells.first();
                    const QString pth = ev + QStringLiteral("bus/%1/").arg(j);
                    const QString fnp = fp + QStringLiteral("bus/%1/").arg(j);
                    // the two door cells on the low edge of the strip, at 4 and 5 along its length
                    const int d0x = ns ? p.x : p.x + 4, d0z = ns ? p.z + 4 : p.z;
                    const int d1x = ns ? p.x : p.x + 5, d1z = ns ? p.z + 5 : p.z;
                    const int mx = ns ? p.x + 1 : p.x + 5, mz = ns ? p.z + 5 : p.z + 1;   // the strip's middle
                    const QString doorFill = QStringLiteral("fill %1 %2 %3 %4 %5 %6 ")
                                                 .arg(d0x).arg(gy + 1).arg(d0z)
                                                 .arg(d1x).arg(gy + 2).arg(d1z);
                    QStringList bl;
                    bl << QStringLiteral("# the shell of tinted glass (nothing inside is visible), the roof, the open doors\n"
                                         "execute if score #bs_%1 osmc_enc matches 1.. run return 0").arg(j)
                       << QStringLiteral("execute unless entity @a[x=%1,y=%2,z=%3,distance=..64] run return 0")
                             .arg(mx).arg(gy + 1).arg(mz)
                       << QStringLiteral("fill %1 %2 %3 %4 %5 %6 minecraft:tinted_glass")
                             .arg(p.x).arg(gy + 1).arg(p.z).arg(x1).arg(gy + 3).arg(z1)
                       << QStringLiteral("fill %1 %2 %3 %4 %5 %6 minecraft:air")
                             .arg(p.x + 1).arg(gy + 1).arg(p.z + 1).arg(x1 - 1).arg(gy + 2).arg(z1 - 1)
                       << QStringLiteral("fill %1 %2 %3 %4 %5 %6 minecraft:black_concrete")
                             .arg(p.x).arg(gy + 4).arg(p.z).arg(x1).arg(gy + 4).arg(z1)
                       << QStringLiteral("fill %1 %2 %3 %4 %5 %6 minecraft:air")
                             .arg(d0x).arg(gy + 1).arg(d0z).arg(d1x).arg(gy + 2).arg(d1z)
                       << QStringLiteral("scoreboard players set #bs_%1 osmc_enc 1\n"
                                         "scoreboard players set #bo_%1 osmc_enc 1\n"
                                         "scoreboard players set #bc_%1 osmc_enc_t 6").arg(j)
                       << QStringLiteral("playsound minecraft:entity.iron_golem.death master @a[x=%1,y=%2,z=%3,distance=..64] %1 %2 %3 0.7 1.5\n"
                                         "title @a[x=%1,y=%2,z=%3,distance=..32] actionbar {\"text\":\"route 13 is not on the city map, and the doors are open\",\"color\":\"gray\",\"italic\":true}")
                             .arg(mx).arg(gy + 1).arg(mz);
                    add(pth + QStringLiteral("go.mcfunction"), (bl.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
                    add(pth + QStringLiteral("open.mcfunction"), QStringLiteral(
                            "# the doors open\n%1air\n"
                            "scoreboard players set #bo_%2 osmc_enc 1\n"
                            "scoreboard players set #bc_%2 osmc_enc_t 6\n")
                            .arg(doorFill).arg(j).toUtf8());
                    add(pth + QStringLiteral("close.mcfunction"), QStringLiteral(
                            "# the doors close on the breathing inside\n%1minecraft:tinted_glass\n"
                            "scoreboard players set #bo_%2 osmc_enc 0\n"
                            "scoreboard players set #bc_%2 osmc_enc_t 9\n")
                            .arg(doorFill).arg(j).toUtf8());
                    add(pth + QStringLiteral("in.mcfunction"), QStringLiteral(
                            "# nobody rides the 13: the dark inside only breathes\n"
                            "tp @s %1 %2 %3\n"
                            "effect give @s minecraft:darkness 12 0 true\n"
                            "playsound minecraft:block.bubble_column.upward_inside master @a[distance=..24] %1 %2 %3 1.0 0.6\n"
                            "title @s actionbar {\"text\":\"something in the dark bus stopped breathing when you stepped in\",\"color\":\"dark_purple\",\"italic\":true}\n")
                            .arg(shot.x + 0.5, 0, 'f', 1).arg(shot.y + 1).arg(shot.z + 0.5, 0, 'f', 1).toUtf8());
                    add(pth + QStringLiteral("legend.mcfunction"), QStringLiteral(
                            "# the photo came out — the 13 becomes a story people tell, not a fear\n"
                            "scoreboard players set #bs_%1 osmc_enc 2\n"
                            "scoreboard players set #pt_%1 osmc_enc_t 0\n"
                            "title @a[x=%2,y=%3,z=%4,distance=..64] actionbar {\"text\":\"you photographed the 13. the city keeps it as a legend\",\"color\":\"gold\",\"italic\":true}\n")
                            .arg(j).arg(mx).arg(gy + 1).arg(mz).toUtf8());
                    add(pth + QStringLiteral("leave.mcfunction"), QStringLiteral(
                            "# at dawn the 13 rides back to the depot that never existed\n"
                            "fill %2 %3 %4 %5 %6 %7 minecraft:air\n"
                            "scoreboard players set #bs_%1 osmc_enc 0\n"
                            "scoreboard players set #pt_%1 osmc_enc_t 0\n"
                            "playsound minecraft:entity.minecart.riding master @a[x=%8,y=%9,z=%10,distance=..64] %8 %9 %10 0.8 0.6\n")
                            .arg(j).arg(p.x).arg(gy + 1).arg(p.z).arg(x1).arg(gy + 4).arg(z1)
                            .arg(mx).arg(gy + 1).arg(mz).toUtf8());
                    add(pth + QStringLiteral("watch.mcfunction"), QStringLiteral(
                            "# the doors breathe on their timer; the inside stays nobody's; and someone\n"
                            "# at the stop stands very still with a clock in hand\n"
                            "execute unless score #bs_%2 osmc_enc matches 2 if score #day osmc_enc_t matches 0..11999 run function %1leave\n"
                            "scoreboard players remove #bc_%2 osmc_enc_t 1\n"
                            "execute if score #bc_%2 osmc_enc_t matches ..0 if score #bo_%2 osmc_enc matches 1 run function %1close\n"
                            "execute if score #bc_%2 osmc_enc_t matches ..0 if score #bo_%2 osmc_enc matches 0 run function %1open\n"
                            "execute unless score #bs_%2 osmc_enc matches 2 if entity @a[x=%3,y=%4,z=%5,dx=%6,dy=1,dz=%7] run function %1in\n"
                            "execute if entity @a[x=%8,y=%9,z=%10,distance=..2,nbt={SelectedItem:{id:\"minecraft:clock\"}}] run scoreboard players add #pt_%2 osmc_enc_t 1\n"
                            "execute unless entity @a[x=%8,y=%9,z=%10,distance=..2,nbt={SelectedItem:{id:\"minecraft:clock\"}}] run scoreboard players set #pt_%2 osmc_enc_t 0\n"
                            "execute if score #pt_%2 osmc_enc_t matches 18.. run function %1legend\n")
                            .arg(fnp).arg(j)
                            .arg(p.x + 1).arg(gy + 1).arg(p.z + 1).arg(x1 - p.x - 2).arg(z1 - p.z - 2)
                            .arg(shot.x).arg(shot.y + 1).arg(shot.z).toUtf8());
                    cycle += QStringLiteral(
                        "execute if score #bs_%1 osmc_enc matches 1.. run function %2watch\n")
                        .arg(j).arg(fnp).toUtf8();
                }
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
    add(QStringLiteral("datapacks/osmc_enc/data/minecraft/tags/function/load.json"),
             QByteArray("{\"values\":[\"osmc_enc:load\"]}"));
    add(QStringLiteral("datapacks/osmc_enc/data/minecraft/tags/function/tick.json"),
             QByteArray("{\"values\":[\"osmc_enc:tick\"]}"));
    add(QStringLiteral("datapacks/osmc_enc/pack.mcmeta"),
             QByteArray("{\"pack\":{\"description\":\"OSMC: random city events over the exported places\",\"min_format\":82,\"max_format\":121}}"));
}
