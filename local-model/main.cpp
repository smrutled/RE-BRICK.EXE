// ===========================================================================
//  BRICK  --  64-bit Windows port of BRICK.EXE (Windows 3.1, 16-bit NE)
// ===========================================================================
//
//  Reverse engineered from E:\PROGRAMS\WINGAMES\ARCADE\BRICK\BRICK.EXE
//  with Ghidra (project "Brick").  Reconstructs the game logic, physics,
//  timing, artwork and window behaviour of the original in modern Win32.
//
//  ---------------------------------------------------------------------------
//  HOW THE ORIGINAL WORKS  (from the decompilation of WNDPROC at 1000:051a)
//  ---------------------------------------------------------------------------
//  * Window class "Bricks", fixed client area 319 x 329 px.
//  * WM_CREATE builds an off-screen "play field" buffer:
//      - black background,
//      - a sunken double bevel frame drawn by DrawBrickPattern() (nested
//        3-sided brackets with light/dark gray pens),
//      - an 8 x 12 board of bricks (InitializeBoard),
//      - `lives` gray ball icons at y = 291, spaced 16 px, from x = 20.
//  * Bricks are 24 x 12 slices of a 24 x 96 resource (ID 0x385):
//      cell x = col*25 + 13,  cell y = row*13 + 44,  source y = state*12.
//    Displayed colors top-to-bottom (states 0..7):
//      0 empty(black)  1 silver  2 cyan  3 yellow  4 blue
//      5 red  6 green  7 magenta
//    InitializeBoard: each cell has a 1-in-8 chance to be a random
//    special (2..7), otherwise an ordinary silver brick (state 1).
//  * CycleCellState (what happens when the ball hits a brick):
//      1 -> 0,   4 -> 2,   5 -> 3,   everything else -> 1.
//    (Blue erodes to cyan, red to yellow; green/magenta degrade to silver;
//     silver and empty vanish.)
//  * Ball: 8 x 8 sprite from resource 0x386 (8 x 16 bitmap; bottom half is
//    the gray ball, top half an unused erase box).  Moves vx/vy per timer
//    tick.  Wall bounces at x < 13 / x > 304 / y < 13 (top bounce forces
//    vy = +10).  Below y = 307 the ball is lost.
//  * Paddle: 48 x 12 gray bar (resource 0x388) drawn at (paddleX, 271).
//    Follows the mouse: paddleX = mouseX - 24.  When the ball crosses the
//    line y = 0x106/0x107 over the paddle, vy flips; where it hit nudges
//    vx by -1/+1 (spin).
//  * Losing the ball sets a "pause" state, waits one second, erases one
//    life icon with a black rectangle, respawns the ball at
//    (lives*16 + 20, 291) with vx = 2, vy = -5.  The original never lets
//    the life counter reach 0 (no game-over screen).
//  * Random: MSVC-style LCG (NextRandomValue):
//      seed = seed * 214013 + 2531523;  value = (seed >> 16) & 0x7fff
//    seeded from the system clock at WM_CREATE.
//  * Controls: the ball timer runs ONLY while the left mouse button is
//    "grabbed".  WM_LBUTTONDOWN captures the mouse, hides the cursor,
//    clips it to the window and starts the timer; a second WM_LBUTTONDOWN
//    toggles it all off again (pause).  Right mouse button single-steps
//    one animation tick (0x204 handler in the original).
//  * The original ran on 16-bit Windows whose timers tick at ~55 ms, so
//    55 ms is used here to keep the original ball speed.
//
//  Original 16-bit globals (DAT_1008_xxxx) are noted next to the state
//  members below.
//
//  Build from an x64 Native Tools prompt:
//      cl /nologo /EHsc /O2 /W4 main.cpp ^
//         /link /SUBSYSTEM:WINDOWS /ENTRY:WinMainCRTStartup user32.lib gdi32.lib
// ===========================================================================

#include <windows.h>

// ---------------------------------------------------------------------------
// Play-field geometry (every constant straight from the disassembly)
// ---------------------------------------------------------------------------
const int kClientW    = 0x13F;   // 319 px client width
const int kClientH    = 0x145;   // 329 px client height
const int kBoardRows  = 8;
const int kBoardCols  = 12;
const int kBrickW     = 24;      // brick sprite width  (resource 0x385)
const int kBrickH     = 12;      // brick sprite height
const int kColPitch   = 0x19;    // 25 px column pitch
const int kRowPitch   = 0x0D;    // 13 px row pitch
const int kFieldX     = 0x0D;    // column 0 at x = 13
const int kFieldY     = 0x2C;    // row 0 at y = 44

const int kPaddleW    = 48;      // resource 0x388 paddle
const int kPaddleH    = 12;
const int kPaddleY    = 0x10F;   // paddle top row (271)
const int kBounceY    = 0x107;   // ball crosses 0x106 -> 0x107 to bounce
const int kBallSize   = 8;
const int kIconY      = 0x123;   // life-icon row (291)
const int kIconStep   = 0x10;    // 16 px between icons
const int kWallLeft   = 0x0D;    // bounce if ballX < 13
const int kWallRight  = 0x130;   // bounce if ballX > 304
const int kWallTop    = 0x0D;    // bounce if ballY < 13 (vy becomes +10)
const int kWallBottom = 0x133;   // ball lost when ballY > 307
const int kTimerId    = 1;       // original SetTimer id 1
const int kTickMs     = 55;      // 16-bit Windows tick granularity

// ---------------------------------------------------------------------------
// Embedded artwork, extracted verbatim from the NE resources.
// Each 4bpp indexed DIB stored its own palette; the three sprites used here
// share the same 16-color table (VGA with red/blue at 8/12 etc.), so one
// table bakes all of them.  Note indices 1/4 are dark red / dark blue.
// Pixel bytes pack two 4bpp pixels per byte, high nibble = left pixel,
// rows stored top-down as displayed.
// ---------------------------------------------------------------------------
static const COLORREF kPalette[16] = {
    RGB(0,0,0),          RGB(128,0,0),     RGB(0,128,0),     RGB(128,128,0),
    RGB(0,0,128),        RGB(128,0,128),   RGB(0,128,128),   RGB(128,128,128),
    RGB(192,192,192),    RGB(255,0,0),     RGB(0,255,0),     RGB(255,255,0),
    RGB(0,0,255),        RGB(255,0,255),   RGB(0,255,255),   RGB(255,255,255),
};

// Resource 0x385: the brick strip, 24 x 96 - eight stacked 24 x 12 bricks.
// DrawBoardCell uses source y = state * 12.
static const unsigned char kBmpBrickStrip[96][12] = {
    // --- state 0: empty (black) ---
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // --- state 1: silver ---
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xF8},
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x87},
    {0xFF,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x77},
    {0xFF,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x77},
    {0xFF,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x77},
    {0xFF,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x77},
    {0xFF,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x77},
    {0xFF,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x77},
    {0xFF,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x77},
    {0xFF,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x77},
    {0xF8,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77},
    {0x87,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77},
    // --- state 2: cyan ---
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xF8},
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x86},
    {0xFF,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0x66},
    {0xFF,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0x66},
    {0xFF,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0x66},
    {0xFF,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0x66},
    {0xFF,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0x66},
    {0xFF,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0x66},
    {0xFF,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0x66},
    {0xFF,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0xEE,0x66},
    {0xF8,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66},
    {0x86,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66},
    // --- state 3: yellow ---
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xF8},
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x83},
    {0xFF,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0x33},
    {0xFF,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0x33},
    {0xFF,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0x33},
    {0xFF,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0x33},
    {0xFF,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0x33},
    {0xFF,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0x33},
    {0xFF,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0x33},
    {0xFF,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0xBB,0x33},
    {0xF8,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33},
    {0x83,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33},
    // --- state 4: blue ---
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xF8},
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x84},
    {0xFF,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x44},
    {0xFF,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x44},
    {0xFF,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x44},
    {0xFF,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x44},
    {0xFF,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x44},
    {0xFF,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x44},
    {0xFF,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x44},
    {0xFF,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x44},
    {0xF8,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44},
    {0x84,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44},
    // --- state 5: red ---
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xF8},
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x81},
    {0xFF,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x11},
    {0xFF,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x11},
    {0xFF,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x11},
    {0xFF,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x11},
    {0xFF,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x11},
    {0xFF,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x11},
    {0xFF,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x11},
    {0xFF,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x11},
    {0xF8,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11},
    {0x81,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11},
    // --- state 6: green ---
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xF8},
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x82},
    {0xFF,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0x22},
    {0xFF,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0x22},
    {0xFF,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0x22},
    {0xFF,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0x22},
    {0xFF,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0x22},
    {0xFF,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0x22},
    {0xFF,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0x22},
    {0xFF,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0x22},
    {0xF8,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22},
    {0x82,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22},
    // --- state 7: magenta ---
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xF8},
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x85},
    {0xFF,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0x55},
    {0xFF,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0x55},
    {0xFF,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0x55},
    {0xFF,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0x55},
    {0xFF,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0x55},
    {0xFF,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0x55},
    {0xFF,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0x55},
    {0xFF,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0x55},
    {0xF8,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55},
    {0x85,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55},
};

// Resource 0x386: the ball, 8 x 16.  Bottom 8 rows are the ball itself;
// the top 8 rows were an erase box the original never used here.
static const unsigned char kBmpBall[16][4] = {
    {0xFF,0x00,0x00,0xFF},
    {0xF0,0x00,0x00,0x0F},
    {0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00},
    {0xF0,0x00,0x00,0x0F},
    {0xFF,0x00,0x00,0xFF},
    {0x00,0x77,0x77,0x00},
    {0x07,0x77,0x77,0x70},
    {0x88,0x88,0x77,0x77},
    {0xFF,0xF8,0x87,0x77},
    {0xFF,0xFF,0x88,0x77},
    {0x8F,0xFF,0xF8,0x77},
    {0x0F,0xFF,0xF8,0x70},
    {0x00,0x8F,0xF8,0x00},
};
const int kBallSrcY = 8;  // the ball half starts at source row 8

// Resource 0x388: the paddle, 48 x 12 gray beveled bar.
static const unsigned char kBmpPaddle[12][24] = {
    {0x87,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77},
    {0xF8,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77},
    {0xFF,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x77},
    {0xFF,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x77},
    {0xFF,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x77},
    {0xFF,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x77},
    {0xFF,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x77},
    {0xFF,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x77},
    {0xFF,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x77},
    {0xFF,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x77},
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x87},
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xF8},
};

// ---------------------------------------------------------------------------
// Game state.  Original 16-bit data-segment globals in comments.
// ---------------------------------------------------------------------------
struct Game {
    int board[kBoardRows][kBoardCols]; // DAT_1008_020e: 0..7 per cell
    int ballX;        // DAT_1008_0012
    int ballY;        // DAT_1008_0016
    int prevBallX;    // DAT_1008_0014
    int prevBallY;    // DAT_1008_0018
    int vx;           // DAT_1008_001a
    int vy;           // DAT_1008_001c
    int lives;        // DAT_1008_001e
    int paddleX;      // DAT_1008_0010
    int grabbed;      // DAT_1008_0096 (mouse "held"; toggled by clicks)
    int state;        // DAT_1008_0098: 0 = play, 1 = launch, 2 = lost pause

    HDC     field;     // DAT_1008_02d2: off-screen play-field buffer
    HDC     bricks;    // DAT_1008_0202: brick-strip source bitmap
    HDC     ball;      // DAT_1008_0204: ball source bitmap
    HDC     paddle;    // DAT_1008_02d0: paddle source bitmap
} G;

static unsigned rngSeed;  // DAT_1008_00ac/00ae: 32-bit LCG state

// NextRandomValue / FUN_1000_112c: MSVC-style LCG, multiply 0x343FD, add
// 0x269EC3, return high word masked to 15 bits.
static unsigned NextRandomValue()
{
    rngSeed = rngSeed * 214013u + 2531523u;
    return (rngSeed >> 16) & 0x7FFFu;
}

// SeedRandomGenerator - the original seeded from the 16-bit system clock.
static void SeedRandomGenerator(unsigned seed) { rngSeed = seed; }

// ---------------------------------------------------------------------------
// InitializeBoard: fill the 8x12 board.  1-in-8 chance of a random special
// brick (2..7); ordinary bricks are state 1 (silver).
// ---------------------------------------------------------------------------
static void InitializeBoard()
{
    for (int row = 0; row < kBoardRows; ++row)
        for (int col = 0; col < kBoardCols; ++col)
            if ((NextRandomValue() & 7) == 0)
                G.board[row][col] = (int)(NextRandomValue() % 6) + 2;
            else
                G.board[row][col] = 1;
}

// DrawBoardCell: blit the brick sprite for one cell onto the play field.
// 'win' mirrors the original's param_1: when called from the ball timer with
// a live window DC, the original's second BITBLT (param_1 != 0) copied the
// FULL 24x12 cell from the field buffer straight to the window.  Without it
// only the ball's small dirty rect is refreshed and destroyed bricks appear
// as half-erased "tunneled" blocks.
static void DrawBoardCell(int row, int col, HDC win)
{
    int x = col * kColPitch + kFieldX;
    int y = row * kRowPitch + kFieldY;
    BitBlt(G.field, x, y, kBrickW, kBrickH, G.bricks, 0,
           G.board[row][col] * kBrickH, SRCCOPY);
    if (win)
        BitBlt(win, x, y, kBrickW, kBrickH, G.field, x, y, SRCCOPY);
}

// CycleCellState: the brick "erosion" table, exactly as decompiled.
static void CycleCellState(int row, int col)
{
    int v = G.board[row][col];
    if      (v == 1) G.board[row][col] = 0;   // silver -> empty
    else if (v == 4) G.board[row][col] = 2;   // blue   -> cyan
    else if (v == 5) G.board[row][col] = 3;   // red    -> yellow
    else             G.board[row][col] = 1;   // rest   -> silver
}

// ---------------------------------------------------------------------------
// DrawBrickPattern: the decorative sunken frame - nested three-sided
// brackets drawn with dark/light gray pens, ported line-for-line.
// ---------------------------------------------------------------------------
static void SelectDrawingPen(HDC dc, int which)
{
    static HPEN pens[2];
    if (!pens[0]) {
        pens[0] = CreatePen(PS_SOLID, 1, RGB(128,128,128)); // dark
        pens[1] = CreatePen(PS_SOLID, 1, RGB(192,192,192)); // light
    }
    SelectObject(dc, pens[which & 1]);
}

static void DrawBrickPattern(HDC dc, int top, int left, int right,
                             int bottom, int count, int pen)
{
    SetROP2(dc, R2_COPYPEN);
    SelectDrawingPen(dc, pen);

    int i = 0;
    for (; i < count; ++i) {              // three nested "corner" strokes
        --bottom;
        MoveToEx(dc, bottom, top, NULL);
        LineTo(dc, left, top);
        LineTo(dc, left, right);
        ++top; ++left; --right;
    }
    if (pen < 2)
        SelectDrawingPen(dc, pen ^ 1);    // other pen for the closing legs
    for (; i > 0; --i) {
        ++bottom;
        MoveToEx(dc, bottom, top, NULL);
        LineTo(dc, bottom, right);
        LineTo(dc, left, right);
        --left;
    }
}

// ---------------------------------------------------------------------------
// DrawBitmapResource: the original drew the paddle DIB with
// SetDIBitsToDevice; a BitBlt from the baked sprite is equivalent.
// ---------------------------------------------------------------------------
static void DrawBitmapResource(HDC dc)
{
    BitBlt(dc, G.paddleX, kPaddleY, kPaddleW, kPaddleH, G.paddle, 0, 0,
           SRCCOPY);
}

// ---------------------------------------------------------------------------
// UpdatePlayfieldRegion: copy the dirty rectangle (old-ball union new-ball,
// +8 px slack like the original) from the play-field buffer to the window,
// then paint the ball on top.
// ---------------------------------------------------------------------------
static void UpdatePlayfieldRegion(HDC dc)
{
    int left   = G.prevBallX < G.ballX ? G.prevBallX : G.ballX;
    int top    = G.prevBallY < G.ballY ? G.prevBallY : G.ballY;
    int width  = abs(G.ballX - G.prevBallX) + kBallSize;
    int height = abs(G.ballY - G.prevBallY) + kBallSize;
    G.prevBallX = G.ballX;
    G.prevBallY = G.ballY;

    // restore background under the ball, then stamp the ball down
    BitBlt(dc, left, top, width, height, G.field, left, top, SRCCOPY);
    BitBlt(dc, G.ballX, G.ballY, kBallSize, kBallSize, G.ball,
           0, kBallSrcY, SRCCOPY);
}

// ---------------------------------------------------------------------------
// One ball-tick.  Faithful port of the WM_TIMER branch of WNDPROC.
// Probe geometry (from the disassembly), all integer divisions truncating
// like the original's unsigned math:
//   row = (ballY + dY) / 13 - 1,  col = (ballX + dX) / 25 - 1
// Four corners of the ball are probed; a hit flips one velocity component
// and snaps the ball just outside the brick it hit.
// ---------------------------------------------------------------------------
static void BallTick(HWND hwnd, HDC win)
{
    if (G.state == 0) {                       // normal play
        int prevY = G.ballY;
        G.ballX += G.vx;
        G.ballY += G.vy;

        // Paddle bounce - the original test, verbatim:
        //     (0x106 < newY) && (oldY < 0x107) && paddleX <= newX+3
        //                            && newX+4 <= paddleX+48
        // i.e. the ball BOTTOM (ballY+8) crossed the paddle top (271):
        // it was at y<=262 before and y>=263 now.  Both bounds are
        // INCLUSIVE and asymmetric - when vy=5 the fall passes exactly
        // through y=262 on every life (291 = 106 mod 5), and a stricter
        // prevY < 262 test misses the hit, letting the ball tunnel down
        // through the paddle.  Snap Y=263 and flip vy, exactly as original.
        if (prevY < kBounceY && G.ballY > kBounceY - 1 &&
            G.paddleX <= G.ballX + 3 &&
            G.ballX + 4 <= G.paddleX + kPaddleW)
        {
            G.ballY = kBounceY;
            G.vy = -G.vy;
            int hit = (G.ballX - G.paddleX) + 3;   // contact point
            if (hit < 0x10)      G.vx -= 1;        // left edge: slow down
            else if (hit > 0x20) G.vx += 1;        // right edge: speed up
        }

        if (G.ballX < kWallLeft)   { G.vx = -G.vx; G.ballX = kWallLeft - 1; }
        if (G.ballX > kWallRight)  { G.vx = -G.vx; G.ballX = kWallRight + 1; }
        if (G.ballY < kWallTop)    { G.vy = 10;    G.ballY = kWallTop - 1; }

        if (G.ballY > kWallBottom) {
            // lost the ball: never lets lives reach 0
            if (G.lives == 0) G.lives = 1;
            G.state = 2;
            SetTimer(hwnd, kTimerId, 1000, NULL);   // one-second pause
        }

        // ---- four brick-corner probes (dY, dX, which axis flips) ----
        static const struct { int dY, dX; bool vertical; } kProbes[4] = {
            { -0x1F,  0x10, true  },   // bottom probe -> flip vy
            { -0x17,  0x10, true  },   // bottom probe -> flip vy
            { -0x1C,  0x0D, false },   // top probe    -> flip vx
            { -0x1C,  0x15, false },   // top probe    -> flip vx
        };
        for (int p = 0; p < 4; ++p) {
            int row = (G.ballY + kProbes[p].dY) / kRowPitch - 1;
            int col = (G.ballX + kProbes[p].dX) / kColPitch - 1;
            if (row < 0 || row >= kBoardRows ||
                col < 0 || col >= kBoardCols ||
                G.board[row][col] == 0)
                continue;

            if (kProbes[p].vertical) {
                G.vy = -G.vy;
                // snap below the brick (p==0) or above it (p==1)
                G.ballY = (p == 0) ? row * kRowPitch + 0x39
                                   : row * kRowPitch + 0x24;
            } else {
                G.vx = -G.vx;
                // snap to the right (p==2) / left (p==3) of the brick
                G.ballX = (p == 2) ? col * kColPitch + 0x25
                                   : col * kColPitch + 0x04;
            }
            CycleCellState(row, col);
            DrawBoardCell(row, col, win);   // full cell to field AND window
        }
    }
    else if (G.state == 1) {                  // launched: rising past paddle
        G.ballX += G.vx;
        G.ballY += G.vy;
        if (G.ballY < kBounceY)
            G.state = 0;
    }
    else {                                    // state 2: lost-ball pause
        G.state = 1;
        SetTimer(hwnd, kTimerId, kTickMs, NULL);
        G.lives -= 1;
        G.ballX = G.lives * kIconStep + 0x14;   // respawn on icon row
        G.ballY = kIconY;
        // erase the spent life icon from the play-field buffer
        // (original: RECTANGLE with the black brush on the field DC)
        RECT icon = { G.ballX, G.ballY, G.ballX + 9, G.ballY + 9 };
        FillRect(G.field, &icon, (HBRUSH)GetStockObject(BLACK_BRUSH));
        G.vx = 2;
        G.vy = -5;
    }
}

// ---------------------------------------------------------------------------
// WM_CREATE: build the off-screen play-field buffer exactly as the original
// did - black background, beveled frame, board, life icons.
// ---------------------------------------------------------------------------
static void CreatePlayfield()
{
    RECT whole = { 0, 0, kClientW, kClientH };
    FillRect(G.field, &whole, (HBRUSH)GetStockObject(BLACK_BRUSH));

    // outer dark + inner light nested brackets (the sunken frame)
    RECT inner = { 0x0C, 0x0C, 0x134, 0x13A };
    FillRect(G.field, &inner, (HBRUSH)GetStockObject(BLACK_BRUSH));
    DrawBrickPattern(G.field, 0, 0, 0x144, 0x13E, 3, 1);
    DrawBrickPattern(G.field, 9, 9, 0x13B, 0x135, 3, 0);
    FillRect(G.field, &inner, (HBRUSH)GetStockObject(BLACK_BRUSH));

    InitializeBoard();
    for (int r = 0; r < kBoardRows; ++r)
        for (int c = 0; c < kBoardCols; ++c)
            DrawBoardCell(r, c, NULL);      // init: field only, no window yet

    // life icons: gray ball sprite stamped at (20 + 16*i, 291)
    for (int i = 0; i < G.lives; ++i)
        BitBlt(G.field, 0x14 + i * kIconStep, kIconY, kBallSize, kBallSize,
               G.ball, 0, kBallSrcY, SRCCOPY);
}

// ---------------------------------------------------------------------------
// Debug helpers (not in the original; used for automated visual checks):
// 'S' saves the client area as a 24bpp BMP, 'R' restarts the game.
// ---------------------------------------------------------------------------
static void SaveClientShot(HWND hwnd)
{
    RECT rc; GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    HDC win = GetDC(hwnd);
    HDC mem = CreateCompatibleDC(win);
    HBITMAP bmp = CreateCompatibleBitmap(win, w, h);
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, win, 0, 0, SRCCOPY);

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;                 // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 24;
    bi.bmiHeader.biCompression = BI_RGB;
    const int stride = (w * 3 + 3) & ~3;
    BYTE* buf = new BYTE[stride * h];
    GetDIBits(mem, bmp, 0, h, buf, &bi, DIB_RGB_COLORS);

    BITMAPFILEHEADER fh;
    fh.bfType    = 0x4D42;
    fh.bfSize    = sizeof(fh) + sizeof(BITMAPINFOHEADER) + stride * h;
    fh.bfReserved1 = fh.bfReserved2 = 0;
    fh.bfOffBits = sizeof(fh) + sizeof(BITMAPINFOHEADER);
    HANDLE f = CreateFileA("client_shot.bmp", GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD n;
        WriteFile(f, &fh, sizeof(fh), &n, NULL);
        WriteFile(f, &bi.bmiHeader, sizeof(bi.bmiHeader), &n, NULL);
        WriteFile(f, buf, stride * h, &n, NULL);
        CloseHandle(f);
    }
    delete[] buf;
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(hwnd, win);
}

static void ResetGame(HWND hwnd)
{
    KillTimer(hwnd, kTimerId);
    G.grabbed = 0;
    G.state   = 0;
    G.lives   = 3;
    G.paddleX = 138;
    G.ballX = 158; G.ballY = 152;
    G.prevBallX = 158; G.prevBallY = 152;
    G.vx = 2; G.vy = 5;
    SeedRandomGenerator((unsigned)GetTickCount());
    CreatePlayfield();
    InvalidateRect(hwnd, NULL, FALSE);
}

// ---------------------------------------------------------------------------
// Window procedure - mirrors the original WNDPROC message handling.
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {

    case WM_CREATE:
        SeedRandomGenerator((unsigned)GetTickCount());
        G.lives    = 3;      // DAT_1008_001e init
        G.paddleX  = 138;    // DAT_1008_0010 init
        G.ballX = 158;       // DAT_1008_0012 init
        G.ballY = 152;       // DAT_1008_0016 init
        G.prevBallX = 158;
        G.prevBallY = 152;
        G.vx = 2;            // DAT_1008_001a init
        G.vy = 5;            // DAT_1008_001c init (ball starts falling)
        G.state = 0;
        G.grabbed = 0;
        CreatePlayfield();
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, kTimerId);
        PostQuitMessage(0);
        return 0;

    case WM_ERASEBKGND:
        return 1;                             // we repaint everything

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        BitBlt(dc, 0, 0, kClientW, kClientH, G.field, 0, 0, SRCCOPY);
        DrawBitmapResource(dc);               // paddle over the field
        UpdatePlayfieldRegion(dc);            // ball on top
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if ((UINT_PTR)wp == kTimerId) {
            HDC dc = GetDC(hwnd);
            BallTick(hwnd, dc);               // hits blit full cells to window
            UpdatePlayfieldRegion(dc);
            ReleaseDC(hwnd, dc);
        }
        return 0;

    case WM_LBUTTONDOWN:
        // Original toggles the "held" flag: click starts the game, a second
        // click releases the capture and pauses.
        G.grabbed = (G.grabbed == 0);
        if (G.grabbed) {
            ShowCursor(FALSE);
            SetCapture(hwnd);
            RECT r; GetClientRect(hwnd, &r);
            ClientToScreen(hwnd, (LPPOINT)&r.left);
            ClientToScreen(hwnd, (LPPOINT)&r.right);
            ClipCursor(&r);
            SetTimer(hwnd, kTimerId, kTickMs, NULL);
            // snap the paddle under the mouse immediately (the original
            // re-centred the cursor over the paddle on grab)
            {
                HDC dc = GetDC(hwnd);
                int oldX = G.paddleX;
                G.paddleX = (int)LOWORD(lp) - 0x18;
                if (G.paddleX < 0) G.paddleX = 0;
                if (G.paddleX > kClientW - kPaddleW)
                    G.paddleX = kClientW - kPaddleW;
                int lo = oldX < G.paddleX ? oldX : G.paddleX;
                int hi = (oldX > G.paddleX ? oldX : G.paddleX) + kPaddleW;
                BitBlt(dc, lo, kPaddleY, hi - lo, kPaddleH,
                       G.field, lo, kPaddleY, SRCCOPY);
                DrawBitmapResource(dc);
                ReleaseDC(hwnd, dc);
            }
            // Original: SetCursorPos over the paddle and ClipCursor to the
            // x-band [36,290] so the paddle can never leave the field.
            {
                POINT cp = { G.paddleX + kPaddleW / 2, kPaddleY + kPaddleH / 2 };
                ClientToScreen(hwnd, &cp);
                SetCursorPos(cp.x, cp.y);
                RECT band = { 36, r.top, 290, r.bottom };
                ClientToScreen(hwnd, (LPPOINT)&band.left);
                ClientToScreen(hwnd, (LPPOINT)&band.right);
                ClipCursor(&band);
            }
        } else {
            KillTimer(hwnd, kTimerId);
            ReleaseCapture();
            ClipCursor(NULL);
            ShowCursor(TRUE);
        }
        return 0;

    case WM_KEYDOWN:
        if (wp == 'S') { SaveClientShot(hwnd); return 0; }   // dump screenshot
        if (wp == 'R') { ResetGame(hwnd);   return 0; }      // new game
        if (wp == 'P') {                                      // dump state
            HANDLE f = CreateFileA("state.txt", GENERIC_WRITE, 0, NULL,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (f != INVALID_HANDLE_VALUE) {
                char buf[128];
                int n = wsprintfA(buf, "%d %d %d %d %d %d %d",
                                  G.ballX, G.ballY, G.vx, G.vy,
                                  G.state, G.lives, G.paddleX);
                DWORD w; WriteFile(f, buf, n, &w, NULL); CloseHandle(f);
            }
            return 0;
        }
        return 0;

    case WM_RBUTTONDOWN:
        // Original 0x204 handler: kill the timer, run exactly one tick.
        KillTimer(hwnd, kTimerId);
        {
            HDC dc = GetDC(hwnd);
            BallTick(hwnd, dc);
            UpdatePlayfieldRegion(dc);
            ReleaseDC(hwnd, dc);
        }
        return 0;

    case WM_MOUSEMOVE:
        if (!G.grabbed) return 0;
        {
            // Original: paddleX = mouseX - 24, wipe the band between the
            // old and new paddle positions, redraw the paddle.
            HDC dc = GetDC(hwnd);
            int oldX = G.paddleX;
            G.paddleX = (int)LOWORD(lp) - 0x18;
            int lo = oldX < G.paddleX ? oldX : G.paddleX;
            int hi = (oldX > G.paddleX ? oldX : G.paddleX) + kPaddleW;
            BitBlt(dc, lo, kPaddleY, hi - lo, kPaddleH,
                   G.field, lo, kPaddleY, SRCCOPY);
            DrawBitmapResource(dc);
            ReleaseDC(hwnd, dc);
        }
        return 0;

    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// MakeSprite: bake a 4bpp indexed table into a 24bpp top-down bitmap and
// return a memory DC with it selected.  Keeps all palette handling in one
// place; everything else is plain BitBlt.
// ---------------------------------------------------------------------------
static HDC MakeSprite(HDC ref, int w, int h, const unsigned char* rows)
{
    HDC dc = CreateCompatibleDC(ref);
    HBITMAP bmp = CreateCompatibleBitmap(ref, w, h);
    HBITMAP old = (HBITMAP)SelectObject(dc, bmp);

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;               // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 24;
    bi.bmiHeader.biCompression = BI_RGB;

    const int stride = (w * 3 + 3) & ~3;
    BYTE* buf = new BYTE[stride * h];
    for (int y = 0; y < h; ++y) {
        const unsigned char* src = rows + (size_t)y * ((w + 1) / 2);
        BYTE* dst = buf + (size_t)y * stride;
        for (int x = 0; x < w; ++x) {
            int idx = (x & 1) ? (src[x / 2] & 0x0F) : (src[x / 2] >> 4);
            COLORREF c = kPalette[idx];
            dst[x * 3 + 0] = GetBValue(c);
            dst[x * 3 + 1] = GetGValue(c);
            dst[x * 3 + 2] = GetRValue(c);
        }
    }
    SetDIBits(dc, bmp, 0, h, buf, &bi, DIB_RGB_COLORS);
    delete[] buf;

    SelectObject(dc, old);
    SelectObject(dc, bmp);
    return dc;
}

// ---------------------------------------------------------------------------
// WinMain - class registration and message loop (GetMessage loop preserved
// from the original's InitializeGameApplication).
// ---------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow)
{
    // NOTE: deliberately NOT DPI-aware.  This is a 1:1 GDI bitmap port;
    // leaving scaling to Windows gives a crisp integer upscale on HiDPI
    // desktops (a Win11 200% desktop stretches the whole window 2x, which
    // keeps the 1990-era 640x480-era pixel sizes comfortable to play).
    WNDCLASS wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.lpszClassName = "Bricks";                   // original class name
    RegisterClass(&wc);

    // Bake the play-field buffer and the three sprite bitmaps once, before
    // the window is created (WM_CREATE paints into G.field).
    HDC ref = GetDC(NULL);
    G.field  = CreateCompatibleDC(ref);
    SelectObject(G.field, CreateCompatibleBitmap(ref, kClientW, kClientH));
    G.bricks = MakeSprite(ref, 24, 96, &kBmpBrickStrip[0][0]);
    G.ball   = MakeSprite(ref,  8, 16, &kBmpBall[0][0]);
    G.paddle = MakeSprite(ref, 48, 12, &kBmpPaddle[0][0]);
    ReleaseDC(NULL, ref);

    // Client 319 x 329, non-resizable - same footprint as the original.
    RECT rc = { 0, 0, kClientW, kClientH };
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);

    HWND hwnd = CreateWindow("Bricks", "Bricks",
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top,
                             NULL, NULL, hInst, NULL);
    if (!hwnd) return 0;

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    // Win11 captions don't match AdjustWindowRect exactly; nudge once so the
    // client is precisely 319 x 329 (this is in logical pixels, so it is
    // correct under any DPI scaling).
    {
        RECT c; GetClientRect(hwnd, &c);
        int dx = kClientW - (int)c.right, dy = kClientH - (int)c.bottom;
        if (dx || dy) {
            RECT w; GetWindowRect(hwnd, &w);
            MoveWindow(hwnd, w.left, w.top,
                       (w.right - w.left) + dx, (w.bottom - w.top) + dy, TRUE);
            InvalidateRect(hwnd, NULL, TRUE);
        }
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
