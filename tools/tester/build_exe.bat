@echo off
REM Builds a standalone Windows .exe for the URTC Tester.
REM Run this on a Windows machine with Python installed.
REM
REM Usage:
REM   build_exe.bat
REM
REM Output: dist\URTC_Tester.exe (no Python installation needed to run it)
REM
REM NOTE: every step below runs through "python -m" rather than calling
REM pip/pyinstaller directly - see the flasher's build_exe.bat for the
REM full reasoning (Scripts\ not always being on PATH).

python -m pip install --upgrade pip
python -m pip install -r requirements.txt
python -m pip install pyinstaller

REM Clean slate before compiling - see build_exe.sh for the reasoning.
if exist build rmdir /s /q build
if exist dist rmdir /s /q dist

REM --icon sets what Explorer/the taskbar shows for the .exe file itself -
REM separate from root.iconphoto() in the code, which sets the title-bar/
REM Alt-Tab icon of the running window. Both need setting for a consistent
REM icon everywhere.
python -m PyInstaller --onefile --windowed --noconfirm --name "URTC_Tester" ^
    --icon "assets\urtc_icon.ico" ^
    --add-data "assets;assets" ^
    urtc_tester.py

echo.
echo Build complete. Find the executable at dist\URTC_Tester.exe
pause
