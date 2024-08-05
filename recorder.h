// recorder.h
#pragma once

#include <Windows.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <ShellScalingApi.h>
#pragma comment(lib, "Shcore.lib")

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

class ScreenRecorder {
public:
    ScreenRecorder();
    ~ScreenRecorder();

    void StartRegionSelection();
    void StopRecording();
    void ToggleRecording();
    void LogCaptureDetails();
    bool IsRecording() const { return m_isRecording; }
    bool IsSelecting() const { return m_isSelecting; }

    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK OverlayWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    static void LogDebug(const std::string& message);
    HWND GetOverlayWindow() const { return m_overlayWindow; }
    void SetOverlayWindow(HWND hwnd) { m_overlayWindow = hwnd; }

private:
    void InitializeDrawingResources();
    void CleanupDrawingResources();
    void CaptureFrames();
    std::vector<BYTE> CaptureScreen();
    bool InitializeVideoEncoder(const char* filename, int width, int height);
    void EncodeAndSaveVideo(const char* filename);
    std::string GenerateUniqueFilename();
    void ShowRecordingIndicator();
    void HideRecordingIndicator();
    void DrawSelectionRect();

    std::vector<std::vector<BYTE>> m_capturedFrames;
    std::atomic<bool> m_isRecording;
    std::atomic<bool> m_isSelecting;
    RECT m_selectedRegion;
    HWND m_overlayWindow;
    HWND m_indicatorWindow;
    HWND m_selectionFeedbackWindow;

    HBRUSH m_hDarkenBrush;
    HBRUSH m_hSelectionBrush;
    HPEN m_hBorderPen;

    static const int FRAME_RATE = 30;
    static const std::chrono::milliseconds FRAME_INTERVAL;

    // FFmpeg contexts
    AVFormatContext* m_formatContext;
    AVStream* m_videoStream;
    AVCodecContext* m_codecContext;
    SwsContext* m_swsContext;

    static ScreenRecorder* s_instance;
};
inline void LogConcise(const std::string& category, const std::string& message) {
    std::ofstream logFile("concise_debug.log", std::ios_base::app);
    logFile << "[" << category << "] " << message << std::endl;
    logFile.close();
}
void ShowInstructions();