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
cl /c %CXXFLAGS% /Fomcworldexporter.obj "%ROOT%\export\mcworldexporter.cpp" || goto :fail
cl %CXXFLAGS% /Fomc_export_test.obj /Fe:mc_export_test.exe "%~dp0mc_export_test.cpp" mcworldexporter.obj bytes.obj %LIBS% || goto :fail
cl %CXXFLAGS% /Fomc_scene_run.obj /Fe:mc_scene_run.exe "%~dp0mc_scene_run.cpp" mcworldexporter.obj bytes.obj %LIBS% || goto :fail
popd
rem Export templates (caves, lairs, pale forest, wrecks ...) must lie next to the exe
copy /y "%ROOT%\resources\templates\*.bin" "%OUT%\" >nul
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
