#include <windows.h>
#include <initguid.h>
#include <devguid.h>
#include <setupapi.h>
#include <cstdio>
#include <vector>
#include <string>

static std::wstring regStr(HDEVINFO di, PSP_DEVINFO_DATA d, DWORD prop) {
    DWORD req = 0, type = 0;
    SetupDiGetDeviceRegistryPropertyW(di, d, prop, &type, nullptr, 0, &req);
    if (!req) return L"";
    std::vector<wchar_t> buf(req / sizeof(wchar_t) + 1, 0);
    if (!SetupDiGetDeviceRegistryPropertyW(di, d, prop, &type, (PBYTE)buf.data(), req, nullptr)) return L"";
    return buf.data();
}

static DWORD regDword(HDEVINFO di, PSP_DEVINFO_DATA d, DWORD prop) {
    DWORD v = 0, type = 0;
    SetupDiGetDeviceRegistryPropertyW(di, d, prop, &type, (PBYTE)&v, sizeof(v), nullptr);
    return v;
}

static unsigned parseHex(const std::wstring& s, const wchar_t* token, int digits) {
    size_t p = s.find(token);
    if (p == std::wstring::npos) return 0;
    p += wcslen(token);
    std::wstring h = s.substr(p, digits);
    return (unsigned)wcstoul(h.c_str(), nullptr, 16);
}

int main() {
    HDEVINFO di = SetupDiGetClassDevsW(&GUID_DEVCLASS_SYSTEM, nullptr, nullptr, DIGCF_PRESENT);
    if (di == INVALID_HANDLE_VALUE) { printf("no devinfo\n"); return 1; }
    SP_DEVINFO_DATA info; info.cbSize = sizeof(info);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(di, i, &info); ++i) {
        std::wstring env = regStr(di, &info, SPDRP_ENUMERATOR_NAME);
        if (_wcsicmp(env.c_str(), L"PCI") != 0) continue;
        std::wstring hw = regStr(di, &info, SPDRP_HARDWAREID);
        DWORD bus = regDword(di, &info, SPDRP_BUSNUMBER);
        DWORD addr = regDword(di, &info, SPDRP_ADDRESS);
        DWORD device = (addr >> 16) & 0xFFFF;
        DWORD func = addr & 0xFFFF;
        unsigned ven = parseHex(hw, L"VEN_", 4);
        unsigned dev = parseHex(hw, L"DEV_", 4);
        std::wstring desc = regStr(di, &info, SPDRP_DEVICEDESC);
        wprintf(L"%02X:%02X.%X  %04X:%04X  %s\n", bus, device, func, ven, dev, desc.c_str());
    }
    SetupDiDestroyDeviceInfoList(di);
    return 0;
}