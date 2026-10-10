# BRICK — a 16-bit Windows game, reverse engineered and rebuilt for x64

**Demo project:** a local LLM (Qwen3.8-Flash-Next-NVFP4) used Ghidra to fully reverse
engineer `BRICK.EXE`, a 16-bit Windows 3.1 brick-breaker game (Win16 NE executable,
~10 KB, 1990/91), and reimplemented it as a clean, human-readable 64-bit Windows GDI
application in a single C++ file.

Original binary: `E:\PROGRAMS\WINGAMES\ARCADE\BRICK\BRICK.EXE`

## What you're looking at

| File | Role |
|---|---|
| `main.cpp` | Complete port. Every constant, algorithm and sprite in it came out of the decompilation. |
| `build.bat` | Build script → `BRICK.exe` (x64). Run it from any cmd prompt; it locates Visual Studio itself. |
| `screenshot.png` | The port running (paused, freshly reset). |

### Build & run

```bat
build.bat
BRICK.exe
```

Controls are the original's: **click** grabs the mouse and starts the game (cursor is
clipped, paddle follows the mouse), **click again** pauses, **right-click** single-steps
one frame. Three lives; the original had no game-over screen, this one keeps that faith.

Three debug keys were added for automated testing (they are not in the original):
`S` saves the client area to `client_shot.bmp`, `R` restarts, `P` dumps game state to `state.txt`.

## How the reverse engineering was done

1. **Ghidra over MCP** — connected via [bethington/ghidra-mcp](https://github.com/bethington/ghidra-mcp) to the existing `Brick` project, opened `BRICK.EXE`,
   and decompiled the whole program (it's tiny: ~5 KB of 8086 code across 2 segments).
   The 16-bit `WinMain`/window proc map 1:1 onto modern Win32 calls, so the decompiler
   output reads almost like the original C source.
2. **Game logic** — recovered function by function and documented in the source comments
   with their original Ghidra names and data-segment addresses (`DAT_1008_xxxx`):
   - `InitializeGameApplication` — window class `"Bricks"`, 319×329 client,
     double-buffered via `CreateCompatibleDC`/`CreateCompatibleBitmap`.
   - `InitializeBoard` — 8×12 grid, each cell a 1-in-8 chance of a random *special*
     brick (states 2–7), otherwise ordinary (state 1).
   - `CycleCellState` — the brick erosion table on hit:
     `1→0, 4→2, 5→3, everything else→1` (blue erodes to cyan, red to yellow,
     specials eventually degrade to ordinary, ordinary vanish).
   - `DrawBoardCell` — `BitBlt` of a 24×12 slice at `source y = state*12` from the
     brick-strip bitmap; cells laid out at pitch 25×13 from origin (13, 44).
   - `WNDPROC` — the whole game: ball state machine (play / launch / lost-ball pause),
     wall & paddle physics, four brick-corner collision probes of the form
     `row = (y+d)/13 − 1`, `col = (x+d)/25 − 1` with snap-back constants, mouse
     capture on button-down, 55 ms tick.
   - `NextRandomValue`/`SeedRandomGenerator` — MSVC-style LCG:
     `seed = seed*214013 + 2531523; return (seed>>16)&0x7fff`.
3. **Artwork extraction from the NE file** — the game's bitmaps are embedded DIBs in the
   NE resource table (IDs 0x385/0x386/0x388). Located them by signature scan, decoded
   the 4bpp indexed pixels (including one RLE4 paddle), and embedded the real pixel data
   verbatim in `main.cpp` — the port uses the *actual original art*, not lookalikes.
4. **Verification loop** — the port has the debug keys above, which let the agent drive
   the game headlessly with `PostMessage` and read back its own pixel-exact screenshots
   and state, instead of fighting DPI/border issues with screen capture.

## Bugs the verification loop caught (and their fixes)

- **Paddle tunneling.** The original's bounce test is *asymmetric and inclusive*
  (`oldY < 0x107 && newY > 0x106`). A "prettified" strict version misses the exact
  frame where the ball lands on the boundary when the fall step divides evenly — and it
  does, every time (291 ≡ 262 mod 5). Fixed by porting the bounds verbatim; then proved
  with a deterministic frame-step test that walks the ball through that exact frame:
  bounce registers, zero tunnel-throughs.
- **Block removal glitches** — solved the same way: collision probes must reproduce the
  original's integer-truncated division and snap constants exactly.

## Fidelity notes

- All geometry, timing (55 ms tick), physics constants, the RNG, life handling
  (3 lives, respawn at `lives*16+20, 291`, vx=2/vy=−5), mouse-grab semantics, cursor
  clipping band, and window title/class come from the disassembly, value for value.
- Deliberately **not** DPI-aware: on a HiDPI desktop Windows bitmap-stretches the
  whole window by an integer factor, which keeps the 1991 pixel sizes comfortable.
- The Win11 invisible-border math eats ~4 px of dead black margin below the icon row;
  no game element is affected.
- The original had no game-over state (life count is floored at 1); the port matches.

## Toolchain

- x64 Native Tools (MSVC `cl`), no dependencies beyond `user32`/`gdi32`.
- Single translation unit written in C style, compiled as C++ (`.cpp`); clean at `/W4 /O2`.
