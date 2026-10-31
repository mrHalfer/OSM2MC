# Changelog

All notable changes to tMC are written here, newest first. The format follows [Keep a Changelog](https://keepachangelog.com/);
versions are `1.0.YY.MMDDN` (`N` is the number of the build of the day, see [DEVELOPING.md](DEVELOPING.md)).

## [Unreleased]

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
- A gallery of screenshots in the READMEs (`images/shots/`, 18 pictures 1600×800 with 400×200 previews).

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
