@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-LocalRoomTest.ps1" -Account bob -CopyCredentialOnly
pause
