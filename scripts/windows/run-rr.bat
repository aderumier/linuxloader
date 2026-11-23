@echo off
rem Ring Riders: paths from this file's folder (%~dp0), wherever linuxloader is.
cd /d "%~dp0games\Ring Riders\home\joc2001\joc2003\salidas"
"%~dp0linuxloader.exe" -c "%~dp0linuxloader.ini" -g "%~dp0games\Ring Riders\home\joc2001\joc2003\salidas" > "%~dp0rr-run.log" 2>&1
echo exit=%errorlevel% >> "%~dp0rr-run.log"
