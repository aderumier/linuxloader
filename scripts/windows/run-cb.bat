@echo off
rem Cruis'n Blast: paths from this file's folder (%~dp0), wherever linuxloader is.
cd /d "%~dp0games\Cruis'n Blast"
"%~dp0linuxloader.exe" -c "%~dp0linuxloader.ini" -g "%~dp0games\Cruis'n Blast" > "%~dp0cb-run.log" 2>&1
echo exit=%errorlevel% >> "%~dp0cb-run.log"
