@echo off
setlocal enabledelayedexpansion

echo Compiling Resources...
rc.exe /nologo brick.rc
if errorlevel 1 (
    echo Error compiling resources!
    exit /b 1
)

echo Compiling 64-bit C++ Application...
cl.exe /nologo /W4 /O2 /std:c++17 /EHsc /utf-8 brick.cpp brick.res user32.lib gdi32.lib /Fe:brick.exe /link /SUBSYSTEM:WINDOWS
if errorlevel 1 (
    echo Compilation failed!
    exit /b 1
)

echo Build Successful: brick.exe (x64)
