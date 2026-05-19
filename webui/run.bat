@echo off
REM BOT32 - Launch web UI (auto-opens browser)
cd /d "%~dp0"
title BOT32 Web UI

REM Install/update Python deps quietly (only if requirements changed)
python -m pip install -r requirements.txt --quiet --disable-pip-version-check 1>nul 2>&1

REM Start the server (auto-opens browser after 1.5s)
python server.py %*

pause
