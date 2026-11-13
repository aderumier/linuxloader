@echo off
rem Big Buck HD Wild 8.40: paths from this file's folder (%~dp0), wherever linuxloader is.
cd /d "%~dp0games\Big Buck HD Wild 8.40"
"%~dp0linuxloader.exe" -c "%~dp0linuxloader.ini" -g "%~dp0games\Big Buck HD Wild 8.40" > "%~dp0bbhd840-run.log" 2>&1
echo exit=%errorlevel% >> "%~dp0bbhd840-run.log"
