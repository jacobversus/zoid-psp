@echo off
title Check Video for PSP
if "%~1"=="" (echo Drag one or more .mp4 videos onto this icon to see whether ZOID can play them. & pause & exit /b)
:next
if "%~1"=="" goto done
echo.
echo %~nx1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\check-video.ps1" -Path "%~1"
shift
goto next
:done
echo.
pause
