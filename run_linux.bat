@echo off
rem Runs the LINUX build of OSMC from Windows through WSL (the GUI shows through WSLg). Accepts the same arguments as osmc;
rem relative paths resolve where the bat was called from, "E:\..." paths are converted to WSL paths automatically. Examples:
rem   run_linux.bat                                  (GUI window)
rem   run_linux.bat tests\data\osm_buildings.osm --dry-run --nogui
rem Needs a WSL distro and the AppImage in E:\Builds\tMC\package (made by make_release_linux.bat).
rem Variables: OSMC_WSL (distro, default Ubuntu-22.04), OSMC_PACKAGE (AppImage folder); for an UNPACKED tar.gz bundle
rem instead of the AppImage set OSMC_DIR (a Windows path to the extracted OSMC folder or its parent), OSMC_APPIMAGE - see run_linux_wsl.sh.
setlocal enabledelayedexpansion
if not defined OSMC_WSL set "OSMC_WSL=Ubuntu-22.04"
if not defined OSMC_PACKAGE set "OSMC_PACKAGE=%~dp0..\..\Builds\tMC\package"
rem wsl only exports environment variables listed in WSLENV; /u turns the Windows path into a WSL path
set "WSLENV=OSMC_DIR/u:OSMC_APPIMAGE/u"

set "SH="
for /f "delims=" %%s in ('wsl -d %OSMC_WSL% wslpath -u "%~dp0run_linux_wsl.sh"') do set "SH=%%s"
if not defined SH ( echo cannot reach the WSL distro %OSMC_WSL% & exit /b 1 )
set "PKG="
for /f "delims=" %%p in ('wsl -d %OSMC_WSL% wslpath -u "%OSMC_PACKAGE%"') do set "PKG=%%p"
set "WD="
for /f "delims=" %%w in ('wsl -d %OSMC_WSL% wslpath -u "%CD%"') do set "WD=%%w"

rem quote every argument: wsl gives the command line to the shell as one string, unquoted backslashes would be eaten there
set "ARGS="
:args
if "%~1"=="" goto run
set "ARGS=!ARGS! "%~1""
shift
goto args
:run
wsl -d %OSMC_WSL% -- bash "%SH%" "%PKG%" "%WD%" !ARGS!
endlocal
