#pragma once

#include <windows.h>

#include <vector>

#include "comparison.h"
#include "storage.h"

class AppWindow
{
public:
    explicit AppWindow(HINSTANCE hInstance);
    ~AppWindow();

    bool create(); // registers class + creates controls, then performs first scan
    void run();    // blocks in the message loop

private:
    HINSTANCE m_hInstance = nullptr;
    HWND m_hwnd = nullptr;
    HWND m_comboA = nullptr;
    HWND m_comboB = nullptr;
    HWND m_list = nullptr;
    HWND m_advice = nullptr;

    std::vector<StorageDevice> m_devices;
    std::vector<CompareRow> m_rows;
    std::vector<Advice> m_advices;

    void createChildren();
    void refresh();
    void rebuildDevices();
    void renderComparison();
    void exportReport();

    const StorageDevice* byDiskNumber(int diskNumber) const;

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void onCommand(WPARAM wParam, LPARAM lParam);
};