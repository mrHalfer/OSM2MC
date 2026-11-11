@echo off
rem Builds the C++ test harness: mc_export_test.exe and mc_scene_run.exe from export\mcworldexporter.cpp into tests\build. Success = ALL-OK.
rem Variables VCVARS and QT may be set from outside.
if not defined VCVARS call :findvs
if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not defined QT call :findqt
if not defined QT set "QT=C:\Qt\5.15.2\msvc2019_64"
call "%VCVARS%" x64 >nul
set ROOT=%~dp0..\..
set OUT=%~dp0..\build
if not exist "%OUT%" mkdir "%OUT%"
pushd "%OUT%"
set CXXFLAGS=/nologo /utf-8 /O2 /MD /EHsc /DNDEBUG /DUNICODE /D_UNICODE /DQT_NO_DEBUG -I%QT%\include -I%QT%\include\QtCore -I%QT%\include\QtGui -I%QT%\include\QtConcurrent -I%QT%\mkspecs\win32-msvc -I"%ROOT%" -I"%ROOT%\export"
set LIBS=/link /LIBPATH:%QT%\lib Qt5Core.lib Qt5Gui.lib Qt5Concurrent.lib
cl /c %CXXFLAGS% /Fobytes.obj "%ROOT%\export\format\bytes.cpp" || goto :fail
cl /c %CXXFLAGS% /Foblocks.obj "%ROOT%\export\blocks.cpp" || goto :fail
cl /c %CXXFLAGS% /Foanvil.obj "%ROOT%\export\anvil.cpp" || goto :fail
cl /c %CXXFLAGS% /Fobuildingmerge.obj "%ROOT%\export\buildingmerge.cpp" || goto :fail
cl /c %CXXFLAGS% /Focarset.obj "%ROOT%\export\carset.cpp" || goto :fail
cl /c %CXXFLAGS% /Fodtmgrid.obj "%ROOT%\core\dtmgrid.cpp" || goto :fail
cl /c %CXXFLAGS% /Foground.obj "%ROOT%\export\ground.cpp" || goto :fail
cl /c %CXXFLAGS% /Focaveset.obj "%ROOT%\export\caveset.cpp" || goto :fail
cl /c %CXXFLAGS% /Fofacade.obj "%ROOT%\export\facade.cpp" || goto :fail
cl /c %CXXFLAGS% /Fogeom.obj "%ROOT%\export\geom.cpp" || goto :fail
cl /c %CXXFLAGS% /Fosites.obj "%ROOT%\export\sites.cpp" || goto :fail
cl /c %CXXFLAGS% /Folairset.obj "%ROOT%\export\lairset.cpp" || goto :fail
cl /c %CXXFLAGS% /Fopaledonor.obj "%ROOT%\export\paledonor.cpp" || goto :fail
cl /c %CXXFLAGS% /Forails.obj "%ROOT%\export\rails.cpp" || goto :fail
cl /c %CXXFLAGS% /Fomcworldexporter.obj "%ROOT%\export\mcworldexporter.cpp" || goto :fail
cl /c %CXXFLAGS% /Fovoxelize.obj "%ROOT%\export\voxelize.cpp" || goto :fail
cl /c %CXXFLAGS% /Foexportrun.obj "%ROOT%\export\exportrun.cpp" || goto :fail
cl %CXXFLAGS% /Fomc_export_test.obj /Fe:mc_export_test.exe "%~dp0mc_export_test.cpp" mcworldexporter.obj voxelize.obj exportrun.obj geom.obj sites.obj rails.obj anvil.obj buildingmerge.obj carset.obj dtmgrid.obj facade.obj ground.obj paledonor.obj lairset.obj caveset.obj blocks.obj bytes.obj %LIBS% || goto :fail
cl %CXXFLAGS% /Fomc_scene_run.obj /Fe:mc_scene_run.exe "%~dp0mc_scene_run.cpp" mcworldexporter.obj voxelize.obj exportrun.obj geom.obj sites.obj rails.obj anvil.obj buildingmerge.obj carset.obj dtmgrid.obj facade.obj ground.obj paledonor.obj lairset.obj caveset.obj blocks.obj bytes.obj %LIBS% || goto :fail
popd
rem Export templates (caves, lairs, pale forest, wrecks ...) lie in the "assets" folder next to the exe (the old flat copy is still searched)
if not exist "%OUT%\assets" mkdir "%OUT%\assets"
copy /y "%ROOT%\resources\templates\*.bin" "%OUT%\assets\" >nul
echo ALL-OK
exit /b 0
:fail
popd
echo BUILD-FAILED
exit /b 1

:findvs
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 0
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsall.bat"
exit /b 0

:findqt
if exist "C:\Qt\5.15.2\msvc2019_64\bin\qmake.exe" ( set "QT=C:\Qt\5.15.2\msvc2019_64" & exit /b 0 )
for /f "delims=" %%q in ('where qmake 2^>nul') do if not defined QT set "QT=%%~dpq.."
exit /b 0
