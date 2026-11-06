# Changelog

All notable changes to OSMC are written here, newest first. The format follows [Keep a Changelog](https://keepachangelog.com/);
versions are `1.0.YY.MMDDN` (`N` is the number of the build of the day, see [DEVELOPING.md](DEVELOPING.md)).

## [Unreleased]


### Changed

- The executable is renamed to `OSMC.exe` (the program is OSMC); the application/settings name is
  OSMC too, so the first launch starts with the default settings. The `.tmc` project format is unchanged.
  The project file `osmc.pro`, the icon `osmc.ico` and the translations `osmc_ru.ts`/`osmc_zh.ts`
  are renamed as well.

### Changed

- The product is renamed to **OSMC** (https://osmc.pro) in the user interface, the window title,
  the dialogs, the application description, the classifier warnings, the Russian translation and the READMEs.
  Technical names (the executable, the project files, the settings) are unchanged.
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
