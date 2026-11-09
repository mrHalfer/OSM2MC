# Contributing to OSMC

Contributions are welcome: bug fixes, OSM mapping rules, translation, documentation.

## Contributor License Agreement

The project is published under the **GPL-3.0-or-later** license and the copyright
holder also offers commercial licenses. To keep that possible, every pull request
requires a signed **Contributor License Agreement** (text: [docs/CLA.md](docs/CLA.md)).
On your first pull request the `cla-assistant` bot posts a link - sign it there
(your GitHub login is used to verify the signature), and the check turns green.
Signing grants the project owner the right to relicense your contribution; you
keep the copyright and may still use your own code as you wish.

## Before opening a pull request

- Build: `build.bat` on Windows (qmake + nmake) or `./build.sh` on Linux (see `DEVELOPING.md`).
- Tests: `python tests/run_tests.py` (see `tests/README.md`).
- Code style follows the surrounding code: 4-space indents, braces on their own line,
  `camelCase` names, `kConst` constants; comments and UI strings in English.
- The `core/`, `input/`, `osm/`, `scene/`, `export/` layers must stay free of GUI code
  (no `app/` includes, no QtWidgets) so the headless console build keeps compiling.
- User-visible changes get a line under `[Unreleased]` in `CHANGELOG.md`.
- Do not touch `third_party/`, `resources/*.dat` and `resources/templates/*.bin`
  unless the change is exactly about them.

One logical change per pull request; describe what a reviewer should see in the
exported world, if the change is visible.
