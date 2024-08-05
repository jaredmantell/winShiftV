// main.cpp
#include "recorder.h"

HWND g_hwnd = NULL;
HHOOK g_hook = NULL;
ScreenRecorder* g_recorder = nullptr;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

HWND CreateOverlayWindow(HINSTANCE hInstance) {
    WNDCLASS wc = {};
    wc.lpfnWndProc = ScreenRecorder::OverlayWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "OverlayWindowClass";
    wc.hCursor = LoadCursor(NULL, IDC_CROSS);
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
            WS_EX_TOPMOST | WS_EX_LAYERED,
            "OverlayWindowClass",
            NULL,
            WS_POPUP,
            0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
            NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) {
        ScreenRecorder::LogDebug("Failed to create overlay window. Error: " + std::to_string(GetLastError()));
    } else {
        // Make the window semi-transparent
        SetLayeredWindowAttributes(hwnd, 0, DARKENING_ALPHA, LWA_ALPHA);
        ScreenRecorder::LogDebug("Overlay window created successfully");
    }

    return hwnd;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {\
    // Check DPI awareness
    DPI_AWARENESS_CONTEXT context = GetThreadDpiAwarenessContext();
    if (AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        OutputDebugString("Application is Per Monitor V2 DPI aware\n");
    } else if (AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)) {
        OutputDebugString("Application is Per Monitor DPI aware\n");
    } else if (AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_SYSTEM_AWARE)) {
        OutputDebugString("Application is System DPI aware\n");
    } else if (AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_UNAWARE)) {
        OutputDebugString("Application is not DPI aware\n");
    }CreateDirectoryA("C:/ScreenRecordings", NULL);
#ifdef _DEBUG
    AllocConsole();
    FILE* pCout;
    freopen_s(&pCout, "CONOUT$", "w", stdout);
    freopen_s(&pCout, "CONOUT$", "w", stderr);
#endif
    ScreenRecorder::LogDebug("Screen Recorder application starting...");
    ShowInstructions();

    // Initialize FFmpeg
    av_log_set_level(AV_LOG_VERBOSE);
    av_log_set_callback([](void*, int level, const char* fmt, va_list vl) {
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), fmt, vl);
        ScreenRecorder::LogDebug("FFmpeg: " + std::string(buffer));
    });

    const char CLASS_NAME[] = "Screen Recorder Window Class";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    g_hwnd = CreateWindowEx(
            WS_EX_TOOLWINDOW,              // Window style (invisible)
            CLASS_NAME,                    // Window class
            "Screen Recorder",             // Window text
            WS_POPUP,                      // Window style
            0, 0, 0, 0,                    // Size and position (0 size makes it invisible)
            NULL, NULL, hInstance, NULL
    );

    if (g_hwnd == NULL) {
        ScreenRecorder::LogDebug("Failed to create window");
        return 0;
    }

    g_recorder = new ScreenRecorder();
    HWND overlayWindow = CreateOverlayWindow(hInstance);
    g_recorder->SetOverlayWindow(overlayWindow);
    if (g_recorder->GetOverlayWindow() == NULL) {
        ScreenRecorder::LogDebug("Failed to create overlay window");
        return 0;
    }

    g_hook = SetWindowsHookEx(WH_KEYBOARD_LL, ScreenRecorder::LowLevelKeyboardProc, NULL, 0);
    if (g_hook == NULL) {
        DWORD error = GetLastError();
        char errorMsg[256];
        snprintf(errorMsg, sizeof(errorMsg), "Failed to set keyboard hook! Error code: %lu", error);
        ScreenRecorder::LogDebug(errorMsg);
        MessageBox(NULL, errorMsg, "Error", MB_OK);
        return 0;
    }

    ScreenRecorder::LogDebug("Application started. Press Win+Shift+V to start region selection.");
    MessageBox(NULL, "Application started. Press Win+Shift+V to start region selection.", "Screen Recorder", MB_OK | MB_ICONINFORMATION);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_hook != NULL) {
        UnhookWindowsHookEx(g_hook);


    }

    delete g_recorder;

    return (int)msg.wParam;
}

int main(int argc, char* argv[]) {
    return WinMain(GetModuleHandle(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT);
}