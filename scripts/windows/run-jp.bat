@echo off
rem Jurassic Park: paths from this file's folder (%~dp0), wherever linuxloader is.
cd /d "%~dp0games\Jurassic Park"
"%~dp0linuxloader.exe" -c "%~dp0jp.ini" -g "%~dp0games\Jurassic Park" > "%~dp0jp-run.log" 2>&1
echo exit=%errorlevel% >> "%~dp0jp-run.log"
