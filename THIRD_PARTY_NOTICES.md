# Third-party components

tMC itself is under the MIT license (`LICENSE`). A release package also contains these components; each keeps its own license:

| Component | Use | License |
|---|---|---|
| Qt 5.15 (Core, Gui, Widgets, Concurrent) | the user interface (DLLs next to `tMC.exe`, linked dynamically) | LGPL v3 (https://www.qt.io/licensing); the Qt sources are available from https://download.qt.io |
| GDAL 3.4 (`gdal304.dll`) | reading shapefiles, OSM and height rasters | MIT/X11 (https://gdal.org/license.html) |
| PROJ, GEOS, SQLite, libtiff, zlib, libxml2, expat, Xerces-C, libcurl, OpenSSL, SpatiaLite, libpq, libmysql and others (the DLLs of the GDAL runtime) | dependencies of GDAL | their own licenses — see the sites of the projects; check them before redistributing a release |
| OpenStreetMap data | the map data downloaded online | ODbL, (c) OpenStreetMap contributors (https://www.openstreetmap.org/copyright) |

Minecraft is a trademark of Mojang AB; tMC is not affiliated with Mojang or Microsoft. The structure templates (`*.bin`) were cut out of a game world.
