#include "global.hpp"

bool g_dragging = FALSE;
POINT g_dragStartScreen;
POINT g_winStart;

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_LBUTTONDOWN:
    {
        SetCapture(hWnd);
        g_dragging = true;
        SetCursor(LoadCursor(NULL, IDC_ARROW));

        GetCursorPos(&g_dragStartScreen);
        RECT wr;
        GetWindowRect(hWnd, &wr);
        g_winStart.x = wr.left;
        g_winStart.y = wr.top;
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_dragging)
        {
            SetCursor(LoadCursor(NULL, IDC_ARROW));

            POINT p;
            GetCursorPos(&p);
            int dx = p.x - g_dragStartScreen.x;
            int dy = p.y - g_dragStartScreen.y;

            SetWindowPos(hWnd, NULL,
                g_winStart.x + dx, g_winStart.y + dy,
                0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (g_dragging)
        {
            g_dragging = FALSE;
            ReleaseCapture();
            return 0;
        }
        break;
    case WM_SETCURSOR:
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        return TRUE;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

BOOL InitWindows()
{
    memset(&g_dragStartScreen, 0, sizeof(g_dragStartScreen));
    memset(&g_winStart, 0, sizeof(g_winStart));

    WNDCLASSEXW wc;
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = NULL;
    wc.cbWndExtra = NULL;
    wc.hInstance = NULL;
    wc.hIcon = LoadIcon(0, IDI_APPLICATION);
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = L"";
    wc.lpszClassName = L"Test";
    wc.hIconSm = LoadIcon(0, IDI_APPLICATION);
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowW(wc.lpszClassName, L"Test", WS_POPUP,
        0, 0, 800, 600, NULL, NULL, wc.hInstance, NULL);

    if (!CreateDeviceD3D(g_hwnd))
    { 
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return FALSE; 
    }

    RECT rc;
    GetWindowRect(g_hwnd, &rc);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    SetWindowPos(
        g_hwnd, NULL,
        (screenW - winW) / 2,
        (screenH - winH) / 2,
        0, 0,
        SWP_NOZORDER | SWP_NOSIZE
    );

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    SetCursor(LoadCursor(NULL, IDC_ARROW));
    return TRUE;
}

int main1()
{
    if (!InitWindows())
        return 0;

    if (!CreateD3D11Render())
        return 0;
    memset(&g_gifStarting, 0, sizeof(g_gifStarting));
    LoadGif(&g_gifStarting, g_dev, g_ctx, g_gifRawDataStating, sizeof(g_gifRawDataStating));

    //memset(&g_gifLoading, 0, sizeof(g_gifLoading));
    //LoadGif(&g_gifLoading, g_dev, g_ctx, g_gifRawDataLoading, sizeof(g_gifRawDataLoading));

    DATA_GIF_DX11* gifTarget = &g_gifStarting;

    LARGE_INTEGER fq; 
    QueryPerformanceFrequency(&fq);
    LARGE_INTEGER last;
    QueryPerformanceCounter(&last);

    MSG msg;
    memset(&msg, 0, sizeof(msg));

    BOOL exit = FALSE;
    while (!exit)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                exit = TRUE;
                break;
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        LARGE_INTEGER now; 
        QueryPerformanceCounter(&now);
        
        float dt = float(double(now.QuadPart - last.QuadPart) / double(fq.QuadPart));
        last = now;

        RenderGif(gifTarget, dt, TRUE);
        /*
        if (gifTarget == &g_gifLoading)
        {
            RenderGif(gifTarget, dt, TRUE);
        }
        else
        {
            if (RenderGif(gifTarget, dt, FALSE))
            {
                gifTarget = &g_gifLoading;
                ImGuiGifDX11_Reset(&g_gifStarting);
            }
        }
        */

        g_sc->Present(g_dragging ? 0 : 1, 0);
    }

    DestroyD3D11Render();
    DestroyDeviceD3D();
    return 0;
}

int main()
{

  
    CreateThread(0, 0, (LPTHREAD_START_ROUTINE)main1, 0, 0, 0);
    while (1);
}