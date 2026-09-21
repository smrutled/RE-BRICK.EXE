# RE-BRICK.EXE: Re-engineering a 16-bit Windows 3.1 Game for Modern 64-bit Windows

> **Demo Project**: Demonstrating automated reverse engineering and modern source porting of a vintage 16-bit Windows executable using an **AI Agent** paired with the **Ghidra MCP Server**.

- **Original Game**: [BRICK on the Internet Archive](https://archive.org/details/win3_BRICK)
- **Target Platform**: Modern 64-bit Windows (Windows 10 / 11, x64)
- **Toolchain**: Microsoft Visual Studio (MSVC) C++17

---

## Overview

`BRICK.EXE` is a classic 16-bit Windows 3.1 Breakout / Arkanoid clone distributed as a New Executable (NE) binary for protected-mode x86. Because modern 64-bit editions of Windows dropped NTVDM and native 16-bit executable support, vintage 16-bit Windows binaries cannot run directly on modern 64-bit machines without emulation (such as DOSBox, Winevdm, or virtual machines).

In this project, an AI coding agent connected directly to a live Ghidra session via the **Ghidra Model Context Protocol (MCP) server** to inspect the binary, decompile its routines, extract its embedded 16-color graphical assets, and re-implement the entire game in clean, idiomatic modern 64-bit C++.

---

## Reverse Engineering Workflow with Ghidra MCP

The porting process followed these key phases:

### 1. Program Inspection & Segmentation
Using the Ghidra MCP tools (`get_current_program_info`, `list_segments`, `list_imports`, `list_exports`):
- Identified the executable format: `New Executable (NE)` for `x86:LE:16:Protected Mode`.
- Mapped segments:
  - `Code1`: 0x131c bytes (~4.8 KB) of code.
  - `Data2`: 0x2da bytes of static data.
  - `Rsrc0` – `Rsrc4`: 5 embedded resources (DIB bitmaps, icon directory, icon image).
- Mapped Win16 API imports: `USER` (`CreateWindow`, `SetTimer`, `ClipCursor`, etc.), `GDI` (`BitBlt`, `CreateCompatibleDC`, `Rectangle`, `LineTo`), and `KERNEL` (`InitTask`, `InitApp`).

### 2. Decompilation & Logic Extraction
Using `decompile_function` and `disassemble_function`:
- Traced the entry point (`entry` at `1000:0d01`) through `InitTask` and `InitApp` into the game's `WinMain` (`1000:0010`) and `WndProc` (`1000:051a`).
- Isolated Microsoft C Runtime routines (`rand`, `abs`, startup) from the 9 core game routines.
- Discovered original variable names preserved in debug strings within the static data segment: `hdcBall`, `hdcBrick`, `hdcTemp`, `hbmpTemp`, `hdcBack`, `hbmpBack`.
- Decoded game mechanics:
  - **Brick Matrix**: 8 rows × 12 columns. Bricks are 24 × 12 pixels with 1-pixel spacing.
  - **Multi-hit Durability**: States 4 & 5 degrade into damaged states 2 & 3, then state 1, then destroyed state 0.
  - **Ball Physics & Deflection**: Bounces off walls and paddle. Deflection angle scales with horizontal offset from paddle center.
  - **3D Bevel Rendering**: Authentic Windows 3.1 double-beveled border (`DrawBevel`) with raised outer and sunken inner borders.

### 3. Resource & Asset Extraction
Resource tables in the NE binary were parsed using a custom Python script (`extract_assets.py` run via `uv`):
- `brick.ico`: 32×32 16-color application icon (Resource ID 908)
- `bitmap_901.bmp`: 24×96 16-color brick sprite sheet containing 8 durability/color states (Resource ID 901)
- `bitmap_902.bmp`: 8×16 16-color ball sprite and 1-bit transparency mask (Resource ID 902)
- `bitmap_904.bmp`: 48×12 16-color paddle sprite (Resource ID 904)

### 4. Modern 64-bit C++ Architecture & Fixes
- **Type Safety**: Replaced 16-bit far pointers, Pascal calling conventions, and segment arithmetic with 64-bit types (`INT_PTR`, `LRESULT`, `WPARAM`, `LPARAM`).
- **Encapsulation**: Wrapped game state, offscreen device contexts, and lifetimes into the `BrickGame` C++ class.
- **Double-Buffering & High-DPI Scaling**: Implemented clean composite double-buffering with configurable integer scaling (defaulting to 2x for modern high-resolution displays).
- **Bug Fixes during Iteration**:
  - Replaced GDI slice-erasing with a clean backbuffer restore (`BitBlt`), eliminating trailing artifacts.
  - Used `NULL_PEN` (stock object 8) for clean borderless fills on erased life counters.
  - Unified screen presentation so all brick damage is rendered immediately without artifacts.

---

## File Structure

```text
├── brick.hpp           # Game architecture, constants, and BrickGame class declaration
├── brick.cpp           # Modern C++ implementation (WinMain, WndProc, physics, rendering)
├── brick.rc            # Win32 resource script embedding icon and bitmap sprites
├── build.bat           # 64-bit MSVC build script
├── extract_assets.py   # Python script (uv compatible) to extract resources from BRICK.EXE
├── brick.ico           # Extracted application icon
├── bitmap_901.bmp      # Extracted brick sprite sheet
├── bitmap_902.bmp      # Extracted ball sprite & mask
└── bitmap_904.bmp      # Extracted paddle sprite
```

---

## How to Build & Run

### Prerequisites
- Windows 10 / 11 (64-bit)
- Visual Studio 2022 (or Build Tools) with **Desktop development with C++** installed

### Building
Open the **x64 Native Tools Command Prompt for VS** and run:

```cmd
build.bat
```

This compiles `brick.rc` using `rc.exe` and links `brick.cpp` with MSVC `cl.exe` (`/W4 /O2 /std:c++17`):

```cmd
Compiling Resources...
Compiling 64-bit C++ Application...
Build Successful: brick.exe (x64)
```

Run the resulting executable:
```cmd
brick.exe
```

---

## Controls

| Input | Action |
|---|---|
| **Mouse Move** | Move paddle left / right |
| **Left Click** | Lock / capture mouse cursor & start / pause game |
| **Right Click** | Pause & single-step one simulation frame |

---

## Acknowledgments
- Original author of `BRICK.EXE` (1991).
- [Internet Archive](https://archive.org/details/win3_BRICK) for preserving historical Windows 3.1 software.
- [National Security Agency (NSA)](https://github.com/NationalSecurityAgency/ghidra) for Ghidra.
