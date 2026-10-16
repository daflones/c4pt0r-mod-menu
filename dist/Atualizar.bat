@echo off
title C4pt0r - Atualizador
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0updater.ps1"
