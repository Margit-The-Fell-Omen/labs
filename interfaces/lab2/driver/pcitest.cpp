#include <windows.h>
#include <winioctl.h>
#include <cstdio>

#define IOCTL_PCI_READ_CONFIG \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

struct Request { ULONG bus, device, function, offset, length; };
struct Response { ULONG length; UCHAR data[256]; };

bool isAdmin() {
    BOOL admin = FALSE;
    PSID admins = NULL;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0,0,0,0,0,0, &admins);
    CheckTokenMembership(NULL, admins, &admin);
    if (admins) FreeSid(admins);
    return admin != FALSE;
}

int main(int argc, char **argv) {
    ULONG bus = (argc > 1) ? strtoul(argv[1],0,0) : 0;
    ULONG dev = (argc > 2) ? strtoul(argv[2],0,0) : 0;
    ULONG func = (argc > 3) ? strtoul(argv[3],0,0) : 0;
    printf("isAdmin=%d\n", isAdmin() ? 1 : 0);

    HANDLE h = CreateFileW(L"\\\\.\\PciConfigReader", GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("CreateFile failed, err=%lu\n", GetLastError());
        return 1;
    }
    printf("device opened OK\n");

    Request req = { bus, dev, func, 0, 256 };
    Response resp = {};
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_PCI_READ_CONFIG, &req, sizeof(req), &resp, sizeof(resp), &returned, NULL);
    printf("DeviceIoControl ok=%d returned=%lu err=%lu resp.length=%lu\n", ok, returned, GetLastError(), resp.length);
    for (ULONG i = 0; i < 64 && i < resp.length; ++i) {
        printf("%02X ", resp.data[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");
    CloseHandle(h);
    return 0;
}