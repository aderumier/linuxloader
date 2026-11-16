@echo off
rem Centipede Chaos (64-bit, linuxloader64.exe): paths from this file's folder (%~dp0), wherever linuxloader is.
"%~dp0linuxloader64.exe" -c "%~dp0linuxloader.ini" -g "%~dp0games\Centipede Chaos" > "%~dp0centipede-run.log" 2>&1
echo exit=%errorlevel% >> "%~dp0centipede-run.log"
