@echo off
setlocal enabledelayedexpansion

:: ============================================================
:: Plant Rover - Automated Setup Script for Windows
:: ============================================================
:: This script installs Python and all required dependencies
:: ============================================================

title Plant Rover Setup

echo.
echo ============================================================
echo           PLANT ROVER - Automated Setup
echo ============================================================
echo.
echo This will install:
echo   - Python 3.11 (if not installed)
echo   - All required Python libraries
echo   - Verify installation
echo.
echo Press Ctrl+C to cancel, or
pause
echo.

:: ============================================================
:: Check if Python is installed
:: ============================================================
echo [1/6] Checking Python installation...
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo Python not found! Downloading installer...

    :: Create temp directory
    if not exist "%TEMP%\PlantRoverSetup" mkdir "%TEMP%\PlantRoverSetup"

    :: Download Python 3.11 installer
    echo Downloading Python 3.11...
    powershell -Command "& {Invoke-WebRequest -Uri 'https://www.python.org/ftp/python/3.11.8/python-3.11.8-amd64.exe' -OutFile '%TEMP%\PlantRoverSetup\python_installer.exe'}"

    if exist "%TEMP%\PlantRoverSetup\python_installer.exe" (
        echo Installing Python... (this may take a minute)
        start /wait "" "%TEMP%\PlantRoverSetup\python_installer.exe" /quiet InstallAllUsers=1 PrependPath=1 Include_test=0

        :: Wait for installation to complete
        timeout /t 30 /nobreak >nul

        :: Clean up
        rd /s /q "%TEMP%\PlantRoverSetup" 2>nul

        :: Refresh PATH
        refreshenv >nul 2>&1 || (
            echo Python installed! Please close and reopen this window, then run setup.bat again.
            pause
            exit
        )
    ) else (
        echo ERROR: Failed to download Python installer.
        echo Please download manually from: https://www.python.org/downloads/
        pause
        exit
    )
)

:: Get Python version
for /f "tokens=2" %%i in ('python --version 2^>^&1') do set PYTHON_VERSION=%%i
echo Python %PYTHON_VERSION% found!
echo.

:: ============================================================
:: Upgrade pip
:: ============================================================
echo [2/6] Upgrading pip to latest version...
python -m pip install --upgrade pip --quiet
echo pip upgraded!
echo.

:: ============================================================
:: Install ML Training Requirements
:: ============================================================
echo [3/6] Installing ML Training libraries...
cd /d "%~dp0plant_rover_training"
if exist requirements.txt (
    echo Installing from plant_rover_training/requirements.txt...
    pip install -r requirements.txt --quiet
    echo ML libraries installed!
) else (
    echo Warning: requirements.txt not found in plant_rover_training/
)
echo.

:: ============================================================
:: Install Detection Server Requirements
:: ============================================================
echo [4/6] Installing Detection Server libraries...
cd /d "%~dp0detection_server"
if exist requirements.txt (
    echo Installing from detection_server/requirements.txt...
    pip install -r requirements.txt --quiet
    echo Detection server libraries installed!
) else (
    echo Warning: requirements.txt not found in detection_server/
)
echo.

:: ============================================================
:: Install Additional Tools
:: ============================================================
echo [5/6] Installing additional tools...
echo Installing labelImg (for dataset labeling)...
pip install labelImg --quiet
echo labelImg installed!
echo.

:: ============================================================
:: Verify Installation
:: ============================================================
echo [6/6] Verifying installation...
echo.

set ALL_OK=1

:: Check Python
python --version >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] Python
) else (
    echo [FAIL] Python
    set ALL_OK=0
)

:: Check key libraries
echo Checking libraries...
python -c "import ultralytics" >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] ultralytics (YOLOv8)
) else (
    echo [FAIL] ultralytics
    set ALL_OK=0
)

python -c "import cv2" >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] opencv-python
) else (
    echo [FAIL] opencv-python
    set ALL_OK=0
)

python -c "import fastapi" >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] fastapi
) else (
    echo [FAIL] fastapi
    set ALL_OK=0
)

python -c "import albumentations" >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] albumentations
) else (
    echo [FAIL] albumentations
    set ALL_OK=0
)

python -c "import labelImg" >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] labelImg
) else (
    echo [WARN] labelImg (may not be critical)
)

echo.
echo ============================================================

if %ALL_OK%==1 (
    echo           SETUP COMPLETE!
    echo ============================================================
    echo.
    echo All Python dependencies are installed!
    echo.
    echo Next steps:
    echo   1. Install Arduino IDE for firmware uploads:
    echo      https://www.arduino.cc/en/software
    echo.
    echo   2. For ML training, run:
    echo      cd plant_rover_training
    echo      python setup_labeling.py
    echo.
    echo   3. For detection server, run:
    echo      cd detection_server
    echo      python detection_server.py
    echo.
) else (
    echo           SETUP INCOMPLETE
    echo ============================================================
    echo.
    echo Some components failed to install.
    echo Please check the errors above and try again.
    echo.
)

echo Press any key to exit...
pause >nul

:: Return to original directory
cd /d "%~dp0"
