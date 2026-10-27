@echo off
rem Makes gdal-runtime-3.4.zip (for the releases page) from the contents of third_party\gdal\bin.
cd /d "%~dp0"
if not exist "bin\gdal304.dll" ( echo bin\gdal304.dll is not found: put the GDAL runtime into third_party\gdal\bin first & exit /b 1 )
if exist gdal-runtime-3.4.zip del gdal-runtime-3.4.zip
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path 'bin\*' -DestinationPath 'gdal-runtime-3.4.zip' -CompressionLevel Optimal"
if errorlevel 1 ( echo failed & exit /b 1 )
echo Done: %~dp0gdal-runtime-3.4.zip
