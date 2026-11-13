@echo off
rem Big Buck HD Wild: paths from this file's folder (%~dp0), wherever linuxloader is.
cd /d "%~dp0games\Big Buck HD Wild"
"%~dp0linuxloader.exe" -c "%~dp0linuxloader.ini" -g "%~dp0games\Big Buck HD Wild" > "%~dp0bbhd-run.log" 2>&1
echo exit=%errorlevel% >> "%~dp0bbhd-run.log"
