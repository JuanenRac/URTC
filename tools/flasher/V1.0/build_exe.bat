@echo off
REM Builds a standalone Windows .exe for the URTC Flasher.
REM Run this on a Windows machine with Python installed.
REM
REM Usage:
REM   build_exe.bat
REM
REM Output: dist\URTC_Flasher.exe (no Python installation needed to run it)
REM
REM NOTE: every step below runs through "python -m" rather than calling
REM pip/pyinstaller directly. pip.exe and pyinstaller.exe both get installed
REM into Python's Scripts\ folder, which on plenty of Windows setups isn't
REM on PATH (common if Python was installed without checking "Add Python to
REM PATH") - calling them directly then fails with a "not recognized"
REM error even though the install itself succeeded. "python -m" sidesteps
REM this by finding the installed module directly rather than needing its
REM wrapper .exe to be on PATH.

python -m pip install --upgrade pip
python -m pip install -r requirements.txt
python -m pip install pyinstaller

REM Clean slate before compiling: build\ holds PyInstaller's intermediate
REM artifacts (its own bytecode/dependency cache), and dist\ holds the
REM previous output - removing both first means nothing stale from an
REM earlier build can survive into this one, rather than relying on
REM --noconfirm alone to just overwrite the final .exe.
if exist build rmdir /s /q build
if exist dist (
    rmdir /s /q dist
    if exist dist (
        echo ERROR: couldn't remove dist\ - is URTC_Flasher.exe currently running?
        echo Close it first, then run this script again.
        exit /b 1
    )
)

REM --add-data uses ";" as the source/destination separator on Windows -
REM Linux/Mac PyInstaller uses ":" instead (see build_exe.sh). Bundles the
REM assets/ folder (the banner + icon images) into the .exe itself so it
REM doesn't need to sit next to it the way firmware/ does.
REM --icon sets what Explorer/the taskbar shows for the .exe file itself -
REM separate from root.iconphoto() in the code, which sets the title-bar/
REM Alt-Tab icon of the running window. Both need setting for a consistent
REM icon everywhere.
REM --noconfirm: kept as a second layer even with the clean above - in
REM case dist\ gets recreated between the rmdir and this running.
python -m PyInstaller --onefile --windowed --noconfirm --name "URTC_Flasher" ^
    --icon "assets\urtc_icon.ico" ^
    --add-data "assets;assets" ^
    urtc_flasher.py

REM firmware/ is deliberately NOT bundled into the .exe itself (unlike
REM assets/ above) - it's meant to stay editable without a rebuild. Copying
REM whatever's in it right now next to the .exe just means the standalone
REM dist/ folder works out of the box; you can still add more .bin files
REM into dist\firmware\ afterward without touching this script again.
if exist firmware (
    xcopy /E /I /Y firmware dist\firmware >nul
    echo Copied firmware\ into dist\firmware\
)

echo.
echo Build complete. Find the executable at dist\URTC_Flasher.exe
pause
