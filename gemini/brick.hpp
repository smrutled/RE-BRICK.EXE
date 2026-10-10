#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>

// Resource IDs matching original BRICK.EXE
constexpr WORD IDI_APPICON = 908; // 0x38c
constexpr WORD IDB_BRICK   = 901; // 0x385
constexpr WORD IDB_BALL    = 902; // 0x386
constexpr WORD IDB_PADDLE  = 904; // 0x388

// Game Playfield Dimensions (Authentic to original binary)
constexpr int GAME_WIDTH   = 325; // 0x145
constexpr int GAME_HEIGHT  = 319; // 0x13f

constexpr int BRICK_ROWS   = 8;
constexpr int BRICK_COLS   = 12;
constexpr int BRICK_WIDTH  = 24;  // 0x18
constexpr int BRICK_HEIGHT = 12;  // 0x0c

constexpr int PADDLE_WIDTH = 48;  // 0x30
constexpr int PADDLE_HEIGHT= 12;  // 0x0c
constexpr int PADDLE_Y     = 271; // 0x10f

constexpr int BALL_SIZE    = 8;

constexpr UINT_PTR TIMER_ID = 1;

// RAII wrappers for GDI objects
struct GdiObjectDeleter {
    void operator()(HGDIOBJ obj) const noexcept {
        if (obj) DeleteObject(obj);
    }
};

struct DcDeleter {
    void operator()(HDC dc) const noexcept {
        if (dc) DeleteDC(dc);
    }
};

class BrickGame {
public:
    BrickGame();
    ~BrickGame();

    bool Init(HWND hWnd, HINSTANCE hInstance);
    void Cleanup();

    void OnPaint(HWND hWnd);
    void OnMouseMove(HWND hWnd, int mouseX);
    void OnLButtonDown(HWND hWnd);
    void OnRButtonDown(HWND hWnd);
    void OnTimer(HWND hWnd);
    void OnDestroy(HWND hWnd);
    void Present(HDC hdc);

    int GetScale() const noexcept { return m_scale; }
    void SetScale(int scale) noexcept { m_scale = (scale > 0) ? scale : 1; }

private:
    void InitBricks();
    void DrawBrick(HDC hTargetDC, int row, int col);
    void HitBrick(HDC hTargetDC, int row, int col);
    void DrawPaddle(HDC hTargetDC);
    void DrawBall(HDC hTargetDC);
    void DrawBevel(HDC hdc, int top, int left, int bottom, int right, int width, bool raised);
    void ResetBallAndLives();

    // Pseudo-random generator matching original Microsoft C runtime
    uint16_t NextRand() noexcept;
    void SeedRand(uint32_t seed) noexcept;

    // Window & Instance
    HWND m_hWnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    int m_scale = 2; // Default 2x for modern displays (650x638)

    // DCs and Bitmaps
    HDC m_hdcBall = nullptr;   // 1008:0204
    HDC m_hdcBrick = nullptr;  // 1008:0202
    HDC m_hdcTemp = nullptr;   // 1008:020a
    HDC m_hdcBack = nullptr;   // 1008:02d2
    HDC m_hdcPaddle = nullptr; // paddle DC

    HBITMAP m_hbmpBall = nullptr;     // 1008:02d0 (res 902)
    HBITMAP m_hbmpBrick = nullptr;    // 1008:02d8 (res 901)
    HBITMAP m_hbmpPaddle = nullptr;   // res 904
    HBITMAP m_hbmpTemp = nullptr;     // 1008:0206 (325x319)
    HBITMAP m_hbmpBack = nullptr;     // 1008:02ce (325x319)

    HPEN m_hpenGray = nullptr;        // 1008:020c RGB(128,128,128)
    HPEN m_hpenWhite = nullptr;       // Stock WHITE_PEN
    HPEN m_hpenNull = nullptr;        // Stock NULL_PEN (stock obj 8)
    HBRUSH m_hbrLtGray = nullptr;     // 1008:0200 Stock LTGRAY_BRUSH
    HBRUSH m_hbrBlack = nullptr;      // Stock BLACK_BRUSH

    // Game state variables (matching original data offsets in 1008:0000)
    int16_t m_paddleX = 138;          // 1008:0010
    int16_t m_oldPaddleX = 138;
    int16_t m_ballX = 138;            // 1008:0012
    int16_t m_oldBallX = 138;         // 1008:0014
    int16_t m_ballY = 158;            // 1008:0016
    int16_t m_oldBallY = 158;         // 1008:0018
    int16_t m_ballVX = 2;             // 1008:001a
    int16_t m_ballVY = 5;             // 1008:001c
    int16_t m_lives = 5;              // 1008:001e
    uint16_t m_mouseCaptured = 0;     // 1008:0096
    uint16_t m_gameState = 0;         // 1008:0098 (0: in play, 1: launching, 2: died)

    int16_t m_brickGrid[BRICK_ROWS][BRICK_COLS]{}; // 1008:020e

    // PRNG seed state (1008:00ac / 1008:00ae)
    uint32_t m_randSeed = 1;
};
