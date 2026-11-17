@echo off
title Fix PSP Names
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\fix-names-wizard.ps1"
pause
