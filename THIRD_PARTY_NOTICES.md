# Third-party components

OSMC itself is under the **GPL-3.0-or-later** license (`LICENSE`, Copyright (C) 2026 Denis Rakhmatullin (mrHalfer)):
modified versions and derived works may be distributed only under the same license, with the complete corresponding
source code (the project sources live at https://github.com/mrHalfer/OSM2MC). A release package also contains the components below; each keeps its own license.
This list is made in good faith from what the package contains; the license texts of the libraries are those of their projects, check them before redistributing a modified package.

## Qt 5.15 (LGPL v3)

OSMC uses Qt Core, Gui, Widgets, Concurrent (and the SVG module, the `platforms`, `styles`, `imageformats`, `iconengines` plugins loaded by the Qt DLLs).
Qt is used under the **GNU LGPL v3** (https://www.qt.io/licensing, https://www.gnu.org/licenses/lgpl-3.0.html).
- Qt is linked **dynamically**: the Qt DLLs lie next to `osmc.exe` as separate files and can be replaced by another build of the same Qt version (5.15.x, MSVC 64-bit) — nothing of Qt is built into `osmc.exe`.
- The Qt source code is available from https://download.qt.io (Qt 5.15.2); the sources of OSMC are in this repository, so the program can be relinked with a modified Qt.
- Qt is a trademark of The Qt Company; OSMC is not affiliated with it.

## The GDAL runtime (the DLLs in `third_party/gdal/bin` and in the release `gdal-runtime-3.4`)

| Component | License |
|---|---|
| GDAL / OGR 3.4 (`gdal304.dll`) — reading shapefiles, OSM, rasters | MIT/X11 (https://gdal.org/license.html) |
| PROJ 7 (`proj_7_2.dll`, `projlib`) — coordinate systems | MIT |
| GEOS (`geos.dll`, `geos_c.dll`) | LGPL 2.1 |
| SQLite (`sqlite3.dll`) | public domain |
| SpatiaLite (`spatialite.dll`) | MPL 1.1 / GPL 2 / LGPL 2.1 (tri-license) |
| libtiff (`tiff.dll`) | libtiff license (BSD-like) |
| zlib (`zlib.dll`) | zlib license |
| libxml2 (`libxml2.dll`), expat (`libexpat.dll`), libiconv (`iconv-2.dll`) | MIT / MIT / LGPL |
| Xerces-C (`xerces-c_3_2.dll`) | Apache 2.0 |
| libcurl (`libcurl.dll`) | curl license (MIT-like) |
| OpenSSL 1.1 (`libssl-1_1-x64.dll`, `libcrypto-1_1-x64.dll`) | OpenSSL / SSLeay license (https://www.openssl.org/source/license.html) |
| OpenJPEG (`openjp2.dll`) | BSD 2-clause |
| FreeXL (`freexl.dll`) | MPL 1.1 / GPL 2 / LGPL 2.1 (tri-license) |
| OGDI (`ogdi.dll`) | BSD-like (OGDI license) |
| libpq (`LIBPQ.dll`) | PostgreSQL license |
| libmysql (`libmysql.dll`) | GPL 2 with the FLOSS exception of MySQL (it is a dependency of the GDAL build and is loaded by it, OSMC itself does not call MySQL) |

## Microsoft Visual C++ runtime

`vcruntime140*.dll`, `msvcp140*.dll`, `concrt140.dll`, `vccorlib140.dll` are the redistributable files of Microsoft Visual C++, placed next to the exe (app-local deployment) under the Microsoft license terms for redistributable code.

## Data

- **OpenStreetMap** data (downloaded online or taken from an extract): **Open Database License (ODbL) 1.0**, © OpenStreetMap contributors (https://www.openstreetmap.org/copyright). A world exported from OSM data is a derived database: when you publish it (a server, a download), keep the attribution "Map data © OpenStreetMap contributors (ODbL)" and the share-alike conditions of the ODbL. The exported zip contains the file `ATTRIBUTION.txt` with this line.
- Map tiles shown in the online mode are provided by the OpenStreetMap Foundation under its tile usage policy.
- **Terrain (elevation data from AWS Terrain Tiles)** downloaded online (the "Terrain (elevation data from AWS Terrain Tiles)" checkbox, the height matrix `DTM.tif`): the **AWS Terrain Tiles** dataset (Mapzen/Tilezen joerd: SRTM, GMTED2010, USGS 3DEP, EU-DEM, ETOPO1 and national DEMs), https://registry.opendata.aws/terrain-tiles. The attribution required by the data providers is written into the `DTM.tif` metadata and into the world's `ATTRIBUTION.txt` (https://github.com/tilezen/joerd/blob/master/docs/attribution.md).
- **Vehicle models** of the parking-lot cars (template `car_templates.bin`): the block-built car and motorcycle
  shapes are made by **Nordic_Studios** (https://www.curseforge.com/members/nordic_studios). OSMC re-saves them in
  its own template format and stamps them into parking lots as they stand; the author of the models gets the credit.
- **Playground structures** of the children's playgrounds and sports grounds (template `playground_templates.bin`):
  the block-built playground structures are made by **domonman**
  (https://www.planetminecraft.com/project/medieval-style-playground/). OSMC re-saves them in its own template
  format and stamps them into the playgrounds as they stand; the author of the structures gets the credit.
- **Landmark 3D models** (the STL files in `assets/stl`, voxelized into the world): the Statue of Liberty model
  (`Statue_of_libertyV1.stl`) is made by **MrRicketts** (https://www.thingiverse.com/thing:32488) and the Christ the
  Redeemer model (`ChristReedemer.stl`) is made by **JuanG3D** (https://www.thingiverse.com/thing:744525), both under
  the Creative Commons Attribution-ShareAlike license. OSMC voxelizes them into blocks and stands them at their real
  coordinates; the authors get the credit.

Minecraft is a trademark of Mojang AB; OSMC is not affiliated with Mojang or Microsoft. The structure templates (`*.bin`) were cut out of a game world.
