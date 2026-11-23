@echo off
rem Gaelco Championship Tuning Race: paths from this file's folder (%~dp0), wherever linuxloader is.
cd /d "%~dp0games\Gaelco Championship Tuning Race\home\joc2001\joc2004\salidas"
"%~dp0linuxloader.exe" -c "%~dp0linuxloader.ini" -g "%~dp0games\Gaelco Championship Tuning Race\home\joc2001\joc2004\salidas" > "%~dp0gctr-run.log" 2>&1
echo exit=%errorlevel% >> "%~dp0gctr-run.log"
