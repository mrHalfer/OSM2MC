@echo off
rem Shadow build of tMC (Makefile, objects and tMC.exe live outside the source folder).
rem Settings via environment variables, defaults in brackets:
rem   VCVARS     path to vcvarsall.bat of Visual Studio
rem   QTDIR      Qt 5.15.2 MSVC kit, default C:\Qt\5.15.2\msvc2019_64
rem   GDAL_DIR   another GDAL folder with include and lib (by default third_party\gdal of the project is used, see tMC.pro)
rem   TMC_BUILD  build folder, default is Builds\tMC next to the Projects folder
rem Result: %TMC_BUILD%\release\tMC.exe, log: %TMC_BUILD%\build_nmake.log
rem Visual Studio with the MSVC x64 toolset is found with vswhere (any edition/version); the Qt kit — C:\Qt\5.15.2\msvc2019_64 or the qmake that is in PATH
if not defined VCVARS call :findvs
if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not defined QTDIR call :findqt
if not defined QTDIR set "QTDIR=C:\Qt\5.15.2\msvc2019_64"
if not defined TMC_BUILD set "TMC_BUILD=%~dp0..\..\Builds\tMC"
call "%VCVARS%" x64 >nul
if not exist "%TMC_BUILD%" mkdir "%TMC_BUILD%"
cd /d "%TMC_BUILD%"
if not exist Makefile "%QTDIR%\bin\qmake.exe" "%~dp0tMC.pro" -spec win32-msvc "CONFIG+=release" > qmake.log 2>&1
nmake release > build_nmake.log 2>&1
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
