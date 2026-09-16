@echo off
chcp 65001 >nul
title ClassFlow Uninstall
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall.ps1"
