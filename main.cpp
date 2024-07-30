#include <Windows.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <sstream>
#include <fstream>
#include <iomanip>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#define VK_LWIN 0x5B
#define ID_HOTKEY 1

#define DARKENING_ALPHA 128
#define SELECTION_ALPHA 64
#define BORDER_THICKNESS 2

// Forward declarations
void EncodeAndSaveVideo(const char* filename);
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK OverlayWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void ShowRecordingIndicator();
void HideRecordingIndicator();
std::string GenerateUniqueFilename();

std::vector<std::vector<BYTE>> g_capturedFrames;
std::atomic<bool> g_isRecording(false);
const int FRAME_RATE = 30;
const std::chrono::milliseconds FRAME_INTERVAL(1000 / FRAME_RATE);

HHOOK g_hook = NULL;
bool g_hotkeyPressed = false;
HWND g_hwnd = NULL;  // Global window handle
HWND g_indicatorWindow = NULL;

AVFormatContext *formatContext = nullptr;
AVStream *videoStream = nullptr;
AVCodecContext *codecContext = nullptr;
SwsContext *swsContext = nullptr;

std::ofstream logFile("debug.log", std::ios_base::app);

// Global variables
RECT g_selectedRegion = {0, 0, 0, 0};
bool g_isSelecting = false;
HWND g_overlayWindow = NULL;

HBRUSH g_hDarkenBrush = NULL;
HBRUSH g_hSelectionBrush = NULL;
HPEN g_hBorderPen = NULL;

void InitializeDrawingResources() {
    g_hDarkenBrush = CreateSolidBrush(RGB(0, 0, 0));
    g_hSelectionBrush = CreateSolidBrush(RGB(128, 128, 128));
    g_hBorderPen = CreatePen(PS_SOLID, BORDER_THICKNESS, RGB(255, 0, 0));
}

void CleanupDrawingResources() {
    if (g_hDarkenBrush) DeleteObject(g_hDarkenBrush);
    if (g_hSelectionBrush) DeleteObject(g_hSelectionBrush);
    if (g_hBorderPen) DeleteObject(g_hBorderPen);
}

void ShowInstructions() {
    const char* instructions =
            "Screen Recorder Instructions:\n\n"
            "1. Press Win+Shift+V to start region selection.\n"
            "2. Click and drag to select the recording area.\n"
            "3. Release the mouse button to start recording.\n"
            "4. Press Win+Shift+V again to stop recording.\n\n"
            "A red border will appear during region selection.\n"
            "A small 'Recording' indicator will show when recording is in progress.";

    MessageBox(NULL, instructions, "Screen Recorder Instructions", MB_OK | MB_ICONINFORMATION);
}

void LogDebug(const std::string& message) {
    OutputDebugStringA((message + "\n").c_str());
    logFile << message << std::endl;
    logFile.flush();
#ifdef _DEBUG
    std::cout << message << std::endl;
#endif
}

std::vector<BYTE> CaptureScreen() {
    HDC hScreenDC = GetDC(NULL);
    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);

    int width = g_selectedRegion.right - g_selectedRegion.left;
    int height = g_selectedRegion.bottom - g_selectedRegion.top;

    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

    BitBlt(hMemoryDC, 0, 0, width, height, hScreenDC, g_selectedRegion.left, g_selectedRegion.top, SRCCOPY);

    BITMAPINFOHEADER bi;
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = -height;  // Negative height to ensure top-down DIB
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = 0;
    bi.biXPelsPerMeter = 0;
    bi.biYPelsPerMeter = 0;
    bi.biClrUsed = 0;
    bi.biClrImportant = 0;

    int imageSize = width * height * 4;  // 4 bytes per pixel (32 bits)
    std::vector<BYTE> buffer(imageSize);

    GetDIBits(hMemoryDC, hBitmap, 0, height, buffer.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hScreenDC);

    return buffer;
}

int roundToEven(int n) {
    return n & ~1;
}

void CaptureFrames() {
    LogDebug("Entering CaptureFrames function");
    while (g_isRecording) {
        auto start = std::chrono::high_resolution_clock::now();

        std::vector<BYTE> frame = CaptureScreen();
        g_capturedFrames.push_back(std::move(frame));

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        if (duration < FRAME_INTERVAL) {
            std::this_thread::sleep_for(FRAME_INTERVAL - duration);
        }
    }
    LogDebug("Exiting CaptureFrames function. Frames captured: " + std::to_string(g_capturedFrames.size()));
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKbdStruct = (KBDLLHOOKSTRUCT*)lParam;
        BOOL bWinKeyDown = GetAsyncKeyState(VK_LWIN) & 0x8000;

        if (wParam == WM_KEYDOWN && pKbdStruct->vkCode == 'V' && bWinKeyDown && (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
            LogDebug("Hotkey Win+Shift+V detected");
            if (!g_isRecording && !g_isSelecting) {
                g_isSelecting = true;
                ShowWindow(g_overlayWindow, SW_SHOW);
                SetForegroundWindow(g_overlayWindow);
                LogDebug("Region selection started");
            } else if (g_isRecording) {
                g_isRecording = false;
                LogDebug("Recording stopped. Frames captured: " + std::to_string(g_capturedFrames.size()));
                HideRecordingIndicator();
                EncodeAndSaveVideo(GenerateUniqueFilename().c_str());
            }
            return 1; // Prevent further processing
        }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

bool InitializeVideoEncoder(const char* filename, int width, int height) {
    LogDebug("Initializing video encoder...");
    LogDebug("FFmpeg version: " + std::string(av_version_info()));
    width = roundToEven(width);
    height = roundToEven(height);
    LogDebug("Adjusted dimensions: " + std::to_string(width) + "x" + std::to_string(height));

    char error[AV_ERROR_MAX_STRING_SIZE];
    int ret;

    avformat_alloc_output_context2(&formatContext, NULL, NULL, filename);
    if (!formatContext) {
        LogDebug("Could not allocate output context");
        return false;
    }

    const AVCodec *codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        LogDebug("Could not find libx264 encoder");
        return false;
    }

    videoStream = avformat_new_stream(formatContext, NULL);
    if (!videoStream) {
        LogDebug("Could not allocate stream");
        return false;
    }

    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext) {
        LogDebug("Could not allocate encoding context");
        return false;
    }

// Set codec parameters
    codecContext->codec_id = AV_CODEC_ID_H264;
    codecContext->codec_type = AVMEDIA_TYPE_VIDEO;
    codecContext->width = width;
    codecContext->height = height;
    codecContext->time_base = AVRational{1, FRAME_RATE};
    codecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    codecContext->bit_rate = 400000;
    codecContext->gop_size = 12;

    if (formatContext->oformat->flags & AVFMT_GLOBALHEADER)
        codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

// Open the codec
    ret = avcodec_open2(codecContext, codec, NULL);
    if (ret < 0) {
        av_strerror(ret, error, AV_ERROR_MAX_STRING_SIZE);
        LogDebug("Could not open codec: " + std::string(error));
        return false;
    }

    ret = avcodec_parameters_from_context(videoStream->codecpar, codecContext);
    if (ret < 0) {
        av_strerror(ret, error, AV_ERROR_MAX_STRING_SIZE);
        LogDebug("Could not copy codec parameters: " + std::string(error));
        return false;
    }

    ret = avio_open(&formatContext->pb, filename, AVIO_FLAG_WRITE);
    if (ret < 0) {
        av_strerror(ret, error, AV_ERROR_MAX_STRING_SIZE);
        LogDebug("Could not open output file: " + std::string(error));
        return false;
    }

    ret = avformat_write_header(formatContext, NULL);
    if (ret < 0) {
        av_strerror(ret, error, AV_ERROR_MAX_STRING_SIZE);
        LogDebug("Error occurred when opening output file: " + std::string(error));
        return false;
    }

    swsContext = sws_getContext(width, height, AV_PIX_FMT_BGRA,
                                width, height, AV_PIX_FMT_YUV420P,
                                SWS_BICUBIC, NULL, NULL, NULL);
    if (!swsContext) {
        LogDebug("Could not initialize the conversion context");
        return false;
    }

    LogDebug("Video encoder initialized successfully");
    return true;
}

void EncodeAndSaveVideo(const char* filename) {
    LogDebug("Starting to encode and save video...");

    if (g_capturedFrames.empty()) {
        LogDebug("No frames captured!");
        MessageBox(NULL, "No frames captured!", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    int width = roundToEven(g_selectedRegion.right - g_selectedRegion.left);
    int height = roundToEven(g_selectedRegion.bottom - g_selectedRegion.top);

    if (width <= 0 || height <= 0) {
        LogDebug("Invalid region size: " + std::to_string(width) + "x" + std::to_string(height));
        MessageBox(NULL, "Invalid region size!", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    std::stringstream ss;
    ss << "Initializing video encoder with dimensions: " << width << "x" << height;
    LogDebug(ss.str());

    if (!InitializeVideoEncoder(filename, width, height)) {
        LogDebug("Failed to initialize video encoder!");
        MessageBox(NULL, "Failed to initialize video encoder!", "Error", MB_OK | MB_ICONERROR);
        return;
    }
    AVFrame *frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = width;
    frame->height = height;
    av_frame_get_buffer(frame, 0);

    AVPacket *pkt = av_packet_alloc();

    for (size_t i = 0; i < g_capturedFrames.size(); i++) {
        const uint8_t *srcSlice[1] = { g_capturedFrames[i].data() };
        int srcStride[1] = { 4 * width };

        sws_scale(swsContext, srcSlice, srcStride, 0, height,
                  frame->data, frame->linesize);

        frame->pts = i;

        int ret = avcodec_send_frame(codecContext, frame);
        if (ret < 0) {
            char error[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, error, AV_ERROR_MAX_STRING_SIZE);
            LogDebug("Encoding Error: " + std::string(error));
            break;
        }

        while (ret >= 0) {
            ret = avcodec_receive_packet(codecContext, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                char error[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, error, AV_ERROR_MAX_STRING_SIZE);
                LogDebug("Encoding Error: " + std::string(error));
                break;
            }

            av_packet_rescale_ts(pkt, AVRational{1, FRAME_RATE}, videoStream->time_base);
            pkt->stream_index = videoStream->index;
            ret = av_interleaved_write_frame(formatContext, pkt);
            av_packet_unref(pkt);
        }
    }

// Flush encoder
    avcodec_send_frame(codecContext, NULL);
    while (true) {
        int ret = avcodec_receive_packet(codecContext, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            char error[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, error, AV_ERROR_MAX_STRING_SIZE);
            LogDebug("Encoding Error: " + std::string(error));
            break;
        }

        av_packet_rescale_ts(pkt, codecContext->time_base, videoStream->time_base);
        pkt->stream_index = videoStream->index;
        av_interleaved_write_frame(formatContext, pkt);
        av_packet_unref(pkt);
    }

    av_write_trailer(formatContext);

    avcodec_free_context(&codecContext);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    sws_freeContext(swsContext);
    avio_closep(&formatContext->pb);
    avformat_free_context(formatContext);

    LogDebug("Video saved successfully!");
    MessageBox(NULL, "Video saved successfully!", "Success", MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK OverlayWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static POINT start = {0, 0};
    static POINT end = {0, 0};
    static bool isDrawing = false;

    switch (uMsg) {
        case WM_CREATE:
            InitializeDrawingResources();
            return 0;

        case WM_DESTROY:
            CleanupDrawingResources();
            return 0;

        case WM_LBUTTONDOWN:
            start.x = LOWORD(lParam);
            start.y = HIWORD(lParam);
            end = start;
            isDrawing = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, TRUE);
            LogDebug("Mouse down at " + std::to_string(start.x) + ", " + std::to_string(start.y));
            return 0;

        case WM_MOUSEMOVE:
            if (isDrawing) {
                end.x = LOWORD(lParam);
                end.y = HIWORD(lParam);
                InvalidateRect(hwnd, NULL, TRUE);
                LogDebug("Drawing to " + std::to_string(end.x) + ", " + std::to_string(end.y));
            }
            return 0;

        case WM_LBUTTONUP:
            if (isDrawing) {
                isDrawing = false;
                ReleaseCapture();
                g_selectedRegion.left = min(start.x, end.x);
                g_selectedRegion.top = min(start.y, end.y);
                g_selectedRegion.right = max(start.x, end.x);
                g_selectedRegion.bottom = max(start.y, end.y);
                ShowWindow(hwnd, SW_HIDE);
                g_isSelecting = false;
                g_isRecording = true;
                g_capturedFrames.clear();
                std::thread captureThread(CaptureFrames);
                captureThread.detach();
                ShowRecordingIndicator();
                LogDebug("Recording started with region: " +
                         std::to_string(g_selectedRegion.left) + ", " +
                         std::to_string(g_selectedRegion.top) + ", " +
                         std::to_string(g_selectedRegion.right) + ", " +
                         std::to_string(g_selectedRegion.bottom));
            }
            return 0;

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

// Darken the entire screen
            RECT screenRect;
            GetClientRect(hwnd, &screenRect);
            SetBkColor(hdc, RGB(0, 0, 0));
            SetTextColor(hdc, RGB(255, 255, 255));
            ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &screenRect, NULL, 0, NULL);

            if (isDrawing) {
// Draw the selection rectangle
                RECT selectionRect = {min(start.x, end.x), min(start.y, end.y),
                                      max(start.x, end.x), max(start.y, end.y)};
                FillRect(hdc, &selectionRect, g_hSelectionBrush);

// Draw the border
                HPEN oldPen = (HPEN)SelectObject(hdc, g_hBorderPen);
                HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, selectionRect.left, selectionRect.top, selectionRect.right, selectionRect.bottom);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

HWND CreateOverlayWindow(HINSTANCE hInstance) {
    WNDCLASS wc = {};
    wc.lpfnWndProc = OverlayWindowProc;
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
        LogDebug("Failed to create overlay window. Error: " + std::to_string(GetLastError()));
    } else {
// Make the window semi-transparent
        SetLayeredWindowAttributes(hwnd, 0, DARKENING_ALPHA, LWA_ALPHA);
        LogDebug("Overlay window created successfully");
    }

    return hwnd;
}

void ShowRecordingIndicator() {
    WNDCLASS wc = {};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "RecordingIndicatorClass";
    RegisterClass(&wc);

    g_indicatorWindow = CreateWindowEx(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            "RecordingIndicatorClass",
            "Recording",
            WS_POPUP | WS_VISIBLE,
            10, 10, 100, 30,
            NULL, NULL, GetModuleHandle(NULL), NULL
    );

    if (g_indicatorWindow) {
        SetWindowLong(g_indicatorWindow, GWL_EXSTYLE,
                      GetWindowLong(g_indicatorWindow, GWL_EXSTYLE) | WS_EX_LAYERED);
        SetLayeredWindowAttributes(g_indicatorWindow, RGB(255, 0, 0), 200, LWA_COLORKEY | LWA_ALPHA);

// Set a timer to blink the indicator
        SetTimer(g_indicatorWindow, 1, 500, NULL);
    }
}

void HideRecordingIndicator() {
    if (g_indicatorWindow) {
        DestroyWindow(g_indicatorWindow);
        g_indicatorWindow = NULL;
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_HOTKEY:
            LogDebug("WM_HOTKEY received in WindowProc");
            if (wParam == ID_HOTKEY && g_hotkeyPressed) {
                g_hotkeyPressed = false;  // Reset the flag

                if (!g_isRecording && !g_isSelecting) {
// Start region selection
                    g_isSelecting = true;
                    ShowWindow(g_overlayWindow, SW_SHOW);
                    SetForegroundWindow(g_overlayWindow);
                    LogDebug("Region selection started!");
                } else if (g_isRecording) {
// Stop recording
                    g_isRecording = false;
                    char buffer[100];
                    snprintf(buffer, sizeof(buffer), "Recording stopped! Frames captured: %zu", g_capturedFrames.size());
                    LogDebug(buffer);
                    MessageBox(hwnd, buffer, "Screen Recorder", MB_OK | MB_ICONINFORMATION);
                    EncodeAndSaveVideo(GenerateUniqueFilename().c_str());
                }
            }
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

std::string GenerateUniqueFilename() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << "C:/ScreenRecordings/recording_";
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d_%H-%M-%S");
    ss << ".mp4";

    return ss.str();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    CreateDirectoryA("C:/ScreenRecordings", NULL);
#ifdef _DEBUG
    AllocConsole();
    FILE* pCout;
    freopen_s(&pCout, "CONOUT$", "w", stdout);
    freopen_s(&pCout, "CONOUT$", "w", stderr);
#endif
    LogDebug("Screen Recorder application starting...");
    ShowInstructions();

// Initialize FFmpeg
    av_log_set_level(AV_LOG_VERBOSE);
    av_log_set_callback([](void*, int level, const char* fmt, va_list vl) {
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), fmt, vl);
        LogDebug("FFmpeg: " + std::string(buffer));
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
        LogDebug("Failed to create window");
        return 0;
    }

    g_overlayWindow = CreateOverlayWindow(hInstance);
    if (g_overlayWindow == NULL) {
        LogDebug("Failed to create overlay window");
        return 0;
    }

    g_hook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    if (g_hook == NULL) {
        DWORD error = GetLastError();
        char errorMsg[256];
        snprintf(errorMsg, sizeof(errorMsg), "Failed to set keyboard hook! Error code: %lu", error);
        LogDebug(errorMsg);
        MessageBox(NULL, errorMsg, "Error", MB_OK);
        return 0;
    }

    LogDebug("Application started. Press Win+Shift+V to start region selection.");
    MessageBox(NULL, "Application started. Press Win+Shift+V to start region selection.", "Screen Recorder", MB_OK | MB_ICONINFORMATION);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_hook != NULL) {
        UnhookWindowsHookEx(g_hook);
    }

    return (int)msg.wParam;
}

int main(int argc, char* argv[]) {
    return WinMain(GetModuleHandle(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT);
}