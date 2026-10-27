# GDAL (a minimal copy for building)

The unmodified GDAL 3.4 headers that tMC uses (`gdal*.h`, `ogr*.h`, `cpl*.h`) and the import library `lib/gdal_i.lib` for `gdal304.dll` (GDAL 3.4).
They are here so that the project builds out of the box (see `tMC.pro`); they are not a complete GDAL distribution.

To run tMC you need the GDAL 3.4 runtime (see below).
If you build with another GDAL, set the environment variable `GDAL_DIR` to its folder (with `include` and `lib\gdal_i.lib`).

GDAL is © its authors and is distributed under the MIT/X11 license: https://gdal.org/license.html

## The runtime for running and debugging

The GDAL runtime is not stored in git (about 100 MB). Download the GDAL 3.4 runtime set (`gdal304.dll`, its 21 dependency DLLs and the folders `gdal-data`, `projlib`, `gdalplugins`; about 83 MB)
from the releases of the project (the file `gdal-runtime-3.4.zip`) and unpack it into `third_party/gdal/bin`. The set is exactly the contents of that folder, so a new one is made by zipping it.
Any other GDAL 3.4.x build with the same DLL name (`gdal304.dll`) works too.
After every build `tMC.pro` copies everything from this folder next to `tMC.exe`, so the program starts right from Qt Creator or `build.bat`.
