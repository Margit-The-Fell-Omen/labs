#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "dscamera.h"
#include "reportlog.h"
#include "snapframe.h"

class AppWindow
{
public:
    explicit AppWindow(HINSTANCE hInstance);
    ~AppWindow();

    bool create();
    void run();

private:
    HINSTANCE m_hInstance = nullptr;
    HWND m_hwnd = nullptr;
    HWND m_comboCam = nullptr;
    HWND m_comboFmt = nullptr;
    HWND m_btnConnect = nullptr;
    HWND m_btnSnap = nullptr;
    HWND m_btnRecStart = nullptr;
    HWND m_btnRecStop = nullptr;

    HBRUSH m_bgBrush = nullptr;

    DsCamera m_camera;
    ReportLog m_log;
    std::vector<CameraDevice> m_devices;
    std::vector<std::wstring> m_logLines;

    std::wstring m_dir;
    std::wstring m_sDir;
    std::wstring m_stateText;
    std::wstring m_sName;
    std::wstring m_sVendor;
    std::wstring m_sRes;
    std::wstring m_sFps;
    std::wstring m_sPix;
    std::wstring m_sBackend;
    COLORREF m_dotColor = RGB(0x92, 0x83, 0x74);

    int m_deviceCount = -1;
    bool m_connected = false;
    bool m_recording = false;

    enum
    {
        IDC_COMBO_CAM = 1001,
        IDC_COMBO_FMT,
        IDC_BROWSE,
        IDC_CONNECT,
        IDC_SNAP,
        IDC_REC_START,
        IDC_REC_STOP,
        IDC_SAVE_REPORT,
        IDC_REFRESH,
        kTimerFps = 2001,
        kTimerPoll,
    };

    void createChildren();
    void setUi();
    void refreshCameras(bool fromDeviceChange);
    void log(const std::wstring& text);
    void appendLogLine(const std::wstring& line);
    void doConnect();
    void doDisconnect();
    void doSnapshot();
    void doRecordStart();
    void doRecordStop();
    void doBrowse();
    void doSaveReport();
    void updateInfo(bool force);

    void paintUi(HDC hdc);
    void paintLog(HDC hdc);

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void onCommand(WPARAM wParam, LPARAM lParam);
    void onTimer(WPARAM wParam);
};