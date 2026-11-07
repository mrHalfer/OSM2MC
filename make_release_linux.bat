@echo off
rem Makes the LINUX release of OSMC: AppImage (desktop) + tar.gz (servers). Runs inside the pinned Debian 11 WSL distro
rem (its glibc is the compatibility floor: the result runs on Debian 11/12/13, Ubuntu 20.04+, Fedora 34+).
rem Needs the distro once set up: make_release_linux.bat does everything: source sync -> slim GDAL -> build -> bundle -> checks -> pack.
rem Result: E:\Builds\tMC\package\OSMC-<version>-x86_64.AppImage and OSMC-<version>-linux64.tar.gz (alongside the Windows zip).
rem Variables: OSMC_WSL (distro name, default Debian-11).
setlocal
cd /d "%~dp0"
if not defined OSMC_WSL set "OSMC_WSL=Debian-11"
if not defined OSMC_ZLIST set "OSMC_ZLIST=C:\Temp\opencode\tmc_files.zlist"

git ls-files -co --exclude-standard -z > "%OSMC_ZLIST%" || ( echo git ls-files failed & exit /b 1 )
wsl -d %OSMC_WSL% -u root -- bash -c "set -eu; mkdir -p ~/tmc /root/builds/tmc; tar -C /mnt/e/Projects/tMC -cf - --null -T /mnt/c/Temp/opencode/tmc_files.zlist 2>/dev/null | tar -C ~/tmc -xf -; cd ~/tmc; chmod +x build.sh make_release_linux.sh 2>/dev/null; OSMC_BUILD=/root/builds/tmc OSMC_OUT=/mnt/e/Builds/tMC bash make_release_linux.sh"
if errorlevel 1 ( echo Linux release FAILED & exit /b 1 )
echo Done: see E:\Builds\tMC\package
endlocal
