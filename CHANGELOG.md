# Changelog

All notable changes to tMC are written here, newest first. The format follows [Keep a Changelog](https://keepachangelog.com/);
versions are `1.0.YY.MMDDN` (`N` is the number of the build of the day, see [DEVELOPING.md](DEVELOPING.md)).

## [Unreleased]

### Added
- A gallery of screenshots in the READMEs (`images/shots/`, 15 pictures 1600×800 with 400×200 previews).

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
