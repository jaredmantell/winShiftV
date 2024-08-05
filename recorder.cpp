// recorder.cpp
#include "recorder.h"

ScreenRecorder* ScreenRecorder::s_instance = nullptr;
const std::chrono::milliseconds ScreenRecorder::FRAME_INTERVAL(1000 / FRAME_RATE);

std::ofstream logFile("debug.log", std::ios_base::app);
std::string av_error_to_string(int errnum) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, errbuf, AV_ERROR_MAX_STRING_SIZE);
    return std::string(errbuf);
}
ScreenRecorder::ScreenRecorder()
        : m_isRecording(false), m_isSelecting(false),
          m_formatContext(nullptr), m_videoStream(nullptr),
          m_codecContext(nullptr), m_swsContext(nullptr),
          m_overlayWindow(nullptr), m_selectionFeedbackWindow(nullptr) {
    s_instance = this;
    InitializeDrawingResources();
}

ScreenRecorder::~ScreenRecorder() {
    CleanupDrawingResources();
    s_instance = nullptr;
}

void ScreenRecorder::InitializeDrawingResources() {
    m_hDarkenBrush = CreateSolidBrush(RGB(0, 0, 0));
    m_hSelectionBrush = CreateSolidBrush(RGB(128, 128, 128));
    m_hBorderPen = CreatePen(PS_SOLID, BORDER_THICKNESS, RGB(255, 0, 0));
}

int roundToEven(int n) {
    return n & ~1;
}
void ScreenRecorder::LogCaptureDetails() {
    HMONITOR hMonitor = MonitorFromWindow(m_overlayWindow, MONITOR_DEFAULTTONEAREST);
    UINT dpiX, dpiY;
    GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    float scaleFactor = dpiX / 96.0f;

    int width = m_selectedRegion.right - m_selectedRegion.left;
    int height = m_selectedRegion.bottom - m_selectedRegion.top;
    int scaledWidth = static_cast<int>(width * scaleFactor);
    int scaledHeight = static_cast<int>(height * scaleFactor);

    std::stringstream ss;
    ss << "Selected region: " << m_selectedRegion.left << "," << m_selectedRegion.top
       << " to " << m_selectedRegion.right << "," << m_selectedRegion.bottom;
    ss << " | Size: " << width << "x" << height;
    ss << " | Scaled region: "
       << static_cast<int>(m_selectedRegion.left * scaleFactor) << ","
       << static_cast<int>(m_selectedRegion.top * scaleFactor) << " to "
       << static_cast<int>(m_selectedRegion.right * scaleFactor) << ","
       << static_cast<int>(m_selectedRegion.bottom * scaleFactor);
    ss << " | Scaled size: " << scaledWidth << "x" << scaledHeight;
    ss << " | DPI: " << dpiX << "x" << dpiY << " (Scale factor: " << scaleFactor << ")";
    LogDebug(ss.str());
}
void ScreenRecorder::CleanupDrawingResources() {
    if (m_hDarkenBrush) DeleteObject(m_hDarkenBrush);
    if (m_hSelectionBrush) DeleteObject(m_hSelectionBrush);
    if (m_hBorderPen) DeleteObject(m_hBorderPen);
}

void ScreenRecorder::LogDebug(const std::string& message) {
    OutputDebugStringA((message + "\n").c_str());
    logFile << message << std::endl;
    logFile.flush();
#ifdef _DEBUG
    std::cout << message << std::endl;
#endif
}

void ScreenRecorder::StartRegionSelection() {
    if (!m_isRecording && !m_isSelecting) {
        m_isSelecting = true;
        ShowWindow(m_overlayWindow, SW_SHOW);
        SetForegroundWindow(m_overlayWindow);
        LogDebug("Region selection started");
    }
}

void ScreenRecorder::StopRecording() {
    if (m_isRecording) {
        m_isRecording = false;
        LogDebug("Recording stopped. Frames captured: " + std::to_string(m_capturedFrames.size()));
        HideRecordingIndicator();
        if (m_selectionFeedbackWindow) {
            DestroyWindow(m_selectionFeedbackWindow);
            m_selectionFeedbackWindow = nullptr;
        }
        EncodeAndSaveVideo(GenerateUniqueFilename().c_str());
    }
}
void ScreenRecorder::ToggleRecording() {
    if (!m_isRecording) {
        LogDebug("Starting region selection");
        StartRegionSelection();
    } else {
        LogDebug("Stopping recording");
        LogCaptureDetails();

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        StopRecording();

        LogDebug("Recording stopped");
    }
}
void ScreenRecorder::CaptureFrames() {
    LogCaptureDetails();
    LogConcise("CaptureFrames", "Entering CaptureFrames function");
    auto startTime = std::chrono::high_resolution_clock::now();
    int frameCount = 0;
    while (m_isRecording) {
        auto frameStart = std::chrono::high_resolution_clock::now();

        LogConcise("CaptureFrames", "Starting capture of frame " + std::to_string(frameCount));
        std::vector<BYTE> frame = CaptureScreen();
        LogConcise("CaptureFrames", "Finished capture of frame " + std::to_string(frameCount) +
                                    ". Frame size: " + std::to_string(frame.size()) + " bytes");

        m_capturedFrames.push_back(std::move(frame));
        frameCount++;

        auto frameEnd = std::chrono::high_resolution_clock::now();
        auto frameDuration = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - frameStart);

        if (frameDuration < FRAME_INTERVAL) {
            std::this_thread::sleep_for(FRAME_INTERVAL - frameDuration);
        }

        // Ensure we capture for at least 1 second
        auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - startTime);
        if (elapsedTime >= std::chrono::seconds(1)) {
            break;
        }
    }
    LogConcise("CaptureFrames", "Exiting CaptureFrames function. Frames captured: " + std::to_string(m_capturedFrames.size()));
    LogCaptureDetails();
}
std::vector<BYTE> ScreenRecorder::CaptureScreen() {
    LogDebug("Starting screen capture");
    LogCaptureDetails();

    HDC hScreenDC = GetDC(NULL);
    if (!hScreenDC) {
        LogDebug("Failed to get screen DC");
        return {};
    }

    int width = m_selectedRegion.right - m_selectedRegion.left;
    int height = m_selectedRegion.bottom - m_selectedRegion.top;

    LogDebug("Capture area: " + std::to_string(width) + "x" + std::to_string(height) +
             " at (" + std::to_string(m_selectedRegion.left) + "," + std::to_string(m_selectedRegion.top) + ")");

    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    if (!hMemoryDC) {
        LogDebug("Failed to create compatible DC");
        ReleaseDC(NULL, hScreenDC);
        return {};
    }

    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
    if (!hBitmap) {
        LogDebug("Failed to create compatible bitmap");
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hScreenDC);
        return {};
    }

    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

    if (!BitBlt(hMemoryDC, 0, 0, width, height,
                hScreenDC, m_selectedRegion.left, m_selectedRegion.top,
                SRCCOPY | CAPTUREBLT)) {
        LogDebug("BitBlt failed. Error: " + std::to_string(GetLastError()));
        SelectObject(hMemoryDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hScreenDC);
        return {};
    }

    BITMAPINFOHEADER bi = {0};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = -height;  // Negative for top-down DIB
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    std::vector<BYTE> buffer(width * height * 4);

    if (!GetDIBits(hMemoryDC, hBitmap, 0, height, buffer.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS)) {
        LogDebug("GetDIBits failed. Error: " + std::to_string(GetLastError()));
        SelectObject(hMemoryDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hScreenDC);
        return {};
    }

    // Log corner pixel colors for debugging
    auto logPixel = [&](int x, int y, const std::string& corner) {
        int index = (y * width + x) * 4;
        LogDebug(corner + " pixel: R" + std::to_string(buffer[index + 2]) +
                 " G" + std::to_string(buffer[index + 1]) +
                 " B" + std::to_string(buffer[index]) +
                 " A" + std::to_string(buffer[index + 3]));
    };

    logPixel(0, 0, "TopLeft");
    logPixel(width - 1, 0, "TopRight");
    logPixel(0, height - 1, "BottomLeft");
    logPixel(width - 1, height - 1, "BottomRight");

    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hScreenDC);

    LogDebug("Capture completed. Buffer size: " + std::to_string(buffer.size()) + " bytes");
    return buffer;
}
bool ScreenRecorder::InitializeVideoEncoder(const char* filename, int width, int height) {
    LogDebug("Initializing video encoder...");
    LogDebug("Original dimensions: " + std::to_string(width) + "x" + std::to_string(height));

    width = roundToEven(width);
    height = roundToEven(height);

    LogDebug("Adjusted dimensions: " + std::to_string(width) + "x" + std::to_string(height));

    LogDebug("FFmpeg version: " + std::string(av_version_info()));
    char error[AV_ERROR_MAX_STRING_SIZE];
    int ret;

    // Allocate the output media context
    avformat_alloc_output_context2(&m_formatContext, NULL, NULL, filename);
    if (!m_formatContext) {
        LogDebug("Could not allocate output context");
        return false;
    }

    // Find the encoder
    const AVCodec *codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        LogDebug("Could not find libx264 encoder");
        return false;
    }

    // Create a new video stream
    m_videoStream = avformat_new_stream(m_formatContext, NULL);
    if (!m_videoStream) {
        LogDebug("Could not allocate stream");
        return false;
    }

    // Allocate an encoding context
    m_codecContext = avcodec_alloc_context3(codec);
    if (!m_codecContext) {
        LogDebug("Could not allocate encoding context");
        return false;
    }

    // Set codec parameters
    m_codecContext->codec_id = AV_CODEC_ID_H264;
    m_codecContext->codec_type = AVMEDIA_TYPE_VIDEO;
    m_codecContext->width = width;
    m_codecContext->height = height;
    m_codecContext->time_base.num = 1;
    m_codecContext->time_base.den = FRAME_RATE;
    m_codecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    m_codecContext->bit_rate = 1000000;  // Increase bitrate to 1 Mbps
    m_codecContext->gop_size = 10;
    m_codecContext->max_b_frames = 1;
    m_codecContext->qmin = 10;
    m_codecContext->qmax = 51;

    // Set global header flags if needed
    if (m_formatContext->oformat->flags & AVFMT_GLOBALHEADER)
        m_codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // Open the codec
    ret = avcodec_open2(m_codecContext, codec, NULL);
    if (ret < 0) {
        av_strerror(ret, error, AV_ERROR_MAX_STRING_SIZE);
        LogDebug("Could not open codec: " + std::string(error));
        return false;
    }

    // Copy codec parameters to the stream
    ret = avcodec_parameters_from_context(m_videoStream->codecpar, m_codecContext);
    if (ret < 0) {
        av_strerror(ret, error, AV_ERROR_MAX_STRING_SIZE);
        LogDebug("Could not copy codec parameters: " + std::string(error));
        return false;
    }

    // Open the output file
    ret = avio_open(&m_formatContext->pb, filename, AVIO_FLAG_WRITE);
    if (ret < 0) {
        av_strerror(ret, error, AV_ERROR_MAX_STRING_SIZE);
        LogDebug("Could not open output file: " + std::string(error));
        return false;
    }

    // Write the stream header
    ret = avformat_write_header(m_formatContext, NULL);
    if (ret < 0) {
        av_strerror(ret, error, AV_ERROR_MAX_STRING_SIZE);
        LogDebug("Error occurred when opening output file: " + std::string(error));
        return false;
    }

    // Initialize the SwsContext for pixel format conversion
    m_swsContext = sws_getContext(width, height, AV_PIX_FMT_BGRA,
                                  width, height, AV_PIX_FMT_YUV420P,
                                  SWS_BICUBIC, NULL, NULL, NULL);

    if (!m_swsContext) {
        LogDebug("Could not initialize the conversion context");
        return false;
    }

    LogDebug("Video encoder initialized successfully");
    return true;
}
void ScreenRecorder::EncodeAndSaveVideo(const char* filename) {
    LogDebug("Starting to encode and save video...");
    if (m_capturedFrames.empty()) {
        LogDebug("No frames captured!");
        MessageBox(NULL, "No frames captured!", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    int width = m_selectedRegion.right - m_selectedRegion.left;
    int height = m_selectedRegion.bottom - m_selectedRegion.top;

    LogDebug("Encoding video with dimensions: " + std::to_string(width) + "x" + std::to_string(height));
    if (!InitializeVideoEncoder(filename, width, height)) {
        LogDebug("Failed to initialize video encoder!");
        MessageBox(NULL, "Failed to initialize video encoder!", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        LogDebug("Could not allocate video frame");
        return;
    }
    frame->format = m_codecContext->pix_fmt;
    frame->width  = m_codecContext->width;
    frame->height = m_codecContext->height;
    int ret = av_frame_get_buffer(frame, 32);
    if (ret < 0) {
        LogDebug("Could not allocate frame data.");
        return;
    }

    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        LogDebug("Could not allocate packet");
        return;
    }

    AVRational src_tb;
    src_tb.num = 1;
    src_tb.den = FRAME_RATE;

    for (size_t i = 0; i < m_capturedFrames.size(); i++) {
        const uint8_t *srcSlice[1] = { m_capturedFrames[i].data() };
        int srcStride[1] = { width * 4 };  // 4 bytes per pixel for BGRA

        sws_scale(m_swsContext, srcSlice, srcStride, 0, height,
                  frame->data, frame->linesize);

        frame->pts = i;

        ret = avcodec_send_frame(m_codecContext, frame);
        if (ret < 0) {
            LogDebug("Error sending frame for encoding: " + av_error_to_string(ret));
            break;
        }

        while (ret >= 0) {
            ret = avcodec_receive_packet(m_codecContext, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                LogDebug("Error during encoding: " + av_error_to_string(ret));
                break;
            }

            LogDebug("Encoded frame " + std::to_string(i) + ", size: " + std::to_string(pkt->size) + " bytes");

            av_packet_rescale_ts(pkt, src_tb, m_videoStream->time_base);
            pkt->stream_index = m_videoStream->index;
            ret = av_interleaved_write_frame(m_formatContext, pkt);
            if (ret < 0) {
                LogDebug("Error writing frame: " + av_error_to_string(ret));
            }
            av_packet_unref(pkt);
        }
    }

    // Flush the encoder
    avcodec_send_frame(m_codecContext, NULL);
    while (true) {
        ret = avcodec_receive_packet(m_codecContext, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            LogDebug("Error flushing encoder");
            break;
        }

        LogDebug("Flushed frame, size: " + std::to_string(pkt->size) + " bytes");

        av_packet_rescale_ts(pkt, src_tb, m_videoStream->time_base);
        pkt->stream_index = m_videoStream->index;
        av_interleaved_write_frame(m_formatContext, pkt);
        av_packet_unref(pkt);
    }

    av_write_trailer(m_formatContext);

    avcodec_free_context(&m_codecContext);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    sws_freeContext(m_swsContext);
    avio_closep(&m_formatContext->pb);
    avformat_free_context(m_formatContext);

    LogDebug("Video saved successfully!");
    MessageBox(NULL, "Video saved successfully!", "Success", MB_OK | MB_ICONINFORMATION);
}
    std::string ScreenRecorder::GenerateUniqueFilename() {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);

        std::stringstream ss;
        ss << "C:/ScreenRecordings/recording_";
        ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d_%H-%M-%S");
        ss << ".mp4";

        return ss.str();
    }

    void ScreenRecorder::ShowRecordingIndicator() {
        WNDCLASS wc = {};
        wc.lpfnWndProc = DefWindowProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = "RecordingIndicatorClass";
        RegisterClass(&wc);

        m_indicatorWindow = CreateWindowEx(
                WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                "RecordingIndicatorClass",
                "Recording",
                WS_POPUP | WS_VISIBLE,
                10, 10, 100, 30,
                NULL, NULL, GetModuleHandle(NULL), NULL
        );

        if (m_indicatorWindow) {
            SetWindowLong(m_indicatorWindow, GWL_EXSTYLE,
                          GetWindowLong(m_indicatorWindow, GWL_EXSTYLE) | WS_EX_LAYERED);
            SetLayeredWindowAttributes(m_indicatorWindow, RGB(255, 0, 0), 200, LWA_COLORKEY | LWA_ALPHA);

            // Set a timer to blink the indicator
            SetTimer(m_indicatorWindow, 1, 500, NULL);
        }
    }

    void ScreenRecorder::HideRecordingIndicator() {
        if (m_indicatorWindow) {
            DestroyWindow(m_indicatorWindow);
            m_indicatorWindow = NULL;
        }
    }

    void ScreenRecorder::DrawSelectionRect() {
        if (!m_selectionFeedbackWindow) {
            WNDCLASS wc = {};
            wc.lpfnWndProc = DefWindowProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = "SelectionFeedbackClass";
            RegisterClass(&wc);

            m_selectionFeedbackWindow = CreateWindowEx(
                    WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_LAYERED,
                    "SelectionFeedbackClass",
                    NULL,
                    WS_POPUP,
                    m_selectedRegion.left, m_selectedRegion.top,
                    m_selectedRegion.right - m_selectedRegion.left,
                    m_selectedRegion.bottom - m_selectedRegion.top,
                    NULL, NULL, GetModuleHandle(NULL), NULL
            );

            SetLayeredWindowAttributes(m_selectionFeedbackWindow, RGB(255, 0, 0), 128, LWA_COLORKEY | LWA_ALPHA);
        }
        HMONITOR hMonitor = MonitorFromWindow(m_selectionFeedbackWindow, MONITOR_DEFAULTTONEAREST);
        UINT dpiX, dpiY;
        GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        float scaleFactor = dpiX / 96.0f;

        // Adjust the window position and size for DPI scaling
        SetWindowPos(m_selectionFeedbackWindow, HWND_TOPMOST,
                     static_cast<int>(m_selectedRegion.left * scaleFactor),
                     static_cast<int>(m_selectedRegion.top * scaleFactor),
                     static_cast<int>((m_selectedRegion.right - m_selectedRegion.left) * scaleFactor),
                     static_cast<int>((m_selectedRegion.bottom - m_selectedRegion.top) * scaleFactor),
                     SWP_SHOWWINDOW);

        ShowWindow(m_selectionFeedbackWindow, SW_SHOW);
        UpdateWindow(m_selectionFeedbackWindow);

        HDC hdc = GetDC(m_selectionFeedbackWindow);
        HPEN hPen = CreatePen(PS_SOLID, 2, RGB(255, 0, 0));
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

        RECT rect;
        GetClientRect(m_selectionFeedbackWindow, &rect);

        MoveToEx(hdc, 0, 0, NULL);
        LineTo(hdc, rect.right - 1, 0);
        LineTo(hdc, rect.right - 1, rect.bottom - 1);
        LineTo(hdc, 0, rect.bottom - 1);
        LineTo(hdc, 0, 0);

        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);
        ReleaseDC(m_selectionFeedbackWindow, hdc);
    }

    LRESULT CALLBACK ScreenRecorder::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode == HC_ACTION) {
            KBDLLHOOKSTRUCT* pKbdStruct = (KBDLLHOOKSTRUCT*)lParam;
            BOOL bWinKeyDown = GetAsyncKeyState(VK_LWIN) & 0x8000;

            if (wParam == WM_KEYDOWN && pKbdStruct->vkCode == 'V' && bWinKeyDown && (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
                LogDebug("Hotkey Win+Shift+V detected");
                if (s_instance) {
                    s_instance->ToggleRecording();
                }
                return 1; // Prevent further processing
            }
        }
        return CallNextHookEx(NULL, nCode, wParam, lParam);
    }

    LRESULT CALLBACK ScreenRecorder::OverlayWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        static POINT start = {0, 0};
        static POINT end = {0, 0};
        static bool isDrawing = false;

        if (s_instance == nullptr) return DefWindowProc(hwnd, uMsg, wParam, lParam);

        switch (uMsg) {
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

                    // Get the DPI scaling factor
                    HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                    UINT dpiX, dpiY;
                    GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
                    float scaleFactor = dpiX / 96.0f;

                    // Adjust the selected region for DPI scaling
                    s_instance->m_selectedRegion.left = static_cast<LONG>(min(start.x, end.x) / scaleFactor);
                    s_instance->m_selectedRegion.top = static_cast<LONG>(min(start.y, end.y) / scaleFactor);
                    s_instance->m_selectedRegion.right = static_cast<LONG>(max(start.x, end.x) / scaleFactor);
                    s_instance->m_selectedRegion.bottom = static_cast<LONG>(max(start.y, end.y) / scaleFactor);

                    std::stringstream ss;
                    ss << "Visual: " << min(start.x, end.x) << "," << min(start.y, end.y)
                       << " to " << max(start.x, end.x) << "," << max(start.y, end.y)
                       << " | Scaled: " << s_instance->m_selectedRegion.left << ","
                       << s_instance->m_selectedRegion.top << " to "
                       << s_instance->m_selectedRegion.right << ","
                       << s_instance->m_selectedRegion.bottom
                       << " | Scale Factor: " << scaleFactor;
                    LogConcise("Selection", ss.str());

                    ShowWindow(hwnd, SW_HIDE);
                    s_instance->m_isSelecting = false;
                    s_instance->m_isRecording = true;
                    s_instance->m_capturedFrames.clear();
                    s_instance->DrawSelectionRect();
                    std::thread captureThread(&ScreenRecorder::CaptureFrames, s_instance);
                    captureThread.detach();
                    s_instance->ShowRecordingIndicator();
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
                    FillRect(hdc, &selectionRect, s_instance->m_hSelectionBrush);

                    // Draw the border
                    HPEN oldPen = (HPEN)SelectObject(hdc, s_instance->m_hBorderPen);
                    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                    Rectangle(hdc, selectionRect.left, selectionRect.top, selectionRect.right, selectionRect.bottom);
                    SelectObject(hdc, oldPen);
                    SelectObject(hdc, oldBrush);
                }

                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_KEYDOWN:
                if (wParam == VK_ESCAPE) {
                    s_instance->m_isSelecting = false;
                    ShowWindow(hwnd, SW_HIDE);
                    LogDebug("Selection cancelled");
                }
                return 0;
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
void ShowInstructions() {
    const char* instructions =
            "Screen Recorder Instructions:\n\n"
            "1. Press Win+Shift+V to start region selection.\n"
            "2. Click and drag to select the recording area.\n"
            "3. Release the mouse button to start recording.\n"
            "4. Press Win+Shift+V again to stop recording.\n"
            "5. Press ESC during selection to cancel.\n\n"
            "A red border will appear during region selection.\n"
            "A small 'Recording' indicator will show when recording is in progress.";

    MessageBox(NULL, instructions, "Screen Recorder Instructions", MB_OK | MB_ICONINFORMATION);
}
