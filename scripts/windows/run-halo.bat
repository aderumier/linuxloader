@echo off
rem Halo: Fireteam Raven (64-bit, linuxloader64.exe): paths from this file's folder (%~dp0), wherever linuxloader is.
"%~dp0linuxloader64.exe" -c "%~dp0linuxloader.ini" -g "%~dp0games\Halo Fireteam Raven" > "%~dp0halo-run.log" 2>&1
echo exit=%errorlevel% >> "%~dp0halo-run.log"
