#include "appwindow.h"

#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    AppWindow app(hInstance);
    if (!app.create())
        return 1;
    app.run();
    return 0;
}