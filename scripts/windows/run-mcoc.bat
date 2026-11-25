@echo off
rem Marvel Contest of Champions (Unity's Windows player, in the game's own folder): paths from this file's folder (%~dp0).
rem The game folder needs its Windows files first (tools/mcoc-add-windows.sh); its log is mcocwin.log there.
"%~dp0games\Marvel Contest of Champions\mcoc2.exe" -c "%~dp0linuxloader.ini"
echo exit=%errorlevel% > "%~dp0mcoc-run.log"
