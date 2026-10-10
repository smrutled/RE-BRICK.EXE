#include "brick.hpp"
#include <windowsx.h>
#include <algorithm>
#include <cstdlib>

BrickGame::BrickGame() {
    SeedRand(GetTickCount());
}

BrickGame::~BrickGame() {
    Cleanup();
}

void BrickGame::SeedRand(uint32_t seed) noexcept {
    m_randSeed = seed;
}

uint16_t BrickGame::NextRand() noexcept {
    m_randSeed = m_randSeed * 214013U + 2531011U;
    return static_cast<uint16_t>((m_randSeed >> 16) & 0x7FFF);
}

void BrickGame::DrawBevel(HDC hdc, int top, int left, int bottom, int right, int width, bool raised) {
    int count = 0;
    SetROP2(hdc, R2_COPYPEN);
    SelectObject(hdc, raised ? m_hpenWhite : m_hpenGray);

    while (count < width) {
        right--;
        MoveToEx(hdc, right, top, nullptr);
        LineTo(hdc, left, top);
        LineTo(hdc, left, bottom);
        top++;
        left++;
        bottom--;
        count++;
    }

    SelectObject(hdc, raised ? m_hpenGray : m_hpenWhite);

    while (count > 0) {
        count--;
        right++;
        bottom++;
        MoveToEx(hdc, right, top, nullptr);
        LineTo(hdc, right, bottom);
        LineTo(hdc, left, bottom);
        top--;
        left--;
    }
}

void BrickGame::DrawBrick(HDC, int row, int col) {
    int y = row * 13 + 44;
    int x = col * 25 + 13;
    int brickType = m_brickGrid[row][col];

    // Source Y = brickType * 12, Height = 12, Width = 24
    BitBlt(m_hdcBack, x, y, BRICK_WIDTH, BRICK_HEIGHT, m_hdcBrick, 0, brickType * 12, SRCCOPY);
    BitBlt(m_hdcTemp, x, y, BRICK_WIDTH, BRICK_HEIGHT, m_hdcBack, x, y, SRCCOPY);
}

void BrickGame::HitBrick(HDC, int row, int col) {
    if (row < 0 || row >= BRICK_ROWS || col < 0 || col >= BRICK_COLS) return;

    int16_t val = m_brickGrid[row][col];
    if (val == 1) {
        m_brickGrid[row][col] = 0; // destroyed
    } else if (val == 4) {
        m_brickGrid[row][col] = 2; // damaged
    } else if (val == 5) {
        m_brickGrid[row][col] = 3; // damaged
    } else {
        m_brickGrid[row][col] = 1;
    }
    DrawBrick(nullptr, row, col);
}

void BrickGame::InitBricks() {
    for (int row = 0; row < BRICK_ROWS; ++row) {
        for (int col = 0; col < BRICK_COLS; ++col) {
            uint16_t r = NextRand();
            if ((r & 7) == 0) {
                m_brickGrid[row][col] = static_cast<int16_t>((NextRand() % 6) + 2);
            } else {
                m_brickGrid[row][col] = 1;
            }
            DrawBrick(nullptr, row, col);
        }
    }
}

void BrickGame::DrawPaddle(HDC hTargetDC) {
    BitBlt(hTargetDC, m_paddleX, PADDLE_Y, PADDLE_WIDTH, PADDLE_HEIGHT, m_hdcPaddle, 0, 0, SRCCOPY);
}

void BrickGame::DrawBall(HDC) {
    int minX = std::min<int>(m_ballX, m_oldBallX);
    int minY = std::min<int>(m_ballY, m_oldBallY);
    int dirtyW = std::abs(m_ballX - m_oldBallX) + BALL_SIZE;
    int dirtyH = std::abs(m_ballY - m_oldBallY) + BALL_SIZE;

    m_oldBallX = m_ballX;
    m_oldBallY = m_ballY;

    if (minY + dirtyH > 307) {
        dirtyH = 307 - minY;
    }

    // 1. Restore clean background in the dirty area from m_hdcBack
    BitBlt(m_hdcTemp, minX, minY, dirtyW, dirtyH, m_hdcBack, minX, minY, SRCCOPY);

    // 2. Render transparent ball into m_hdcTemp using mask (SRCAND) and sprite (SRCPAINT)
    // Ball mask is at source Y=8, color sprite at source Y=0
    BitBlt(m_hdcTemp, m_ballX, m_ballY, BALL_SIZE, BALL_SIZE, m_hdcBall, 0, 8, 0x008800C6); // SRCAND
    BitBlt(m_hdcTemp, m_ballX, m_ballY, BALL_SIZE, BALL_SIZE, m_hdcBall, 0, 0, 0x00EE0086); // SRCPAINT
}

bool BrickGame::Init(HWND hWnd, HINSTANCE hInstance) {
    m_hWnd = hWnd;
    m_hInstance = hInstance;

    HDC hdcWin = GetDC(hWnd);
    if (!hdcWin) return false;

    m_hdcBall = CreateCompatibleDC(hdcWin);
    m_hdcBrick = CreateCompatibleDC(hdcWin);
    m_hdcPaddle = CreateCompatibleDC(hdcWin);
    m_hdcTemp = CreateCompatibleDC(hdcWin);
    m_hdcBack = CreateCompatibleDC(hdcWin);

    m_hbmpBall = LoadBitmap(hInstance, MAKEINTRESOURCE(IDB_BALL));
    m_hbmpBrick = LoadBitmap(hInstance, MAKEINTRESOURCE(IDB_BRICK));
    m_hbmpPaddle = LoadBitmap(hInstance, MAKEINTRESOURCE(IDB_PADDLE));
    m_hbmpTemp = CreateCompatibleBitmap(hdcWin, GAME_WIDTH, GAME_HEIGHT);
    m_hbmpBack = CreateCompatibleBitmap(hdcWin, GAME_WIDTH, GAME_HEIGHT);

    ReleaseDC(hWnd, hdcWin);

    if (!m_hdcBall || !m_hdcBrick || !m_hdcPaddle || !m_hdcTemp || !m_hdcBack ||
        !m_hbmpBall || !m_hbmpBrick || !m_hbmpPaddle || !m_hbmpTemp || !m_hbmpBack) {
        return false;
    }

    SelectObject(m_hdcBall, m_hbmpBall);
    SelectObject(m_hdcBrick, m_hbmpBrick);
    SelectObject(m_hdcPaddle, m_hbmpPaddle);
    SelectObject(m_hdcTemp, m_hbmpTemp);
    SelectObject(m_hdcBack, m_hbmpBack);

    m_hpenGray = CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
    m_hpenWhite = static_cast<HPEN>(GetStockObject(WHITE_PEN));
    m_hpenNull = static_cast<HPEN>(GetStockObject(NULL_PEN));
    m_hbrLtGray = static_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH));
    m_hbrBlack = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));

    // Fill m_hdcBack with standard dialog gray
    SetROP2(m_hdcBack, R2_COPYPEN);
    SelectObject(m_hdcBack, m_hpenWhite);
    SelectObject(m_hdcBack, m_hbrLtGray);
    Rectangle(m_hdcBack, 0, 0, GAME_WIDTH + 2, GAME_HEIGHT + 2);

    // Outer raised 3D bevel and inner sunken 3D bevel
    DrawBevel(m_hdcBack, 0, 0, GAME_HEIGHT - 1, GAME_WIDTH - 1, 3, true);
    DrawBevel(m_hdcBack, 9, 9, GAME_HEIGHT - 10, GAME_WIDTH - 10, 3, false);

    // Inner playfield (black)
    SetROP2(m_hdcBack, R2_COPYPEN);
    SelectObject(m_hdcBack, m_hpenWhite);
    SelectObject(m_hdcBack, m_hbrBlack);
    Rectangle(m_hdcBack, 12, 12, 314, 308);

    // Populate bricks
    InitBricks();

    // Draw initial lives indicator icons at bottom
    m_lives = 5;
    int lifeX = 20;
    int lifeY = 291;
    for (int i = 0; i < m_lives; ++i) {
        BitBlt(m_hdcBack, lifeX, lifeY, BALL_SIZE, BALL_SIZE, m_hdcBall, 0, 8, 0x008800C6);
        BitBlt(m_hdcBack, lifeX, lifeY, BALL_SIZE, BALL_SIZE, m_hdcBall, 0, 0, 0x00EE0086);
        lifeX += 16;
    }

    // Copy clean backbuffer to m_hdcTemp
    BitBlt(m_hdcTemp, 0, 0, GAME_WIDTH, GAME_HEIGHT, m_hdcBack, 0, 0, SRCCOPY);

    // Initial paddle & ball
    m_paddleX = 138;
    m_oldPaddleX = 138;
    DrawPaddle(m_hdcTemp);

    m_ballX = 138;
    m_ballY = 158;
    m_oldBallX = 138;
    m_oldBallY = 158;
    m_ballVX = 2;
    m_ballVY = 5;
    DrawBall(nullptr);

    return true;
}

void BrickGame::Cleanup() {
    if (m_hdcBall) { DeleteDC(m_hdcBall); m_hdcBall = nullptr; }
    if (m_hdcBrick) { DeleteDC(m_hdcBrick); m_hdcBrick = nullptr; }
    if (m_hdcPaddle) { DeleteDC(m_hdcPaddle); m_hdcPaddle = nullptr; }
    if (m_hdcTemp) { DeleteDC(m_hdcTemp); m_hdcTemp = nullptr; }
    if (m_hdcBack) { DeleteDC(m_hdcBack); m_hdcBack = nullptr; }

    if (m_hbmpBall) { DeleteObject(m_hbmpBall); m_hbmpBall = nullptr; }
    if (m_hbmpBrick) { DeleteObject(m_hbmpBrick); m_hbmpBrick = nullptr; }
    if (m_hbmpPaddle) { DeleteObject(m_hbmpPaddle); m_hbmpPaddle = nullptr; }
    if (m_hbmpTemp) { DeleteObject(m_hbmpTemp); m_hbmpTemp = nullptr; }
    if (m_hbmpBack) { DeleteObject(m_hbmpBack); m_hbmpBack = nullptr; }

    if (m_hpenGray) { DeleteObject(m_hpenGray); m_hpenGray = nullptr; }
}

void BrickGame::Present(HDC hdc) {
    if (!hdc || !m_hdcTemp) return;
    if (m_scale == 1) {
        BitBlt(hdc, 0, 0, GAME_WIDTH, GAME_HEIGHT, m_hdcTemp, 0, 0, SRCCOPY);
    } else {
        SetStretchBltMode(hdc, COLORONCOLOR);
        StretchBlt(hdc, 0, 0, GAME_WIDTH * m_scale, GAME_HEIGHT * m_scale,
                   m_hdcTemp, 0, 0, GAME_WIDTH, GAME_HEIGHT, SRCCOPY);
    }
}

void BrickGame::OnPaint(HWND hWnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);
    Present(hdc);
    EndPaint(hWnd, &ps);
}

void BrickGame::OnMouseMove(HWND hWnd, int mouseX) {
    if (!m_mouseCaptured) return;

    int newPaddleX = (mouseX / m_scale) - 24;
    if (newPaddleX < 13) newPaddleX = 13;
    if (newPaddleX > 314 - PADDLE_WIDTH) newPaddleX = 314 - PADDLE_WIDTH;

    if (newPaddleX == m_paddleX) return;

    // Cleanly restore black playfield under previous paddle position
    BitBlt(m_hdcTemp, m_oldPaddleX, PADDLE_Y, PADDLE_WIDTH, PADDLE_HEIGHT,
           m_hdcBack, m_oldPaddleX, PADDLE_Y, SRCCOPY);

    m_paddleX = static_cast<int16_t>(newPaddleX);
    m_oldPaddleX = m_paddleX;

    // Draw paddle at updated position
    DrawPaddle(m_hdcTemp);

    // Present entire frame
    HDC hdc = GetDC(hWnd);
    if (hdc) {
        Present(hdc);
        ReleaseDC(hWnd, hdc);
    }
}

void BrickGame::OnLButtonDown(HWND hWnd) {
    m_mouseCaptured = !m_mouseCaptured;

    if (!m_mouseCaptured) {
        KillTimer(hWnd, TIMER_ID);
        ReleaseCapture();
        ClipCursor(nullptr);
        ShowCursor(TRUE);
    } else {
        ShowCursor(FALSE);
        SetCapture(hWnd);

        // Position cursor at current paddle center
        POINT ptCenter{ (m_paddleX + 24) * m_scale, (PADDLE_Y + 6) * m_scale };
        ClientToScreen(hWnd, &ptCenter);
        SetCursorPos(ptCenter.x, ptCenter.y);

        // Restrict cursor horizontally within playfield
        // Center range: [13 + 24 = 37, (314 - 48) + 24 = 290]
        POINT ptLeft{ 37 * m_scale, 0 };
        POINT ptRight{ 290 * m_scale, 0 };
        ClientToScreen(hWnd, &ptLeft);
        ClientToScreen(hWnd, &ptRight);

        RECT clipRect;
        clipRect.left = ptLeft.x;
        clipRect.right = ptRight.x;
        clipRect.top = ptCenter.y - 1;
        clipRect.bottom = ptCenter.y + 1;
        ClipCursor(&clipRect);

        // Start timer (~60 fps)
        SetTimer(hWnd, TIMER_ID, 16, nullptr);
    }
}

void BrickGame::OnRButtonDown(HWND hWnd) {
    // Original feature: right click pauses and single-steps one frame
    KillTimer(hWnd, TIMER_ID);
    OnTimer(hWnd);
}

void BrickGame::OnTimer(HWND hWnd) {
    if (m_gameState == 0) {
        // Normal in-play physics
        m_ballX += m_ballVX;
        int nextY = m_ballY + m_ballVY;

        // Paddle collision check
        if (nextY > 262 && m_ballY < 263 &&
            m_paddleX <= m_ballX + 3 && m_ballX + 4 <= m_paddleX + PADDLE_WIDTH) {
            nextY = 263;
            m_ballVY = -m_ballVY;
            int hitOffset = (m_ballX - m_paddleX) + 3;
            if (hitOffset < 16) {
                m_ballVX--;
            } else if (hitOffset > 32) {
                m_ballVX++;
            }
        }
        m_ballY = static_cast<int16_t>(nextY);

        // Left wall
        if (m_ballX < 13) {
            m_ballVX = -m_ballVX;
            m_ballX = 12;
        }
        // Right wall
        if (m_ballX > 304) {
            m_ballVX = -m_ballVX;
            m_ballX = 305;
        }
        // Top wall
        if (m_ballY < 13) {
            m_ballVY = 10;
            m_ballY = 12;
        }
        // Bottom boundary (missed paddle)
        if (m_ballY > 307) {
            if (m_lives == 0) {
                m_lives = 1;
            }
            m_gameState = 2;
            SetTimer(hWnd, TIMER_ID, 1000, nullptr); // Wait 1 second before reset
        }

        // Brick collisions (4 check points matching original 16-bit logic)
        int local6 = m_ballY - 44;
        int iVar5 = m_ballX - 12;

        // Check Point 1: Bottom of ball hitting brick top
        int row = (m_ballY - 31) / 13 - 1;
        int col = (m_ballX + 16) / 25 - 1;
        if (row >= 0 && row < BRICK_ROWS && col >= 0 && col < BRICK_COLS && m_brickGrid[row][col] != 0) {
            m_ballVY = -m_ballVY;
            m_ballY = static_cast<int16_t>(row * 13 + 57);
            HitBrick(nullptr, row, col);
        }

        // Check Point 2: Top of ball hitting brick bottom
        col = (iVar5 + 28) / 25 - 1;
        row = (local6 + 21) / 13 - 1;
        if (row >= 0 && row < BRICK_ROWS && col >= 0 && col < BRICK_COLS && m_brickGrid[row][col] != 0) {
            m_ballVY = -m_ballVY;
            m_ballY = static_cast<int16_t>(row * 13 + 36);
            HitBrick(nullptr, row, col);
        }

        // Check Point 3: Right side of ball hitting brick left
        col = (iVar5 + 25) / 25 - 1;
        row = (local6 + 16) / 13 - 1;
        if (row >= 0 && row < BRICK_ROWS && col >= 0 && col < BRICK_COLS && m_brickGrid[row][col] != 0) {
            m_ballVX = -m_ballVX;
            m_ballX = static_cast<int16_t>(col * 25 + 37);
            HitBrick(nullptr, row, col);
        }

        // Check Point 4: Left side of ball hitting brick right
        col = (iVar5 + 33) / 25 - 1;
        row = (local6 + 16) / 13 - 1;
        if (row >= 0 && row < BRICK_ROWS && col >= 0 && col < BRICK_COLS && m_brickGrid[row][col] != 0) {
            m_ballVX = -m_ballVX;
            m_ballX = static_cast<int16_t>(col * 25 + 4);
            HitBrick(nullptr, row, col);
        }
    } else if (m_gameState == 1) {
        // Launching ball
        m_ballX += m_ballVX;
        m_ballY += m_ballVY;
        if (m_ballY < 263) {
            m_gameState = 0;
        }
    } else if (m_gameState == 2) {
        // Respawning after death
        m_gameState = 1;
        SetTimer(hWnd, TIMER_ID, 16, nullptr);
        m_lives--;

        // Erase one life ball at bottom cleanly with NULL_PEN
        int deadLifeX = m_lives * 16 + 20;
        int deadLifeY = 291;
        SelectObject(m_hdcBack, m_hpenNull);
        SelectObject(m_hdcBack, m_hbrBlack);
        Rectangle(m_hdcBack, deadLifeX, deadLifeY, deadLifeX + 10, deadLifeY + 10);
        BitBlt(m_hdcTemp, deadLifeX, deadLifeY, 10, 10, m_hdcBack, deadLifeX, deadLifeY, SRCCOPY);

        m_ballX = static_cast<int16_t>(deadLifeX);
        m_ballY = 291;
        m_ballVX = 2;
        m_ballVY = -5;
    }

    DrawBall(nullptr);
    HDC hdc = GetDC(hWnd);
    if (hdc) {
        Present(hdc);
        ReleaseDC(hWnd, hdc);
    }
}

void BrickGame::OnDestroy(HWND hWnd) {
    KillTimer(hWnd, TIMER_ID);
    Cleanup();
    PostQuitMessage(0);
}

// Win32 Entry Point & WndProc
static BrickGame g_game;

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        if (!g_game.Init(hWnd, reinterpret_cast<LPCREATESTRUCT>(lParam)->hInstance)) {
            return -1;
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        g_game.OnPaint(hWnd);
        return 0;

    case WM_MOUSEMOVE:
        g_game.OnMouseMove(hWnd, GET_X_LPARAM(lParam));
        return 0;

    case WM_LBUTTONDOWN:
        g_game.OnLButtonDown(hWnd);
        return 0;

    case WM_RBUTTONDOWN:
        g_game.OnRButtonDown(hWnd);
        return 0;

    case WM_TIMER:
        g_game.OnTimer(hWnd);
        return 0;

    case WM_DESTROY:
        g_game.OnDestroy(hWnd);
        return 0;

    default:
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"Bricks";

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH));
    wc.lpszClassName = CLASS_NAME;
    wc.hIconSm       = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));

    if (!RegisterClassExW(&wc)) {
        return 0;
    }

    int scale = g_game.GetScale();
    RECT rc = { 0, 0, GAME_WIDTH * scale, GAME_HEIGHT * scale };
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    int winWidth = rc.right - rc.left;
    int winHeight = rc.bottom - rc.top;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - winWidth) / 2;
    int posY = (screenH - winHeight) / 2;

    HWND hWnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Bricks",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        posX, posY, winWidth, winHeight,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hWnd) return 0;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
