@echo off
rem ===========================================================================
rem  BRICK  -  build script
rem  Produces BRICK.exe (64-bit Windows GUI) from main.cpp
rem
setlocal

if exist BRICK.exe del BRICK.exe 2>nul
if errorlevel 1 (
    echo [build] ERROR: BRICK.exe is running - close the game first.
    exit /b 1
)

echo [build] compiling main.cpp ...
cl /nologo /EHsc /O2 /W4 /FeBRICK.exe /Fobrick.obj main.cpp ^
   /link /SUBSYSTEM:WINDOWS /ENTRY:WinMainCRTStartup user32.lib gdi32.lib
if errorlevel 1 (
    echo [build] FAILED
    exit /b 1
)

del /q brick.obj 2>nul
echo [build] OK -^> BRICK.exe
endlocal
