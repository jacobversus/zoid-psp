@echo off
title Add Desktop Shortcuts
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\add-shortcuts.ps1"
pause
