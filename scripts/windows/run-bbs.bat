@echo off
rem Big Buck Safari: paths from this file's folder (%~dp0), wherever linuxloader is.
cd /d "%~dp0games\Big Buck Safari"
"%~dp0linuxloader.exe" -c "%~dp0linuxloader.ini" -g "%~dp0games\Big Buck Safari" > "%~dp0bbs-run.log" 2>&1
echo exit=%errorlevel% >> "%~dp0bbs-run.log"
