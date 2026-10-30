# Changelog

All notable changes to tMC are written here, newest first. The format follows [Keep a Changelog](https://keepachangelog.com/);
versions are `1.0.YY.MMDDN` (`N` is the number of the build of the day, see [DEVELOPING.md](DEVELOPING.md)).

## [Unreleased]

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
