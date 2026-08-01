#!/usr/bin/env python3
"""
URTC Tester - live CAN bus exerciser for the URTC (Universal Robot Tool
Controller) board.
Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>

Licensed under the GNU General Public License v3.0 (GPL-3.0), matching the
URTC firmware and the flasher tool. See LICENSE in the repository root.

Connects over the same USB-CAN adapter/interface the flasher uses, asks
the board which of its 12 tool profiles it's currently jumpered for, and
shows only that tool's own controls and live telemetry - per CANBUS.TXT -
rather than one window trying to represent all 12 at once.

Shares its transport layer (SLCAN/SocketCAN) with urtc_flasher.py, since
both tools ultimately just need to get CAN frames on and off the same
kind of adapter - but this tool never touches flash: everything it does
is runtime commands and telemetry against the currently-running
application, nothing that can leave the board any less working than it
started. Zero non-stdlib dependencies beyond pyserial, matching the flasher.

This is the entry point only - the splash screen and main window setup.
Everything else lives in its own module: tester_config.py (config/
language/protocol constants), tester_transports.py (SLCAN/SocketCAN),
tester_bus_monitor.py (the background CAN read thread), and TesterGUI
itself, split by responsibility across tester_gui_core.py (connection/
detection/window lifecycle) and 3 mixins it combines - tester_common_
panels.py, tester_panel_helpers.py, and tester_tool_panels.py.
"""

import tkinter as tk

from tester_config import BANNER_IMAGE_PATH, _center_geometry
from tester_gui_core import TesterGUI


def _show_splash_then(root, on_done):
    """Shows the banner centered on screen for 5s, then calls on_done() and
    closes the splash. Built as a borderless Toplevel rather than the
    banner being part of the main window itself, so the main window can
    stay smaller - the banner only needs screen space for those first 5
    seconds, not for the entire session. Skipped straight to on_done() if
    the banner image can't be loaded for any reason (missing file, etc.) -
    a missing splash was never worth blocking startup over.
    """
    try:
        banner_img = tk.PhotoImage(file=BANNER_IMAGE_PATH)
    except (tk.TclError, OSError):
        on_done()
        return

    splash = tk.Toplevel(root)
    splash.withdraw()  # stays hidden until correctly positioned below - see the flasher's identical fix for the full reasoning
    splash.overrideredirect(True)
    splash.configure(bg="#14171C")
    label = tk.Label(splash, image=banner_img, bg="#14171C", borderwidth=0)
    label.image = banner_img
    label.pack()

    splash.geometry(_center_geometry(splash, banner_img.width(), banner_img.height()))
    splash.attributes("-topmost", True)
    splash.deiconify()  # only shown now that it's already correctly positioned

    def _finish():
        splash.destroy()
        on_done()

    splash.after(5000, _finish)


def main():
    root = tk.Tk()
    root.withdraw()
    # Sized from actual measured content across all 5 supported languages
    # and all 12 tool panels (root.winfo_reqwidth/reqheight after
    # building the real GUI, not a guess) - re-measured after
    # redesigning the 3D printer panel (the tallest of the 12) into 2
    # columns instead of one long stack. Worst case: Spanish for width
    # (1342px measured), German for height (1054px measured). A small
    # margin above those worst-case measurements.
    root.geometry(_center_geometry(root, 1360, 1075))
    app = TesterGUI(root)

    def _reveal_main():
        root.deiconify()

    _show_splash_then(root, _reveal_main)
    root.mainloop()


if __name__ == "__main__":
    main()
