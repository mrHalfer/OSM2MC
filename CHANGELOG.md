# Changelog

All notable changes to OSMC are written here, newest first. The format follows [Keep a Changelog](https://keepachangelog.com/);
versions are `1.0.YY.MMDDN` (`N` is the number of the build of the day, see [DEVELOPING.md](DEVELOPING.md)).

## [Unreleased]

### Added

- The rules book pedestal is baked into the world by the exporter itself: a glowstone lamp and a
  lectern facing the spawn, with the book written by the same pass that fills churches (author
  "the city"). The pack repairs the pedestal and re-pages the book on every load, so a taken book
  comes back and a Russian world overwrites the English book the chunk itself carries.
- A short console knob for the dread of the city: `/function osmc_enc:dread/s25` - one function per
  level the night reads (s0, s25, s50, s75, s95, s100), and the scoreboard command
  `/scoreboard players set #dread osmc_enc_t 25` works outright; the rules book near the spawn tells
  about the knob too (function macros died in 26.x, so every level got its own generated function).
- The ground around every portal of a military base is scorched into an irregular stain of netherrack
  streaked with nether gold ore (a request): the mark of a big piglin exodus out of the Nether. The
  stain eats bushes and grass but never the fence, the roads, the water or a building.
- The "City events" window carries a practical note under the legend - the window of hours, the
  chance, how the event ends - on its own frosted plate: the same click that cleared the old plate
  now clears the note, while the legend above stays open (the two texts stopped sharing one plate).
- The events pack speaks the interface language: an export made under a Russian interface writes the
  player-facing texts of `osmc_enc` (the night's chat lines, the sentences of the fear levels, the
  rules book) in Russian; every other language keeps the English originals.

### Changed

- The night counts broken glass through the game's own statistics, not through a list of coordinates:
  the baked window census is gone, the pack reads the scoreboard criteria `minecraft.mined:minecraft.glass`
  and `minecraft.mined:minecraft.glass_pane` (a user's finding). Every window or pane a player breaks
  raises the dread within a second of the break - all glass of the city, not the sampled half of it -
  and every counted break answers with a soft glass sound to the neighbours; one time in twelve the
  night whispers that the sound travelled further than the glass. The counters stay readable: the
  debt of a player lives in `osmc_glass`/`osmc_pane` until the next tick.
- Monsters calm the city through the game's own kill statistics, not through an advancement (a user's
  world never granted the kill deed): the pack keeps the hostile list itself - one
  `minecraft.killed:<kind>` objective per monster kind (`osmc_k_*`; totalKillCount stays unread, it
  counts peaceful animals too) - and every tenth counted kill takes one point off the dread, the
  remainder of the pool waiting across the nights. The objectives reset on world load, so a pack put
  into a lived-in world cannot cash the player's past kills into tonight's calm.
- City events are not a lottery over the whole map any more: every event rolls its dice on evenly
  spaced slots of its hours — the dread tier buys both the rolls (one per tier plus one) and the
  chance (ten percent per tier) — and every winning roll wakes exactly ONE place: the places of its
  kind answer through marker entities buried at the anchors, the ten nearest to a player within 300
  blocks try first, and only when nobody near woke does a second search reach a thousand. The night
  waits while nobody is online — the slots are spent in front of a witness, not at an empty screen.
  A woken place is busy till dawn, so two legends never share a twenty-block corner. Pumpkin faces
  left the cycle watch too: every jack near a player rolls for itself every five seconds, the dread
  tier buys the faces turns, a turned face rests a minute.
- Portals of a military base now grow from the center of the contour outward instead of parking by the
  wall, and no two of them stand closer than 100 blocks to each other (the owner's game check).
- Windows of buildings on a military base are tinted glass now - the dark glass of a guarded zone.
- The dread of a city is counted from PLAIN glass only: the black stained panes of bandit lairs and the
  tinted windows of military bases no longer raise or calm it, and the statistic criteria of tinted
  and stained glass stay unread by the pack.
- An active Nether portal now burns across the whole inside of its frame (a 2x3 sheet), not just the
  lower two rows - both the forest ruins and the portals of the military bases.
- Benches along park paths stand one block back from the paving (a free gap cell between the path and
  the seat) and look at the path: a stair block faces its BACK side, so seats whose back stood to the
  path turned around (the street benches of the OSM points got the same turn).
- Dread calms down over deeds, not over sitting: a glassed flat and a slept night no longer pay
  anything at all. Instead: a torch lit in the dark takes a point off (once in five minutes), a church
  bell takes three, a finished trade one - the two together at most three times a Minecraft day per
  player, midnight renews the count; every tenth monster laid to rest takes one, and a tamed cat or
  dog takes three, as often as kindness goes. The fog park does not read the fear of whoever enters
  - it sets it to twenty-five.
- The bench not for everyone: now the fog itself walks the city, about every three minutes taking one
  oak bench within fifty steps of a player - its seat turns dark. Standing three seconds before such
  a bench sinks the sitter into the fog park; the way out waits at the park's middle bench, and dawn
  lets go of the bench the fog held.
- Floor nine and a half is fifty-fifty now: behind the door stands either a hall with a chest (nine
  times of ten an ordinary one, once of ten a chest of the mansion) or a company of five to fifteen
  monsters (and once of ten instead of the company there is only a warden). The lot is any mob but the bosses, the
  creepers and the raiders - the Nether folk included. The floor promises each guest one of its four
  doors - chosen blind on every visit, and only that one door carries back to your flight; by morning
  nothing stays on the half floor.

### Fixed

- The rules book pedestal searched near (0, 0), not near the world's real spawn (a user report: no
  book by his spawn, which the editor had marked): the search read the spawn coordinates before the
  exporter had decided them. The spawn point is now resolved before the world assembly, so the
  pedestal and the book land beside the marked spawn even when it sits far from the map center.
- The lighting deed paid exactly once (a user count): its re-arm revocation was not honored by the
  version, and a player whose deadline score had never been set failed the cooldown check silently.
  The deed function now seeds the missing deadline with zero, and the cycle re-arms every calm deed
  for each online hand once per ten seconds as a backstop - a double re-arm cannot pay twice.
- The cyan terracotta trim of concrete facades (corner pilasters, the parapet ring) exported as
  stone (a user report): the trim looks its block up in the palette by name, the palette did not
  carry `minecraft:cyan_terracotta`, and the miss landed below the palette - in stone. The palette
  now holds cyan terracotta (87, 91, 91).
- The rules book pedestal could be missing outright (a user report, his built-up city map): the
  pedestal search demanded free ground within five steps of the spawn, and every ring cell was a
  trunk, a post or a parked car. The search now reaches 24 steps over clean ground and 48 steps
  where the only clutter is one block above the surface (tall grass, a flower - the lamp replaces it).
- In worlds exported under a Russian interface the rules book on the pedestal stayed English: the
  book baked into the chunk is written in English only, and the pack restocked the lectern solely
  when the page went missing. The pack now re-pages the book on every load in its own language.
- Broken windows went unheard by the city (a user counted his breaks in survival with the dread
  score unchanged): the old census watched every second house only, up to four plain glass blocks
  per house, and glass panes never. The game's own mining statistics replaced the census (see
  Changed), so the dread now grows on every broken window and pane with a second.
- Nether portals did not appear in the worlds at all: the block was baked as `minecraft:portal`,
  which is not a block id in either target (the real name is `minecraft:nether_portal` in both
  1.21.x and 26.x - the vanilla jar's blockstates say so), so the palette entry dropped on load and
  an "activated" forest ruin or base portal never looked lit.
- The butcher's barrels and the house stoves stocked the meat under its pre-26.x name (`raw_beef` and
  friends), and 26.x dropped those stacks as unknown items on load; the barrels and stoves now carry
  `beef`/`porkchop`/`chicken`/`mutton`, while 1.21.x worlds still get the `raw_` names.
- **The whole events pack never ran in 26.x worlds**: the two commands that drive everything
  (`load` and `cycle`) failed to parse, so the night said nothing and no rule was kept. The game's
  own error log named the six culprits: the trade statistic criterion (the trade calm rides the
  `villager_trade` advancement now), the retired `moisture` particle (the fog park breathes `cloud`
  instead), the positional options of `dust` (the pack writes the component form `dust{color,scale}`),
  the `soul_fire` particle (`soul_fire_flame`), an `s` suffix on an effect duration, and the removed
  `polished_calcite` block (the flashing lamps switch `calcite` now).
- Seven sound events the pack plays do not exist in 26.x (renamed there, or never were - checked
  against the game's own sounds list), so those moments of the night were silent: the replacements
  are `beacon.activate`, `beacon.power_select`, `redstone_torch.burnout`, `metal_pressure_plate.click_off`,
  `wooden_door.close`, `elder_guardian.curse` and `wolf.growl`.
- The rules book never came back after being taken: an empty lectern still carries its `Book` tag,
  so the "book missing" guard never fired; it reads `lectern[has_book=false]` now.
- **Worlds with events opened in safe mode in 26.x**: the advancement registry aborted on the deed
  filters - an entity tag sat in a field that parses as a plain entity id, and the tame file missed a
  closing brace - so the game refused every datapack and offered `--safeMode`. The four deed
  advancements now carry the shape of their format family: 26.x reads a trigger filter as ONE
  condition dispatched by `type` with namespaced predicate keys, 1.21.x as an ARRAY of conditions
  dispatched by `condition` - spelled exactly as the game's own vanilla advancements spell it, with
  the monster list riding the entity_type tag alone; and `pack.mcmeta` states the real range in both
  key notations, so the events pack loads from data pack format 48 (1.21.0) instead of only 82 up.

## [2.1.26.11241] — 2026-11-24

> **Events are in test mode.** Every event of this release is playable and every world keeps its
> night, but the set of events, their scenes and balance are still an experiment - a later release
> may rework them. Without the checkbox (or `--encounters`) the events write nothing into the world.

### Added

- Random city events (off by default): the "City events (random encounters)" checkbox in the window and
  `--encounters` on the command line put an "osmc_enc" datapack into the world, write a `sites.json`
  registry of the exported places (parks, schools, graveyards, metro, tower living - the code, layer and
  catalog name of every site with its blocks) and drop an invisible marker entity at the center of each
  site, so the datapack finds places with tags from inside the world. The "Events..." window lists every
  event (pursuers, park and yard scenes, graveyard and metro scenes, street legends, bonus legends) as a
  checkable row; choosing a row opens its description behind a frosted blur that a click clears.
  Switched-off events are not written. On the command line: `--encounters-off <id>,<id>`.
  The event logic grows release by release - the pipeline, the dispatcher and a demo event ship now
- The first real event logic in the pack; the window marks every event that is not implemented yet with
  "[IN PROGRESS]": a street lamp blinking through the night, a rain cloud pouring over one street while
  the sky stays clear, a chalk arrow on the asphalt erased and redrawn overnight in a new color, a
  playground swing riding with an invisible rider, jack o'lantern faces in a graveyard turning toward a
  far witness, and a sitting cat council at a bus stop that scatters when you come too close (and goes
  home at dawn). The exporter bakes the exact coordinates of every lamp, bus stop, playground, flat road
  patch and graveyard jack into the generated functions at export time; without `--encounters` the world
  stays byte-identical to the one built before this feature.
- Six more events with real logic: a mast lamp at a crossroad blinking until someone comes to look,
  the windows of one house lighting up row by row bottom to top and going dark all at once, her
  cabinet appearing by a school door while a silhouette walks to it, a yard scarecrow that looks away
  from the house by day and watches it by night with a board's warning, a fresh grave mound whose soil
  gives itself back when dug, and a graveyard bell striking 03:07 seven times even after someone broke
  it. The exporter now also bakes crossroads, tall glass facades, private yards, school facades and
  flat graveyard ground into the pack.
- Four more events with real logic, now finding their places in real cities: the night rider whose
  ignition is a pressure plate lying by a parked car's door (step on it and the engine turns over in a
  car nobody owns, and his "garage" - a fresh oil stain - appears on a dark lot across the quarter;
  reach the lot and the car gets bored), the tall shadow who circles one street lamp and trades places
  with himself while you look at his light, letting go only when the sun takes the street, route 13
  flickering past a bus stop that never had the route - and whoever stands at that stop very still
  with a clock in hand photographs it into a legend, so the stop stays quiet for good - and the windows
  of the matron's own office catching light on her night rounds until somebody finds her cabinet. The
  exporter now bakes its own street and park lamp posts (the OSM "lighting mast" node is almost absent
  in real extracts), parked cars with their door cells, bus stops and swept school facades as anchors
  for these events.
- The night fountain in the yard: when somebody stands by it after dusk the bowl empties in a single
  tick - the whole water column at once, so nothing keeps spilling through the quarter - leaving a dark
  "mouth" in the pool that flickers with portal light for half a minute before the water climbs back
  into the very same cells, colder. The exporter bakes every water cell of the fountains it builds,
  which is also why a city without fountains simply never gets this event.
- The last train: around 23:00 a train the exporter dug himself walks his tunnel one cell every two
  seconds - the platform of one line station, three floating minecart-cars, the headlights sweeping
  every hostile shadow off the platform - and just past the hall it carries whoever stood on the
  platform to a secret room at the end of the line, beside somebody's chest. The morning hides the
  cars again. The exporter bakes the station halls, the tunnel chains and the secret rooms of its own
  metro as the route, so a city without a metro line honestly never gets this event.
- The bench not for everyone: sit long enough on one of the evening's chosen street benches and the
  city falls silent - you are sitting in a fog park that is on no map, in a small void dimension of the
  pack's own, under a dusk that never ends, beside a pond and trees wrapped in pale fog-colored leaves.
  Stand up from the park's bench and the traffic comes back, the seat here cold as it should be. The
  park is rebuilt from scratch on every world load, so it was always there; only the benches the fog
  picks are new each night.
- The ninth-and-a-half floor: after midnight the stairwell wall of a tall residential tower grows a
  door that is on none of its plans, and whoever opens it from the flight stands on the same landing
  mirrored - windows into the void, doors onto nothing - till any opened there walks them back to
  their own steps. The exporter picks the houses (every fourth of those eleven floors and taller) and
  bakes the real cells of the landing the door appears in, so the door always stands in a wall that
  really is there
- Parks now sit their own benches along the lit paths: about every eighth path cell gets a seat of two
  oak stairs on the lawn across, facing the path, wherever the two cells under it are really free -
  no lamp, no tree, no furniture, no fence, no water, no lawn stepping under the seat. The benches
  mapped as `amenity=bench` nodes stand where they stood, and the evening's fog now picks the park
  benches into its park along with the street ones

### Fixed

- The events no longer freeze their night in Minecraft 26.x: the pack computes its day clock as the
  remainder of `time query gametime` over 24000, because 26.x retired `time query daytime` with its
  clock rework and the frozen score left every night event asleep. 1.21.x keeps the old query
- The pack survives 26.x "Safe Mode" checks: the fog leaves, the flat dimensions and the dug soil of
  the fresh grave are now written in the shape each game generation reads, where before 26.x silently
  ignored the mismatched files (or took the whole pack down with them)

## [2.0.26.11191] — 2026-11-19

### Added

- A rules catalog for the export: every export layer is described by its own `rules/<layer>.json` - all its
  object codes (the typical height used when the map gives none, which structure to build at a point, the
  game effects: swamp, prison refuge, military base) and the mesh style of the layer itself (shadows,
  outlines, line widths, road markings, the material and lift columns). Releases ship the `rules/` folder
  next to the program
- The "Export rules" dialog shows a live "Resolution in the rules catalog" pane: what the code of the
  selected rule builds on its layer (family, effect, point figure, height mode and typical value, material
  column, road width and markings), refreshed while the rule is being edited
- A spot structure picker in the "Export rules" dialog: the "Model..." button lists the catalog point
  codes (bus stops, communication towers, monuments...) with an isometric preview of the figure each one
  builds; the choice fills the CODE of the rule (and the layer when the structure lives on another one),
  and the resolution pane shows the figure of the resolved code

### Changed

- **Breaking**: the reference files merged into the catalog - `default.t3d`, `heights.csv` and
  `point_models.csv` are gone and `osm_rules.json` moved to `rules/osmrules.json`; old setups are migrated
  by hand (v2.0 keeps no backward compatibility)
- **Breaking**: `.tqt` projects are not opened anymore (the program says so plainly); a folder with
  shapefiles stayed a command-line-only input (`OSMC <folder> -o world.zip ...`)
- `--t3d <file>` became `--rules <folder>`; the catalog is looked for next to the program, in the current
  folder and next to the source data; `--osm-rules` also resolves inside the found catalog
- The window keeps a single data row (the OSM file, next to the online map): the hidden project/folder row
  and the `--debug` option are removed; the "Export rules" dialog kept only the OSM rules tab - the
  3D-settings, point-models and heights tabs went with the files they edited
- The prison refuge and the military base overlays live on one "Territories" layer (`terpol`) now, the
  object code decides (53230000 prison, 53220000 military base)
- An object code the catalog does not describe is no longer dropped: the object keeps the generic behavior
  of its layer and the export log warns about the code
- The "As" field of the "Export rules" dialog offers a choice instead of free text: points are a Structure
  (the "Model..." button beside it picks the catalog figure with a preview; the leveled ground square of
  the old "square:N" stays behind as the rule's own value), an Area (a park or a playground
  marked by a point — a square N m of side) or a Crosswalk N m long; lines and polygons may trade geometry
  (a closed line becomes a polygon, a polygon its boundary line); the old "square:10"/"crosswalk:5" items
  were example leftovers, the number always meant meters

## [1.8.26.11181] — 2026-11-18

### Added

- A new "Grounds" source (`lcapol`): `natural=bare_rock` turns the ground under the contour into stone mixed
  with terracotta with rare magma outcrops, `natural=sand` turns it into forced sand with dune hummocks over the
  relief; trees do not grow under either contour (rare cacti and dead bushes grow on the sand)
- A new "Prison refuge" source (`pripol`, the prison effect bit): a bandit refuge — every building inside the
  contour becomes a raider lair whatever its tags are, the interiors and the chests follow the raiders, iron
  golem spawners keep the fight going, bandit patrols walk the ground, and the whole refuge is fenced with a
  stone wall like a military base is (the banner posts stand in the gaps of the fence)
- A new "Military base" source (`milpol`, the military effect bit): a strictly guarded territory — a four-block
  stone wall along the contour (a road through it stays a gate), a piglin garrison inside (seven in ten of them
  sun-protected, a quarter of them brutal piglin brutes, three in ten carry a crossbow), no ordinary residents,
  and the buildings hold piglin loot; the "Effect" column of the "Export rules" dialog offers both new bits

### Changed

- `landuse=military` no longer lands in `csrpol` as a fenced park with golem guards (code 53210000 is gone):
  a military ground is a separate "Military base" overlay now (code 53220000)
- A military base no longer turns into a park: by definition a `milpol` contour is not a park, a kindergarten
  or a village - no floating island (park or kindergarten/school) hangs over it, the flower_forest park biome,
  park lamps and a bell square never go inside the contour either. Everything else follows the cover: forests,
  meadows and lone trees grow on the base wherever the corresponding polygons lie under it
- The automatic greening pass adds parks only over large lawns (the code marking OSM squares and big flower
  beds); previously a city short of its park quota also grew parks from the largest empty lots near buildings,
  and a base parade ground could thereby turn into a full park with floating island and flower fields

## [1.7.26.11163] — 2026-11-16

### Added

- A new "Export rules" button (bottom-left of the main window) opens a dialog to edit the reference files
  visually, without a text editor: OSM tag rules (`osm_rules.json`), the layer sources with their effect and
  point-model/material columns (`default.t3d` - the tab shows only the fields OSMC really reads, the rest of the
  file is kept as is), point structure models (`point_models.csv`, with an isometric preview of the model of
  the selected row) and typical building heights (`heights.csv`). The next export reads the saved files right
  away

### Changed

- The 3D settings colors (`colorMode`, `singleColor`, `color1`, `color2`) no longer reach the exported world:
  they decorate only the tqt 3D window. OSMC paints the objects itself - the building walls (and the glass and
  panel material tinted by them) are the gray-blue concrete, the pedestrian road carpets are white; the other
  kinds were already decided by their code

### Fixed

- `--version` (and the window that shows it) prints the real program version instead of the placeholder "1.0"
- The "Export rules" dialog returns the OSM rule list to ascending "#" order every time it reloads the rules -
  a stray click on the "#" header used to keep the descending order across reloads and filters

## [1.7.26.11162] — 2026-11-16

### Fixed

- The `tmc_tall` build-height datapack now loads in every game of the 1.21 line, not only from 1.21.4: its
  supported format range now starts at data pack format 48 (Minecraft 1.21.0)
- A world saved in the 1.21.x format no longer loses the spawn point (a user report on 1.21.0): it now carries
  the classic SpawnX/Y/Z fields, which the older games read — instead they ignored our 26.x `spawn` compound
  and dropped the player at (0,0,0), in the generated land around the export

## [1.7.26.11161] — 2026-11-16

### Added

- TALL WORLDS: every exported world now carries a small `tmc_tall` dimension_type datapack that widens the
  overworld build range to Y=655 (the floor stays at the vanilla −64), and exported chunks write an
  impenetrable bedrock floor at Y=−64 — so a 700-metre mountain range stays a mountain, and cave
  complexes can never be dug out into the void
- Tunnels THROUGH MOUNTAINS: a car tunnel whose line stands under 9 or more rows of ground right behind its
  portals and crosses no other roadway now stays FLAT at the ground level of its ends — the tube bores straight
  through the hill and the portals are cut as door notches in the slope, instead of digging the road down under
  the street and climbing it back up

### Changed

- STEEP SLOPES STAY CLIFFS (a user request): on steep slopes the final ground height is snapped to a level
  lattice with a step that grows with the slope (1 block on gentle ground, up to 4 blocks on a cliff face),
  so a rock reads as horizontal treads with 2..4-block vertical faces instead of a long one-block staircase —
  the height profile is preserved, gentle plains and city ground are untouched
- The height matrix keeps up to 480 blocks of real relief (was 150), and the exporter compresses ground
  heights only above 460 blocks (was 240): a user-request Rio de Janeiro clip that used to export as small
  hills now exports the actual rock mountain; the whole export is raised or sunk so its LOWEST terrain point
  along the area edge stands at the ~68 generator level — flat and gently rolling cities are placed exactly
  as before, a mountain clip keeps its full height above the surrounding world instead of being sunk to fit
- Terrain relief keeps its detail: the downloaded height matrix is now blurred by ONE tile pixel (was an
  80-metre box applied twice) — the old blur planed mountain crests and small hills into gentle ramps, the
  Rio massif lost about 40 metres of its summit ridge to it
- The automatic Pale Garden choice now considers ONLY vegpol code 71111110 (dense tall forests): undergrowth,
  bushes and meadow sites on mountain slopes made ruined-looking terrain (a user request); a Pale Garden
  placed by the user's own chunk mark still accepts any of the previously allowed vegetation codes
- Parking lots are marked as a GRID of spots over the whole polygon (a user request): spot bands 12 blocks deep
  follow one another across the lot separated by unmarked aisles of the same width, so the middle of a large
  lot is no longer an empty field; the spot lines of parallel lot edges now share one phase and cancel where they
  meet instead of interleaving into double-density lines, cars fill the new spots as before
- A marked parking lot almost never stands empty now (a user request): when every spot of a lot refused its car,
  the lot retries all its spots once ignoring the 45..90% fill percentage — 90% of marked lots get at least one car
- A building tagged entirely as a shopping mall is no longer furnished mall ALL THE WAY UP (a user request):
  the racks, counters and trader crowds live on its first three floors, the storeys above furnish as an office
  building — merged or mistagged 20-storey malls no longer flood the world with loot and traders
- The export window is simpler (a user request): the "World file (zip)" field is gone (the zip goes next to the
  source data, only `-o` or a project file can name another path), and the service fields (.t3d, OSM rules,
  Enhancer, "real meters") are gone too — those stay command-line options; "Caves underground" and the new
  "Surface terrain" checkbox (on by default, switches the height matrix off entirely, leaving the terrain the
  game generates itself) now share one row, and the datapack checkbox reads
  "World datapack (for correct generation)" with a tooltip about the required experimental features
- In `-debug` the "Project / folder" row no longer REPLACES the OSM file row: the OSM file is always chosen in
  its own row, and the project/folder row appears below it as an extra service row
- The export-destination label now names the chosen instance: "Export: Modrinth - {instance}" /
  "Export: CurseForge - {instance}"

## [1.7.26.11141] — 2026-11-14

### Added

- The OSM `layer` tag MULTIPLIES a bridge's lift (a user request): a `layer=2` ramp now rides over a `layer=1`
  one — the stacked interchanges of Los Angeles; up to four levels, at most 40 blocks of lift all in all

### Changed

- Road interchanges and tunnels (the owner's test round) got their grades gentler and their details fixed:
  a viaduct climbs one block per eight cells of the line, a tunnel sinks one per ten (one per three for a
  foot underpass), and each climbing end may only spend its share of the line outside a reserved flat middle,
  so the deck and the tunnel floor always keep their plane somewhere; the earlier one-block-per-five-cells
  ramps of 1.6.26.11131 superseded
- A tunnel descent STARTS BEFORE the tagged section (OSM marks a tunnel only where the road has already left
  the satellite image): the open cut extends along the straight approach road up to 30 cells outward, down to
  the natural ground there, so a portal no longer opens in the middle of the surface roadway; the portal walls
  retreat until the floor meets the ground (a wide open basin you can drive into)
- When a viaduct climb still does not fit its line, its first up to two blocks MOVE ONTO the straight
  approach road in front of the bridge end — the roadway rises on a low embankment with the same half-blocks
  easing the steps; the export log reports the number of such lifted approach cells
- A surface roadbed running over a tunnel portal stays UNTOUCHABLE (the owner's choice): a covered stone-brick
  chamber goes under it instead of the open cut, so that tunnel may come out worse — the surface road never breaks
- Inside a tunnel the floor carries the line's own marking, and lanterns hang only deep in the dug span (from
  the eighth cell between the portals, the fourth for a foot underpass) — never above the start of a descent
- A tunnel sinks by its `level` as well, up to four 8-block levels deep (the README said 1..3, the exporter
  always read 1..4)

### Fixed

- Parapets no longer stand ACROSS a roadway: a wall only rises where the neighbour of a deck cell is open
  ground, not a ramp step of the same way, the other tube of a dual carriageway or a tunnel (the old rule
  fenced every ramp of a climb row by row)
- No pier or girder embankment of an upper deck now lands on the deck or ramp of a lower one — a `level=2`
  column used to stand right on the `level=1` roadway of the same interchange

## [1.6.26.11131] — 2026-11-13

### Added

- Multi-level road interchanges and road tunnels from OSM tags: a way with `bridge=yes` now rises on a viaduct
  above the ground and the other roads — a FLAT deck at least five free blocks over the highest ground under it,
  of the roadway material on a stone-brick girder, with piers along the span, stone brick wall parapets and the
  marking on the deck; lines that continue each other join in the air, and a `level=2` way starts its climb from
  the deck of a `level=1` one. A way with `tunnel=yes` sinks into an underground road that keeps a single FLAT
  floor under the lowest ground along the joined line (the tunnel never repeats the relief): a lit four-block
  interior under a stone-brick lid with four rock rows over it; tunnel lines that continue each other join
  underground, and the ends that reach no tunnel ramp out through open-cut portals. The grades are gentle (one
  block per five cells) and every step of a climb is eased by two half-block slabs in a row, so a ramp can be
  driven up and down; the `level` tag picks how many 8-block levels to rise or sink (1..4, default one)

### Changed

- Game balance for loot and shop fittings: a barrel anywhere (a house, a shop, a warehouse) now holds no more
  than four items — the goods are laid in by the exporter itself from the same village themes, since the game's
  generator gives up to five stacks and heaps of up to 21 items in one. Mall racks and weapon stands are built
  of light gray concrete instead of planks and bookshelves (harder to farm), and golden apples and golden
  carrots on the stands became three times rarer
- Building platforms blend into the relief gently: the margin and the ramp are smoothed until no step wider
  than one block remains around the house — the platform could perch two blocks above the ground on its edge

## [1.6.26.11122] — 2026-11-12

### Added

- "3D figures" button in the window (a user request): it opens the model list of landmarks.json with a voxel preview
  of every model rendered at its standard size, and a button to load any STL file — one that parses may be placed too.
  A click on the map stands the figure there; its mark opens the settings of THIS copy: the block it is built of
  (typed by hand or picked from the list), the azimuth and the size, and the placement "on the roof" / "in place of
  the building". The per-instance settings never touch landmarks.json or the model defaults; the placed figures are
  saved into the .tmc project file and stand again when it is opened.

### Fixed

- Landmark placement "instead" left the building standing: the clearing wrote air with set(), which the voxel
  storage silently ignores — the model stood inside the building it was supposed to replace (1.6.26.11121).

## [1.6.26.11121] — 2026-11-12

### Added

- Block-built playground structures imported from .schem files (a user request): a BIG children's playground gets one
  of them (the small ones keep the exporter's sandboxes and carousels), and a sports ground with a marked court gets
  one into the free area outside the court marking. The structures keep their exact block grid in four rotations and
  are stored in `playground_templates.bin` next to the other templates (no file — the procedural structures are used).
- Landmark 3D models from STL files (a user request): the exporter reads `landmarks.json` (the file, the real
  longitude/latitude, the height in blocks, the material, the yaw and the placement of every landmark), voxelizes the
  binary or ASCII STL at that height and stands the model at its place. The `placement` knob: "roof" (default) stands
  the model on the roof of the building its centre fell on, "instead" razes that building and stands the model on the
  lot. The material may name any block: a name outside the built-in palette joins it at runtime. The models are z-up;
  the shell is sampled and the interior filled, so a closed mesh comes out solid. The Statue of Liberty and Christ the
  Redeemer are included (see `THIRD_PARTY_NOTICES.md`).

### Changed

- Every building stands on a flat margin of two blocks at its own level, and only then the ground ramps back to the
  terrain (a user request): the house no longer meets the relief with a step right at its wall. The ramp length
  follows the real height difference (one block per step where possible), and roads/railways are never touched by the
  platforms — the roads stay the most important thing on the map.
- The terrain noise is calmer near the buildings and on gentle ground (no abrupt two-block steps in the residential
  quarters, the plain relief is stretched) and stronger far from everything, where the wild ground stays hilly and
  mountainous according to the height matrix (a user request).
- Parking lots: the marking lines are drawn from the LONG edges first, so on a narrow lot the lines run ACROSS it
  instead of along it — many more spots for cars; a narrow lot may take a shorter line (from 6 blocks) when the full
  12-block one does not fit.
- The half-slab ledge on a road step is extended two cells downhill along the road (with the marking and the curb),
  so a one-block rise is spread over three cells instead of one (a user request).
- The ground stage is several times faster (a user request: the speed of the ground and vegetation stages): the
  height matrix is resampled into the project CRS once, instead of a PROJ transform for every column. The transform
  ran under a mutex, so the parallel ground fill was effectively single-threaded; on the Электросталь export the
  ground stage went from 62 s to 9 s and the whole export from 78 s to 26 s (Cuba: 271 s to 77 s and 401 s to 204 s).

### Fixed

- The powered rails of the imported playground structures turned the wrong way after a rotation: the rail `shape`
  property (`ascending_north/east/south/west` and the curves) is now rotated together with the structure.

## [1.6.26.11111] — 2026-11-11

### Added

- Real terrain from open elevation data (a user request): the online mode gets a **Terrain (elevation data from AWS
  Terrain Tiles)** checkbox (on by default). Together with the OSM extract it downloads the open height tiles for the
  boundary and builds a height matrix next to the extract, named after the data file
  (`osm_city_20261111_120000.osm.tif`; a plain `DTM.tif` is still found): the matrix is normalized so its minimum is
  exactly the relief the exporter draws today, and everything above it rises - mountains and canyons appear (up to
  150 blocks), while the small relief is smoothed away and left to the existing terrain noise. The tiles' required
  attribution goes into the matrix metadata and into the world's `ATTRIBUTION.txt`. The download waits 15 seconds for
  the FIRST response of the tile server; once a tile has arrived it continues normally, and an unreachable server
  (typically blocked without a VPN) leaves the world without terrain (the window warns about it), while the area
  itself is still downloaded.
- A terrain-only export (a user request): when no OSM object matches the rules of the loaded area but a height matrix
  is there, the terrain itself becomes the world - mountains and canyons are exported without buildings.
- The project file is suggested under the name of the opened data file (`osm_city_..._120000.tmc`) instead of
  `project.tmc` (a user request).

### Changed

- The terrain is interpreted, not copied (a user request: "blindly following the contour lines looks bad"): the matrix
  gives only the shape - a slope map (8-block cells) decides how rugged the ground is, gentle ground becomes a wavy
  plain, steep ground gets ridged noise, so mountains and cliffs keep their place and the DSM's regular contour
  terraces disappear into the noise. The matrix itself is read with bilinear interpolation.
- Buildings stand on the HIGHEST point of their footprint (a user request: the median left half of a first floor
  underground): the scene puts the object there and the exporter fills the ground under it up to that one level.
- Playgrounds, parking lots, courtyards, squares and point objects stand at the AVERAGE ground level of their area:
  that level is spread over the pad itself and a flat margin of two blocks around it, then a smooth ramp returns to
  the terrain (no wall of earth at the pad's edge). The level is computed when the terrain is ready — a pad used to be
  measured before the terrain sampler was initialized and flew up to the build height limit (255).
- Roads and railways grade the relief (a user request: the contour lines cut across a road even along it): the track
  surface is smoothed across its width and along its axis, the ground under it is cut/filled to that level, and on
  both sides there is a FLAT margin of two blocks at the track's own level before the ground returns to the terrain.
  Railways are graded exactly like the roads, so a track lies on top of the relief instead of cutting into it or
  vanishing under it.
- The height steps of the roads are smoothed with slabs of the road's own material and color (a user request): a
  gray concrete slab on the motor roadway, a light gray concrete slab on the pedestrian roads, a white wool slab on
  the markings and a light gray wool slab on the curbs. A bottom slab on the lower cell halves a one-block step; a
  step of several blocks gets a half-slab ledge per level (dirt/podzol roads have no matching slab in the game).
- Roads inside the Pale Garden are not graded (a user request): they follow the garden's own relief with its steep
  steps. The grading runs after the Pale Garden is chosen (it is picked in buildGround) — before, the garden's roads
  were graded by mistake and cut a flat strip across the forest.
- Road markings, crosswalks and the curbs along the roadway are placed on the final ground: a marking left at the
  ribbon's level used to sink into a road that the terrain had raised.

### Fixed

- The export no longer crashes on an extract where an OSM way crosses far beyond the downloaded area (a national
  park, a long river): the flat patches (vegetation, water, sites) are rasterized only inside the export area; a
  huge polygon used to eat gigabytes of memory.
- The flattened platforms and the graded roads are lifted together with the world (the `yShift` of the "normal
  surroundings" mode): in high mountain areas the ground under the houses and sites was left at the unshifted level
  and stood as huge earth columns above them.

## [1.5.26.11101] — 2026-11-10

### Added

- High-rise facades read better (a user request): residential and office towers stand their window rows on a
  sill course of a stone that contrasts with the wall colour. The wall material from the OSM tags stays
  everywhere else; schools,
  kindergartens, malls and private houses are untouched.
- Flat roofs gain rare rooftop gear (a user request): a stair hut with a slab top
  and a plate doormat, a vent pipe under a slab cap, a TV antenna with horns, a satellite
  dish - small, sparse, never on small roofs.
- Apartment blocks and offices grew dangerous (a user request): on roughly every fifth floor a random room
  holds a creeper - schools, kindergartens, hospitals, churches and malls stay safe.
- Cemetery grave rows get two new gravestone shapes: a small polished obelisk (diorite or granite, some with a
  lantern on top) and a broken cross stump; the old two-block stone wall remains the most common gravestone, and
  the stone crosses between the dark oaks are untouched.
- Parking lots are no longer empty (a user request): the marked spots between the white lines now hold exact copies
  of fifty-nine donor city-map vehicles - sedans in eight colours, an SUV, campers, pickups, hot rods, police cars
  and delivery vans, motorcycles, ATVs and go-karts, taxis, jeeps, an ambulance, a firetruck, police and garbage
  trucks, food trucks, tractors and a couple of construction machines. Every car is the donor block grid re-saved as
  our template (car_templates.bin), with the donors' panes, slabs, stairs, buttons and carpets exactly where they
  stand in the donor, kept in four rotations so cars park nose-in or back-in per spot; the long ones park pulled
  towards the aisle. A TNT block is baked into the middle of every car: the world's datapack (the same
  one that answers /locate) relights a TNT core punched loose right in its socket (only car cores: any other TNT in
  the world behaves as usual), so breaking carelessly into someone's car blows it up together with the burglar.
  Every lot gets its own fullness (from half empty to packed), and a spot that refuses its first car tries two
  more; a car needs the whole lot floor under its
  footprint flat and free, so lots with tilted edges stay car-free. The cars reuse the cave block-state registry,
  so they appear in worlds exported with caves.
- Flat roofs are readable from above (a user request): the roof cap is now a dark membrane concrete (a light one
  where the facade is dark, so the contrast always holds) ringed by a parapet of the wall material; stepped
  building complexes fence every tier. Private houses, churches and domes keep their own roofs untouched.
- Sports grounds and children's playgrounds get one featured build each (a user request): in a free part of the
  site outside the courts stands a play tower with a platform, a lantern roof and a hidden chest, a log fort with
  a gate and a fenced parapet walk, or a plank bridge between two towers - the small equipment fills the rest.
- Three real green ruined portals (non-snowy, temperate-grass originals with crying obsidian in their frames)
  extracted from fresh donor worlds join the forest ruins, so temperate forests get true green ruins instead of
  only the snow-stripped copies.

### Changed

- The `.bin` structure templates moved out of the program folder into a new `assets` subfolder next to the
  executable - the release packs read cleaner (the old layout with the files straight next to the program keeps working)

## [1.5.26.11091] — 2026-11-09

### Added

- **Ruined portals cut out of a real game world** (a user request): ruined portals are extracted from generated
  Minecraft worlds (tests/tools/extract_ruins.py, PGD2 patch) and rarely placed in forests from 5000 m2 - a snowy
  portal goes to taiga forests, and a snow-free version of the same ruin to forests of any type. Each one brings its
  netherrack apron, magma, obsidian frame and a chest holding the real `ruined_portal` loot.
- **Spruce (taiga) forests are livelier** (a user request): big spruce forests now get three times as many abandoned
  camps (bandit outposts are left as they were), and their undergrowth is rich - berry bushes stand ripe (with
  berries on them, not empty shrubs) and are ~3.5x more frequent, mushrooms are ~3x more frequent.

### Changed

- **OSMC is now licensed under the GNU GPL v3** (GPL-3.0-or-later, was MIT): the program stays free, but any modified
  or derived version that is distributed must remain open under the same license (copyleft). Commercial licensing is
  available from the copyright holder on request.
- **Villages and the private sector are greener** (a user request): the single trees on the empty lands (where the
  map has no vegetation at all) are ~2.7x more frequent near the private sector - within 48 blocks of a private
  house, as long as no multi-storey building (5+ floors) stands within 40 blocks and the climate is not the desert.
- **The fences from OSM `barrier=fence` lines are now two-storey stone brick walls** instead of one-storey wooden
  fences (a user request); the lower wall of a two-storey run renders as one continuous two-block fence.
- **Private houses are straightened to the block axes only when it is safe** (a user request): the snap is skipped
  when the turned footprint would land on a building of another type. Houses of the private sector may still
  overlap each other (they merge into one building) and may climb over a shed (44000000) - only that is allowed.
- **Windows on the staircase walls of rotated private houses** (a user request): a window may now stand on a step
  of a rotated footprint and is cut through the WHOLE wall thickness with glass (not a "shiny facade" with a wall
  right behind the pane); the walk stops at the interior air. Only the true corners of the outline stay blind (they
  are taken from the mesh hull, not from the raster); a rare thin tooth may still end as a single pane.
- **A fence is stronger than a road**: the roadway no longer erases fence lines - where a widening or a wide ribbon
  of a road would step onto a fence, the road width is trimmed instead. A road may still cross a fence: where the
  road axis pierces the fence line, the width is kept and the fence itself breaks there.

### Fixed

- Fixed the axis snap of private houses (a user report: "the houses are still barely straightened"): the turn angle
  was computed from an arbitrary mesh edge (the mesh vertices are not ordered as a polygon) and then passed in
  degrees to `cos`/`sin` without converting to radians - so the houses were turned by a random angle. The angle now
  comes from the longest edge of the convex hull of the outline and is converted to radians.
- Fixed the lit jack o'lanterns on cemetery posts being saved as unlit furnaces (a regression introduced in
  1.5.26.11081 together with the house stoves: the furnace block range overlapped the jack o'lantern rotations).

## [1.5.26.11081] — 2026-11-08

### Added

- **Wooden fences are built from OSM lines tagged `barrier=fence`** (a user request). The line is laid out as
  a fence of oak fence blocks along its axis; lines that touch each other, csrpol site fences and house walls
  join into one continuous fence (the connection properties are stitched in one pass). The fence skips
  roadways, rails, structures and water - where the barrier breaks in OSM, it breaks in the world too.
- **Brick private houses now have a furnished mansard.** The second floor of a brick house gets a bed, bookshelves
  and a table; kitchens, workshops, residents and pets stay on the 1st floor. Earlier the upstairs was left an
  empty attic — and houses whose gable stands only one or two rows above the eaves (the common OSM case) now get
  a livable low mansard too, since a bed and a shelf fit under the rafters. Carpet compositions now also dress the
  1st floor of a private house, wherever the furniture leaves free space.
- **Every private house has 1–2 working stoves on the 1st floor** (a user request). The furnaces stand along the
  walls with their fronts facing the room, and each one is filled through its block entity: 1–4 pieces of coal or
  charcoal in the fuel slot, and in about half of them some raw food (beef, pork, chicken, mutton, potatoes,
  carrots) in the cooking slot. These are the only stoves of a private house: the random kitchen corners no longer
  add extra furnaces there, so a house never gets more than two.
- **A basement mob nest now has a real spawner block** in the middle of the room — the pile of mobs is no longer a
  one-off, the block keeps spawning its monster: a zombie, a skeleton, a spider or a witch (the witch joined the
  spawner types for this).

### Changed

- **Half of private houses now have a basement** (earlier it was about every third): the private sector got
  noticeably more hatches and cellar mob nests.
- **No monster spawners stand above ground in private houses any more** — in the private sector they live only in
  the basements now. Public buildings and utility sheds keep theirs as before.
- **Private houses split about half-and-half between brick and logs.** Earlier a house was brick only when its
  walls could hold a real second storey (about 9 block rows), so almost every house came out logs. A brick house
  is now any house with a second storey (wall height >= 9) OR a big one (>= 6 rows and a footprint of about
  200 m2 or more): the slab and the mansard fit there even though the gable is short. Small and low houses remain
  log ones with a plank ceiling, as before.

### Fixed

- **Private houses rotated off the axes are now straightened reliably.** The rotation had the wrong sign for
  footprints tilted more than 45°, so those houses were turned FURTHER from the axis instead of toward it; and
  footprints leaning on the north-south axis were silently skipped altogether. Every tilted private house except
  the almost axis-aligned ones is now snapped to the nearest axis, which restores the window grid on its facades.
- **Electrified rails are now active in the exported world.** A powered rail stood on its redstone block but was
  written with `powered=false` — and a chunk loaded from the world file gets no block update, so the rail stayed
  in its braking state until something nearby changed. The block state now carries the power itself: the rails
  glow and boost minecarts right after the world is loaded (metro tunnels, `electrified` railways and trams).

## [1.5.26.11073] — 2026-11-07

### Fixed

- **Exports with a boundary polygon (`--clip`) no longer build the whole data rectangle.** Before, the ground,
  trees, turf and every planting stage were built for ALL chunks of the noise-data rectangle and the polygon was
  applied only when writing the archive — hours of work and gigabytes of memory on big clips (the peak could reach
  tens of GB). The fill set is now clipped to the polygon right before the heavy stages (keeping boundary
  buildings and small lakes whole and the crowns of boundary trees exactly as before); the exported world is the
  same. A small clip went from ~13 minutes to ~13 seconds, a big one from ~39 minutes to ~3.5 minutes, the peak
  memory fell from ~26 GB (and ~51 GB of commit) to ~0.4 GB and ~8.6 GB respectively.

## [1.5.26.11072] — 2026-11-07

### Added

- **Linux (x64) support.** The same exporter now builds and runs on Linux: the GUI window on X11/Wayland desktops
  and the batch mode (`--nogui`) on servers without a display. Two artifacts are released next to the Windows zip:
  `OSMC-<version>-x86_64.AppImage` (double-clickable, runs even without FUSE) and `OSMC-<version>-linux64.tar.gz`
  (a server bundle in the same shape as the Windows zip: unpack and run `OSMC/osmc`). All Qt/GDAL/PROJ libraries,
  plugins, fonts and resources are bundled — nothing to install. The floor is glibc 2.31 (Debian 11 and newer).
- Continuous integration: every push and pull request builds and tests on Windows and Linux, and the Linux AppImage
  is built in a Debian 11 Docker container so the artifacts stay compatible with the declared glibc floor.

### Changed

- The "Load the map" button is gone: the data preview loads automatically as soon as the source file is ready
  (opening a file or a project, changing the path, finishing a download), so the button had nothing left to do.
  The status line above the preview and the hint texts are updated accordingly.

## [1.4.26.11061] — 2026-11-06

### Changed

- The product is renamed to **OSMC** (https://osmc.pro) in the user interface, the window title,
  the dialogs, the application description, the classifier warnings, the Russian translation and the READMEs.
- The executable is renamed to `osmc.exe`; the application/settings name is OSMC too, so the first launch
  starts with the default settings. The `.tmc` project format is unchanged.
  The project file `osmc.pro`, the icon `osmc.ico`, the style `osmc.qss`/`osmc.qrc` and the translations
  `osmc_ru.ts`/`osmc_zh.ts` (and the embedded `osmc_ru.qm`/`osmc_zh.qm`) are renamed as well.
- The build and test environment variables are renamed to `OSMC_BUILD`, `OSMC_EXE` and `OSMC_GDAL`.

## [1.3.26.11052] — 2026-11-05

### Added

- Three screenshots in the README galleries: street infrastructure at sunset, a creepy cemetery, a metro station hall.

### Changed

- Cemeteries: about 1.5x fewer stone crosses and jack o'lanterns — the paths between the graves are wider now.
- Cemeteries: buried chests under the old graves are twice as common; buried ones hold expensive loot in every fifth chest (the rest — middling), ground chests mostly cheap loot with some middling.

### Fixed

- Railway platforms: the stone-brick half slabs now cover the whole platform polygon, not only its rim; the slabs also follow the paving restored above underground halls and tunnels (where rails or the station really cross the platform the slab still gives up its cell).
- Cemeteries: the whole grave kit — the gravestone and cross rows, the stone crosses, pumpkins, monsters and grave trees — no longer spawns on paved roadways, parking lots and station squares, including the roads that run through the cemetery polygon.

## [1.3.26.11051] — 2026-11-05

### Added
- New point objects built on the map: a lighting mast (`tower:type=lighting`, polished calcite with glowstone),
  a street bench (`amenity=bench`), a small street sculpture (`artwork_type=sculpture`, three variants) and a
  bus stop shelter (`highway=bus_stop`) - a 5x3 three-wall booth laid along the carriageway, the open long
  side facing the nearest car road, its floor level with the ground.
- Railway platforms (`railway=platform`) become boarding platforms paved with stone bricks and raised by a
  half-block of stone brick slabs; the slab steps back where a lamp post or a piece of furniture stands.
- Cemeteries (`landuse=cemetery`): the ground turns into a dark forest - tall dense dark oaks, stone crosses,
  carved pumpkins and lit jack o'lanterns, loot chests (some buried under a headstone), and zombie, skeleton,
  spider and witch spawns.
- Ordinary railway tracks run on a three-column-wide stone ballast (tram tracks get stone bricks); powered
  rails come every tenth rail on straight sections and sit on redstone, so they stay active.

## [1.3.26.11041] — 2026-11-04

### Added
- Metro entrance buildings recognized as stations: `building=transportation` with `subway=yes` gets the same
  station hall as `building=train_station`, and so does a transportation building that stands on or within
  about 10 blocks of a `railway=subway_entrance` node.
- Metro station vestibule: the whole first floor of a station building is laid out as an entrance hall -
  polished stone checker tiles, a ticket counter, vending machines, lamp posts and info panels - so a
  bedroom no longer shares the floor with a metro entrance.
- The "Open file" dialog now starts in `Documents\tMC` (created on the first click) when no source file was
  chosen yet.
- The export-destination label next to the "Export settings" button is clickable: it opens the folder where
  the world will be saved (the chosen saves folder, the output zip's folder, or the source data folder).
- Metro vestibules are furnished: waiting benches, a bell on the ticket counter, waste bins, potted plants
  and blank-banner posters along the walls; the two cells in front of every entrance door stay free.

### Changed
- Metro deeper by 5 blocks: the tunnel floor is 15 blocks under the surface (deeper under water), a lone
  station hall 17.
- Metro lines: a long diagonal is straightened as a 10-block straight run followed by a 20-block diagonal
  correction back toward the line as it is mapped, and once the correction rejoins it, the real OSM geometry
  is repeated to the node (before: straight runs of up to 80 blocks joined by right-angle bends).
- Only the nearest line bends to a station now; the other lines keep their mapped geometry, so parallel lines
  stay parallel instead of knotting into a shared corridor at every station, and they get rails in the hall
  only where their own path already runs under it. A line that passes within 20 blocks of a hall is not
  pulled at all - the hall widens to contain it instead.
- The rails' attraction point is pulled off the hall centre to the side opposite the entrance shaft - the
  tracks cross the hall mid-way and the shaft never walls up the corridor under it.
- Subway tunnels are narrower: the corridor and its floor are 5 columns wide across all three build passes
  (was 7), the lining stands a column out and the ceiling rounding follows the narrower profile; the
  station rings, the loot-room doors and the rail column sets use the same width.
- Tunnels: the two floor cells along each wall stand one block over the rails - the trains run in a shallow
  trench between walkways.
- The station entrance shaft rebuilt: no ladder anymore, but an apartment-block staircase - 2-block-wide
  flights of stone brick steps climbing the pit turn about turn, a full-width masonry landing ending every
  flight, and a 2-wide ledge with a torch in the middle of each flight so nobody falls down the well and
  nothing crowds the steps. The doorway into the hall and the mouth at the surface wear a plain light
  frame - smooth quartz posts on iron bases under an iron beam with a hanging lantern, no ticket machines
  in the way. The 5x5 pit stands strictly inside the station building: near the hall centre in a big
  building, or in a free corner when a line crosses that spot. The pit, the wall ring and the lobby
  passage are checked against the building walls, so a building too narrow for the ring loses the ring
  and one too narrow for the pit gets no shaft at all rather than a door carved through the facade. The
  top landing and every exit passage land flush with the ground - the entrance is walked into, not
  jumped up.
- Tiny one-storey entrance huts are swept away and take the very same street entrance as a lobby building
  gets - the pit, the ring fence, the light portal frame - standing on the hut's own footprint, where it
  fits free of the rails; the frame without the ticket machines is square and compact enough to fit
  anywhere. A hut over rails tight enough for even the pit does not fit keeps its building and gets no
  entrance.
- Station halls: the benches by the long walls now sit with their backs to the wall and face the tracks.
- The "Boundary" tool is the active one when the offline map loads (it was "Spawn" before).
- Children's playgrounds (`leisure=playground` and small sports plots) get a gang of 4-6 baby villagers
  playing on them instead of iron golems; golems still guard sports grounds and military bases.
- Electrified rails are powered: a redstone block is laid under every powered rail, so the rail is always
  active and never slows a minecart down.

### Fixed
- Large metro projects no longer look hung near the end of the export: the forest-treasure pass re-scanned
  every already-placed chest for every candidate (a quadratic scan taking minutes inside a forest polygon
  clipped into the world), the vegetation grids iterated forest bounding boxes far outside the export
  rectangle, and the progress bar had no names for the exporter's inner stages - it stayed on
  "writing the zip" from halfway through the run while the world was still being planted.
- Metro vestibule vending machines (and one lab interior block) were built of plain stone: the polished
  blackstone they are made of was missing from the block palette and its id silently fell back to stone.

## [1.3.26.11031] — 2026-11-03

### Added
- Railway stations (`building=train_station`) become big underground station halls under the surface: a two-block
  checkerboard floor, pillars with chiseled caps every few rows, benches, a statue, a wall mosaic and hanging
  lamps; the platform edges by the tracks are painted white and yellow. Every metro line that passes within about
  96 blocks of a station now bends to cross exactly under it, and the hall floor lands on the tunnel level, so the
  corridors run straight through the station; rails, lamps and electrification are redrawn through the hall. A
  station without a line underneath gets the same hall without tracks. Each hall has an escape shaft with a ladder
  to the surface, the opening fenced with stone brick walls.
- Military grounds (`landuse=military`) are fenced: the plot gets a stone-brick boundary wall two rows high with
  brick pillars every few cells and two open gateways, and iron golems guard the grounds - from 2 for a small unit
  up to 10 for a huge base, depending on the plot area. The ground inside stays as it was.
- Chinese (Simplified) interface: the whole UI is translated (auto-detected from the system language, selectable in
  the "Language" list, or with `--lang zh`).

### Changed
- The "Boundary" button now stands before the "Spawn" button in the tool row.
- Station halls: the ladder to the surface now starts INSIDE the station building, not on the hall edge by the
  fence; the opening cuts through the lobby floor and is framed by a chiselled stone-brick portal with a glowing
  lantern under its beam, so the entrance is easy to spot inside the building.

### Fixed
- Sports plots: when a pitch with court markings lies inside a bigger fenced plot, benches and playground equipment
  of the outer plot are no longer placed right through the pitch lines - the court zone (with a small margin) is
  reserved across all overlapping plots.
- Station halls: the lamps over the metro tracks inside a hall hang from the hall ceiling now; before they were
  left at the (lower) tunnel ceiling row and hung in mid air under the taller hall vault.

## [1.3.26.11021] — 2026-11-02

### Added
- Underground metro: a subway line drawn in a tunnel (`railway=subway` + `tunnel=yes`) no longer falls through the
  rules and does not lie on the surface — the exporter digs a real tunnel ~10 blocks deep: the floor is two blocks,
  the interior is five blocks high with slightly rounded top corners, the walls are a one-block lining of a mixed
  stone masonry (stone bricks, cracked and mossy ones, polished andesite, granite and diorite, andesite, cobblestone).
  Real lines in OSM are long diagonals, and the block grid would turn them into a staircase turning on every block —
  the route is straightened instead: straight runs of up to 80 blocks joined by right-angle bends, stations and
  curve breaks stay in place. Slopes never land on a bend (a sloped rail cannot turn), and where the corridors of
  the two tracks touch, cross or pass one over another they are fused into one shell — no stray pillars inside
  the corridor, at a crossing or at a bend.
  The tunnel cuts through caves on its way, the surface above it stays intact (grass, roads and buildings know
  nothing about the rails below). The rails sit on the tunnel floor, every twentieth is electrified - a powered rail
  cannot be curved, so those lie on straight runs only, and a hit that lands on a turn is carried to the next straight
  cell and laid there at once as a row of 2-5 powered rails. A lantern
  hangs from the ceiling every few blocks, so the tunnels are lit. A subway *without* `tunnel=yes` stays ignored,
  as before.
- Electrified surface railways (a railway with an `electrified` tag other than `no`) get powered rails like the metro
  ones, with the same straight-run carry-over; ordinary stone replaces the ground under the track of such a line.
- Trams (`railway=tram`) are now real tram tracks: electrified even without an electrification tag, with stone bricks
  instead of the ground under the track.
- Secret loot rooms hide off the metro tunnels: about at every 200th hashed corridor cell that lies at
  least ~96 blocks from a station a narrow passage branches into a small rock chamber with a double chest
  of the rarest loot tables, guarded by zombies, skeletons, spiders or witches.

## [1.3.26.11011] — 2026-11-01

### Added
- Lakes and areal rivers are alive: fish live in the water as real creatures (like the animals in the forest), not spawned from spawners. Cod (salmon in cold climates, tropical fish and pufferfish in warm ones) swim in every lake and river polygon that really has spots deeper than three blocks — a puddle stays empty; quite large reservoirs (from ~0.3 km²) also get squid, and reservoirs next to a park also get axolotls. About one fish per 2000 m², at least two and at most twelve per reservoir. Swimming pools, canals and ditches stay clean.

### Changed
- New program icon: an original picture drawn for tMC, replacing the former one made from the Minecraft Wiki logo (CC BY-SA 3.0 by Oakar567). The third-party attribution for the old icon is removed from `THIRD_PARTY_NOTICES.md` and the READMEs, since the new icon carries no third-party license.

### Fixed
- Wetlands (`natural=wetland`) now really set the chunk biome to a swamp, not only their trees and flora: before, the exported chunks kept the plain biome, so the game did not tint the grass, leaves and water of the swamp green. The biome cells under a wetland now carry `minecraft:swamp` (both in the modern and in the 1.21.x export format).

## [1.2.26.10311] — 2026-10-31

### Changed
- Church domes without a `roof:colour` and without a known `roof:material` are now plain gray concrete instead of always being gilded; a white dome is now built of polished quartz, which is whiter than white concrete (`roof:colour=white`, `snow`, `ivory`, `pearl` or any near-white hex color). Colored domes and their spires (gold, lapis, diamond, redstone, coal), copper and metal domes are unchanged.

### Added
- A label next to the "Export settings" button always shows where the world goes now: local zip, launcher, Modrinth or CurseForge (hovering it shows the exact folder).
- Small OSM objects marked by a single point are now built in the world too: `historic=memorial` gets a small statue in one of six variants (obelisk, stele with a plaque, eternal flame, megalith, broken column, shattered stele), `historic=monument` gets a tall stele in one of several materials, `amenity=fountain` gets a classic fountain — a stone ring with a pool and a post with water running down it, waste bins (`amenity=waste_disposal`) become composters, and communication towers (`tower:type=communication` or `communication:mobile_phone=yes`) become ~32-block spires of stone fence with an iron base and a quartz antenna — on top of a tall building such a tower grows from its roof and is only as tall as needed. Nothing is built where the point's ground is water, a roadway or another structure. Which layers give such objects and which code maps to which model is set by the user: the point layers are ordinary sources in the 3D settings with a "model field" (the column holding the object code), and the code-to-model table lives in `point_models.csv` next to the program (like `heights.csv`); a code absent from the table is not built.
- Wetlands (`natural=wetland`) became a swamp biome OVERLAY rather than an object: the forest under a wetland turns into a grove of swamp oaks with vines, open swamp ground gets rare oaks, ferns, mushrooms and dead branches — while lakes, roads and buildings under the contour stay as they were; open water is not drawn. The biome is attached to the wetland layer by a new "swamp" effect in the 3D settings, so custom layers can carry it too.

### Fixed
- The Pale Garden no longer gets abrupt terrain walls inside the forest: the areas where an extra forest mansion from the donor template is replaced with ordinary forest used to end at the box edges with steps up to 38 blocks (the replacement terrain is mirrored, so the edges did not match). The terrain now ramps from the neighbor ground just outside the box through 12 blocks.
- Pale oaks no longer stand half over the water at the edge of a drawn pond: the 2x2 trunk used to hang over the columns of a lake (most visible in a wetland, where ponds are common) because only the anchor column of the tree was checked for water. Now all four trunk columns must be dry.

## [1.2.26.10305] — 2026-10-30

### Added
- Two new climate zones. "Temperate rainforest (big oaks)" — Oregon and Washington, the coast of British Columbia and south-east Alaska, and the Valdivian and Magellanic forests of south Chile and the Argentinian foothills: broadleaf forests where every third tree is a big branched oak. "Dark forest (dark oaks)" — Hokkaido with the southern Kurils and the Colchic forests of the western Caucasus: every tree there is a dark oak. Also on the command line: `--climate rainforest` and `--climate dark-forest`.
- Every exported world carries an `export_info.json` in the root of the archive (or of the saves folder): the exporter version, the bounding box of the loaded data, the export boundary, the chosen spawn point and structure marks (stronghold, ancient city, pale oak forest), and all export settings including the climate zone actually used. The same info goes into the "export" section of a `.tmc` project file saved with "Save project". Paths to the source file or folder are never written, so the archive stays safe to share.

### Changed
- The automatic climate zone is now detected from the center of the drawn export boundary, not from the center of all loaded data (without a boundary it works as before); the "auto" item in the "Climate" dropdown updates while the boundary is drawn or cleared.
- The "Climate" dropdown in the main window behaves like the ones in the export settings: it always opens below the field, shows at most 10 zones and scrolls with a visible scrollbar.

## [1.1.26.10303] — 2026-10-30

### Added
- In the "Export settings" lists of Modrinth and CurseForge instances each name also tells the game version and the mod loader of the instance, e.g. "My Pack (26.3, Forge 66.1.0)" or "My Pack (26.3, Vanilla)"; when that cannot be found out the plain instance name is shown.

### Changed
- The instance lists show at most 10 instances at a time and scroll further; the "Export settings" window uses a larger font.

### Fixed
- The instance dropdown always opens below the field: with the last instance selected the list used to grow upward.
- The dropdown scrollbar is clearly visible in both the light and the dark theme (it was there but nearly invisible).

## [1.1.26.10302] — 2026-10-30

### Added
- An "Export settings" button next to "Export" chooses where the world goes: the usual zip next to the source file (as before), the saves folder of a Minecraft launcher, a Modrinth instance or a CurseForge instance (both apps are found by their standard install folders, instances are picked from a list). For the launcher and the instances the world is saved unpacked — no archive, Minecraft sees it right away. The choice is remembered between sessions.

### Changed
- Exporting the same data again no longer replaces the previous world: when the world file name is derived automatically from the source file and such a file already exists, the new export is saved with the date and time in the name (an explicitly chosen output path is still overwritten).
- The default spawn point is a point inside the drawn boundary near its center (the centroid when it falls inside, otherwise the middle of the longest inside span), not the center of the downloaded data: a very long object sticking out of the boundary could move the spawn outside the generated area (a point chosen by a click wins over both).
- The boundary tool: a click farther than 500 m from a drawn boundary clears it and starts a new boundary from that click (earlier such clicks were ignored, and the old boundary had to be removed with a button first). Works both on the online map and on the preview.

## [1.1.26.10301] — 2026-10-30

### Added
- A new climate zone — monsoon (palms and oaks, about four palms per oak): south-eastern USA (Florida, Georgia, South Carolina), southern China with Taiwan, and southern Japan (Kyushu, Okinawa) are recognized as such automatically.
- A new climate zone — semi-arid (palms and cacti): the Gulf coast of the USA (southern Texas, coastal Louisiana); sand underfoot, cacti on the open lands, palms and oaks in the sparse forests.
- Cacti and dry bushes also speckle the open lands of the desert zone (dry bushes are new there too).

## [1.1.26.10292] — 2026-10-29

### Added
- The export report (the finish dialog and the log) now says how long the export took: "Exported in N min M s" after the statistics line.
- Cacti (1–3 blocks tall) dot the open lands of the desert climate zone alongside the rare trees.

## [1.1.26.10291] — 2026-10-29

### Changed
- The repository is renamed: https://github.com/mrHalfer/OSM2MC (the links in the READMEs, in `setup.bat` and in the `User-Agent` of the program).
- Jungle trees in the tropical climate zone look like the vanilla ones: vines stand in the crown, hang threads of 1–5 blocks under it, poke above the crown and grow along the trunk (saved without properties, exactly as a real world saves them); every fifth tree — in a forest and on the open lands alike — is a big one up to ~32 blocks tall with a 2×2 trunk, a flat 3-layer round crown (12, 10 and 8 blocks wide from bottom to top) and 2–3 side branches with leaf caps.
- In the tundra climate zone about half of the trees on the open lands are now spruces (earlier all of them were birches).

## [1.1.26.10283] — 2026-10-28

### Fixed
- "Invalid default projection: EPSG:3395" on a computer where other programs set their own `PROJ_LIB` / `GDAL_DATA` (PostgreSQL, QGIS, Python …): the PROJ and GDAL data bundled next to the exe now always win over the environment, and the PROJ search path is also set explicitly. The error message shows what to check.

## [1.1.26.10282] — 2026-10-28

### Added
- The look of a dome follows `roof:colour` / `roof:material`: the concrete of the nearest colour; a spire only on yellow/gold (gold block), blue (lapis block), light blue (diamond block), red (redstone block) and black (coal block) domes. A glass dome (`roof:material=glass`) is a transparent hemisphere with the ceiling under it cut out, so the upper floor is seen through it.
- A gallery of screenshots in the READMEs (`images/shots/`, 21 pictures 1600×800 with 400×200 previews).

### Changed
- Buildings with the glass material: the walls are concrete (the floors, the roof, the foundation and the inner partitions too), and the windows are the panoramic ones, as in the shopping malls.
- A closed line with `building:part` (when the data reader puts it into the `lines` layer instead of polygons) is a building part too.

## [1.1.26.10281] — 2026-10-28

### Added
- **Building parts raised above the ground** (OSM `building:part` with `min_height` / `building:min_level`): they are built as floating bodies from their own base height (`MINH`), with no foundation, entrance or basement.
- A raised part is **glued with the building under it** (the slab between them is removed): one interior with common floors and stairs. Residential parts are glued only when the plans overlap. Parts that could not be glued get a ladder shaft from the lower room.
- The bottom row of the walls of a raised part is a solid ring of light gray concrete (glass and windows in that row too) — it visually separates the tiers (churches, towers on podiums).
- **Onion domes** (`roof:shape=onion` / `dome`): a yellow concrete dome with a small golden spire; the body under it is lowered by `roof:height`.
- **Monopitch roofs** (`roof:shape=skillion` with `roof:height` and `roof:direction`, degrees or N/NE/…): a sloping wedge over the body.
- Small building parts get walls 1 block thick (plan up to ~400 m² or narrower than 9 blocks).
- A new program icon (a multi-size `.ico`).
- `CHANGELOG.md`; the user README is split into `README.md` / `README.ru.md` (with screenshots) and `DEVELOPING.md` for developers.

### Changed
- The copyright holder is Denis Rakhmatullin (mrHalfer) (`LICENSE`, `THIRD_PARTY_NOTICES.md`, the exe properties).
- The program icon is under CC BY-SA 3.0 (the Minecraft Wiki logo by Oakar567): attribution in `THIRD_PARTY_NOTICES.md` and the READMEs.

### Fixed
- The top blocks of a roof are never glass now (windows are not cut in the two upper rows of a column, also on the roofs of the lower parts of a stepped building).
- The ground is not raised to a body that hangs more than 3 blocks above it.
- Ladders to the raised parts are placed only inside a building (the cell must be enclosed by walls in the lower and the upper room); if there is no such place, no ladder is built.
- Under the part of a glued complex that hangs over a passage the ground is no longer raised up to its base.

## [1.0.26.10281] — 2026-10-28

### Fixed
- Wheat fields stand on plowed soil (`farmland`), not on turf.
- Yards between houses (OSM) are no longer a solid canopy: the trees are on an 8×8 grid with clearings.

### Added
- `ATTRIBUTION.txt` (ODbL 1.0) in the zip of a world made from OpenStreetMap data.
- `THIRD_PARTY_NOTICES.md`: the complete list of the bundled components and licenses.

## [1.0] — 2026-10-28

The first public release: the export of OpenStreetMap areas (the online map with search and a boundary, or a local `.osm`/`.pbf`) into a Minecraft Java world (26.x and 1.21.x), project files, English and Russian interface, a command-line mode.
