@echo off
setlocal
rem Makes a ready-to-run release of tMC: builds it, collects everything it needs next to tMC.exe (Qt DLLs by windeployqt, the GDAL runtime from third_party\gdal\bin,
rem the structure templates, the default settings and rules, the README files), checks that the result starts WITHOUT Qt and GDAL in PATH and packs it into a zip.
rem Result: %TMC_BUILD%\package\tMC-<version>-win64.zip (the folder %TMC_BUILD%\package\tMC is the same thing unpacked).
rem Needs the same tools as build.bat (VCVARS, QTDIR, TMC_BUILD may be set from outside) and the GDAL runtime in third_party\gdal\bin (see setup.bat).
cd /d "%~dp0"
set "ROOT=%~dp0"
if not defined TMC_BUILD set "TMC_BUILD=%~dp0..\..\Builds\tMC"
for %%i in ("%TMC_BUILD%") do set "TMC_BUILD=%%~fi"

if not exist "third_party\gdal\bin\gdal304.dll" (
  echo The GDAL runtime is not in third_party\gdal\bin: run setup.bat first.
  exit /b 1
)

echo === 1/5 building ===
call "%ROOT%build.bat"
if not exist "%TMC_BUILD%\release\tMC.exe" (
  echo The build failed, see %TMC_BUILD%\build_nmake.log
  exit /b 1
)
rem build.bat does not keep the Visual Studio / Qt variables (it runs in its own environment): find them here the same way
if not defined VCVARS call :findvs
if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not defined QTDIR call :findqt
if not defined QTDIR set "QTDIR=C:\Qt\5.15.2\msvc2019_64"
call "%VCVARS%" x64 >nul

set "VER=1.0"
for /f "tokens=3" %%v in ('findstr /b /c:"    VERSION" "%ROOT%tMC.pro"') do set "VER=%%v"
set "PKG=%TMC_BUILD%\package"
set "DST=%PKG%\tMC"
if exist "%DST%" rmdir /s /q "%DST%"
mkdir "%DST%"

echo === 2/5 collecting the files ===
copy /y "%TMC_BUILD%\release\tMC.exe" "%DST%\" >nul
for %%f in (default.t3d osm_rules.json heights.csv tmc_buildings.dat) do if exist "%ROOT%resources\%%f" copy /y "%ROOT%resources\%%f" "%DST%\" >nul
copy /y "%ROOT%resources\templates\*.bin" "%DST%\" >nul
xcopy /E /Y /Q /I "%ROOT%third_party\gdal\bin" "%DST%" >nul
for %%f in (README.md README.ru.md CHANGELOG.md LICENSE THIRD_PARTY_NOTICES.md) do if exist "%ROOT%%%f" copy /y "%ROOT%%%f" "%DST%\" >nul
if exist "%ROOT%images" (mkdir "%DST%\images" & copy /y "%ROOT%images\*.png" "%DST%\images\" >nul)

echo === 3/5 Qt libraries (windeployqt) ===
"%QTDIR%\bin\windeployqt.exe" --release --no-translations --no-opengl-sw --no-system-d3d-compiler --compiler-runtime "%DST%\tMC.exe" >nul
if errorlevel 1 (
  echo windeployqt failed
  exit /b 1
)

rem The Visual C++ runtime goes next to the exe (app-local), the same version the program was built with: nothing has to be installed on the target machine
if defined VCToolsRedistDir for /d %%d in ("%VCToolsRedistDir%x64\Microsoft.VC*.CRT") do copy /y "%%d\*.dll" "%DST%\" >nul
if exist "%DST%\vc_redist.x64.exe" del "%DST%\vc_redist.x64.exe"

echo === 4/5 a check: the program must start without Qt and GDAL in PATH ===
set "SAVEPATH=%PATH%"
set "PATH=%SystemRoot%\System32;%SystemRoot%"
set "TEST_LOG=%PKG%\smoke.log"
if exist "%TEST_LOG%" del "%TEST_LOG%"
"%DST%\tMC.exe" "%ROOT%tests\data\osm_buildings.osm" --dry-run --nogui --lang en --log "%TEST_LOG%"
set "RC=%ERRORLEVEL%"
set "PATH=%SAVEPATH%"
if not "%RC%"=="0" (
  echo The check failed ^(exit code %RC%^): the release does not start by itself, see %TEST_LOG%
  exit /b 1
)
findstr /c:"Dry run" "%TEST_LOG%" >nul || ( echo The check failed: no result in %TEST_LOG% & exit /b 1 )
echo     ok

echo === 5/5 packing ===
set "ZIP=%PKG%\tMC-%VER%-win64.zip"
if exist "%ZIP%" del "%ZIP%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%DST%' -DestinationPath '%ZIP%' -CompressionLevel Optimal"
if errorlevel 1 ( echo packing failed & exit /b 1 )
echo.
echo Done: %ZIP%
endlocal
exit /b 0

:findvs
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 0
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsall.bat"
exit /b 0

:findqt
if exist "C:\Qt\5.15.2\msvc2019_64\bin\qmake.exe" ( set "QTDIR=C:\Qt\5.15.2\msvc2019_64" & exit /b 0 )
for /f "delims=" %%q in ('where qmake 2^>nul') do if not defined QTDIR set "QTDIR=%%~dpq.."
exit /b 0
