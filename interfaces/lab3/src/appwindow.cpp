#include "appwindow.h"

#include "ataregs.h"
#include "directregs.h"
#include "nvme.h"
#include "report.h"
#include "strutil.h"
#include "winapi.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

namespace {

constexpr wchar_t kClassName[] = L"HDSsdAnalyzerWnd";

enum {
    IDC_COMBO_A = 1001,
    IDC_COMBO_B,
    IDC_REFRESH,
    IDC_EXPORT,
    IDC_LIST,
    IDC_ADVICE,
};

// Window is fixed-size for simplicity; coordinates are absolute client px.
constexpr int kMargin = 12;
constexpr int kLabelW = 118;
constexpr int kComboX = kMargin + kLabelW;
constexpr int kComboW = 480;
constexpr int kBtn1W = 150;
constexpr int kWindowW = 940;
constexpr int kWindowH = 700;

} // namespace

AppWindow::AppWindow(HINSTANCE hInstance)
    : m_hInstance(hInstance)
{
}

AppWindow::~AppWindow() = default;

bool AppWindow::create()
{
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &AppWindow::wndProc;
    wc.hInstance = m_hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        0, kClassName, widen("Сравнительный анализатор HDD vs SSD").c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, kWindowW, kWindowH,
        nullptr, nullptr, m_hInstance, nullptr);

    if (!m_hwnd)
        return false;

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    createChildren();
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    refresh();
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
    auto makeStatic = [&](const wchar_t* text, int x, int y, int w, int h) -> HWND {
        return CreateWindowExW(0, L"STATIC", text,
                               WS_CHILD | WS_VISIBLE | SS_LEFT,
                               x, y, w, h, m_hwnd, nullptr, m_hInstance, nullptr);
    };

    makeStatic(widen("Накопитель A:").c_str(), kMargin, 16, kLabelW, 20);
    makeStatic(widen("Накопитель B:").c_str(), kMargin, 50, kLabelW, 20);

    m_comboA = CreateWindowExW(0, L"COMBOBOX", L"",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                               kComboX, 12, kComboW, 300, m_hwnd,
                               reinterpret_cast<HMENU>(IDC_COMBO_A), m_hInstance, nullptr);

    m_comboB = CreateWindowExW(0, L"COMBOBOX", L"",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                               kComboX, 46, kComboW, 300, m_hwnd,
                               reinterpret_cast<HMENU>(IDC_COMBO_B), m_hInstance, nullptr);

    CreateWindowExW(0, L"BUTTON", widen("Обновить").c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    kComboX + kComboW + 12, 12, kBtn1W, 28, m_hwnd,
                    reinterpret_cast<HMENU>(IDC_REFRESH), m_hInstance, nullptr);

    CreateWindowExW(0, L"BUTTON", widen("Экспорт отчёта (HTML)").c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    kComboX + kComboW + 12, 48, kBtn1W + 40, 28, m_hwnd,
                    reinterpret_cast<HMENU>(IDC_EXPORT), m_hInstance, nullptr);

    m_list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS,
                             kMargin, 86, kWindowW - 2 * kMargin - 16, 330, m_hwnd,
                             reinterpret_cast<HMENU>(IDC_LIST), m_hInstance, nullptr);
    ListView_SetExtendedListViewStyle(m_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    makeStatic(widen("Рекомендации и предупреждения:").c_str(), kMargin, 424, kWindowW - 24, 18);

    m_advice = CreateWindowExW(0, L"EDIT", L"",
                               WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
                                   ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                               kMargin, 444, kWindowW - 2 * kMargin - 16, 210, m_hwnd,
                               reinterpret_cast<HMENU>(IDC_ADVICE), m_hInstance, nullptr);

    const std::wstring headers[3] = {
        widen("Параметр"),
        widen("Накопитель A"),
        widen("Накопитель B"),
    };
    const int widths[3] = {230, 330, 330};
    for (int i = 0; i < 3; ++i) {
        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.iSubItem = i;
        col.cx = widths[i];
        col.pszText = const_cast<wchar_t*>(headers[i].c_str());
        SendMessageW(m_list, LVM_INSERTCOLUMNW, i, reinterpret_cast<LPARAM>(&col));
    }
}

void AppWindow::refresh()
{
    rebuildDevices();
    renderComparison();
}

const StorageDevice* AppWindow::byDiskNumber(int diskNumber) const
{
    for (const StorageDevice& device : m_devices) {
        if (device.diskNumber == diskNumber)
            return &device;
    }
    return nullptr;
}

void AppWindow::rebuildDevices()
{
    const int prevA = static_cast<int>(SendMessageW(
        m_comboA, CB_GETITEMDATA, SendMessageW(m_comboA, CB_GETCURSEL, 0, 0), 0));
    const int prevB = static_cast<int>(SendMessageW(
        m_comboB, CB_GETITEMDATA, SendMessageW(m_comboB, CB_GETCURSEL, 0, 0), 0));

    m_devices = WinApiEnumerator::enumerate();

    // Lowest-level identity/health reads per transport :
    //  - NVMe    -> NVMe admin commands (Identify + SMART/Health log)
    //  - SATA/IDE -> ATA task-file registers: IDENTIFY + S.M.A.R.T. READ DATA
    for (StorageDevice& device : m_devices) {
        if (!device.detectedByWinApi)
            continue;

        if (device.busType == 17 /* BusTypeNvme */) {
            std::wstring errMsg;
            if (NvmeRegisters::populate(device.diskNumber, device, errMsg)) {
                device.isNvme = true;
                device.healthOk = true;
            }
        } else {
            unsigned char status = 0, error = 0;
            std::vector<unsigned char> buf;
            std::wstring errMsg;
            bool ok = false;
            bool direct = false;

            // IDENTIFY DEVICE — write/read the task-file registers.
            if (AtaDirect::supported()) {
                ok = AtaDirect::identifyViaRegisterPorts(0x1F0, buf, status, error, errMsg);
                if (!ok || (status & 0x01))
                    ok = AtaDirect::identifyViaRegisterPorts(0x3F0, buf, status, error, errMsg);
                direct = ok;
            }
            if (!ok || (status & 0x01))
                ok = AtaRegisters::identifyDevice(device.diskNumber, buf, status, error, errMsg);

            if (ok && !(status & 0x01)) {
                device.ataIdentifyOk = true;
                device.ataStatusRegister = status;
                device.ataErrorRegister = error;
                device.ataDirectRegister = direct;

                unsigned short word217 = 0;
                bool rotational = false;
                if (AtaRegisters::decodeRotationRate(buf, word217, rotational)) {
                    device.ataRotationRateWord = word217;
                    if (rotational) {
                        device.rpm = static_cast<unsigned int>(word217 - 1);
                        device.rpmKnown = true;
                    }
                }
            }

            // S.M.A.R.T. READ DATA — temperature / hours / wear / errors.
            std::vector<unsigned char> smartBuf;
            bool smartOk = false;
            if (direct) {
                smartOk = AtaDirect::readSmartViaRegisterPorts(0x1F0, smartBuf, status, error,
                                                               errMsg);
                if (!smartOk || (status & 0x01))
                    smartOk = AtaDirect::readSmartViaRegisterPorts(0x3F0, smartBuf, status, error,
                                                                   errMsg);
            }
            if (!smartOk || (status & 0x01))
                smartOk = AtaRegisters::readSmartData(device.diskNumber, smartBuf, status, error,
                                                      errMsg);

            if (smartOk && !(status & 0x01)) {
                AtaRegisters::AtaSmartInfo info;
                if (AtaRegisters::parseSmartData(smartBuf, info) && info.haveData) {
                    if (info.temperatureC > 0) {
                        device.hasTemperature = true;
                        device.temperatureC = info.temperatureC;
                    }
                    if (info.powerOnHours > 0) {
                        device.powerOnHours = info.powerOnHours;
                        device.hasReliability = true;
                    }
                    if (info.wearPercent >= 0) {
                        device.wearPercent = info.wearPercent;
                        device.hasReliability = true;
                    }
                    device.readErrorsUncorrected = info.uncorrectableErrors;
                    device.startStopCycleCount = info.startStopCount;
                    device.healthOk = true;
                }
            }
        }

        TypeSignals signals;
        signals.seekPenaltyKnown = device.seekPenaltyKnown;
        signals.seekPenalty = device.seekPenalty;
        signals.trimKnown = device.trimKnown;
        signals.trimSupported = device.trimSupported;
        signals.rpmKnown = device.rpmKnown;
        signals.rpm = device.rpm;
        signals.isNvme = device.isNvme;

        device.type = detectType(signals);
        device.typeDetail = typeDetail(signals);
    }

    SendMessageW(m_comboA, CB_RESETCONTENT, 0, 0);
    SendMessageW(m_comboB, CB_RESETCONTENT, 0, 0);

    for (const StorageDevice& device : m_devices) {
        const std::wstring label = wformat(L"PhysicalDrive%d — %ls [%ls]",
                                           device.diskNumber,
                                           device.friendlyName.empty() ? device.model.c_str()
                                                                       : device.friendlyName.c_str(),
                                           driveTypeToString(device.type).c_str());
        const int idxA = static_cast<int>(SendMessageW(m_comboA, CB_ADDSTRING, 0,
                                                       reinterpret_cast<LPARAM>(label.c_str())));
        SendMessageW(m_comboA, CB_SETITEMDATA, idxA, device.diskNumber);

        const int idxB = static_cast<int>(SendMessageW(m_comboB, CB_ADDSTRING, 0,
                                                       reinterpret_cast<LPARAM>(label.c_str())));
        SendMessageW(m_comboB, CB_SETITEMDATA, idxB, device.diskNumber);
    }

    auto selectByDisk = [](HWND combo, int diskNumber, int defaultIdx) {
        const int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
        for (int i = 0; i < count; ++i) {
            if (static_cast<int>(SendMessageW(combo, CB_GETITEMDATA, i, 0)) == diskNumber) {
                SendMessageW(combo, CB_SETCURSEL, i, 0);
                return;
            }
        }
        SendMessageW(combo, CB_SETCURSEL, defaultIdx < count ? defaultIdx : 0, 0);
    };

    selectByDisk(m_comboA, prevA, 0);
    selectByDisk(m_comboB, prevB, m_devices.size() > 1 ? 1 : 0);
}

void AppWindow::renderComparison()
{
    const bool enough = m_devices.size() >= 2;

    ListView_DeleteAllItems(m_list);

    if (!enough) {
        SetWindowTextW(m_advice,
                       widen("Для сравнения необходимо подключить как минимум два накопителя.\r\n"
                             "В виртуальной машине добавьте второй виртуальный диск и нажмите «Обновить».")
                           .c_str());
        EnableWindow(m_list, FALSE);
        return;
    }

    EnableWindow(m_list, TRUE);

    const int numA = static_cast<int>(SendMessageW(
        m_comboA, CB_GETITEMDATA, SendMessageW(m_comboA, CB_GETCURSEL, 0, 0), 0));
    const int numB = static_cast<int>(SendMessageW(
        m_comboB, CB_GETITEMDATA, SendMessageW(m_comboB, CB_GETCURSEL, 0, 0), 0));

    const StorageDevice* a = byDiskNumber(numA);
    const StorageDevice* b = byDiskNumber(numB);
    if (!a || !b)
        return;

    if (a->diskNumber == b->diskNumber) {
        SetWindowTextW(m_advice, widen("Выбран один и тот же накопитель в обоих списках.").c_str());
        m_rows.clear();
        m_advices.clear();
        return;
    }

    m_rows = compareDevices(*a, *b);
    m_advices = recommendationsFor(*a);
    const std::vector<Advice> advB = recommendationsFor(*b);
    m_advices.insert(m_advices.end(), advB.begin(), advB.end());
    const std::vector<Advice> advCmp = comparativeAdvice(*a, *b);
    m_advices.insert(m_advices.end(), advCmp.begin(), advCmp.end());

    for (size_t i = 0; i < m_rows.size(); ++i) {
        const CompareRow& row = m_rows[i];

        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.iSubItem = 0;
        item.pszText = const_cast<wchar_t*>(row.label.c_str());
        ListView_InsertItem(m_list, &item);

        std::wstring left = row.left;
        std::wstring right = row.right;
        if (row.flags & CompareRow::LeftBetter)
            left += widen("  (лучше)");
        else if (row.flags & CompareRow::RightBetter)
            right += widen("  (лучше)");

        ListView_SetItemText(m_list, static_cast<int>(i), 1, const_cast<wchar_t*>(left.c_str()));
        ListView_SetItemText(m_list, static_cast<int>(i), 2, const_cast<wchar_t*>(right.c_str()));
    }

    std::wstring text;
    for (const Advice& advice : m_advices) {
        const wchar_t* prefix = L"ИНФО";
        if (advice.level == Advice::Warning)
            prefix = L"ВНИМАНИЕ";
        else if (advice.level == Advice::Critical)
            prefix = L"КРИТИЧНО";

        const std::wstring scope = advice.diskNumber < 0
                                       ? widen("Сводно")
                                       : wformat(L"PhysicalDrive%d", advice.diskNumber);

        text += wformat(L"[%ls] %ls — %ls\r\n", prefix, scope.c_str(), advice.text.c_str());
    }
    if (text.empty())
        text = widen("Проблем не обнаружено.");

    SetWindowTextW(m_advice, text.c_str());
}

void AppWindow::exportReport()
{
    const int numA = static_cast<int>(SendMessageW(
        m_comboA, CB_GETITEMDATA, SendMessageW(m_comboA, CB_GETCURSEL, 0, 0), 0));
    const int numB = static_cast<int>(SendMessageW(
        m_comboB, CB_GETITEMDATA, SendMessageW(m_comboB, CB_GETCURSEL, 0, 0), 0));

    const StorageDevice* a = byDiskNumber(numA);
    const StorageDevice* b = byDiskNumber(numB);
    if (!a || !b || m_rows.empty())
        return;

    wchar_t path[MAX_PATH] = L"storage-comparison-report.html";

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"HTML-отчёт (*.html)\0*.html\0Все файлы (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"html";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn))
        return;

    std::wstring error;
    if (exportReportHtml(path, *a, *b, m_rows, m_advices, error)) {
        ShellExecuteW(m_hwnd, L"open", path, nullptr, nullptr, SW_SHOWNORMAL);
    }

    MessageBoxW(m_hwnd, error.c_str(), widen("Экспорт отчёта").c_str(), MB_OK | MB_ICONINFORMATION);
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
        case WM_COMMAND:
            self->onCommand(wParam, lParam);
            return 0;
        case WM_DESTROY:
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

    if (id == IDC_REFRESH && code == BN_CLICKED) {
        refresh();
        return;
    }
    if (id == IDC_EXPORT && code == BN_CLICKED) {
        exportReport();
        return;
    }
    if (code == CBN_SELCHANGE && (id == IDC_COMBO_A || id == IDC_COMBO_B)) {
        renderComparison();
        return;
    }
}
