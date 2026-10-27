@echo off
setlocal
rem One-step preparation of a new machine for tMC (run it once after cloning):
rem   1) checks the tools (Visual Studio C++, Qt 5.15.2 MSVC, Python) and says what is missing;
rem   2) downloads the GDAL 3.4 runtime (about 83 MB) into third_party\gdal\bin if it is not there yet.
rem Then open tMC.pro in Qt Creator (or run build.bat). The runtime is copied next to tMC.exe after every build.
rem The download address can be changed with the variable GDAL_RUNTIME_URL.
cd /d "%~dp0"
if not defined GDAL_RUNTIME_URL set "GDAL_RUNTIME_URL=https://github.com/mrHalfer/tMC/releases/download/gdal-runtime-3.4/gdal-runtime-3.4.zip"

echo === tMC: checking the environment ===
set "VSOK="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSOK=%%i"
if defined VSOK ( echo [ok]  Visual Studio C++: %VSOK% ) else echo [!!]  Visual Studio with the "Desktop development with C++" workload (or Build Tools) is not found: https://visualstudio.microsoft.com/downloads/

set "QTOK="
if exist "C:\Qt\5.15.2\msvc2019_64\bin\qmake.exe" set "QTOK=C:\Qt\5.15.2\msvc2019_64"
if not defined QTOK for /f "delims=" %%q in ('where qmake 2^>nul') do if not defined QTOK set "QTOK=%%~dpq.."
if defined QTOK ( echo [ok]  Qt: %QTOK% ) else echo [!!]  Qt 5.15.2 (the MSVC 2019 64-bit kit) is not found: install it with the Qt online installer (https://www.qt.io/download-qt-installer) or set QTDIR

set "PYOK="
where py >nul 2>&1 && set "PYOK=py -3"
if not defined PYOK where python >nul 2>&1 && set "PYOK=python"
if defined PYOK ( echo [ok]  Python: %PYOK%  ^(only for the tests and devtools; not needed to build^) ) else echo [--]  Python 3 is not found ^(only the tests and devtools need it, building does not^): https://www.python.org/downloads/

echo.
echo === GDAL runtime ===
if exist "third_party\gdal\bin\gdal304.dll" (
  echo [ok]  third_party\gdal\bin already has the GDAL runtime
  goto :done
)
echo Downloading %GDAL_RUNTIME_URL%
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; try { $z=Join-Path $env:TEMP 'gdal-runtime-3.4.zip'; Invoke-WebRequest -UseBasicParsing -Uri $env:GDAL_RUNTIME_URL -OutFile $z; New-Item -ItemType Directory -Force 'third_party\gdal\bin' | Out-Null; Expand-Archive -Force $z 'third_party\gdal\bin'; Remove-Item $z; Write-Host '[ok]  the GDAL runtime is unpacked into third_party\gdal\bin' } catch { Write-Host ('[!!]  could not download: ' + $_.Exception.Message); exit 1 }"
if errorlevel 1 (
  echo Download the file gdal-runtime-3.4.zip from the releases page of the project by hand and unpack it into third_party\gdal\bin
  echo ^(or set GDAL_RUNTIME_URL to its address and run setup.bat again^).
)
:done
echo.
echo Next: open tMC.pro in Qt Creator and press Run, or run build.bat ^(the result is Builds\tMC\release\tMC.exe^).
endlocal
