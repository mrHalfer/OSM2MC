#include "app/encountersdialog.h"
#include "app/theme.h"
#include "export/encounters.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QStackedLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>
#include <QDebug>
#include <cstring>

namespace
{
// The ids MUST repeat export/encounters.h in the same order and count (checked at open time). Titles,
// legends and practical hints are translatable literals (lupdate takes all three into osmc_ru/osmc_zh);
// the legends retell the idea texts of docs/city-encounters.md in the author voice, the hints say what
// the shipped pack really does. The right column shows the legend open and the hint below it behind
// the frost plate (BlurPane) until the plate is clicked.
struct Row { const char* id; const char* group; const char* title; const char* desc; const char* hint; };
const Row kRows[] = {
    { "lurker",                 QT_TRANSLATE_NOOP("EncountersDialog", "Pursuers"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Lurker — a faceless figure in the lamplight"),
      QT_TRANSLATE_NOOP("EncountersDialog", "A thin faceless figure in black. It rises far off in a lamp's light - "
                              "by the park with the flying island, by a lonely lamp on the road, at "
                              "the edge of a quarter. Walk closer and it waits behind you in the "
                              "alley. It behaves like hearing: noise, footsteps nearer than your own."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 19:00-06:00, about 10% per cycle: the tall one circles the light "
                              "of one street lamp and keeps his distance; he lets go only when the "
                              "sun takes the street. The church-light finale is still coming.") },
    { "taxi",                   nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Night Taxicab"),
      QT_TRANSLATE_NOOP("EncountersDialog", "By the night parking lot or the metro stop stands a car, and beside it "
                              "a fellow traveller. Start the engine - a silhouette on the back seat. "
                              "Step out to look - nobody, and the door slams itself. The arc closes "
                              "where his garage stands in the industrial zone: an empty place with a "
                              "stain."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 19:00-06:00, about 10% per cycle: a plate lying by one car's "
                              "door is his ignition. Find his empty spot with the stain in the "
                              "industrial zone and the rider is gone for good.") },
    { "echo",                   nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Echo of the tunnels"),
      QT_TRANSLATE_NOOP("EncountersDialog", "In the metro tunnels and the underpasses it gives your own steps back "
                              "a breath late, as if from another tunnel. The closer it is, the more "
                              "in sync. Lead it into the street: the Echo does not go outside."),
      QT_TRANSLATE_NOOP("EncountersDialog", "No dice at all: it listens in a baked tunnel - a step is heard where "
                              "you stood one cycle ago. Stand still and only silence answers; "
                              "outdoors it cannot follow.") },
    { "matron",                 nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Night Matron (schools, hospitals)"),
      QT_TRANSLATE_NOOP("EncountersDialog", "In schools, kindergartens and hospital wings the light comes on in her "
                              "offices by a schedule, and a note 'check the 3rd floor' shows on the "
                              "block's board. Find her locker with the key and the block calms down."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 19:00-06:00, about 10% per cycle: in one school/hospital wing "
                              "the light turns on in her office (314) and someone walks the corridor; "
                              "by morning it goes dark again. The locker with the key is still "
                              "coming.") },
    { "mower",                  nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Mower in the fog"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The owner of a lawn that ran out. Invisible by day, his motor is "
                              "audible in the fog; by morning the grass carries even stripes nobody "
                              "mowed. The stripes cut a path to his shed - find it."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Late night 00:00-06:00, about 8% per cycle: one stripe a night - three "
                              "cells of grass trodden with the hum of something small and low. Dawn "
                              "gives the whole lawn back.") },
    { "upstairs",               nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Neighbour Above"),
      QT_TRANSLATE_NOOP("EncountersDialog", "A stomp strictly above your head wherever you spend the night. One "
                              "floor up is a door that is on no plan. Go up to the roof: a chair "
                              "waits there, a little yard, and a view over the whole city."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Late night 00:00-06:00, about 14% per cycle: furniture dragged seven "
                              "blocks over your head, or three knocks with a door behind them. Only "
                              "sound - the blocks never move.") },
    { "third_passenger",        nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Third Passenger"),
      QT_TRANSLATE_NOOP("EncountersDialog", "In a 'car' (a tunnel with tracks) there are always more riders than "
                              "boarded. Do not turn around until the terminus - it passes."),
      QT_TRANSLATE_NOOP("EncountersDialog", "No dice at all: in a baked tunnel with tracks two shadows repeat your "
                              "path one and two cycles behind you, and a third set of steps belongs "
                              "to nobody. The count is announced once per walk; the shadows dissolve "
                              "at dawn.") },
    { "bus13",                  nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "Bus No. 13"),
      QT_TRANSLATE_NOOP("EncountersDialog", "At night, at a terminus on the outskirts, a route that does not exist "
                              "stops. The doors stand open; do not step inside - darkness and "
                              "breathing. Keep its timetable and photograph it, and it becomes a "
                              "harmless legend."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 19:00-06:00, about 12% per cycle: at a stop without route 13 the "
                              "13 only flickers past twice, with the roar of a passing engine. Stand "
                              "there very still with a clock in hand and the stop stays quiet for "
                              "good.") },
    { "carousel",               QT_TRANSLATE_NOOP("EncountersDialog", "Parks and yards"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Midnight Carousel"),
      QT_TRANSLATE_NOOP("EncountersDialog", "It spins and plays by itself while nobody watches. It already works as "
                              "a demo: cold sparks over the marked parks."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 19:00-06:00, about 8% per cycle: a cold end-rod shimmer flies "
                              "over the parks marked for the carousel. The carousel itself is still "
                              "coming.") },
    { "swings",                 nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Swings Nobody Rides"),
      QT_TRANSLATE_NOOP("EncountersDialog", "On the playground's fourth swing an invisible rider rocks; beside it "
                              "in the sand, a boot print that was not there in the evening."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 19:00-06:00, about 25% per cycle: the seat of the swings goes "
                              "along the arc eight positions at a time under nobody; the playground "
                              "is picked by the roll.") },
    { "chalk_art",              nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "Chalk That Redraws Itself"),
      QT_TRANSLATE_NOOP("EncountersDialog", "A children's chalk drawing on asphalt: 'drive here'. At night the "
                              "drawing changes - it is redrawn toward a new address."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 19:00-06:00, about 40% per cycle: the chalk arrow on the asphalt "
                              "is erased and redrawn pointing to a new address, in another color.") },
    { "twin_island",            nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Twin Island"),
      QT_TRANSLATE_NOOP("EncountersDialog", "Over the park that already got its flying island a second one rises, "
                              "with no base under it. A spare exit for the Lurker."),
      QT_TRANSLATE_NOOP("EncountersDialog", "The logic is coming: by the design the second island will rise over "
                              "the first.") },
    { "dry_fountain",           nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Fountain Running Downward"),
      QT_TRANSLATE_NOOP("EncountersDialog", "At night the fountain's water flows not into the bowl but three blocks "
                              "below ground level. Under the fountain, a cavity that is on no plan."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 19:00-06:00, about 10% per cycle: the bowl drains in one tick - "
                              "the water leaves through the bottom - and half a minute later climbs "
                              "back, colder.") },
    { "bench_otherworld",       nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Bench Not For Everyone"),
      QT_TRANSLATE_NOOP("EncountersDialog", "Sit here - and the same bench stands in fog and rain, with not one "
                              "sound from the city. A mini-dimension; the way out is simply to stand "
                              "up."),
      QT_TRANSLATE_NOOP("EncountersDialog", "About every three minutes the fog takes one oak bench within fifty "
                              "blocks of a player - its seat turns dark. Stand three seconds before "
                              "the dark bench and you sink into the park; the way out waits before "
                              "the middle seat. The dread flattens to twenty-five.") },
    { "stray_dog",              nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Dog Without An Owner"),
      QT_TRANSLATE_NOOP("EncountersDialog", "It follows the player in silence and disappears the moment you come "
                              "near people."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night: a dog with no address falls in beside one walker - it keeps to "
                              "him alone, slips away among people and is gone by dawn. It never makes "
                              "a sound on its own.") },
    { "pumpkin_faces",          QT_TRANSLATE_NOOP("EncountersDialog", "Graveyards"),
      QT_TRANSLATE_NOOP("EncountersDialog", "Pumpkins That Turn To You"),
      QT_TRANSLATE_NOOP("EncountersDialog", "Jack-o'-lanterns turn their faces toward the player - the block "
                              "changes while you watch from afar."),
      QT_TRANSLATE_NOOP("EncountersDialog", "No dice at all: every cycle the cemetery jacks turn their carved face "
                              "to whoever watches from a distance - and never to someone standing "
                              "close.") },
    { "fresh_grave",            nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Fresh Grave"),
      QT_TRANSLATE_NOOP("EncountersDialog", "Lit up as 'fresh soil'. Do not dig: the shovel's sound breaks off on "
                              "its own."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 19:00-06:00, about 10% per cycle: one grave is lit up with fresh "
                              "soil; dig into it and the grave digs itself back up.") },
    { "procession",             nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Night Procession"),
      QT_TRANSLATE_NOOP("EncountersDialog", "At night a column of unseen mourners walks to the gates - only "
                              "particles and steps. Stand in its way and they walk through."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Late night 00:00-06:00, about 6% per cycle: soul fire marches along a "
                              "graveyard alley from grave to grave, two seconds a cell; one "
                              "procession at a time, and it walks only while somebody could see it.") },
    { "grave_caretaker",        nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Caretaker"),
      QT_TRANSLATE_NOOP("EncountersDialog", "A skeleton in the guard house: while you are near he pretends to sit; "
                              "turn away and he already stands at the door."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Late night 00:00-06:00, about 8% per cycle: come to one side of the "
                              "mound and the caretaker is already standing on the opposite one; at "
                              "dawn he leaves.") },
    { "clock_0307",             nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "Always 03:07"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The cemetery clock strikes 03:07 even after the player broke its "
                              "mechanism."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Seven bell strikes at exactly 03:07; the cemetery clock rebuilds "
                              "itself after a break. Nothing more.") },
    { "last_train",             QT_TRANSLATE_NOOP("EncountersDialog", "Metro and tunnels"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Last Train"),
      QT_TRANSLATE_NOOP("EncountersDialog", "Every few nights a train walks the tunnel: hostile mobs stay away "
                              "while it runs. You can 'catch' it and ride to the stash - a teleport "
                              "room."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Deep night 05:00-06:00, about 10% per cycle: the train moves one cell "
                              "every two seconds, clears the platform of mobs, and past the hall "
                              "carries whoever stood on the platform to a room at the end of the "
                              "line.") },
    { "phantom_station",        nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Station That Is Not"),
      QT_TRANSLATE_NOOP("EncountersDialog", "A signpost to the exit of a station that does not exist appears. "
                              "Follow it: an empty hall, one burning lamp and a school desk."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 01:00-06:00, about 6% per cycle: the hall that is on no map "
                              "lights up for about a minute - a lantern and a bell stand in it while "
                              "nobody looks closely; the morning sweep removes them.") },
    { "extra_flight",           nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Stair With One Flight More"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The escalator or the vestibule stairs climb one flight more than the "
                              "outside shows. Inside, a decoy room."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 03:00-06:00, about 6% per cycle: rail dust six times and a roar "
                              "nobody counts the same way twice. The blocks never move.") },
    { "breathing_rails",        nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Breathing Rails"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The gap between the rails cyclically widens and narrows. At the middle "
                              "of the span, a voice."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Late night 00:00-06:00, about 6% per cycle: a red pulse walks the "
                              "route, thinning and thickening, and at the middle a low sound leans in "
                              "to listen to the sleeper. Only light and sound move; nobody is ever "
                              "sealed inside.") },
    { "dark_wave",              nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Darkness Behind You"),
      QT_TRANSLATE_NOOP("EncountersDialog", "Light in the tunnel goes out in segments behind your back: every lamp "
                              "keeps its own timer, and the dark catches up."),
      QT_TRANSLATE_NOOP("EncountersDialog", "The logic is coming: by the design the tunnel lamps will go out "
                              "segment by segment behind the walker.") },
    { "floor_half",             QT_TRANSLATE_NOOP("EncountersDialog", "Tower living"),
      QT_TRANSLATE_NOOP("EncountersDialog", "Floor Nine And A Half"),
      QT_TRANSLATE_NOOP("EncountersDialog", "A door between the ninth and tenth floors. Behind it the same floor, "
                              "only in mirrors: a duplicated mini-floor."),
      QT_TRANSLATE_NOOP("EncountersDialog", "After midnight the door that is on no plan shows in the stairwell of a "
                              "tall house (about 8% per cycle). Open it from the flight and you stand "
                              "on the landing. The floor takes each guest fifty-fifty: a chest in the "
                              "hall (nine of ten plain, one of ten from a mansion), or company: five "
                              "to fifteen monsters, or a warden. Any door there brings you back to "
                              "your own flight; by morning nothing is left.") },
    { "ghost_lift",             nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Lift Without A Shaft"),
      QT_TRANSLATE_NOOP("EncountersDialog", "Call the lift on an empty flight - there will be a sound and 'doors' "
                              "opening onto a blind wall."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Late night 00:00-06:00, about 8% per cycle: press a button where there "
                              "is no button and the shaft hums behind the stairwell wall, doors "
                              "sliding across plain plaster. The cabin never comes.") },
    { "window_wave",            nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Window Wave"),
      QT_TRANSLATE_NOOP("EncountersDialog", "Light in every window of one house turns on in turn from bottom to "
                              "top, over twenty seconds, and goes out all at once."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 19:00-06:00, about 8% per cycle: a wave of light runs up one "
                              "house's windows and dies at once. Houses without a fit row of windows "
                              "sit it out.") },
    { "roof_garden",            nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Roof Garden"),
      QT_TRANSLATE_NOOP("EncountersDialog", "On the roof: a garden bed, an armchair and a sign 'do not disturb'. "
                              "The owner will shush anyone trampling the beds."),
      QT_TRANSLATE_NOOP("EncountersDialog", "The logic is coming: by the design the roof will keep a garden with a "
                              "guardian.") },
    { "boarded_door",           nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Boarded-Over Door"),
      QT_TRANSLATE_NOOP("EncountersDialog", "A door that is on the plan but boarded over. At night there is crying "
                              "behind it. Tear the boards off - a room full of inscriptions."),
      QT_TRANSLATE_NOOP("EncountersDialog", "The logic is coming: by the design a boarded door and a room behind "
                              "it.") },
    { "extra_car",              QT_TRANSLATE_NOOP("EncountersDialog", "Streets and parking"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Extra Car"),
      QT_TRANSLATE_NOOP("EncountersDialog", "A car on the parking lot that was not on the list (OSMC places cars "
                              "deterministically - so this one is 'extra'). Its tape deck plays the "
                              "news about that driver."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 01:00-06:00, about 8% per cycle: a radio talks beside one of the "
                              "cars - three note block taps like a broadcast without words, a line on "
                              "the actionbar; silence by morning.") },
    { "flasher",                nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Flasher Without A Car"),
      QT_TRANSLATE_NOOP("EncountersDialog", "A flasher without a car over an empty intersection. Come near - it "
                              "dies here and lights up behind your back."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Evening 18:00-06:00, about 10% per cycle: a blue blink over one of the "
                              "crossings; approach and it goes out here and comes on behind you.") },
    { "loop_interchange",       nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Interchange Loop"),
      QT_TRANSLATE_NOOP("EncountersDialog", "You passed the same interchange sign three times - and the block "
                              "around you 're-assembled': the neighboring chunks swapped in a "
                              "mini-location."),
      QT_TRANSLATE_NOOP("EncountersDialog", "The logic is coming: by the design a block reassembling around the "
                              "same sign.") },
    { "roadworks",              nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Roadworks On The Viaduct"),
      QT_TRANSLATE_NOOP("EncountersDialog", "'Roadworks' of blocks on the viaduct ahead. Clear the wreck and behind "
                              "it is a cliff into nothing: the world's end, staged."),
      QT_TRANSLATE_NOOP("EncountersDialog", "The logic is coming: by the design a wreck on the viaduct with a cliff "
                              "behind it.") },
    { "hitchhiker",             nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Hitchhiker"),
      QT_TRANSLATE_NOOP("EncountersDialog", "A voice from the road edge asks for a ride. Take him and two hundred "
                              "meters on he vanishes, leaving a note."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 04:00-06:00, about 6% per cycle: somebody waits at the road edge "
                              "with a lantern; come close to offer a ride and the lantern burns on "
                              "empty asphalt. By morning a note lies there: 'I did not ride. thank "
                              "you anyway'.") },
    { "night_game",             QT_TRANSLATE_NOOP("EncountersDialog", "Schools"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Game In The Gym"),
      QT_TRANSLATE_NOOP("EncountersDialog", "At night somebody plays on the gym's net - the sounds of a ball. Step "
                              "in: the ball rolls toward the door."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 01:00-06:00, about 10% per cycle: a snowball hops between three "
                              "cells of the school gym - the yard plays by itself; by morning the "
                              "ball lies still.") },
    { "skip_rope",              nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Skipping Rope"),
      QT_TRANSLATE_NOOP("EncountersDialog", "In the yard a skipping rope jumps that nobody turns - and 'two of "
                              "them' jump into it."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 01:00-06:00, about 8% per cycle: the rhythm of a note block, a "
                              "rope of crit particles across the dark, and on every beat two small "
                              "silhouettes rise and sit back down.") },
    { "auditorium",             nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Empty Auditorium"),
      QT_TRANSLATE_NOOP("EncountersDialog", "A spotlight switches on by itself over one empty chair; on the stage, "
                              "the chalk outline of a 'graduate'."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 02:00-06:00, about 6% per cycle: a pillar of end rod over the "
                              "school yard, three slow pulses, and the outline of a graduate drawn in "
                              "white sparks.") },
    { "timetable",              nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "Yesterday's Timetable"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The subjects on the board 'for tomorrow' get rewritten to "
                              "'yesterday's' - including teachers the school never had."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Morning 07:00-12:00, about 8% per cycle: dust over the school's chest, "
                              "a scrap left on its lid, and the last line reads a name the city does "
                              "not have. The scrap can be picked up.") },
    { "banya",                  QT_TRANSLATE_NOOP("EncountersDialog", "Private sector"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Banya With Lights On"),
      QT_TRANSLATE_NOOP("EncountersDialog", "A banya that is 'abandoned' yet steams and glows. Inside, a basin and "
                              "a broom; take the broom and you will hear 'put it back', and it will "
                              "not be yours to take until night."),
      QT_TRANSLATE_NOOP("EncountersDialog", "The logic is coming: by the design a banya with steam and a broom that "
                              "must go back.") },
    { "unlocked_gate",          nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Gate That Is Always Locked"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The wicket that is 'always locked' stands open for the third time. "
                              "Behind it the same plot, only in winter: a mini-substitution."),
      QT_TRANSLATE_NOOP("EncountersDialog", "The logic is coming: by the design a wicket into the same plot, but "
                              "winter.") },
    { "scarecrow",              nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Scarecrow That Turned"),
      QT_TRANSLATE_NOOP("EncountersDialog", "By day it stands with its back to the house - you may check and turn "
                              "it. At night it faces the house, and on its chest a board says 'leave "
                              "by morning'."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night: the scarecrow turns to face the house, puts on a carved pumpkin "
                              "and the warning 'leave by morning'; by day it turns back.") },
    { "howling_dog",            nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Dog Howling At One Fence"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The neighbor's dog howls at one fence only. By that fence, an "
                              "underground run into the forest."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Late night 00:00-06:00, about 10% per cycle: a howl from the field "
                              "past the last house, and six seconds later an answer from further off. "
                              "The underground run is still being dug.") },
    { "moths",                  QT_TRANSLATE_NOOP("EncountersDialog", "Woods and industry"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The White Moths"),
      QT_TRANSLATE_NOOP("EncountersDialog", "A flight of white moth particles around one lamp. Stand in the center "
                              "- the wing sound dies right in your ear."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 01:00-06:00, about 10% per cycle: a ring of moths turns around "
                              "one lamp for about a minute; stand in the middle and the world goes "
                              "quiet.") },
    { "power_hum",              nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Hum Of The Power Line"),
      QT_TRANSLATE_NOOP("EncountersDialog", "A power-line cut with no pylons - the wires lie in the earth, and "
                              "fifty hertz is audible. At the cut's end, a meter booth serving the "
                              "whole district."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 02:00-06:00, about 8% per cycle: one lamp carries the hum "
                              "tonight - three low pulses under the light, and the ordinary night "
                              "again. The wires stay underground.") },
    { "dead_camp",              nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Dead Camp"),
      QT_TRANSLATE_NOOP("EncountersDialog", "An abandoned camp in the woods (OSMC builds such): the fire burns "
                              "itself out, a fresh spoon lies by the coals."),
      QT_TRANSLATE_NOOP("EncountersDialog", "The logic is coming: by the design a fire dying out by itself and "
                              "somebody's spoon.") },
    { "conveyor",               nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Conveyor At Night"),
      QT_TRANSLATE_NOOP("EncountersDialog", "At night the shop floor's conveyor rolls a crate out onto the street. "
                              "Inside: a mannequin in working overalls."),
      QT_TRANSLATE_NOOP("EncountersDialog", "The logic is coming: by the design a crate rolling out at night with a "
                              "'worker' inside.") },
    { "forest_window",          nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Window In The Taiga"),
      QT_TRANSLATE_NOOP("EncountersDialog", "In the taiga by the private sector a path leads exactly to a 'window' "
                              "between houses. In the window, the light of your own campfire."),
      QT_TRANSLATE_NOOP("EncountersDialog", "The logic is coming: by the design a path leading to a lit window in "
                              "the taiga.") },
    { "lake_lightpath",         QT_TRANSLATE_NOOP("EncountersDialog", "Water"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Light Path On The Bottom"),
      QT_TRANSLATE_NOOP("EncountersDialog", "In the park lake a path of pale stone lies along the bottom. Walk it "
                              "and the light goes out in patches behind your back."),
      QT_TRANSLATE_NOOP("EncountersDialog", "The logic is coming: by the design a light path under water dying "
                              "section by section behind the walker.") },
    { "bridge_offerings",       nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "Offerings On The Railing"),
      QT_TRANSLATE_NOOP("EncountersDialog", "On the bridge railing 'offerings' arrange themselves in rows of three. "
                              "By morning the row has grown again."),
      QT_TRANSLATE_NOOP("EncountersDialog", "The logic is coming: by the design offerings on the railing, filling "
                              "up by morning.") },
    { "cloud_reflection",       nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Reflection Of An Absent Cloud"),
      QT_TRANSLATE_NOOP("EncountersDialog", "In the fountain's or the river's water a cloud is reflected that is "
                              "not in this sky."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 01:00-06:00, about 6% per cycle: a white softness drifts over "
                              "the water three sweeps, passing under your own reflection.") },
    { "fourth_wall",            QT_TRANSLATE_NOOP("EncountersDialog", "Underground"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Fourth Wall"),
      QT_TRANSLATE_NOOP("EncountersDialog", "In a cave grotto a corridor opens onto a 'flooded' copy of the street "
                              "that is above."),
      QT_TRANSLATE_NOOP("EncountersDialog", "The logic is coming: by the design a corridor into a flooded copy of "
                              "the street.") },
    { "rune_circle",            nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Glowing Circle"),
      QT_TRANSLATE_NOOP("EncountersDialog", "A glowing patterned circle on a cave floor. Stand in the center - the "
                              "world's sounds fall silent for thirty seconds."),
      QT_TRANSLATE_NOOP("EncountersDialog", "The logic is coming: by the design a pattern that silences the world "
                              "at its center.") },
    { "ghost_rolley",           nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Ghost Rrolley"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The metro draisine comes if you simply stand on the rails and wait."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Late night 00:00-06:00, about 6% per cycle: stand on the rails and "
                              "wait - a real handcart rolls along the baked route toward you with "
                              "sparks, and at the far end it simply stops existing.") },
    { "base_radio",             QT_TRANSLATE_NOOP("EncountersDialog", "Base and prison"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Base Radio At Night"),
      QT_TRANSLATE_NOOP("EncountersDialog", "At night the base loudspeakers read out coordinates and 'do not go'. "
                              "Go there: empty - but the 'meeting' never appears there again."),
      QT_TRANSLATE_NOOP("EncountersDialog", "The logic is coming: by the design coordinates from a loudspeaker and "
                              "a cancelled meeting.") },
    { "lone_sentry",            nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Sentry Not In The Garrison"),
      QT_TRANSLATE_NOOP("EncountersDialog", "By the base wicket stands one sentry who is not in the garrison. He "
                              "does not pass the gate - he walks around the fence."),
      QT_TRANSLATE_NOOP("EncountersDialog", "The logic is coming: by the design a sentry the gate will not take.") },
    { "refugee_camp",           nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Camp That Vanishes By Dawn"),
      QT_TRANSLATE_NOOP("EncountersDialog", "A camp of sacks and crates grows by the industrial zone and is gone by "
                              "dawn. In the sacks, 'passports' carrying the names of people from this "
                              "city."),
      QT_TRANSLATE_NOOP("EncountersDialog", "The logic is coming: by the design a camp that exists at night and is "
                              "gone by dawn.") },
    { "blinking_lamp",          QT_TRANSLATE_NOOP("EncountersDialog", "Micro wonders"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Winking Lamp"),
      QT_TRANSLATE_NOOP("EncountersDialog", "A lamp winks right above the player - a change of its lit state."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 19:00-06:00, about 8% per cycle: one street lamp goes dark and "
                              "comes back beat by beat; for the exporter's own lantern the dark is "
                              "the empty cell it hangs in, for a mast the dark glowstones.") },
    { "hush_field",             nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Field Of Silence"),
      QT_TRANSLATE_NOOP("EncountersDialog", "A trap of silence: within ten blocks neither steps nor mobs are heard "
                              "- for fifteen seconds."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Any hour, about 6% per cycle: around one lamp the sound falls out of "
                              "the air for a quarter of a minute; the ambience comes back by itself.") },
    { "local_rain",             nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Rain Above One Street"),
      QT_TRANSLATE_NOOP("EncountersDialog", "Rain falls over one street only: splash particles overhead and rain "
                              "sound, while beside it - dry."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Any hour, about 8% per cycle: a wet column of splash and rain sound "
                              "over one street lamp; the world's weather stays untouched - a local "
                              "stylization.") },
    { "window_walker",          nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Silhouette In The Windows"),
      QT_TRANSLATE_NOOP("EncountersDialog", "On the fifth floor a silhouette with a lantern stands in the window "
                              "and walks along the row of windows like a sleepwalker."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Late night 04:00-06:00, about 6% per cycle: a floating lantern crosses "
                              "one row of a baked facade, one window per second and a half.") },
    { "long_shadows",           nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Shadows Too Long For Noon"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The noon shadows of the houses are ten degrees longer than they should "
                              "be."),
      QT_TRANSLATE_NOOP("EncountersDialog", "In the dusk, while the sky is still light: a dark seam of particles "
                              "creeps away from a lamp three sweeps, each one thinner and longer; "
                              "when the sky goes dark, the shadow is gone - the shadow is all that "
                              "ever stood there.") },
    { "cat_council",            nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Cat Council"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The city's cats gather in a 'council' by one entrance. Come near and "
                              "they file off to their homes."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 19:00-06:00, about 35% per cycle: the cats sit in council by one "
                              "stop; scatter them and the next roll gathers them somewhere else.") },
    { "phone_booth",            QT_TRANSLATE_NOOP("EncountersDialog", "Bonus legends"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Telephone Booth That Rings"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The city's 'telephone for the ears': the booth rings. Take the handset "
                              "and you are dictated a route to a meeting."),
      QT_TRANSLATE_NOOP("EncountersDialog", "The logic is coming: by the design a call dictating the route to a "
                              "meeting.") },
    { "night_market",           nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Night Market"),
      QT_TRANSLATE_NOOP("EncountersDialog", "On an empty square, five minutes of trade. By morning: a row of empty "
                              "crates and change left on the counter."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Night 01:00-06:00, about 6% per cycle: stalls of lantern light with no "
                              "wood set up where the field lay all day, and the murmur of a hundred "
                              "buyers is one villager's whisper; by the time you reach the stop the "
                              "row is packing up.") },
    { "rising_street",          nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Street That Builds Itself"),
      QT_TRANSLATE_NOOP("EncountersDialog", "A street that builds itself: fences, and progress visible every day. "
                              "By the seventh day the house is finished - and it hosts the first "
                              "meeting."),
      QT_TRANSLATE_NOOP("EncountersDialog", "Late night 00:00-06:00, about 6% per cycle: the street 'climbs' by "
                              "sound - a gravel groan under the asphalt, dust through the seams, a "
                              "lamp standing higher over the road. The blocks never move; the "
                              "day-by-day build is still coming.") },
    { "warning_sign",           nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Teasing Sign"),
      QT_TRANSLATE_NOOP("EncountersDialog", "A sign by the metro: 'do not go into the tunnel'. Go in, and in the "
                              "morning the sign reads: 'I told you'."),
      QT_TRANSLATE_NOOP("EncountersDialog", "The logic is coming: by the design a sign that rewrites itself by "
                              "morning.") },
};
const int kRowCount = int(sizeof(kRows) / sizeof(kRows[0]));

constexpr int kIdRole = Qt::UserRole;         // the event id of a leaf row
constexpr int kDescRole = Qt::UserRole + 1;   // its description
constexpr int kHintRole = Qt::UserRole + 2;  // its practical hint (the how, the window, the completion)

//! A real blur (Qt's QGraphicsBlurEffect only darkens here): bilinear shrink to a threadbare grid
//! and back up, so no glyph survives — the shape of the text and its colours stay, the letters die.
QPixmap blurredCopy(const QPixmap& src)
{
    QSize s1(qMax(2, src.width() / 12), qMax(2, src.height() / 12));
    const QPixmap s2 = src.scaled(s1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QSize s3(qMax(2, s1.width() / 3), qMax(2, s1.height() / 3));
    return s2.scaled(s3, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
           .scaled(src.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

//! The text of the description drawn by hand: QWidget::render() over a widget that was never shown
//! gives an empty pixmap (the --gui-encounters shot), while QPainter on a pixmap always paints.
QPixmap textPixmap(const QFont& font, const QColor& color, const QString& text, const QSize& size)
{
    QPixmap pm(size);
    pm.fill(Qt::transparent);
    QPainter pr(&pm);
    pr.setFont(font);
    pr.setPen(color);
    const QRect r(0, 0, size.width(), size.height());
    pr.drawText(r.translated(10, 10).adjusted(0, 0, -20, 0), Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, text);
    return pm;
}

//! The text of a pane without the size hint a word-wrapped QLabel reports for a single
//! long line: neither pane may drag the whole dialog taller than the row list beside it
//! (the two panes of the right column stack, so each keeps to a part of the height).
class PlainLabel : public QLabel
{
public:
    using QLabel::QLabel;
    QSize sizeHint() const override { return QSize(360, 170); }
    QSize minimumSizeHint() const override { return QSize(160, 60); }
};

//! The lower pane: the practical note behind a frosted plate (the legend above it stays open).
//! Clicking the plate clears the frost and shows what was written (docs/city-encounters.md
//! flavour). The text is painted by us as a blurred grab of the label, so the blur is a blur, not
//! a shade.
class BlurPane : public QWidget
{
public:
    explicit BlurPane(QWidget* parent) : QWidget(parent)
    {
        auto* stack = new QStackedLayout(this);
        stack->setStackingMode(QStackedLayout::StackAll);       // the hint stays on top of the text
        mLab = new PlainLabel(this);
        mLab->setWordWrap(true);
        mLab->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        mLab->setTextFormat(Qt::PlainText);
        mLab->setMargin(10);
        mHint = new QLabel(QCoreApplication::translate("EncountersDialog", "Click to read"), this);
        // NB: the context is the DIALOG, not the pane: BlurPane has no Q_OBJECT, so tr() here would
        // ask a different context than lupdate writes into the .ts
        mHint->setObjectName(QStringLiteral("encFrost"));           // the click target for --gui-encounters
        mHint->setAlignment(Qt::AlignCenter);
        mHint->setStyleSheet(QStringLiteral("QLabel { background: rgba(24,26,30,200); color: #cfd2d6;"
                                            " border-radius: 6px; padding: 10px 18px; font-weight: bold; }"));
        mHint->installEventFilter(this);                            // the plate is the click target
        stack->addWidget(mLab);
        stack->addWidget(mHint);
        mHint->raise();
    }

    void setText(const QString& s)
    {
        mDesc = s;
        showHint(true);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* e) override
    {
        if (watched == mHint && e->type() == QEvent::MouseButtonRelease)
        {
            showHint(false);
            return true;
        }
        return QWidget::eventFilter(watched, e);
    }

    void resizeEvent(QResizeEvent* e) override
    {
        QWidget::resizeEvent(e);
        invalidate();
    }

    void paintEvent(QPaintEvent* e) override
    {
        QWidget::paintEvent(e);
        if (!mFrost || size().isEmpty()) return;
        if (mPix.isNull() || mPix.size() != size())
            // the color from the theme (not the palette: the style sheet paints the labels, the
            // palette of a widget it styles keeps the system's light-theme ink)
            mPix = blurredCopy(textPixmap(mLab->font(), TmcTheme::colors().text, mDesc, size()));
        QPainter pr(this);
        pr.drawPixmap(0, 0, mPix);
    }

private:
    void showHint(bool on)
    {
        mFrost = on;
        mHint->setVisible(on);
        // the real text lives in mDesc and is ours to paint; while frosted the label is only empty
        // (hiding it would leave it without geometry: a hidden widget gets none from the layout)
        mLab->setText(on ? QString() : mDesc);
        invalidate();
    }

    void invalidate()
    {
        mPix = QPixmap();
        update();
    }

    QLabel* mLab = nullptr;
    QLabel* mHint = nullptr;
    QString mDesc;                                      // the description, our copy for the blurred paint
    QPixmap mPix;                                       // the blurred text, rebuilt on every text/size change
    bool mFrost = true;
};

}   // namespace

EncountersDialog::EncountersDialog(QWidget* parent, const QStringList& disabled)
    : QDialog(parent)
{
    setWindowTitle(tr("City events"));
    setMinimumSize(880, 560);
    if (kRowCount != kEncCount)
        qWarning("EncountersDialog: %d rows for %d exporter events", kRowCount, kEncCount);
    for (int i = 0; i < kRowCount && i < kEncCount; ++i)
        if (qstrcmp(kRows[i].id, kEncIds[i]) != 0)
            qWarning("EncountersDialog: row %d is \"%s\", the exporter expects \"%s\"", i, kRows[i].id, kEncIds[i]);

    auto* root = new QVBoxLayout(this);
    root->addWidget(new QLabel(tr("Random encounters over the exported world. The checked events ride into the world "
                                  "as the \"osmc_enc\" datapack; the unchecked ones are not written at all. "
                                  "The same list is on the command line: --encounters --encounters-off <id>,<id>…")));

    auto* split = new QHBoxLayout;
    root->addLayout(split, 1);

    auto* tree = new QTreeWidget(this);
    tree->setHeaderHidden(true);
    tree->setColumnCount(1);
    tree->setRootIsDecorated(false);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setUniformRowHeights(false);
    for (int i = 0; i < kRowCount; ++i)
    {
        const QString id = QLatin1String(kRows[i].id);
        if (kRows[i].group)
        {
            auto* head = new QTreeWidgetItem(
                { QCoreApplication::translate("EncountersDialog", kRows[i].group) });
            head->setFlags(Qt::NoItemFlags);                       // a section title: no box, no selection
            QFont f = head->font(0);
            f.setBold(true);
            head->setFont(0, f);
            tree->addTopLevelItem(head);
        }
        QString title = QCoreApplication::translate("EncountersDialog", kRows[i].title);
        if (!encImplemented(kRows[i].id))
            title += QStringLiteral(" [IN PROGRESS]");     // the logic of this event is still coming (export/encounters.cpp)
        auto* item = new QTreeWidgetItem(tree, { title });
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, disabled.contains(id) ? Qt::Unchecked : Qt::Checked);
        item->setData(0, kIdRole, id);
        item->setData(0, kDescRole, QCoreApplication::translate("EncountersDialog", kRows[i].desc));
        item->setData(0, kHintRole, QCoreApplication::translate("EncountersDialog", kRows[i].hint));
        item->setToolTip(0, id);                                   // the --encounters-off key and function name
        tree->addTopLevelItem(item);                               // after its group head, if any
    }
    tree->setCurrentItem(nullptr);                                // the selection starts empty: only boxes are on
    split->addWidget(tree, 5);

    // the right column: the legend is read as is, the practical note hides behind the frost
    // until its plate is clicked (the flavour of docs/city-encounters.md)
    auto* right = new QWidget(this);
    auto* rl = new QVBoxLayout(right);
    auto* art = new PlainLabel(right);
    art->setWordWrap(true);
    art->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    art->setTextFormat(Qt::PlainText);
    art->setMargin(10);
    art->setText(tr("Select an event on the left."));
    {   // the legend is the author's voice - a notch bigger than the practical note below it
        QFont f = art->font();
        f.setPointSizeF(f.pointSizeF() * 1.15);
        art->setFont(f);
    }
    auto* pane = new BlurPane(right);
    pane->setVisible(false);                                // the frost plate waits for a chosen event
    rl->addWidget(art, 1);
    rl->addWidget(pane, 1);
    split->addWidget(right, 6);

    auto* buttons = new QHBoxLayout;
    auto* allOn = new QPushButton(tr("All on"), this);
    auto* allOff = new QPushButton(tr("All off"), this);
    buttons->addWidget(allOn);
    buttons->addWidget(allOff);
    buttons->addStretch();
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->addWidget(box);
    root->addLayout(buttons);

    connect(tree, &QTreeWidget::currentItemChanged, this, [pane, art](QTreeWidgetItem* cur, QTreeWidgetItem*)
    {
        if (cur && !cur->data(0, kIdRole).toString().isEmpty())
        {
            art->setText(cur->data(0, kDescRole).toString());
            const QString hint = cur->data(0, kHintRole).toString();
            pane->setText(hint);
            pane->setVisible(!hint.isEmpty());
        }
        else
        {   // the legend carries the welcome line, the frost plate rests until an event is chosen
            art->setText(QObject::tr("Select an event on the left."));
            pane->setVisible(false);
        }
    });
    connect(allOn, &QPushButton::clicked, this, [tree]
    {
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->flags() & Qt::ItemIsUserCheckable)
                tree->topLevelItem(i)->setCheckState(0, Qt::Checked);
    });
    connect(allOff, &QPushButton::clicked, this, [tree]
    {
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->flags() & Qt::ItemIsUserCheckable)
                tree->topLevelItem(i)->setCheckState(0, Qt::Unchecked);
    });
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    mTree = tree;
}

QStringList EncountersDialog::disabled() const
{
    QStringList off;
    for (int i = 0; i < mTree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* item = mTree->topLevelItem(i);
        if (!(item->flags() & Qt::ItemIsUserCheckable)) continue;             // a group head
        if (item->checkState(0) != Qt::Checked)
            off << item->data(0, kIdRole).toString();
    }
    return off;
}
