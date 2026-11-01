@echo off
rem Lanzador para Windows: construye el contenedor de la herramienta (la primera vez, unos minutos) y lo ejecuta con
rem tus ROM. Hace falta Docker Desktop abierto.
rem Uso: herramienta\crear.bat C:\ruta\a\las\roms [C:\ruta\de\salida]
rem   En la carpeta de las ROM: los zip de MAME de wrally (y wrallyd si tu romset es split) y wrally2 (o wrally2a), y si
rem   quieres el logo de PlayStation y la musica del WR2 en el menu, tu BIOS (scph1001.bin, scph101.bin...).
setlocal
if "%~1"=="" (
  echo uso: crear.bat carpeta_con_las_roms [carpeta_de_salida]
  exit /b 1
)
set "ROMS=%~f1"
set "OUT=%~f2"
if "%OUT%"=="" set "OUT=%CD%\salida"
if not exist "%OUT%" mkdir "%OUT%"
docker build -t wr-ps1 -f "%~dp0Dockerfile" "%~dp0.."
if errorlevel 1 exit /b 1
docker run --rm -v "%ROMS%:/roms:ro" -v "%OUT%:/salida" -v wr-ps1-trabajo:/trabajo wr-ps1
if errorlevel 1 exit /b 1
echo Imagenes en %OUT%
