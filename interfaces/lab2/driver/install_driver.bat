@echo off
rem ============================================================
rem  Install and start the PCI config reader driver.
rem  MUST be run from an elevated (Administrator) prompt.
rem ============================================================
setlocal
set DRV=%~dp0pcidriver.sys
set SYSTEM32_DRV=C:\Windows\System32\drivers\pcidriver.sys

if not exist "%DRV%" (
    echo [ERROR] pcidriver.sys not found next to this script.
    exit /b 1
)

rem Check for elevation
net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Run this script as Administrator.
    exit /b 1
)

echo Copying driver to %SYSTEM32_DRV% ...
copy /Y "%DRV%" "%SYSTEM32_DRV%" >nul || (echo Failed to copy. & exit /b 1)

echo Creating service ...
sc create pcidriver type= kernel binPath= "\SystemRoot\System32\drivers\pcidriver.sys" >nul 2>&1
if errorlevel 1 (
    echo Service may already exist, continuing...
)

echo Starting service ...
sc start pcidriver

echo.
echo Done. If the driver failed to start, enable test signing and reboot:
echo   bcdedit /set testsigning on
echo   shutdown /r /t 0
echo Then re-run this script.

endlocal