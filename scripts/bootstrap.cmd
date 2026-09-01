@echo off
rem Builds novusc on Windows from the checked-in C snapshot (needs gcc in PATH).
setlocal
set ROOT=%~dp0..
set OUT=%ROOT%\build
if not "%NOVUS_OUT%"=="" set OUT=%NOVUS_OUT%
if "%NOVUS_CC%"=="" set NOVUS_CC=gcc
if not exist "%OUT%" mkdir "%OUT%"
echo stage0: %NOVUS_CC% bootstrap\novusc.c
%NOVUS_CC% -O2 "%ROOT%\bootstrap\novusc.c" -o "%OUT%\novusc0.exe" -lm || exit /b 1
echo stage1: compiling compiler\main.nv with the snapshot
"%OUT%\novusc0.exe" build "%ROOT%\compiler\main.nv" -o "%OUT%\novusc1.exe" > nul || exit /b 1
echo stage2: compiling compiler\main.nv with stage1
"%OUT%\novusc1.exe" build "%ROOT%\compiler\main.nv" -o "%OUT%\novusc.exe" > nul || exit /b 1
"%OUT%\novusc.exe" version
