@echo off
chcp 65001 >nul
title ClassFlow Setup
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
