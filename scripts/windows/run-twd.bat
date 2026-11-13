@echo off
rem The Walking Dead: paths from this file's folder (%~dp0), wherever linuxloader is.
cd /d "%~dp0games\The Walking Dead"
"%~dp0linuxloader.exe" -c "%~dp0linuxloader.ini" -g "%~dp0games\The Walking Dead" > "%~dp0twd-run.log" 2>&1
echo exit=%errorlevel% >> "%~dp0twd-run.log"
