@echo off
rem Tokyo Cop: paths from this file's folder (%~dp0), wherever linuxloader is.
cd /d "%~dp0games\Tokyo Cop"
"%~dp0linuxloader.exe" -c "%~dp0linuxloader.ini" -g "%~dp0games\Tokyo Cop" > "%~dp0tc-run.log" 2>&1
echo exit=%errorlevel% >> "%~dp0tc-run.log"
