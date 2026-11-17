@echo off
title Shrink Books for PSP
if "%~1"=="" (
  echo Drag a .cbz or .cbr book, or a folder of books, onto this icon.
  echo Smaller copies go to "PSP Books" on your Desktop. Copy them into BOOKS on the Memory Stick.
  echo Drag the Memory Stick's own BOOKS folder here to shrink the books already on it ^(it asks first^).
  echo .cbr books need 7-Zip: run "Install Helpers.bat" once.
  pause & exit /b
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\shrink-dropped.ps1" %*
