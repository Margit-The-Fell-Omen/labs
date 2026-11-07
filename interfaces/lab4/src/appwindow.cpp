#include "appwindow.h"

#include "strutil.h"

#include <commdlg.h>
#include <shlobj.h>

#include <algorithm>
#include <cwchar>

namespace {

constexpr wchar_t kClassName[] = L"WebcamAppWnd";

constexpr int kWndW = 1016;
constexpr int kWndH = 748;

// gruvbox palette
constexpr COLORREF kBg = RGB(0x1D, 0x20, 0x21);
constexpr COLORREF kBg0 = RGB(0x28, 0x28, 0x28);
constexpr COLORREF kBg1 = RGB(0x3C, 0x38, 0x36);
constexpr COLORREF kBg2 = RGB(0x50, 0x49, 0x45);
constexpr COLORREF kFg = RGB(0xEB, 0xDB, 0xB2);
constexpr COLORREF kGray = RGB(0x92, 0x83, 0x74);
constexpr COLORREF kYellow = RGB(0xFB, 0xBD, 0x2F);
constexpr COLORREF kGreen = RGB(0xB8, 0xBB, 0x26);
constexpr COLORREF kRed = RGB(0xFB, 0x49, 0x34);

constexpr RECT kHeaderDivider = {12, 104, 988, 105};
constexpr RECT kPreview = {12, 110, 988, 340};
constexpr RECT kInfoCard = {12, 352, 988, 452};
constexpr RECT kDotRect = {176, 380, 176 + 18, 380 + 18};
constexpr RECT kStateText = {202, 382, 600, 402};
constexpr RECT kLogLabel = {12, 464, 300, 484};
constexpr RECT kLogRect = {12, 484, 988, 566};
constexpr RECT kDirRect = {148, 74, 712, 98};

struct InfoRow
{
    const wchar_t* label;
    const std::wstring* value;
    int labelX;
    int valueX;
    int y;
};

HFONT uiFont()
{
    return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

HBRUSH solid(COLORREF color)
{
    return CreateSolidBrush(color);
}

void fillRect(HDC dc, const RECT& rc, COLORREF color)
{
    HBRUSH b = solid(color);
    FillRect(dc, &rc, b);
    DeleteObject(b);
}

void paintText(HDC dc, int x, int y, COLORREF color, const std::wstring& text)
{
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    TextOutW(dc, x, y, text.c_str(), static_cast<int>(text.size()));
}

void paintValue(HDC dc, const RECT& rc, COLORREF color, const std::wstring& text)
{
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), const_cast<LPRECT>(&rc),
              DT_SINGLELINE | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void setStaticText(HWND hwnd, const std::wstring& text)
{
    if (hwnd)
        SetWindowTextW(hwnd, text.c_str());
}

std::wstring timeStampName(const wchar_t* prefix, const wchar_t* ext)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    return wformat(L"%ls%04d%02d%02d_%02d%02d%02d%ls", prefix, t.wYear, t.wMonth, t.wDay,
                   t.wHour, t.wMinute, t.wSecond, ext);
}

} // namespace

AppWindow::AppWindow(HINSTANCE hInstance)
    : m_hInstance(hInstance)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
}

AppWindow::~AppWindow()
{
    CoUninitialize();
}

bool AppWindow::create()
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &AppWindow::wndProc;
    wc.hInstance = m_hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    m_bgBrush = CreateSolidBrush(kBg);

    m_hwnd = CreateWindowExW(
        0, kClassName, widen("Веб-камера — захват через DirectShow").c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, kWndW, kWndH, nullptr, nullptr, m_hInstance, nullptr);

    if (!m_hwnd)
        return false;

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    wchar_t buffer[MAX_PATH] = {};
    GetCurrentDirectoryW(MAX_PATH, buffer);
    m_dir = buffer;

    createChildren();
    setUi();
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    refreshCameras(false);
    SetTimer(m_hwnd, kTimerFps, 500, nullptr);
    SetTimer(m_hwnd, kTimerPoll, 2000, nullptr);
    log(L"Приложение запущено. Выберите камеру и нажмите «Подключить камеру».");
    return true;
}

void AppWindow::run()
{
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void AppWindow::createChildren()
{
    const HFONT font = uiFont();

    auto makeButton = [&](int id, const wchar_t* text, int& x, int y) -> HWND {
        const int len = static_cast<int>(wcslen(text));
        HWND h = CreateWindowExW(0, L"BUTTON", text,
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                 x, y, 60, 26, m_hwnd,
                                 reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
                                 m_hInstance, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HDC dc = GetDC(h);
        HGDIOBJ oldFont = SelectObject(dc, font);
        SIZE size = {};
        GetTextExtentPoint32W(dc, text, len, &size);
        SelectObject(dc, oldFont);
        ReleaseDC(h, dc);
        const int width = size.cx + 22;
        SetWindowPos(h, nullptr, x, y, width, 26, SWP_NOZORDER);
        x += width + 8;
        return h;
    };

    m_comboCam = CreateWindowExW(0, L"COMBOBOX", L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST
                                     | WS_VSCROLL,
                                 86, 9, 400, 240, m_hwnd,
                                 reinterpret_cast<HMENU>(IDC_COMBO_CAM), m_hInstance, nullptr);
    SendMessageW(m_comboCam, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    int x1 = 496;
    makeButton(IDC_REFRESH, L"Обновить", x1, 9);
    m_btnConnect = makeButton(IDC_CONNECT, L"Подключить камеру", x1, 9);

    m_comboFmt = CreateWindowExW(0, L"COMBOBOX", L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST
                                     | WS_VSCROLL,
                                 86, 42, 96, 140, m_hwnd,
                                 reinterpret_cast<HMENU>(IDC_COMBO_FMT), m_hInstance, nullptr);
    SendMessageW(m_comboFmt, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(m_comboFmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"BMP"));
    SendMessageW(m_comboFmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"JPEG"));
    SendMessageW(m_comboFmt, CB_SETCURSEL, 0, 0);

    int x2 = 192;
    m_btnSnap = makeButton(IDC_SNAP, L"Сделать снимок", x2, 42);
    m_btnRecStart = makeButton(IDC_REC_START, L"Начать запись", x2, 42);
    m_btnRecStop = makeButton(IDC_REC_STOP, L"Остановить запись", x2, 42);
    makeButton(IDC_SAVE_REPORT, L"Сохранить отчёт", x2, 42);

    int x3 = 722;
    makeButton(IDC_BROWSE, L"Обзор…", x3, 74);
}

void AppWindow::setUi()
{
    if (m_dir.empty())
        m_dir = L".";
    m_stateText = L"отключена";
    m_dotColor = kGray;
    m_sName = m_sVendor = m_sRes = m_sFps = m_sPix = m_sBackend = L"—";
    EnableWindow(m_btnSnap, FALSE);
    EnableWindow(m_btnRecStart, FALSE);
    EnableWindow(m_btnRecStop, FALSE);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void AppWindow::log(const std::wstring& text)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    const std::wstring stamp = wformat(L"%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    const std::wstring line = stamp + widen("— ") + text;
    appendLogLine(line);
    m_log.add(text);
}

void AppWindow::appendLogLine(const std::wstring& line)
{
    m_logLines.push_back(line);
    if (m_logLines.size() > 400)
        m_logLines.erase(m_logLines.begin(), m_logLines.end() - 200);
    InvalidateRect(m_hwnd, &kLogRect, FALSE);
}

void AppWindow::refreshCameras(bool fromDeviceChange)
{
    std::vector<CameraDevice> devices = DsCamera::enumerate();

    const bool change =
        fromDeviceChange || static_cast<int>(devices.size()) != m_deviceCount;
    if (!change)
        return;

    const int sel = static_cast<int>(SendMessageW(m_comboCam, CB_GETCURSEL, 0, 0));

    m_deviceCount = static_cast<int>(devices.size());
    m_devices = std::move(devices);

    SendMessageW(m_comboCam, CB_RESETCONTENT, 0, 0);
    for (std::size_t i = 0; i < m_devices.size(); ++i) {
        const std::wstring label =
            wformat(L"%d: %ls", static_cast<int>(i), m_devices[i].name.c_str());
        SendMessageW(m_comboCam, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (sel >= 0 && sel < static_cast<int>(m_devices.size()))
        SendMessageW(m_comboCam, CB_SETCURSEL, sel, 0);
    else if (!m_devices.empty())
        SendMessageW(m_comboCam, CB_SETCURSEL, 0, 0);

    log(wformat(L"Список камер обновлён: обнаружено устройств — %d", m_deviceCount));
}

void AppWindow::doConnect()
{
    if (m_connected) {
        doDisconnect();
        return;
    }
    const int sel = static_cast<int>(SendMessageW(m_comboCam, CB_GETCURSEL, 0, 0));
    if (sel < 0 || static_cast<std::size_t>(sel) >= m_devices.size()) {
        log(L"Ошибка: не выбрана камера");
        m_dotColor = kRed;
        m_stateText = L"ошибка";
        InvalidateRect(m_hwnd, &kDotRect, FALSE);
        InvalidateRect(m_hwnd, &kStateText, FALSE);
        return;
    }
    log(wformat(L"Подключение камеры «%ls»…",
                m_devices[static_cast<std::size_t>(sel)].name.c_str()));

    if (!m_camera.connectByIndex(sel, m_hwnd, kPreview)) {
        log(L"Ошибка: не удалось подключиться к камере (DirectShow).");
        m_dotColor = kRed;
        m_stateText = L"ошибка";
        InvalidateRect(m_hwnd, &kDotRect, FALSE);
        InvalidateRect(m_hwnd, &kStateText, FALSE);
        return;
    }
    m_connected = true;
    m_dotColor = kGreen;
    m_stateText = L"подключена";
    setStaticText(m_btnConnect, widen("Отключить камеру").c_str());
    EnableWindow(m_btnSnap, TRUE);
    EnableWindow(m_btnRecStart, TRUE);
    EnableWindow(m_btnRecStop, FALSE);
    log(wformat(L"Камера подключена: %ls, %dx%d, %ls, %.1f FPS",
                m_camera.deviceName().c_str(), m_camera.width(), m_camera.height(),
                m_camera.pixelFormat().c_str(), m_camera.fps()));
    InvalidateRect(m_hwnd, &kDotRect, FALSE);
    InvalidateRect(m_hwnd, &kStateText, FALSE);
    InvalidateRect(m_hwnd, &kInfoCard, FALSE);
    InvalidateRect(m_hwnd, &kPreview, FALSE);
    updateInfo(true);
}

void AppWindow::doDisconnect()
{
    if (!m_connected)
        return;
    if (m_recording)
        doRecordStop();
    m_camera.disconnect();
    m_connected = false;
    m_dotColor = kGray;
    m_stateText = L"отключена";
    setStaticText(m_btnConnect, widen("Подключить камеру").c_str());
    EnableWindow(m_btnSnap, FALSE);
    EnableWindow(m_btnRecStart, FALSE);
    EnableWindow(m_btnRecStop, FALSE);
    log(L"Камера отключена.");
    InvalidateRect(m_hwnd, &kDotRect, FALSE);
    InvalidateRect(m_hwnd, &kStateText, FALSE);
    InvalidateRect(m_hwnd, &kInfoCard, FALSE);
    InvalidateRect(m_hwnd, &kPreview, FALSE);
    updateInfo(true);
}

void AppWindow::doSnapshot()
{
    if (!m_connected) {
        log(L"Ошибка: камера не подключена.");
        return;
    }
    RawFrame frame;
    std::vector<unsigned char> frameBuf;
    if (!m_camera.grabFrame(frame, frameBuf)) {
        log(L"Ошибка: кадр ещё не получен.");
        return;
    }
    const int fmtSel = static_cast<int>(SendMessageW(m_comboFmt, CB_GETCURSEL, 0, 0));
    const bool jpeg = fmtSel == 1;
    const SnapFormat fmt = jpeg ? SnapFormat::Jpeg : SnapFormat::Bmp;
    const std::wstring path =
        m_dir + (jpeg ? L"\\snap_" : L"\\snap_") + timeStampName(L"", jpeg ? L".jpg" : L".bmp");
    if (saveSnapshot(path, frame, fmt)) {
        log(wformat(L"Снимок сохранён: %ls", path.c_str()));
    } else {
        log(wformat(L"Ошибка: не удалось сохранить снимок «%ls».", path.c_str()));
        m_dotColor = kRed;
        InvalidateRect(m_hwnd, &kDotRect, FALSE);
    }
}

void AppWindow::doRecordStart()
{
    if (!m_connected) {
        log(L"Ошибка: камера не подключена.");
        return;
    }
    if (m_recording)
        return;
    const std::wstring path = m_dir + L"\\rec_" + timeStampName(L"rec_", L".avi");
    std::wstring error;
    if (m_camera.startRecording(path, error)) {
        m_recording = true;
        EnableWindow(m_btnRecStart, FALSE);
        EnableWindow(m_btnRecStop, TRUE);
        log(wformat(L"Запись начата: %ls", path.c_str()));
    } else {
        log(wformat(L"Ошибка: не удалось начать запись. %ls", error.c_str()));
        m_dotColor = kRed;
        InvalidateRect(m_hwnd, &kDotRect, FALSE);
    }
}

void AppWindow::doRecordStop()
{
    if (!m_recording)
        return;
    if (m_camera.stopRecording()) {
        m_recording = false;
        EnableWindow(m_btnRecStart, TRUE);
        EnableWindow(m_btnRecStop, FALSE);
        log(L"Запись остановлена, файл финализирован.");
    } else {
        log(L"Ошибка: не удалось остановить запись.");
    }
}

void AppWindow::doBrowse()
{
    wchar_t buf[MAX_PATH] = {};
    BROWSEINFOW bi = {};
    bi.hwndOwner = m_hwnd;
    bi.lpszTitle = widen("Выберите каталог для сохранения материалов").c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        if (SHGetPathFromIDListW(pidl, buf))
            m_dir = buf;
        CoTaskMemFree(pidl);
    }
    if (m_dir.empty())
        m_dir = L".";
    InvalidateRect(m_hwnd, &kDirRect, FALSE);
    log(wformat(L"Каталог сохранения: %ls", m_dir.c_str()));
}

void AppWindow::doSaveReport()
{
    wchar_t path[MAX_PATH] = L"camera-report.txt";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Текстовый отчёт (*.txt)\0*.txt\0Все файлы (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn))
        return;

    if (m_log.save(path))
        log(wformat(L"Отчёт сохранён: %ls (%d записей)", path,
                    static_cast<int>(m_log.entries().size())));
    else
        log(wformat(L"Ошибка: не удалось сохранить отчёт «%ls».", path));
}

void AppWindow::updateInfo(bool force)
{
    if (!m_connected) {
        m_sName = m_sVendor = m_sRes = m_sFps = m_sPix = m_sBackend = L"—";
        InvalidateRect(m_hwnd, &kInfoCard, FALSE);
        return;
    }
    static int lastRes = 0;
    static int lastFpsTag = 0;
    const int res = m_camera.width() * 10000 + m_camera.height();
    const int fpsTag = static_cast<int>(m_camera.fps() * 10.0);
    if (!force && lastRes == res && lastFpsTag == fpsTag)
        return;
    lastRes = res;
    lastFpsTag = fpsTag;

    m_sName = m_camera.deviceName();
    m_sVendor = m_camera.vendor();
    m_sRes = m_camera.width() > 0 ? wformat(L"%dx%d", m_camera.width(), m_camera.height())
                                  : L"—";
    m_sPix = m_camera.pixelFormat();
    m_sBackend = m_camera.backendInfo();
    InvalidateRect(m_hwnd, &kInfoCard, FALSE);
}

void AppWindow::paintUi(HDC hdc)
{
    const HGDIOBJ hFont = SelectObject(hdc, uiFont());
    const int dcState = SaveDC(hdc);

    RECT client;
    GetClientRect(m_hwnd, &client);
    fillRect(hdc, client, kBg);

    paintText(hdc, 12, 14, kGray, L"Камера:");
    paintText(hdc, 12, 46, kGray, L"Формат:");
    paintText(hdc, 12, 78, kGray, L"Каталог:");

    fillRect(hdc, kDirRect, kBg1);
    RECT dirText = kDirRect;
    dirText.left += 8;
    dirText.top += 2;
    dirText.right -= 4;
    paintValue(hdc, dirText, kFg, m_dir);

    fillRect(hdc, kHeaderDivider, kBg2);

    fillRect(hdc, kPreview, kBg0);
    if (!m_connected) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, kFg);
        DrawTextW(hdc, widen("Камера не подключена — нажмите «Подключить камеру»").c_str(), -1,
                  const_cast<LPRECT>(&kPreview),
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    fillRect(hdc, kInfoCard, kBg0);
    RECT border = kInfoCard;
    HBRUSH borderBrush = solid(kBg2);
    FrameRect(hdc, &border, borderBrush);
    DeleteObject(borderBrush);
    paintText(hdc, 16, 354, kYellow, L"ИНФОРМАЦИЯ ОБ УСТРОЙСТВЕ");

    HBRUSH dotBrush = solid(m_dotColor);
    HPEN pen = CreatePen(PS_SOLID, 1, kBg2);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, dotBrush);
    Ellipse(hdc, kDotRect.left, kDotRect.top, kDotRect.right, kDotRect.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    DeleteObject(dotBrush);
    paintText(hdc, 16, kStateText.top, kGray, L"Состояние:");
    paintText(hdc, kStateText.left, kStateText.top, kFg, m_stateText);

    const InfoRow rows[] = {
        {L"Устройство:", &m_sName, 16, 176, 402},
        {L"Производитель:", &m_sVendor, 16, 176, 426},
        {L"FPS:", &m_sFps, 520, 700, 382},
        {L"Формат пикселей:", &m_sPix, 520, 700, 406},
        {L"Бэкенд:", &m_sBackend, 520, 700, 430},
    };
    for (const InfoRow& row : rows) {
        paintText(hdc, row.labelX, row.y, kGray, row.label);
        RECT rc = {row.valueX, row.y, 980, row.y + 18};
        paintValue(hdc, rc, kFg, *row.value);
    }

    paintText(hdc, kLogLabel.left, kLogLabel.top, kFg, L"Журнал событий:");
    paintLog(hdc);

    m_camera.repaintVideo(m_hwnd, hdc);
    RestoreDC(hdc, dcState);
    SelectObject(hdc, hFont);
}

void AppWindow::paintLog(HDC hdc)
{
    fillRect(hdc, kLogRect, kBg0);
    RECT border = kLogRect;
    HBRUSH borderBrush = solid(kBg2);
    FrameRect(hdc, &border, borderBrush);
    DeleteObject(borderBrush);

    HGDIOBJ oldFont = SelectObject(hdc, uiFont());
    TEXTMETRICW tm = {};
    GetTextMetricsW(hdc, &tm);
    const int lineH = tm.tmHeight + 2;

    const int height = kLogRect.bottom - kLogRect.top;
    const int maxLines = (height - 4) / lineH;
    const int start =
        std::max(0, static_cast<int>(m_logLines.size()) - std::max(0, maxLines));

    SaveDC(hdc);
    IntersectClipRect(hdc, kLogRect.left, kLogRect.top, kLogRect.right, kLogRect.bottom);
    int y = kLogRect.top + 3;
    for (std::size_t i = static_cast<std::size_t>(start); i < m_logLines.size(); ++i) {
        paintText(hdc, kLogRect.left + 6, y, kFg, m_logLines[i]);
        y += lineH;
    }
    RestoreDC(hdc, -1);
    SelectObject(hdc, oldFont);
}

LRESULT CALLBACK AppWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    AppWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<AppWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_COMMAND:
            self->onCommand(wParam, lParam);
            return 0;
        case WM_TIMER:
            self->onTimer(wParam);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            self->paintUi(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DEVICECHANGE:
            self->refreshCameras(true);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kTimerFps);
            KillTimer(hwnd, kTimerPoll);
            self->m_camera.disconnect();
            if (self->m_bgBrush)
                DeleteObject(self->m_bgBrush);
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void AppWindow::onCommand(WPARAM wParam, LPARAM lParam)
{
    const WORD id = LOWORD(wParam);
    const WORD code = HIWORD(wParam);

    if (id == IDC_CONNECT && code == BN_CLICKED) {
        doConnect();
        return;
    }
    if (id == IDC_SNAP && code == BN_CLICKED) {
        doSnapshot();
        return;
    }
    if (id == IDC_REC_START && code == BN_CLICKED) {
        doRecordStart();
        return;
    }
    if (id == IDC_REC_STOP && code == BN_CLICKED) {
        doRecordStop();
        return;
    }
    if (id == IDC_SAVE_REPORT && code == BN_CLICKED) {
        doSaveReport();
        return;
    }
    if (code == BN_CLICKED && id == IDC_BROWSE) {
        doBrowse();
        return;
    }
    if (code == BN_CLICKED && id == IDC_REFRESH) {
        refreshCameras(false);
        return;
    }
}

void AppWindow::onTimer(WPARAM wParam)
{
    if (wParam == kTimerFps) {
        m_camera.updateMeasuredFps();
        if (m_connected) {
            m_sFps = wformat(L"%.1f (изм. %.1f)", m_camera.fps(), m_camera.fpsMeasured());
            InvalidateRect(m_hwnd, &kInfoCard, FALSE);
        }
    } else if (wParam == kTimerPoll) {
        refreshCameras(true);
    }
}