#!/usr/bin/env bash
# Builds a standalone Linux binary for the URTC Flasher.
# Run this on the Linux machine you actually want to run it on - unlike
# cross-compiling, PyInstaller builds a binary for whatever OS it runs on,
# so this won't produce something usable on Windows, and build_exe.bat
# won't produce something usable here.
#
# Usage:
#   chmod +x build_exe.sh   (one-time)
#   ./build_exe.sh
#
# Output: dist/URTC_Flasher (no Python installation needed to run it)
#
# NOTE: python3 -m pip / python3 -m PyInstaller, same reasoning as
# build_exe.bat's Windows PATH note - calling the installed modules
# directly sidesteps any question of whether their wrapper scripts landed
# somewhere on PATH.
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
# artifacts (its own bytecode/dependency cache), and dist/ holds the
# previous output - removing both first means nothing stale from an
# earlier build can survive into this one, rather than relying on
# --noconfirm alone to just overwrite the final binary.
rm -rf build dist

# --noconfirm: kept as a second layer even with the clean above - in case
# dist/ gets recreated between the rm and this running.
python3 -m PyInstaller --onefile --noconfirm --name "URTC_Flasher" \
    --add-data "assets:assets" \
    urtc_flasher.py

# firmware/ is deliberately NOT bundled into the binary itself (unlike
# assets/ above) - it's meant to stay editable without a rebuild. Copying
# whatever's in it right now into dist/ just means the built binary works
# standalone out of the box; you can still add more .bin files into
# dist/firmware/ afterward without touching this script again.
if [ -d firmware ]; then
    mkdir -p dist/firmware
    cp -r firmware/. dist/firmware/
    echo "Copied firmware/ into dist/firmware/"
fi

echo
echo "Build complete. Find the binary at dist/URTC_Flasher"
echo "(chmod +x dist/URTC_Flasher if it isn't already executable)"
