@echo off
REM BOT32 - Launch web UI (Windows)
cd /d "%~dp0"
echo Installing/updating Python dependencies...
python -m pip install -r requirements.txt --quiet
echo.
echo Starting BOT32 web UI...
echo Open http://127.0.0.1:5000 in your browser
echo Press Ctrl+C to stop.
echo.
python server.py %*
pause
