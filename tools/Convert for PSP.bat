@echo off
title Convert for PSP
if "%~1"=="" (
  echo Drag a video, or a folder of videos, onto this icon.
  echo The PSP versions go to "PSP Videos" on your Desktop. Copy them into VIDEO on the Memory Stick.
  echo Works with .mkv .mp4 .m4v .avi .mov .wmv .webm ^(DRM-free only^).
  pause & exit /b
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\convert-dropped.ps1" %*
