@echo off
REM Wrapper batch pour upload_html.py
REM Utilise le Python de PlatformIO pour exécuter le script

setlocal enabledelayedexpansion

set PYTHON_EXE=C:\Users\Administrator\.platformio\penv\Scripts\python.exe
set SCRIPT_PATH=%~dp0upload_html.py

if not exist "%PYTHON_EXE%" (
    echo ❌ Error: Python not found at %PYTHON_EXE%
    exit /b 1
)

if not exist "%SCRIPT_PATH%" (
    echo ❌ Error: upload_html.py not found at %SCRIPT_PATH%
    exit /b 1
)

REM Passer tous les arguments au script Python
"%PYTHON_EXE%" "%SCRIPT_PATH%" %*

exit /b %ERRORLEVEL%
