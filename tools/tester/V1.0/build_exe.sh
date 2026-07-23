#!/usr/bin/env bash
# Builds a standalone Linux binary for the URTC Tester.
# Run this on the Linux machine you actually want to run it on - unlike
# cross-compiling, PyInstaller builds a binary for whatever OS it runs on,
# so this won't produce something usable on Windows, and build_exe.bat
# won't produce something usable here.
#
# Usage:
#   chmod +x build_exe.sh   (one-time)
#   ./build_exe.sh
#
# Output: dist/URTC_Tester (no Python installation needed to run it)
set -euo pipefail

# python3-tk is a separate OS package on Debian/Ubuntu-family distros -
# tkinter isn't pulled in automatically by "pip install", since it isn't a
# pip package at all. Check for it explicitly with a clear message instead
# of letting the build succeed and then fail confusingly at runtime.
if ! python3 -c "import tkinter" 2>/dev/null; then
    echo "tkinter isn't available for this Python install."
    echo "On Debian/Ubuntu:  sudo apt install python3-tk"
    echo "On Fedora:         sudo dnf install python3-tkinter"
    echo "On Arch:           sudo pacman -S tk"
    exit 1
fi

python3 -m pip install --upgrade pip
python3 -m pip install -r requirements.txt
python3 -m pip install pyinstaller

# Clean slate before compiling: build/ holds PyInstaller's intermediate
# artifacts, dist/ holds the previous output - removing both first means
# nothing stale from an earlier build can survive into this one.
rm -rf build dist

python3 -m PyInstaller --onefile --noconfirm --name "URTC_Tester" \
    --add-data "assets:assets" \
    urtc_tester.py

echo
echo "Build complete. Find the binary at dist/URTC_Tester"
echo "(chmod +x dist/URTC_Tester if it isn't already executable)"
