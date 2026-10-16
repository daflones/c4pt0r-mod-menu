@echo off
:: ============================================================================
::  C4pt0r Cheats — Setup Launcher
::  Eleva pra admin e roda o setup.ps1
:: ============================================================================

:: Check admin
>nul 2>&1 "%SYSTEMROOT%\System32\cacls.exe" "%SYSTEMROOT%\System32\config\system"
if '%errorlevel%' NEQ '0' (
    echo [C4pt0r] Pedindo permissao de administrador...
    powershell -Command "Start-Process cmd -ArgumentList '/c \"\"%~f0\"\"' -Verb RunAs"
    exit /b
)

:: Move to script directory
cd /d "%~dp0"

:: Run setup
powershell -ExecutionPolicy Bypass -NoProfile -File "%~dp0setup.ps1"

echo.
echo Pressione qualquer tecla para fechar...
pause >nul
