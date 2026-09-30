@echo off
rem ============================================================
rem  Build pcidriver.sys (PCI config reader kernel driver).
rem  Requires: Visual Studio 2022 with MSVC + Windows Driver Kit.
rem  Run from any plain or developer prompt (vcvars64 is invoked here).
rem ============================================================
setlocal
cd /d "%~dp0"

set WDK_VERSION=10.0.26100.0
set WDK_ROOT=C:\Program Files (x86)\Windows Kits\10

set KM_INC=%WDK_ROOT%\Include\%WDK_VERSION%\km
set UM_INC=%WDK_ROOT%\Include\%WDK_VERSION%\um
set SHARED_INC=%WDK_ROOT%\Include\%WDK_VERSION%\shared
set KM_LIB=%WDK_ROOT%\Lib\%WDK_VERSION%\km\x64

if not exist "%KM_INC%\ntddk.h" (
    echo [ERROR] WDK kernel headers not found at "%KM_INC%".
    exit /b 1
)

set VCVARS="%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist %VCVARS% set VCVARS="%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist %VCVARS% (
    echo [ERROR] vcvars64.bat not found.
    exit /b 1
)

call %VCVARS% >nul

cl /nologo /c /GS- /GR- /Gz /Gy /Oi /Os /W3 /Zl ^
   /D _AMD64_ /D _WIN64 ^
   /D NTDDI_VERSION=NTDDI_WIN10_VB /D DEPRECATE_DDK_FUNCTIONS=1 ^
   /I"%KM_INC%" /I"%UM_INC%" /I"%SHARED_INC%" ^
   pci_driver.c
if errorlevel 1 exit /b 1

link /NOLOGO /DRIVER /ENTRY:DriverEntry /SUBSYSTEM:NATIVE /NODEFAULTLIB ^
     /OUT:pcidriver.sys /LIBPATH:"%KM_LIB%" ^
     pci_driver.obj ntoskrnl.lib
if errorlevel 1 exit /b 1

del pci_driver.obj 2>nul

echo.
echo [OK] pcidriver.sys built successfully.
endlocal