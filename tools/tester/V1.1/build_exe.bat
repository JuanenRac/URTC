@echo off
setlocal EnableDelayedExpansion
REM Builds a standalone Windows .exe for the URTC Tester.
REM Run this on a Windows machine with Python installed.
REM
REM Usage:
REM   build_exe.bat
REM
REM Output: dist\URTC_Tester.exe (no Python installation needed to run it)

echo.
echo  ===============================================================
echo   U R T C   T E S T E R  -  Windows build
echo  ===============================================================
echo   Universal Robot Tool Controller
echo   Author:  JuanenRac (Electro Hobby 3D)
echo   E-mail:  electrohobby3d@gmail.com
echo   License: GPL-3.0
echo  ===============================================================
echo.

REM NOTE: every step below runs through "python -m" rather than calling
REM pip/pyinstaller directly - see the flasher's build_exe.bat for the
REM full reasoning (Scripts\ not always being on PATH).

echo [1/5] Installing Python dependencies...
python -m pip install --upgrade pip >nul
python -m pip install -r requirements.txt
python -m pip install pyinstaller
echo       Done.
echo.

echo [2/5] Cleaning previous build...
REM Clean slate before compiling - see build_exe.sh for the reasoning.
if exist build rmdir /s /q build
if exist dist (
    rmdir /s /q dist
    if exist dist (
        echo       ERROR: couldn't remove dist\ - is URTC_Tester.exe currently running?
        echo       Close it first, then run this script again.
        exit /b 1
    )
)
echo       Done.
echo.

echo [3/5] Compiling URTC_Tester.exe with PyInstaller...
REM --icon sets what Explorer/the taskbar shows for the .exe file itself -
REM separate from root.iconphoto() in the code, which sets the title-bar/
REM Alt-Tab icon of the running window. Both need setting for a consistent
REM icon everywhere.
REM --hidden-import for each of this project's own modules - see the
REM Linux build script's identical comment for the full reasoning.
python -m PyInstaller --onefile --windowed --noconfirm --name "URTC_Tester" ^
    --icon "assets\urtc_icon.ico" ^
    --add-data "assets;assets" ^
    --hidden-import tester_config ^
    --hidden-import tester_transports ^
    --hidden-import tester_bus_monitor ^
    --hidden-import tester_gui_core ^
    --hidden-import tester_common_panels ^
    --hidden-import tester_panel_helpers ^
    --hidden-import tester_tool_panels ^
    urtc_tester.py
if not exist dist\URTC_Tester.exe (
    echo       ERROR: PyInstaller did not produce dist\URTC_Tester.exe - see the output above.
    exit /b 1
)
echo       Done.
echo.

echo [4/5] Copying files that must sit next to the .exe, not inside it...
REM language/ is deliberately NOT bundled into the .exe itself (unlike
REM assets above) - it's meant to stay editable without a rebuild, same
REM reasoning as the flasher's own firmware\ folder. Without this, the
REM built .exe would have nowhere to find its translation files, since
REM LANGUAGE_FOLDER resolves next to the executable, not inside
REM PyInstaller's bundled data.
if exist language (
    if not exist dist\language mkdir dist\language
    xcopy /e /i /y language dist\language >nul
    echo       Copied language\ into dist\language\
)
REM README.md and LICENSE: read directly by the Help menu's Readme/License
REM entries (tester_config.base_dir - next to the .exe, same reasoning as
REM language above). Missing from dist/ meant those menu entries had
REM nothing to open in a built .exe, even though they worked fine running
REM from source.
REM README.md and LICENSE: read directly by the Help menu's Readme/License
REM entries (tester_config.base_dir - next to the .exe, same reasoning as
REM language above). Missing from dist/ meant those menu entries had
REM nothing to open in a built .exe, even though they worked fine running
REM from source. README_*.md (README_spa.md, README_ita.md, etc.) are the
REM per-language versions the Readme menu entry picks up automatically
REM based on the active language - copied via a for loop rather than
REM listing each language explicitly, so a new translation added later is
REM picked up without editing this script again.
if exist README.md (
    copy /Y README.md dist\README.md >nul
    echo       Copied README.md into dist\
)
for %%f in (README_*.md) do (
    copy /Y "%%f" "dist\%%f" >nul
    echo       Copied %%f into dist\
)
if exist ..\..\..\LICENSE (
    copy /Y ..\..\..\LICENSE dist\LICENSE >nul
    echo       Copied LICENSE into dist\
)
echo       Done.
echo.

echo [5/5] Build complete.
echo  ===============================================================
echo   dist\URTC_Tester.exe is ready to run - no Python needed.
echo  ===============================================================
echo.
pause
