# =============================================================================
# URTC Tester - CommonPanelsMixin: panels and actions shared across every
# tool (global status/lighting, F-RAM, expansion board, self-test, bus
# monitor, custom frame sender). No standalone class - mixed into
# TesterGUI in tester_gui_core.py alongside the other panel mixins.
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0 - see LICENSE
# =============================================================================
import struct
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox, filedialog

from tester_config import (
    _, CAN_ID_3DP_HOTEND_TELEM, CAN_ID_3DP_THERMAL_MOTION, CAN_ID_ACTIVE_TOOL_RESP,
    CAN_ID_AOI_TELEMETRY, CAN_ID_DIAG0_RESP, CAN_ID_DRILL_CMD, CAN_ID_DRILL_TELEMETRY,
    CAN_ID_ERASE_FRAM, CAN_ID_EXPANSION_TYPE_RESP, CAN_ID_EXP_SPI_CMD, CAN_ID_EXP_SPI_RESP,
    CAN_ID_FRAM_STATE_RESP, CAN_ID_FREE_TOOL_CONFIG_RESP, CAN_ID_PERIPHERAL_INFO_RESP, CAN_ID_GLOBAL_STATUS, CAN_ID_LASER_CMD, CAN_ID_LASER_TELEMETRY,
    CAN_ID_QUERY_ACTIVE_TOOL, CAN_ID_QUERY_DIAG0, CAN_ID_QUERY_FRAM_STATE,
    CAN_ID_QUERY_VERSION, CAN_ID_SOLDER_SETPOINT, CAN_ID_SOLDER_TELEMETRY,
    CAN_ID_VACUUM_TELEMETRY, CAN_ID_VERSION_RESPONSE, CUSTOM_ID_NAMES, ERASE_FRAM_MAGIC,
    EXPANSION_BOARD_TYPES, TOOL_NAMES,
)


class CommonPanelsMixin:

    def _build_global_panel(self, parent):
        # 0x100 - Status LED (override) + ring LED + OLED mode, all sent
        # together in one 8-byte frame regardless of which tool is active.
        # The override holds 10s from the last frame received before
        # falling back to automatic - low-stakes if it lapses (nothing
        # unsafe happens, it just reverts to the automatic color scheme),
        # so unlike the watchdog-guarded tool commands below, this is a
        # plain "Send" button rather than an auto-repeating keepalive.
        self.status_r = tk.IntVar(value=0)
        self.status_g = tk.IntVar(value=255)
        self.status_b = tk.IntVar(value=0)
        self.ring_r = tk.IntVar(value=0)
        self.ring_g = tk.IntVar(value=0)
        self.ring_b = tk.IntVar(value=255)
        self.ring_on = tk.BooleanVar(value=False)

        ttk.Label(parent, text=_("LBL_STATUS_LED_OVERRIDE")).grid(row=0, column=0, sticky="w", padx=4, pady=2)
        rgb_row = ttk.Frame(parent)
        rgb_row.grid(row=0, column=1, columnspan=3, sticky="w")
        for var in (self.status_r, self.status_g, self.status_b):
            ttk.Spinbox(rgb_row, from_=0, to=255, textvariable=var, width=5).pack(side="left", padx=2)

        ttk.Label(parent, text=_("LBL_RING_LED_RGB")).grid(row=1, column=0, sticky="w", padx=4, pady=2)
        ring_row = ttk.Frame(parent)
        ring_row.grid(row=1, column=1, columnspan=3, sticky="w")
        for var in (self.ring_r, self.ring_g, self.ring_b):
            ttk.Spinbox(ring_row, from_=0, to=255, textvariable=var, width=5).pack(side="left", padx=2)
        ttk.Checkbutton(ring_row, text=_("CHK_RING_ON"), variable=self.ring_on).pack(side="left", padx=(12, 2))

        ttk.Label(parent, text=_("LBL_OLED_MODE")).grid(row=2, column=0, sticky="w", padx=4, pady=2)
        self.night_mode_var, night_combo = self._translated_combobox(
            parent, ["Standard", "Night", "Standby"], "OPT", width=12,
        )
        night_combo.grid(row=2, column=1, sticky="w", padx=4, pady=2)
        ttk.Button(parent, text=_("BTN_SEND"), command=self._send_global_status).grid(row=2, column=2, padx=4, pady=2)
        ttk.Label(
            parent,
            text=_("HELP_AOI_RING_IGNORED"),
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=3, column=0, columnspan=4, sticky="w", padx=4, pady=(0, 4))

    def _send_global_status(self):
        if self.bus is None:
            messagebox.showerror(_("TITLE_NOT_CONNECTED"), _("MSG_CONNECT_FIRST"))
            return
        night_code = {"Standard": 0x00, "Night": 0x01, "Standby": 0x0F}[self.night_mode_var.get()]
        data = bytes([
            max(0, min(255, self._safe_int(self.status_r, 0))),
            max(0, min(255, self._safe_int(self.status_g, 0))),
            max(0, min(255, self._safe_int(self.status_b, 0))),
            night_code,
            max(0, min(255, self._safe_int(self.ring_r, 0))),
            max(0, min(255, self._safe_int(self.ring_g, 0))),
            max(0, min(255, self._safe_int(self.ring_b, 0))),
            0x01 if self.ring_on.get() else 0x00,
        ])
        self.bus.send(CAN_ID_GLOBAL_STATUS, data)
        self.log(_("LOG_SENT_GLOBAL_STATUS", r1=data[0], g1=data[1], b1=data[2], night=self.night_mode_var.get(), r2=data[4], g2=data[5], b2=data[6], ring_on=self.ring_on.get()))

    def _build_expansion_panel(self, parent):
        # CONN_EXPANSION's bit-banged SPI bus (0x180/0x181) - a generic
        # byte passthrough, not TMC5160-register-aware, matching the
        # firmware's own approach (see CANBUS.TXT): this tool doesn't
        # need to know that chip's specific protocol either, just send
        # and show raw bytes.
        self.spi_send_var = tk.StringVar(value="01 02 03 04")
        ttk.Label(parent, text=_("LBL_SPI_BYTES_TO_SEND")).grid(
            row=0, column=0, columnspan=2, sticky="w", padx=4, pady=(4, 0))
        ttk.Entry(parent, textvariable=self.spi_send_var, width=30).grid(
            row=1, column=0, sticky="w", padx=4, pady=2)
        ttk.Button(parent, text=_("BTN_SEND"), command=self._send_expansion_spi).grid(row=1, column=1, padx=4)
        self.spi_response_var = tk.StringVar(value="(nothing sent yet)")
        ttk.Label(parent, text=_("LBL_RESPONSE")).grid(row=2, column=0, sticky="w", padx=4, pady=(4, 0))
        ttk.Label(parent, textvariable=self.spi_response_var, font=("Courier", 9)).grid(
            row=3, column=0, columnspan=2, sticky="w", padx=4)
        ttk.Label(
            parent,
            text=_("HELP_UP_TO_7_BYTES_SPI"),
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=4, column=0, columnspan=2, sticky="w", padx=4, pady=(0, 8))

        ttk.Separator(parent, orient="horizontal").grid(row=5, column=0, columnspan=2, sticky="ew", pady=4)

        # TMC_DIAG0 level (0x182/0x183) - combined stall/fault line,
        # diode-ORed together from the onboard TMC2209's own DIAG and
        # whatever driver populates the expansion connector (see the
        # firmware's own TMC_DIAG0_PIN comment for the full topology).
        # Simple polled read, not a live/pushed value.
        diag_row = ttk.Frame(parent)
        diag_row.grid(row=6, column=0, columnspan=2, sticky="w", padx=4, pady=(4, 2))
        ttk.Button(diag_row, text=_("BTN_QUERY_DIAG0"), command=self._query_diag0).pack(side="left")
        self.diag0_var = tk.StringVar(value="(not queried yet)")
        ttk.Label(diag_row, textvariable=self.diag0_var, font=("Courier", 9)).pack(side="left", padx=(8, 0))

    def _build_fram_panel(self, parent):
        # F-RAM recovered-state query/erase (0x190/0x191/0x192). Deliberately
        # NOT part of the expansion panel above: the FM24CL64B shares the
        # OLED's own hardware I2C2 bus (a core board component), it has
        # nothing to do with CONN_EXPANSION's own bit-banged SPI/I2C bus at all - the expansion
        # connector itself has no F-RAM, no EEPROM, nothing non-volatile on
        # it, and grouping them together implied a connection that isn't
        # real.
        ttk.Label(parent, text=_("LBL_PERSISTENCE_FRAM")).grid(
            row=0, column=0, columnspan=2, sticky="w", padx=4, pady=(4, 2))
        btn_row = ttk.Frame(parent)
        btn_row.grid(row=1, column=0, columnspan=2, sticky="w", padx=4)
        ttk.Button(btn_row, text=_("BTN_QUERY_STATE"), command=self._query_fram_state).pack(side="left")
        ttk.Button(btn_row, text=_("BTN_ERASE_FRAM"), command=self._erase_fram).pack(side="left", padx=(8, 0))
        self.fram_state_var = tk.StringVar(value="(not queried yet)")
        ttk.Label(parent, textvariable=self.fram_state_var, wraplength=380, justify="left").grid(
            row=2, column=0, columnspan=2, sticky="w", padx=4, pady=(4, 4))

        ttk.Separator(parent, orient="horizontal").grid(row=3, column=0, columnspan=2, sticky="ew", pady=4)
        ttk.Label(parent, text=_("LBL_EXPANSION_BOARD_TYPE")).grid(
            row=4, column=0, columnspan=2, sticky="w", padx=4, pady=(0, 2))
        exp_type_row = ttk.Frame(parent)
        exp_type_row.grid(row=5, column=0, columnspan=2, sticky="w", padx=4)
        ttk.Button(exp_type_row, text=_("BTN_QUERY"), command=self._query_expansion_board_type).pack(side="left")
        self.expansion_board_type_var = tk.StringVar(value="(not queried yet)")
        ttk.Label(exp_type_row, textvariable=self.expansion_board_type_var).pack(side="left", padx=(8, 0))
        ttk.Label(
            parent,
            text=_("HELP_READONLY_SET_VIA_FLASHER"),
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=6, column=0, columnspan=2, sticky="w", padx=4, pady=(2, 4))

        ttk.Separator(parent, orient="horizontal").grid(row=7, column=0, columnspan=2, sticky="ew", pady=4)
        # Free tool configuration (0x1A2/0x1A3) - relevant only when the
        # ID-jumper reading is 0x1F (11111b, all 5 installed); see
        # EEPROM.TXT section 5 for the full mechanism. Two values shown,
        # not one: the raw ID-jumper reading (so it's clear whether this
        # register is even being consulted on this board) and the current
        # free_tool_selection (what it resolves to). Read-only here, same
        # reasoning as expansion board type above - the Flasher is the
        # only tool that writes it.
        ttk.Label(parent, text=_("LBL_FREE_TOOL_CONFIG")).grid(
            row=8, column=0, columnspan=2, sticky="w", padx=4, pady=(0, 2))
        free_tool_row = ttk.Frame(parent)
        free_tool_row.grid(row=9, column=0, columnspan=2, sticky="w", padx=4)
        ttk.Button(free_tool_row, text=_("BTN_QUERY"), command=self._query_free_tool_config).pack(side="left")
        self.free_tool_config_var = tk.StringVar(value="(not queried yet)")
        ttk.Label(free_tool_row, textvariable=self.free_tool_config_var, wraplength=300, justify="left").pack(
            side="left", padx=(8, 0))
        ttk.Label(
            parent,
            text=_("HELP_READONLY_SET_VIA_FLASHER"),
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=10, column=0, columnspan=2, sticky="w", padx=4, pady=(2, 4))

        ttk.Separator(parent, orient="horizontal").grid(row=11, column=0, columnspan=2, sticky="ew", pady=4)
        # Peripheral type + device serial number (0x1A4/0x1A5) - see
        # EEPROM.TXT section 6. Exists so multiple otherwise-identical
        # URTC boards on the same CAN bus can be told apart; read-only
        # here, same reasoning as the two sections above - the Flasher
        # is the only tool that writes the serial (the type itself is a
        # fixed constant, never writable by anything).
        ttk.Label(parent, text=_("LBL_PERIPHERAL_INFO")).grid(
            row=12, column=0, columnspan=2, sticky="w", padx=4, pady=(0, 2))
        peripheral_row = ttk.Frame(parent)
        peripheral_row.grid(row=13, column=0, columnspan=2, sticky="w", padx=4)
        ttk.Button(peripheral_row, text=_("BTN_QUERY"), command=self._query_peripheral_info).pack(side="left")
        self.peripheral_info_var = tk.StringVar(value="(not queried yet)")
        ttk.Label(peripheral_row, textvariable=self.peripheral_info_var, wraplength=300, justify="left").pack(
            side="left", padx=(8, 0))
        ttk.Label(
            parent,
            text=_("HELP_READONLY_SET_VIA_FLASHER"),
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=14, column=0, columnspan=2, sticky="w", padx=4, pady=(2, 4))

    # Tool-specific self-test steps: (description, cmd_id_or_None, cmd_data,
    # expect_id_or_None). Every command here is a safe, at-rest value
    # (0 setpoint, 0 speed, 0 power) - this verifies the communication
    # round-trip works, not that an actuator physically responds, since
    # confirming the latter needs a human watching anyway. Tools with no
    # telemetry (plain motion) or that are purely event-driven (scan
    # probe) get an info-only entry rather than a real pass/fail, since
    # there's nothing to safely verify without a physical trigger.
    _SELF_TEST_STEPS = {
        0: [("Soldering iron: safe setpoint (0°C) elicits telemetry",
             CAN_ID_SOLDER_SETPOINT, struct.pack(">H", 0), CAN_ID_SOLDER_TELEMETRY)],
        5: [("Drill: safe speed (0) elicits telemetry",
             CAN_ID_DRILL_CMD, bytes([0, 0]), CAN_ID_DRILL_TELEMETRY)],
        9: [("Laser: safe power/interlock (0/0) elicits endstop telemetry",
             CAN_ID_LASER_CMD, bytes([0, 0]), CAN_ID_LASER_TELEMETRY)],
        10: [("3D printer: safe nozzle setpoint (0°C) elicits hotend telemetry",
              CAN_ID_3DP_THERMAL_MOTION, struct.pack(">H", 0) + bytes([0, 0, 0, 0]), CAN_ID_3DP_HOTEND_TELEM)],
        8: [("AOI: endstop telemetry arrives (no command needed - continuous)", None, None, CAN_ID_AOI_TELEMETRY)],
        4: [("Vacuum: ADC telemetry arrives (no command needed - continuous)", None, None, CAN_ID_VACUUM_TELEMETRY)],
        11: [("Scan probe is event-driven (only sends on physical impact) - "
              "cannot verify without triggering it by hand", None, None, None)],
    }
    _MOTION_TOOL_IDS = (1, 2, 3, 6, 7)

    def _run_self_test(self):
        if self.bus is None:
            messagebox.showerror(_("TITLE_NOT_CONNECTED"), _("MSG_CONNECT_FIRST"))
            return
        if self.active_tool_id is None:
            messagebox.showerror(_("TITLE_NOT_DETECTED"), _("MSG_RUN_DETECT_FIRST"))
            return
        steps = self._SELF_TEST_STEPS.get(self.active_tool_id)
        is_motion = self.active_tool_id in self._MOTION_TOOL_IDS
        if not messagebox.askyesno(
            _("TITLE_RUN_SELF_TEST_Q"),
            _("MSG_SELF_TEST_CONFIRM"),
        ):
            return

        win = tk.Toplevel(self.root)
        win.title(f"Self-Test - {TOOL_NAMES.get(self.active_tool_id, '?')}")
        win.geometry("480x260")
        text = tk.Text(win, state="disabled", wrap="word")
        text.pack(fill="both", expand=True, padx=8, pady=8)

        def _append(line):
            def _do():
                if not text.winfo_exists():
                    return
                text.config(state="normal")
                text.insert("end", line + "\n")
                text.config(state="disabled")
                text.see("end")
            self.root.after(0, _do)

        def _worker():
            _append(f"Testing: {TOOL_NAMES.get(self.active_tool_id, '?')}\n")
            # Generic checks first - meaningful for every tool.
            resp = self.bus.wait_for_one(CAN_ID_ACTIVE_TOOL_RESP, timeout=1.5,
                                          send_after_register=lambda: self.bus.send(CAN_ID_QUERY_ACTIVE_TOOL, b""))
            ok = resp is not None and len(resp) >= 1 and resp[0] == self.active_tool_id
            _append(f"[{'PASS' if ok else 'FAIL'}] Active-tool query (0x110) confirms detected tool")

            resp = self.bus.wait_for_one(CAN_ID_VERSION_RESPONSE, timeout=1.5,
                                          send_after_register=lambda: self.bus.send(CAN_ID_QUERY_VERSION, b"\x00"))
            _append(f"[{'PASS' if resp is not None else 'FAIL'}] Version query (0x7F8) gets a response")

            if is_motion:
                _append("[INFO] This tool has no telemetry to verify - STEP/DIR/EN "
                        "commands are one-shot with no response by design.")
            elif steps:
                for description, cmd_id, cmd_data, expect_id in steps:
                    if expect_id is None:
                        _append(f"[INFO] {description}")
                        continue
                    resp = self.bus.wait_for_one(
                        expect_id, timeout=1.5,
                        send_after_register=(lambda c=cmd_id, d=cmd_data: self.bus.send(c, d)) if cmd_id is not None else None)
                    _append(f"[{'PASS' if resp is not None else 'FAIL'}] {description}")
            else:
                _append("[INFO] No self-test steps defined for this tool yet.")
            _append("\nDone.")

        threading.Thread(target=_worker, daemon=True).start()

    def _open_bus_monitor(self):
        if self.bus is None:
            messagebox.showerror(_("TITLE_NOT_CONNECTED"), _("MSG_CONNECT_FIRST"))
            return
        if getattr(self, "_bus_monitor_win", None) is not None and self._bus_monitor_win.winfo_exists():
            self._bus_monitor_win.deiconify()  # restores if minimized - lift() alone doesn't
            self._bus_monitor_win.lift()  # already open - just bring it to the front rather than opening a second one
            return

        win = tk.Toplevel(self.root)
        win.title("Raw Bus Monitor")
        win.geometry("640x460")
        self._bus_monitor_win = win
        self._bus_monitor_paused = False
        self._bus_monitor_last_tick = None
        self._bus_monitor_row_count = 0
        self._bus_monitor_window_frames = 0
        self._bus_monitor_window_bits = 0
        # _on_frame (below) increments these from the CAN reading thread;
        # _update_stats (below) reads and resets them from the Tkinter
        # main thread via win.after() - a plain lock around both sides
        # keeps the increment and the read-then-reset from interleaving.
        self._bus_monitor_window_lock = threading.Lock()

        top_row = ttk.Frame(win)
        top_row.pack(fill="x", padx=6, pady=6)
        pause_var = tk.BooleanVar(value=False)

        def _toggle_pause():
            self._bus_monitor_paused = pause_var.get()

        ttk.Checkbutton(top_row, text=_("BTN_PAUSE"), variable=pause_var, command=_toggle_pause).pack(side="left")

        def _clear():
            tree.delete(*tree.get_children())
            self._bus_monitor_row_count = 0
            with self._bus_monitor_window_lock:
                self._bus_monitor_last_tick = None

        ttk.Button(top_row, text=_("BTN_CLEAR"), command=_clear).pack(side="left", padx=(8, 0))

        def _parse_ts_to_seconds(ts_str):
            # Recorded as "HH:MM:SS.mmm" (see the bus monitor's own _on_frame
            # above) - parsed back into seconds-since-midnight so a
            # relative offset from the first frame can be computed below,
            # preserving real inter-frame timing instead of a synthetic,
            # uniform per-row value that would discard it entirely.
            try:
                hms, ms = ts_str.split(".")
                h, m, s = (int(x) for x in hms.split(":"))
                return h * 3600 + m * 60 + s + int(ms) / 1000.0
            except (ValueError, AttributeError):
                return None

        def _export(fmt):
            rows = [tree.item(item)["values"] for item in tree.get_children()]
            if not rows:
                messagebox.showinfo(_("TITLE_NOTHING_TO_EXPORT"), _("MSG_TABLE_IS_EMPTY"))
                return
            ext = ".trc" if fmt == "trc" else ".asc"
            path = filedialog.asksaveasfilename(defaultextension=ext, filetypes=[(f"{fmt.upper()} files", f"*{ext}")])
            if not path:
                return
            # Relative offsets (first frame = 0.0) from the real recorded
            # timestamps - falls back to the old synthetic i*10/i*0.01
            # spacing only for a row whose timestamp genuinely can't be
            # parsed, rather than failing the whole export over one bad row.
            parsed = [_parse_ts_to_seconds(r[0]) for r in rows]
            t0 = next((t for t in parsed if t is not None), None)
            offsets_ms = []
            for i, t in enumerate(parsed):
                if t is not None and t0 is not None:
                    offsets_ms.append((t - t0) * 1000.0)
                else:
                    offsets_ms.append(i * 10.0)
            try:
                with open(path, "w") as f:
                    if fmt == "trc":
                        # Simplified PEAK PCAN-View trace format - the key
                        # structural elements (header, one line per frame
                        # with index/offset/type/ID/DLC/data), not
                        # guaranteed byte-identical to what PCAN-View's own
                        # exporter produces for every possible viewer.
                        f.write(";$FILEVERSION=1.1\n;$STARTTIME=0\n;$COLUMNS=N,O,T,I,d,l,D\n;\n")
                        f.write(";   Message Number\n;   Time Offset (ms)\n;   Type\n"
                                ";   ID (hex)\n;   Data Length Code\n;   Data Bytes (hex)\n")
                        f.write(";" + "-" * 80 + "\n")
                        for i, (ts, can_id, dlc, data, dt) in enumerate(rows, start=1):
                            id_hex = str(can_id).split(" ")[0].replace("0x", "").zfill(4)
                            data_hex = str(data)  # already "" for an empty frame (b"".hex() == ""), no special-casing needed
                            f.write(f"{i:>6}) {offsets_ms[i-1]:>10.1f}  Rx    {id_hex}  {dlc}   {data_hex}\n")
                    else:
                        # Simplified Vector ASCII (.asc) trace format -
                        # same caveat as .trc above: the recognizable shape
                        # of the format, not a byte-perfect match to every
                        # CANalyzer/CANoe version's own output.
                        f.write("date " + time.strftime("%a %b %d %H:%M:%S.000 %Y") + "\n")
                        f.write("base hex  timestamps absolute\n")
                        f.write("internal events logged\n")
                        f.write("Begin Triggerblock " + time.strftime("%a %b %d %H:%M:%S.000 %Y") + "\n")
                        for i, (ts, can_id, dlc, data, dt) in enumerate(rows):
                            id_hex = str(can_id).split(" ")[0].replace("0x", "")
                            data_hex = str(data)  # already "" for an empty frame (b"".hex() == ""), no special-casing needed
                            f.write(f"   {offsets_ms[i] / 1000.0:.6f} 1  {id_hex:<15} Rx   d {dlc} {data_hex}\n")
                        f.write("End TriggerBlock\n")
                self.log(_("LOG_EXPORTED_FRAMES", n=len(rows), path=path))
            except OSError as e:
                messagebox.showerror(_("TITLE_EXPORT_FAILED"), str(e))

        ttk.Button(top_row, text=_("BTN_EXPORT_TRC"), command=lambda: _export("trc")).pack(side="left", padx=(8, 0))
        ttk.Button(top_row, text=_("BTN_EXPORT_ASC"), command=lambda: _export("asc")).pack(side="left", padx=(4, 0))
        ttk.Label(
            top_row, text=_("HELP_SHOWS_EVERY_FRAME"),
            foreground="gray",
        ).pack(side="left", padx=(12, 0))

        columns = ("time", "id", "dlc", "data", "dt")
        tree = ttk.Treeview(win, columns=columns, show="headings", height=18)
        for col, label, width in (
            ("time", _("COL_TIME"), 90), ("id", _("COL_ID"), 160), ("dlc", _("COL_DLC"), 40),
            ("data", _("COL_DATA_HEX"), 220), ("dt", _("COL_DT_MS"), 70),
        ):
            tree.heading(col, text=label)
            tree.column(col, width=width, anchor="w")
        scrollbar = ttk.Scrollbar(win, orient="vertical", command=tree.yview)
        tree.configure(yscrollcommand=scrollbar.set)
        tree.pack(side="left", fill="both", expand=True, padx=(6, 0), pady=(0, 6))
        scrollbar.pack(side="right", fill="y", pady=(0, 6))

        # Frames accumulate here (protected by the same window lock already
        # used for the stats counters) instead of each one scheduling its
        # own root.after - a burst of bus traffic would otherwise inject
        # one Tkinter event-queue task per frame, risking a GUI freeze at
        # high frame rates. _flush_pending_rows (below) drains this
        # periodically instead.
        self._bus_monitor_pending_rows = []

        def _on_frame(can_id, data):
            if self._bus_monitor_paused:
                return
            now = time.time()
            ts = time.strftime("%H:%M:%S", time.localtime(now)) + f".{int(now * 1000) % 1000:03d}"

            # Approximate bit count for this frame - standard 11-bit-ID CAN
            # framing overhead (SOF, ID, control, CRC, ACK, EOF, IFS) is
            # about 47 bits before bit-stuffing, plus 8 bits per data byte.
            # Bit-stuffing itself isn't modeled (it depends on the actual
            # bit pattern, not just the frame length), so this is a
            # deliberately approximate, likely-slight-underestimate - a
            # diagnostic aid for spotting gross bus loading, not a
            # certified measurement.
            with self._bus_monitor_window_lock:
                dt_ms = "" if self._bus_monitor_last_tick is None else f"{(now - self._bus_monitor_last_tick) * 1000:.0f}"
                self._bus_monitor_last_tick = now
                self._bus_monitor_window_bits += 47 + 8 * len(data)
                self._bus_monitor_window_frames += 1
                self._bus_monitor_pending_rows.append((ts, can_id, data, dt_ms))

        def _flush_pending_rows():
            if not win.winfo_exists():
                return
            with self._bus_monitor_window_lock:
                rows = self._bus_monitor_pending_rows
                self._bus_monitor_pending_rows = []
            if rows and tree.winfo_exists():
                for ts, can_id, data, dt_ms in rows:
                    id_display = f"0x{can_id:03X}"
                    if can_id in CUSTOM_ID_NAMES:
                        id_display += f" ({CUSTOM_ID_NAMES[can_id]})"
                    tree.insert("", "end", values=(ts, id_display, len(data), data.hex(" "), dt_ms))
                    self._bus_monitor_row_count += 1
                if self._bus_monitor_row_count > 500:
                    # Bounded, same reasoning as every other unbounded-growth
                    # fix elsewhere in this project - a long session
                    # shouldn't grow this window's memory/widget count
                    # without limit. Trims however many rows are now over
                    # the cap, not just one, since a single flush can add
                    # many rows at once under heavy traffic. One delete()
                    # call with every doomed row's id, not one call per
                    # row - each call crosses the Python/Tcl boundary with
                    # its own overhead, which adds up under sustained
                    # high-traffic monitoring.
                    overflow = self._bus_monitor_row_count - 500
                    to_remove = tree.get_children()[:overflow]
                    if to_remove:
                        tree.delete(*to_remove)
                    self._bus_monitor_row_count = 500
                tree.yview_moveto(1.0)  # auto-scroll to the newest row
            win.after(50, _flush_pending_rows)

        win.after(50, _flush_pending_rows)
        self.bus.register_sniffer(_on_frame)

        stats_var = tk.StringVar(value="Frames/s: -- | Bus load: -- % (approx, 500 kbit/s)")
        ttk.Label(win, textvariable=stats_var, foreground="gray").pack(
            side="bottom", fill="x", padx=6, pady=(0, 6))

        def _update_stats():
            if not win.winfo_exists():
                return
            with self._bus_monitor_window_lock:
                frames = self._bus_monitor_window_frames
                bits = self._bus_monitor_window_bits
                self._bus_monitor_window_frames = 0
                self._bus_monitor_window_bits = 0
            load_pct = min(100.0, bits / 500000 * 100)
            stats_var.set(f"Frames/s: {frames} | Bus load: {load_pct:.1f}% (approx, 500 kbit/s)")
            win.after(1000, _update_stats)

        win.after(1000, _update_stats)

        def _on_close():
            self.bus.unregister_sniffer(_on_frame) if self.bus is not None else None
            self._bus_monitor_win = None
            win.destroy()

        win.protocol("WM_DELETE_WINDOW", _on_close)

    def _query_diag0(self):
        if self.bus is None:
            messagebox.showerror(_("TITLE_NOT_CONNECTED"), _("MSG_CONNECT_FIRST"))
            return
        response = self.bus.wait_for_one(CAN_ID_DIAG0_RESP, timeout=1.0,
                                          send_after_register=lambda: self.bus.send(CAN_ID_QUERY_DIAG0, b""))
        if response is None or len(response) < 1:
            self.diag0_var.set("no response")
            self.log(_("LOG_QUERIED_0X182_NO_RESPONSE"))
            return
        level = "HIGH (asserted - stall/error on onboard or expansion driver)" if response[0] else "LOW (clear)"
        self.diag0_var.set(level)
        self.log(_("LOG_EXP_TMC_DIAG0_LEVEL", level=level))

    def _build_custom_frame_panel(self, parent):
        ttk.Label(parent, text=_("LBL_CAN_ID_HEX")).grid(row=0, column=0, sticky="w", padx=4, pady=(4, 0))
        self.custom_id_var = tk.StringVar(value="100")
        ttk.Entry(parent, textvariable=self.custom_id_var, width=8).grid(row=0, column=1, sticky="w", padx=4)
        ttk.Label(parent, text=_("LBL_DATA_BYTES_HEX")).grid(
            row=1, column=0, columnspan=2, sticky="w", padx=4, pady=(4, 0))
        self.custom_data_var = tk.StringVar(value="")
        ttk.Entry(parent, textvariable=self.custom_data_var, width=30).grid(
            row=2, column=0, columnspan=2, sticky="w", padx=4)

        btn_row = ttk.Frame(parent)
        btn_row.grid(row=3, column=0, columnspan=2, sticky="w", padx=4, pady=4)
        ttk.Button(btn_row, text=_("BTN_SEND_ONCE"), command=self._send_custom_frame_once).pack(side="left")

        self.custom_periodic_var = tk.BooleanVar(value=False)
        self.custom_interval_var = tk.IntVar(value=100)
        ttk.Checkbutton(
            btn_row, text=_("LBL_REPEAT_EVERY"), variable=self.custom_periodic_var,
            command=self._toggle_custom_periodic,
        ).pack(side="left", padx=(12, 4))
        ttk.Spinbox(btn_row, from_=10, to=10000, textvariable=self.custom_interval_var, width=6).pack(side="left")
        ttk.Label(btn_row, text=_("LBL_MS_UNIT")).pack(side="left", padx=(2, 0))

        ttk.Label(
            parent,
            text=_("HELP_SENDS_RAW_FRAME"),
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=4, column=0, columnspan=2, sticky="w", padx=4, pady=(0, 4))

        ttk.Separator(parent, orient="horizontal").grid(row=5, column=0, columnspan=2, sticky="ew", pady=4)
        ttk.Button(parent, text=_("BTN_OPEN_BUS_MONITOR"), command=self._open_bus_monitor).grid(
            row=6, column=0, columnspan=2, sticky="w", padx=4, pady=(0, 4))

    def _parse_custom_frame(self):
        """Returns (can_id, data_bytes) or raises ValueError with a message
        suitable for showing the user directly."""
        try:
            can_id = int(self.custom_id_var.get(), 16)
        except (ValueError, tk.TclError):
            raise ValueError(f"'{self.custom_id_var.get()}' isn't a valid hex CAN ID.")
        if not (0 <= can_id <= 0x7FF):
            raise ValueError(f"CAN ID 0x{can_id:X} is outside the standard 11-bit range (0-0x7FF).")
        text = self.custom_data_var.get().strip()
        try:
            values = [int(b, 16) for b in text.split()] if text else []
        except ValueError:
            raise ValueError(f"'{text}' isn't valid space-separated hex bytes, e.g. 01 02 03.")
        out_of_range = [v for v in values if not (0 <= v <= 0xFF)]
        if out_of_range:
            raise ValueError(f"{out_of_range[0]:#x} is out of range for a single byte (00-FF).")
        data = bytes(values)
        if len(data) > 8:
            raise ValueError(f"{len(data)} bytes given - a CAN frame carries at most 8.")
        return can_id, data

    def _send_custom_frame_once(self):
        if self.bus is None:
            messagebox.showerror(_("TITLE_NOT_CONNECTED"), _("MSG_CONNECT_FIRST"))
            return
        try:
            can_id, data = self._parse_custom_frame()
        except ValueError as e:
            messagebox.showerror(_("TITLE_BAD_INPUT"), str(e))
            return
        self.bus.send(can_id, data)
        data_str = data.hex(' ') if data else _("LBL_EMPTY_PARENS")
        self.log(_("LOG_SENT_CUSTOM_FRAME", can_id=can_id, data=data_str))

    def _toggle_custom_periodic(self):
        if not self.custom_periodic_var.get():
            self._stop_keepalive("custom_frame")
            return
        if self.bus is None:
            messagebox.showerror(_("TITLE_NOT_CONNECTED"), _("MSG_CONNECT_FIRST"))
            self.custom_periodic_var.set(False)
            return
        try:
            can_id, data = self._parse_custom_frame()
        except ValueError as e:
            messagebox.showerror(_("TITLE_BAD_INPUT"), str(e))
            self.custom_periodic_var.set(False)
            return
        interval = max(10, self._safe_int(self.custom_interval_var, 100))
        data_str = data.hex(' ') if data else _("LBL_EMPTY_PARENS")
        self.log(_("LOG_REPEATING_CUSTOM_FRAME", interval=interval, can_id=can_id, data=data_str))

        last_parse_failed = [False]  # single-element list so the closure below can mutate it

        def _send():
            # A transient parse failure (e.g. the user just deleted a
            # character mid-edit, a normal temporary state while typing a
            # new value) shouldn't cancel the whole periodic send the way
            # letting this propagate up to _tick's own exception handling
            # would - that deliberately doesn't reschedule after any
            # failure. Skipping just this one tick and trying again next
            # time lets editing resolve itself naturally.
            try:
                can_id, data = self._parse_custom_frame()  # re-parsed each tick - reflects a live-edited field without needing to stop/restart
            except ValueError as e:
                if not last_parse_failed[0]:
                    # Logged once on the transition into a failing state,
                    # not on every tick - otherwise the user has no
                    # indication at all that nothing is actually being
                    # sent if the field stays invalid past a momentary edit.
                    self.log(_("LOG_CUSTOM_PERIODIC_SKIPPING", e=e))
                    last_parse_failed[0] = True
                return
            if last_parse_failed[0]:
                self.log(_("LOG_CUSTOM_PERIODIC_RESUMING"))
                last_parse_failed[0] = False
            self.bus.send(can_id, data)

        self._start_keepalive(
            "custom_frame", interval, _send,
            on_failure=lambda: self.custom_periodic_var.set(False),
        )

    def _send_expansion_spi(self):
        if self.bus is None:
            messagebox.showerror(_("TITLE_NOT_CONNECTED"), _("MSG_CONNECT_FIRST"))
            return
        try:
            tx_bytes = bytes(int(b, 16) for b in self.spi_send_var.get().split())
        except ValueError:
            messagebox.showerror(_("TITLE_BAD_INPUT"), _("MSG_ENTER_HEX_BYTES_EXAMPLE"))
            return
        if not (1 <= len(tx_bytes) <= 7):
            messagebox.showerror(_("TITLE_BAD_INPUT"), _("MSG_ENTER_1_TO_7_BYTES"))
            return
        data = bytes([len(tx_bytes)]) + tx_bytes
        # wait_for_one's own registration happens inside the call, so
        # sending first here (rather than registering the wait before
        # sending) is fine - the round trip for a handful of bit-banged
        # SPI bytes is comfortably within the 1s timeout either way.
        self.bus.send(CAN_ID_EXP_SPI_CMD, data)
        response = self.bus.wait_for_one(CAN_ID_EXP_SPI_RESP, timeout=1.0)
        if response is None:
            self.spi_response_var.set(_("LBL_SPI_NO_RESPONSE"))
            self.log(_("LOG_SENT_0X180_NO_RESPONSE", tx=tx_bytes.hex(' ')))
            return
        n = response[0] if len(response) > 0 else 0
        rx_bytes = response[1:1 + n]
        if len(rx_bytes) < n:
            self.spi_response_var.set(_("LBL_SPI_MALFORMED", n=n, actual=len(rx_bytes)))
            self.log(_("LOG_SENT_0X180_MALFORMED", tx=tx_bytes.hex(' '), n=n, actual=len(rx_bytes), rx=rx_bytes.hex(' ')))
            return
        self.spi_response_var.set(rx_bytes.hex(" ") if rx_bytes else _("LBL_EMPTY_PARENS"))
        self.log(_("LOG_SENT_0X180_RECEIVED", tx=tx_bytes.hex(' '), rx=rx_bytes.hex(' ')))

    def _query_fram_state(self):
        if self.bus is None:
            messagebox.showerror(_("TITLE_NOT_CONNECTED"), _("MSG_CONNECT_FIRST"))
            return
        response = self.bus.wait_for_one(CAN_ID_FRAM_STATE_RESP, timeout=1.0,
                                          send_after_register=lambda: self.bus.send(CAN_ID_QUERY_FRAM_STATE, b""))
        self._show_fram_state(response)

    def _query_expansion_board_type(self):
        if self.bus is None:
            messagebox.showerror(_("TITLE_NOT_CONNECTED"), _("MSG_CONNECT_FIRST"))
            return
        response = self.bus.wait_for_one(CAN_ID_EXPANSION_TYPE_RESP, timeout=1.0,
                                          send_after_register=lambda: self.bus.send(CAN_ID_EXPANSION_TYPE_RESP, b""))
        if response is None or len(response) < 1 or response[0] > 4:
            self.expansion_board_type_var.set("No response (older firmware, or not connected)")
            self.log(_("LOG_QUERIED_0X1A1_NO_RESPONSE"))
            return
        label = EXPANSION_BOARD_TYPES[response[0]]
        self.expansion_board_type_var.set(label)
        self.log(_("LOG_EXPANSION_BOARD_TYPE", label=label))

    def _query_free_tool_config(self):
        if self.bus is None:
            messagebox.showerror(_("TITLE_NOT_CONNECTED"), _("MSG_CONNECT_FIRST"))
            return
        # Queries 0x1A3 specifically, not 0x1A2 - 0x1A2 with any payload
        # byte is interpreted by the firmware as a set command (see
        # CANBUS.TXT), and this tool never writes this register, only
        # reads it. Empty payload here matches that: nothing to
        # (mis)interpret as a value to set.
        response = self.bus.wait_for_one(CAN_ID_FREE_TOOL_CONFIG_RESP, timeout=1.0,
                                          send_after_register=lambda: self.bus.send(CAN_ID_FREE_TOOL_CONFIG_RESP, b""))
        if response is None or len(response) < 2:
            self.free_tool_config_var.set("No response (older firmware, or not connected)")
            self.log(_("LOG_QUERIED_0X1A3_NO_RESPONSE"))
            return
        raw_id_pin, selection = response[0], response[1]
        if raw_id_pin != 31:
            jumper_desc = _("LBL_FREE_TOOL_JUMPERS_OTHER", value=raw_id_pin)
        else:
            jumper_desc = _("LBL_FREE_TOOL_JUMPERS_0X1F")
        if selection == 0 or selection > 12:
            selection_desc = _("LBL_FREE_TOOL_NONE_SELECTED")
        else:
            selection_desc = TOOL_NAMES[selection - 1]
        text = f"{jumper_desc}\n{_('LBL_FREE_TOOL_SELECTION', tool=selection_desc)}"
        self.free_tool_config_var.set(text)
        self.log(_("LOG_FREE_TOOL_CONFIG", jumpers=raw_id_pin, tool=selection_desc))

    def _query_peripheral_info(self):
        if self.bus is None:
            messagebox.showerror(_("TITLE_NOT_CONNECTED"), _("MSG_CONNECT_FIRST"))
            return
        # Queries 0x1A5 specifically, not 0x1A4 - 0x1A4 with any payload
        # byte is interpreted by the firmware as a set command (see
        # CANBUS.TXT), and this tool never writes this register, only
        # reads it.
        response = self.bus.wait_for_one(CAN_ID_PERIPHERAL_INFO_RESP, timeout=1.0,
                                          send_after_register=lambda: self.bus.send(CAN_ID_PERIPHERAL_INFO_RESP, b""))
        if response is None or len(response) < 2:
            self.peripheral_info_var.set("No response (older firmware, or not connected)")
            self.log(_("LOG_QUERIED_0X1A5_NO_RESPONSE"))
            return
        peripheral_type, serial = response[0], response[1]
        type_desc = _("LBL_PERIPHERAL_TYPE_URTC") if peripheral_type == 0x03 else _(
            "LBL_PERIPHERAL_TYPE_UNKNOWN", value=peripheral_type)
        text = f"{type_desc}\n{_('LBL_DEVICE_SERIAL', serial=serial)}"
        self.peripheral_info_var.set(text)
        self.log(_("LOG_PERIPHERAL_INFO", type=type_desc, serial=serial))

    def _show_fram_state(self, response):
        if response is None or len(response) < 8:
            self.fram_state_var.set("No response - board not running this firmware version, or not connected.")
            self.log(_("LOG_QUERIED_0X190_NO_RESPONSE"))
            return
        valid, tool_id, had_error, temp_hi, temp_lo, speed, dir_or_interlock, fan = response[:8]
        if not valid:
            self.fram_state_var.set("No valid saved state (uninitialized F-RAM, or nothing saved yet).")
            self.log(_("LOG_FRAM_STATE_NOTHING_SAVED"))
            return
        temp = (temp_hi << 8) | temp_lo
        tool_name = TOOL_NAMES.get(tool_id, f"unknown ({tool_id})")
        text = (f"Last saved under: {tool_name}\n"
                f"Temperature setpoint: {temp}°C  |  Speed/power: {speed}  |  "
                f"Direction/interlock: {'on' if dir_or_interlock else 'off'}  |  Fan: {fan}\n"
                f"Critical error active at last save: {'YES' if had_error else 'no'}")
        self.fram_state_var.set(text)
        self.log(_("LOG_FRAM_STATE_FULL", tool=tool_name, temp=temp, speed=speed, dir_or_interlock=dir_or_interlock, fan=fan, had_error=had_error))

    def _erase_fram(self):
        if self.bus is None:
            messagebox.showerror(_("TITLE_NOT_CONNECTED"), _("MSG_CONNECT_FIRST"))
            return
        if not messagebox.askyesno(
            _("TITLE_ERASE_FRAM_Q"),
            _("MSG_ERASE_FRAM_CONFIRM"),
            icon="warning",
        ):
            return
        response = self.bus.wait_for_one(CAN_ID_FRAM_STATE_RESP, timeout=1.0,
                                          send_after_register=lambda: self.bus.send(CAN_ID_ERASE_FRAM, ERASE_FRAM_MAGIC))
        if response is None:
            self.log(_("LOG_FRAM_ERASE_NO_CONFIRM"))
            messagebox.showwarning(_("TITLE_NO_CONFIRMATION"), _("MSG_ERASE_NO_RESPONSE"))
            return
        self.log(_("LOG_FRAM_ERASED_CONFIRMED"))
        self._show_fram_state(response)
        messagebox.showinfo(_("TITLE_ERASED"), _("MSG_FRAM_ERASED_CONFIRMED"))

    # -------------------------------------------------------------------
    # Per-tool panels. Each is only ever built for the ONE tool the board
    # is actually jumpered for right now (see rebuild_tool_panel above) -
    # never more than one live at a time, so there's no risk of e.g. the
    # soldering iron's keepalive still firing while the laser panel is
    # showing.
    # -------------------------------------------------------------------

