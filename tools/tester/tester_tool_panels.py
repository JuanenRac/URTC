# =============================================================================
# URTC Tester - ToolPanelsMixin: the 8 tool-specific panel builders
# (soldering iron, shared motion tools, vacuum, drill, AOI, laser, scan
# probe, 3D printer). No standalone class - mixed into TesterGUI in
# tester_gui_core.py alongside the other panel mixins. Relies on
# PanelHelpersMixin's methods (_translated_combobox, _safe_int,
# _start_keepalive, _create_live_graph) being present on the same
# instance via multiple inheritance, not imported directly here.
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0 - see LICENSE
# =============================================================================
import struct
import time
import tkinter as tk
from tkinter import ttk

from tester_config import (
    _, CAN_ID_3DP_HOTEND_FAN_CMD, CAN_ID_3DP_HOTEND_FAN_RPM, CAN_ID_3DP_HOTEND_TELEM,
    CAN_ID_3DP_LAYER_FAN_CMD, CAN_ID_3DP_LAYER_FAN_RPM, CAN_ID_3DP_THERMAL_MOTION,
    CAN_ID_AOI_CMD, CAN_ID_AOI_TELEMETRY, CAN_ID_DRILL_CMD, CAN_ID_DRILL_TELEMETRY,
    CAN_ID_IMPACT_EVENT, CAN_ID_LASER_CMD, CAN_ID_LASER_TELEMETRY, CAN_ID_MOTION_CMD,
    CAN_ID_SOLDER_SETPOINT, CAN_ID_SOLDER_TELEMETRY, CAN_ID_VACUUM_TELEMETRY,
)


class ToolPanelsMixin:

    def _build_soldering_iron_panel(self, parent):
        setpoint = tk.IntVar(value=0)
        active = tk.BooleanVar(value=False)
        temp_var = tk.StringVar(value="-- °C")
        endstop_var = tk.StringVar(value="--")

        ttk.Label(parent, text=_("LBL_SETPOINT_TEMPERATURE")).grid(row=0, column=0, sticky="w", padx=4, pady=4)
        ttk.Spinbox(parent, from_=0, to=450, textvariable=setpoint, width=6).grid(row=0, column=1, sticky="w", padx=4)

        def _send_setpoint():
            value = max(0, min(450, self._safe_int(setpoint, 0)))
            self.bus.send(CAN_ID_SOLDER_SETPOINT, struct.pack(">H", value))

        def _toggle(*_):
            if active.get():
                self.log(_("LOG_SOLDER_SETPOINT_ACTIVE", temp=self._safe_int(setpoint, 0)))
                self._start_keepalive("solder", 150, _send_setpoint, on_failure=lambda: active.set(False))
            else:
                self._stop_keepalive("solder")
                self.bus.send(CAN_ID_SOLDER_SETPOINT, struct.pack(">H", 0))
                self.log(_("LOG_SOLDER_TURNED_OFF"))

        ttk.Checkbutton(parent, text=_("LBL_ACTIVE_250MS_WATCHDOG"),
                         variable=active, command=_toggle).grid(row=0, column=2, sticky="w", padx=8)

        ttk.Separator(parent, orient="horizontal").grid(row=1, column=0, columnspan=3, sticky="ew", pady=6)
        ttk.Label(parent, text=_("LBL_LIVE_TEMPERATURE")).grid(row=2, column=0, sticky="w", padx=4, pady=2)
        ttk.Label(parent, textvariable=temp_var, font=("", 11, "bold")).grid(row=2, column=1, sticky="w")
        ttk.Label(parent, text=_("LBL_ENDSTOP_LIMIT_SWITCH")).grid(row=3, column=0, sticky="w", padx=4, pady=2)
        ttk.Label(parent, textvariable=endstop_var).grid(row=3, column=1, sticky="w")
        graph_frame = ttk.Frame(parent)
        graph_frame.grid(row=4, column=0, columnspan=3, sticky="w", padx=4)
        add_temp_point = self._create_live_graph(graph_frame, y_max=450)

        def _on_telemetry(data):
            if len(data) < 3:
                return
            temp = struct.unpack(">H", data[0:2])[0]
            endstop = data[2]
            self.root.after(0, lambda: temp_var.set(f"{temp} °C"))
            self.root.after(0, lambda: endstop_var.set("TRIGGERED" if endstop else "open"))
            self.root.after(0, lambda: add_temp_point(temp))

        self.bus.register(CAN_ID_SOLDER_TELEMETRY, _on_telemetry)

    def _build_motion_panel(self, parent, tool_id, tool_name):
        # 5 tools (paste/liquid dispenser, screwdriver, both grippers)
        # share the exact same command (0x120) and have no telemetry of
        # their own - a plain stepper: direction + step count, one-shot,
        # no watchdog to satisfy.
        steps = tk.IntVar(value=200)

        ttk.Label(parent, text=_("TITLE_PLAIN_STEPPER_MOTION", tool=tool_name),
                  font=("", 10, "bold")).grid(row=0, column=0, columnspan=3, sticky="w", padx=4, pady=(4, 8))
        ttk.Label(parent, text=_("LBL_DIRECTION")).grid(row=1, column=0, sticky="w", padx=4, pady=4)
        direction, direction_combo = self._translated_combobox(parent, ["Forward", "Reverse"], "OPT", width=12)
        direction_combo.grid(row=1, column=1, sticky="w", padx=4)
        ttk.Label(parent, text=_("LBL_STEPS")).grid(row=2, column=0, sticky="w", padx=4, pady=4)
        ttk.Spinbox(parent, from_=1, to=4294967295, textvariable=steps, width=12).grid(row=2, column=1, sticky="w", padx=4)

        def _send_move():
            dir_byte = 0x01 if direction.get() == "Forward" else 0x00
            # Spinbox's own to=4294967295 only constrains the arrow
            # buttons, not text typed directly into the field (confirmed
            # empirically) - clamping here is what actually keeps this
            # inside struct.pack(">I", ...)'s valid range.
            n = min(max(1, self._safe_int(steps, 1)), 0xFFFFFFFF)
            data = bytes([dir_byte]) + struct.pack(">I", n)
            self.bus.send(CAN_ID_MOTION_CMD, data)
            self.log(_("LOG_TOOL_MOVE", tool=tool_name, direction=self._opt_display(direction.get()), n=n))

        ttk.Button(parent, text=_("BTN_MOVE"), command=_send_move).grid(row=2, column=2, padx=8)
        ttk.Label(
            parent,
            text=_("HELP_ONE_SHOT_NO_TELEMETRY"),
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=3, column=0, columnspan=3, sticky="w", padx=4, pady=(8, 0))

    def _build_vacuum_panel(self, parent):
        # Telemetry only - no commands for this tool at all.
        adc_var = tk.StringVar(value="--")
        detect_var = tk.StringVar(value="--")

        ttk.Label(parent, text=_("TITLE_VACUUM_TELEMETRY"),
                  font=("", 10, "bold")).grid(row=0, column=0, columnspan=2, sticky="w", padx=4, pady=(4, 8))
        ttk.Label(parent, text=_("LBL_ANALOG_READING")).grid(row=1, column=0, sticky="w", padx=4, pady=4)
        ttk.Label(parent, textvariable=adc_var, font=("", 11, "bold")).grid(row=1, column=1, sticky="w")
        ttk.Label(parent, text=_("LBL_PART_DETECTED_LM393")).grid(row=2, column=0, sticky="w", padx=4, pady=4)
        ttk.Label(parent, textvariable=detect_var, font=("", 11, "bold")).grid(row=2, column=1, sticky="w")

        def _on_telemetry(data):
            if len(data) < 3:
                return
            adc = struct.unpack(">H", data[0:2])[0]
            detected = data[2]
            self.root.after(0, lambda: adc_var.set(str(adc)))
            self.root.after(0, lambda: detect_var.set("YES - part picked up" if detected else "no"))

        self.bus.register(CAN_ID_VACUUM_TELEMETRY, _on_telemetry)

    def _build_drill_panel(self, parent):
        speed = tk.IntVar(value=0)
        rpm_var = tk.StringVar(value="--")
        endstop_var = tk.StringVar(value="--")

        ttk.Label(parent, text=_("LBL_SPEED_0_255")).grid(row=0, column=0, sticky="w", padx=4, pady=4)
        speed_scale = ttk.Scale(parent, from_=0, to=255, variable=speed, orient="horizontal", length=160)
        speed_scale.grid(row=0, column=1, sticky="w", padx=4)
        speed_label = ttk.Label(parent, text="0")
        speed_label.grid(row=0, column=2, sticky="w")
        speed_scale.config(command=lambda v: speed_label.config(text=str(int(float(v)))))

        ttk.Label(parent, text=_("LBL_DIRECTION")).grid(row=1, column=0, sticky="w", padx=4, pady=4)
        direction, direction_combo = self._translated_combobox(parent, ["Clockwise", "Counter-clockwise"], "OPT", width=14)
        direction_combo.grid(row=1, column=1, sticky="w", padx=4)

        def _send_drill():
            dir_byte = 0x01 if direction.get() == "Counter-clockwise" else 0x00
            self.bus.send(CAN_ID_DRILL_CMD, bytes([max(0, min(255, self._safe_int(speed, 0))), dir_byte]))

        ttk.Button(parent, text=_("BTN_SEND"), command=_send_drill).grid(row=1, column=2, padx=8)
        ttk.Label(
            parent, text=_("HELP_NO_WATCHDOG_HOLDS_VALUE"),
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=2, column=0, columnspan=3, sticky="w", padx=4, pady=(4, 8))

        ttk.Separator(parent, orient="horizontal").grid(row=3, column=0, columnspan=3, sticky="ew", pady=4)
        ttk.Label(parent, text=_("LBL_ACTUAL_RPM")).grid(row=4, column=0, sticky="w", padx=4, pady=2)
        ttk.Label(parent, textvariable=rpm_var, font=("", 11, "bold")).grid(row=4, column=1, sticky="w")
        ttk.Label(parent, text=_("LBL_ENDSTOP_LIMIT_SWITCH")).grid(row=5, column=0, sticky="w", padx=4, pady=2)
        ttk.Label(parent, textvariable=endstop_var).grid(row=5, column=1, sticky="w")

        def _on_telemetry(data):
            if len(data) < 3:
                return
            rpm = struct.unpack(">H", data[0:2])[0]
            endstop = data[2]
            self.root.after(0, lambda: rpm_var.set(f"{rpm} RPM"))
            self.root.after(0, lambda: endstop_var.set("TRIGGERED" if endstop else "open"))

        self.bus.register(CAN_ID_DRILL_TELEMETRY, _on_telemetry)

    def _build_aoi_panel(self, parent):
        period_us = tk.IntVar(value=1000)
        endstop_var = tk.StringVar(value="--")

        ttk.Label(parent, text=_("LBL_RING_MODE")).grid(row=0, column=0, sticky="w", padx=4, pady=4)
        mode, mode_combo = self._translated_combobox(
            parent, ["Off", "Synchronous strobe", "Fixed continuous"], "OPT", width=16,
        )
        mode_combo.grid(row=0, column=1, sticky="w", padx=4)
        ttk.Label(parent, text=_("LBL_STROBE_PERIOD")).grid(row=1, column=0, sticky="w", padx=4, pady=4)
        ttk.Spinbox(parent, from_=1, to=65535, textvariable=period_us, width=8).grid(row=1, column=1, sticky="w", padx=4)

        def _send_aoi():
            mode_byte = {"Off": 0x00, "Synchronous strobe": 0x01, "Fixed continuous": 0x02}[mode.get()]
            data = bytes([mode_byte]) + struct.pack(">H", max(1, min(65535, self._safe_int(period_us, 1))))
            self.bus.send(CAN_ID_AOI_CMD, data)
            self.log(_("LOG_AOI_MODE_PERIOD", mode=self._opt_display(mode.get()), period=self._safe_int(period_us, 1)))

        ttk.Button(parent, text=_("BTN_SEND"), command=_send_aoi).grid(row=1, column=2, padx=8)
        ttk.Label(
            parent, text=_("HELP_RING_COLOR_FROM_GLOBAL"),
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=2, column=0, columnspan=3, sticky="w", padx=4, pady=(4, 8))

        ttk.Separator(parent, orient="horizontal").grid(row=3, column=0, columnspan=3, sticky="ew", pady=4)
        ttk.Label(parent, text=_("LBL_ENDSTOP_LIMIT_SWITCH")).grid(row=4, column=0, sticky="w", padx=4, pady=2)
        ttk.Label(parent, textvariable=endstop_var).grid(row=4, column=1, sticky="w")

        def _on_telemetry(data):
            if len(data) < 1:
                return
            endstop = data[0]
            self.root.after(0, lambda: endstop_var.set("TRIGGERED" if endstop else "open"))

        self.bus.register(CAN_ID_AOI_TELEMETRY, _on_telemetry)

    def _build_laser_panel(self, parent):
        power = tk.IntVar(value=0)
        interlock = tk.BooleanVar(value=False)
        active = tk.BooleanVar(value=False)
        endstop_var = tk.StringVar(value="--")

        ttk.Label(
            parent, text=_("HELP_INTERLOCK_MUST_BE_ARMED"),
            foreground="#b35900", wraplength=380, justify="left",
        ).grid(row=0, column=0, columnspan=3, sticky="w", padx=4, pady=(4, 8))

        ttk.Label(parent, text=_("LBL_POWER_0_255")).grid(row=1, column=0, sticky="w", padx=4, pady=4)
        power_scale = ttk.Scale(parent, from_=0, to=255, variable=power, orient="horizontal", length=160)
        power_scale.grid(row=1, column=1, sticky="w", padx=4)
        power_label = ttk.Label(parent, text="0")
        power_label.grid(row=1, column=2, sticky="w")
        power_scale.config(command=lambda v: power_label.config(text=str(int(float(v)))))

        ttk.Checkbutton(parent, text=_("LBL_INTERLOCK_ARMED"),
                         variable=interlock).grid(row=2, column=0, columnspan=3, sticky="w", padx=4, pady=4)

        def _send_laser():
            self.bus.send(CAN_ID_LASER_CMD, bytes([
                max(0, min(255, power.get())),
                0x01 if interlock.get() else 0x00,
            ]))

        def _toggle(*_):
            if active.get():
                interlock_state = _("LBL_ARMED") if interlock.get() else _("LBL_SAFE")
                self.log(_("LOG_LASER_POWER_INTERLOCK", power=power.get(), interlock=interlock_state))
                self._start_keepalive("laser", 150, _send_laser, on_failure=lambda: active.set(False))
            else:
                self._stop_keepalive("laser")
                self.bus.send(CAN_ID_LASER_CMD, bytes([0x00, 0x00]))
                self.log(_("LOG_LASER_STOPPED"))

        ttk.Checkbutton(parent, text=_("LBL_ACTIVE_250MS_WATCHDOG"),
                         variable=active, command=_toggle).grid(row=3, column=0, columnspan=3, sticky="w", padx=4)

        ttk.Separator(parent, orient="horizontal").grid(row=4, column=0, columnspan=3, sticky="ew", pady=6)
        ttk.Label(parent, text=_("LBL_ENDSTOP_LIMIT_SWITCH")).grid(row=5, column=0, sticky="w", padx=4, pady=2)
        ttk.Label(parent, textvariable=endstop_var).grid(row=5, column=1, sticky="w")

        def _on_telemetry(data):
            if len(data) < 1:
                return
            endstop = data[0]
            self.root.after(0, lambda: endstop_var.set("TRIGGERED" if endstop else "open"))

        self.bus.register(CAN_ID_LASER_TELEMETRY, _on_telemetry)

    def _build_scan_probe_panel(self, parent):
        # No commands at all - this tool only monitors PB3 and fires an
        # instant, max-priority event on contact.
        count_var = tk.StringVar(value="0")
        last_var = tk.StringVar(value="(none yet)")
        self._probe_impact_count = 0

        ttk.Label(parent, text=_("TITLE_SCAN_PROBE_TELEMETRY"),
                  font=("", 10, "bold")).grid(row=0, column=0, columnspan=2, sticky="w", padx=4, pady=(4, 8))
        ttk.Label(parent, text=_("LBL_IMPACTS_DETECTED_SESSION")).grid(row=1, column=0, sticky="w", padx=4, pady=4)
        ttk.Label(parent, textvariable=count_var, font=("", 11, "bold")).grid(row=1, column=1, sticky="w")
        ttk.Label(parent, text=_("LBL_LAST_IMPACT")).grid(row=2, column=0, sticky="w", padx=4, pady=4)
        ttk.Label(parent, textvariable=last_var).grid(row=2, column=1, sticky="w")
        ttk.Label(
            parent,
            text=_("HELP_SCAN_PROBE_INTERRUPT"),
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=3, column=0, columnspan=2, sticky="w", padx=4, pady=(8, 0))

        def _on_impact(data):
            if len(data) >= 1 and data[0] == 0x01:
                self._probe_impact_count += 1
                ts = time.strftime("%H:%M:%S")
                self.root.after(0, lambda: count_var.set(str(self._probe_impact_count)))
                self.root.after(0, lambda: last_var.set(ts))
                self.log(_("LOG_SCAN_PROBE_IMPACT", ts=ts))

        self.bus.register(CAN_ID_IMPACT_EVENT, _on_impact)

    def _build_3dprinter_panel(self, parent):
        nozzle_setpoint = tk.IntVar(value=0)
        nozzle_active = tk.BooleanVar(value=False)
        extruder_steps = tk.IntVar(value=200)
        layer_fan_power = tk.IntVar(value=0)
        layer_fan_active = tk.BooleanVar(value=False)
        hotend_fan_power = tk.IntVar(value=0)
        hotend_temp_var = tk.StringVar(value="-- °C")
        layer_rpm_var = tk.StringVar(value="--")
        hotend_rpm_var = tk.StringVar(value="--")

        # 2 columns rather than one long stack - left column is the
        # extruder/hotend heater controls, right column is both fans plus
        # telemetry/graph. Roughly balances the 2 columns' height rather
        # than splitting strictly by section count.
        parent.grid_columnconfigure(0, weight=1)
        parent.grid_columnconfigure(1, weight=1)
        left = ttk.Frame(parent)
        left.grid(row=0, column=0, sticky="new")
        right = ttk.Frame(parent)
        right.grid(row=0, column=1, sticky="new")

        ttk.Label(left, text=_("LBL_NOZZLE_SETPOINT")).grid(row=0, column=0, sticky="w", padx=4, pady=4)
        ttk.Spinbox(left, from_=0, to=300, textvariable=nozzle_setpoint, width=6).grid(row=0, column=1, sticky="w", padx=4)

        def _send_thermal_motion():
            temp = max(0, min(300, self._safe_int(nozzle_setpoint, 0)))
            dir_byte = 0x01 if extruder_dir.get() == "Forward" else 0x00
            steps = max(0, self._safe_int(extruder_steps, 0)) & 0xFFFFFF
            data = struct.pack(">H", temp) + bytes([dir_byte]) + steps.to_bytes(3, "big")
            self.bus.send(CAN_ID_3DP_THERMAL_MOTION, data)

        def _toggle_nozzle(*_):
            if nozzle_active.get():
                self.log(_("LOG_3DP_NOZZLE_SETPOINT_ACTIVE", temp=self._safe_int(nozzle_setpoint, 0)))
                self._start_keepalive("nozzle", 150, _send_thermal_motion, on_failure=lambda: nozzle_active.set(False))
            else:
                self._stop_keepalive("nozzle")
                extruder_steps_saved = self._safe_int(extruder_steps, 0)
                extruder_steps.set(0)
                _send_thermal_motion()
                extruder_steps.set(extruder_steps_saved)
                self.log(_("LOG_3DP_NOZZLE_TURNED_OFF"))

        ttk.Checkbutton(left, text=_("LBL_HEATER_ACTIVE_250MS"), variable=nozzle_active,
                         command=_toggle_nozzle).grid(row=0, column=2, sticky="w", padx=8)

        ttk.Label(left, text=_("LBL_EXTRUDER_DIRECTION")).grid(row=1, column=0, sticky="w", padx=4, pady=4)
        extruder_dir, extruder_dir_combo = self._translated_combobox(left, ["Forward", "Retract"], "OPT", width=10)
        extruder_dir_combo.grid(row=1, column=1, sticky="w", padx=4)
        ttk.Label(left, text=_("LBL_EXTRUDER_STEPS")).grid(row=2, column=0, sticky="w", padx=4, pady=4)
        ttk.Spinbox(left, from_=0, to=16777215, textvariable=extruder_steps, width=10).grid(row=2, column=1, sticky="w", padx=4)
        ttk.Button(left, text=_("BTN_MOVE_EXTRUDER_ONCE"), command=_send_thermal_motion).grid(row=2, column=2, padx=8)
        ttk.Label(
            left, text=_("HELP_EXTRUDER_MOTION_SHARED_FRAME"),
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=3, column=0, columnspan=3, sticky="w", padx=4, pady=(0, 8))

        ttk.Label(right, text=_("LBL_LAYER_FAN_POWER")).grid(row=0, column=0, sticky="w", padx=4, pady=4)
        layer_scale = ttk.Scale(right, from_=0, to=255, variable=layer_fan_power, orient="horizontal", length=140)
        layer_scale.grid(row=0, column=1, sticky="w", padx=4)
        layer_label = ttk.Label(right, text="0")
        layer_label.grid(row=0, column=2, sticky="w")
        layer_scale.config(command=lambda v: layer_label.config(text=str(int(float(v)))))

        def _send_layer_fan():
            self.bus.send(CAN_ID_3DP_LAYER_FAN_CMD, bytes([max(0, min(255, layer_fan_power.get()))]))

        def _toggle_layer_fan(*_):
            if layer_fan_active.get():
                self.log(_("LOG_LAYER_FAN_ACTIVE", power=layer_fan_power.get()))
                self._start_keepalive("layer_fan", 400, _send_layer_fan, on_failure=lambda: layer_fan_active.set(False))
            else:
                self._stop_keepalive("layer_fan")
                self.bus.send(CAN_ID_3DP_LAYER_FAN_CMD, bytes([0x00]))
                self.log(_("LOG_LAYER_FAN_STOPPED"))

        ttk.Checkbutton(right, text=_("LBL_ACTIVE_1000MS_WATCHDOG"), variable=layer_fan_active,
                         command=_toggle_layer_fan).grid(row=1, column=0, columnspan=2, sticky="w", padx=4)

        ttk.Label(right, text=_("LBL_HOTEND_FAN_POWER")).grid(row=2, column=0, sticky="w", padx=4, pady=4)
        hotend_fan_scale = ttk.Scale(right, from_=0, to=255, variable=hotend_fan_power, orient="horizontal", length=140)
        hotend_fan_scale.grid(row=2, column=1, sticky="w", padx=4)
        hotend_fan_label = ttk.Label(right, text="0")
        hotend_fan_label.grid(row=2, column=2, sticky="w")
        hotend_fan_scale.config(command=lambda v: hotend_fan_label.config(text=str(int(float(v)))))

        def _send_hotend_fan():
            self.bus.send(CAN_ID_3DP_HOTEND_FAN_CMD, bytes([max(0, min(255, hotend_fan_power.get()))]))

        ttk.Button(right, text=_("BTN_SEND"), command=_send_hotend_fan).grid(row=2, column=3, padx=8)
        ttk.Label(
            right, text=_("HELP_NO_WATCHDOG_STALL_DETECTOR"),
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=3, column=0, columnspan=4, sticky="w", padx=4, pady=(0, 8))

        ttk.Separator(right, orient="horizontal").grid(row=4, column=0, columnspan=4, sticky="ew", pady=4)
        ttk.Label(right, text=_("LBL_HOTEND_TEMPERATURE")).grid(row=5, column=0, sticky="w", padx=4, pady=2)
        ttk.Label(right, textvariable=hotend_temp_var, font=("", 11, "bold")).grid(row=5, column=1, sticky="w")
        ttk.Label(right, text=_("LBL_LAYER_FAN_RPM")).grid(row=6, column=0, sticky="w", padx=4, pady=2)
        ttk.Label(right, textvariable=layer_rpm_var).grid(row=6, column=1, sticky="w")
        ttk.Label(right, text=_("LBL_HOTEND_FAN_RPM")).grid(row=7, column=0, sticky="w", padx=4, pady=2)
        ttk.Label(right, textvariable=hotend_rpm_var).grid(row=7, column=1, sticky="w")
        graph_frame = ttk.Frame(right)
        graph_frame.grid(row=8, column=0, columnspan=4, sticky="w", padx=4)
        add_nozzle_temp_point = self._create_live_graph(graph_frame, y_max=300)

        def _on_hotend_temp(data):
            if len(data) < 2:
                return
            temp = struct.unpack(">H", data[0:2])[0]
            self.root.after(0, lambda: hotend_temp_var.set(f"{temp} °C"))
            self.root.after(0, lambda: add_nozzle_temp_point(temp))

        def _on_layer_rpm(data):
            if len(data) < 2:
                return
            rpm = struct.unpack(">H", data[0:2])[0]
            self.root.after(0, lambda: layer_rpm_var.set(f"{rpm} RPM"))

        def _on_hotend_rpm(data):
            if len(data) < 2:
                return
            rpm = struct.unpack(">H", data[0:2])[0]
            self.root.after(0, lambda: hotend_rpm_var.set(f"{rpm} RPM"))

        self.bus.register(CAN_ID_3DP_HOTEND_TELEM, _on_hotend_temp)
        self.bus.register(CAN_ID_3DP_LAYER_FAN_RPM, _on_layer_rpm)
        self.bus.register(CAN_ID_3DP_HOTEND_FAN_RPM, _on_hotend_rpm)

