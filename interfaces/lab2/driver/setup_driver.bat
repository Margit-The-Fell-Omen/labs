@echo off
rem ============================================================
rem  One-shot setup script for the PCI config reader driver.
rem  Run from an ELEVATED (Administrator) prompt.
rem
rem  Steps:
rem    1. Trust the test signing certificate (root + trusted publisher).
rem    2. Copy pcidriver.sys to System32\drivers.
rem    3. Create and start the "pcidriver" kernel service.
rem ============================================================
setlocal enableextensions
cd /d "%~dp0"

set CERT=%~dp0signing\Lab2Cert.cer
set SYS=%~dp0pcidriver.sys
set DEST=C:\Windows\System32\drivers\pcidriver.sys

net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Run this script as Administrator.
    pause
    exit /b 1
)

if not exist "%CERT%" (
    echo [ERROR] Certificate not found: %CERT%
    pause
    exit /b 1
)
if not exist "%SYS%" (
    echo [ERROR] Driver not found: %SYS%
    echo Build it first with build_driver.bat
    pause
    exit /b 1
)

echo [1/5] Importing test signing certificate into trusted stores...
certutil -addstore Root "%CERT%" >nul
certutil -addstore TrustedPublisher "%CERT%" >nul
if errorlevel 1 echo     (warning) could not add to TrustedPublisher store.

echo [2/5] Stopping driver (if running)...
sc stop pcidriver >nul 2>&1
sc delete pcidriver >nul 2>&1

echo [3/5] Copying driver...
copy /Y "%SYS%" "%DEST%" >nul || (echo Failed to copy driver. & pause & exit /b 1)

echo [4/5] Creating service...
sc create pcidriver type= kernel binPath= "\SystemRoot\System32\drivers\pcidriver.sys" >nul

echo [5/5] Starting service...
sc start pcidriver

echo.
echo Done. Now run pci_registers.exe (as Administrator).
sc query pcidriver
pause
endlocal