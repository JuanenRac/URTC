# =============================================================================
# URTC Tester - PanelHelpersMixin: shared utilities used by every tool
# panel builder (keepalive timers, translated comboboxes, live graphs,
# safe widget-value parsing). No standalone class - mixed into TesterGUI
# in tester_gui_core.py alongside the other panel mixins.
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0 - see LICENSE
# =============================================================================
import tkinter as tk
from tkinter import ttk

from tester_config import _


class PanelHelpersMixin:
    def _start_keepalive(self, name, interval_ms, send_fn, on_failure=None):
        """Registers a periodic resend under `name`, cancelling whatever
        was previously running under that same name first. Matches what a
        real master controller has to do to satisfy the firmware's
        communication watchdogs (soldering iron/laser/3D-printer nozzle:
        250ms, layer fan: 1000ms) - without this, a setpoint sent once
        would get cut by the firmware itself a fraction of a second later,
        which would look like a bug in THIS tool rather than the expected,
        correct safety behavior it actually is.
        on_failure, when given, runs once if send_fn ever raises (a
        disconnected adapter, or an emptied Spinbox the caller's own
        send_fn reads from) - typically used to uncheck whatever "Active"
        checkbox started this, so the UI doesn't keep showing a keepalive
        as running after it's actually stopped rescheduling itself."""
        self._stop_keepalive(name)
        def _tick():
            try:
                send_fn()
            except Exception as e:
                self.log(_("LOG_KEEPALIVE_STOPPED", name=name, e=e))
                self._keepalive_jobs.pop(name, None)
                if on_failure is not None:
                    try:
                        on_failure()
                    except Exception:
                        pass
                return  # deliberately doesn't reschedule - see on_failure above for reflecting this in the UI
            self._keepalive_jobs[name] = self.root.after(interval_ms, _tick)
        _tick()

    def _stop_keepalive(self, name):
        job_id = self._keepalive_jobs.pop(name, None)
        if job_id is not None:
            try:
                self.root.after_cancel(job_id)
            except Exception:
                pass

    def _opt_display(self, value, key_prefix="OPT"):
        """Translates an internal English combobox value (as read from one
        of _translated_combobox's returned StringVars) back into its
        displayed form, for log messages - otherwise a log line would show
        the raw English internal value (e.g. "Forward") stuck in the
        middle of an otherwise fully Spanish message."""
        return _(f"{key_prefix}_{value.upper().replace(' ', '_').replace('-', '_')}")

    def _translated_combobox(self, parent, internal_values, key_prefix, initial=None, **combo_kwargs):
        """Builds a readonly Combobox whose options are shown translated,
        while the returned StringVar's .get() always yields the
        untranslated English value from internal_values - critical for
        every one of these where the selected value is compared directly
        against a fixed English string elsewhere (e.g. a dict keyed by
        "Standard"/"Night"/"Standby" building a CAN payload byte).
        Translating only the display and not that comparison would
        silently break the underlying command the moment the language
        changed.

        key_prefix + "_" + value.upper().replace(" ", "_").replace("-", "_")
        is the translation key looked up for each value (e.g. "Standard"
        under key_prefix "OPT" -> "OPT_STANDARD") - this project's
        existing values are all short, distinct English words/phrases
        that map cleanly onto that pattern rather than needing a
        hand-written dict of exceptions.
        """
        def _key_for(value):
            return f"{key_prefix}_{value.upper().replace(' ', '_').replace('-', '_')}"

        internal_var = tk.StringVar(value=initial or internal_values[0])
        display_values = [_(_key_for(v)) for v in internal_values]
        reverse_map = {_(_key_for(v)): v for v in internal_values}
        display_var = tk.StringVar(value=_(_key_for(internal_var.get())))
        combo = ttk.Combobox(
            parent, textvariable=display_var, values=display_values,
            state="readonly", **combo_kwargs,
        )

        def _on_select(event=None):
            internal_var.set(reverse_map.get(display_var.get(), internal_values[0]))

        combo.bind("<<ComboboxSelected>>", _on_select)
        return internal_var, combo

    def _safe_int(self, var, default=0):
        """Reads a Tkinter IntVar (typically Spinbox-backed), falling back
        to default if the field is currently empty or holds non-numeric
        text - .get() on an IntVar raises tk.TclError in that case (e.g. a
        user selecting the Spinbox's text and deleting it, or typing a
        stray letter, then clicking Send/Move before re-entering a number),
        which would otherwise propagate straight out of a one-shot button
        callback with no useful feedback."""
        try:
            return var.get()
        except tk.TclError:
            return default

    def _create_live_graph(self, parent, y_max, width=340, height=100, max_points=60):
        """Builds a simple live-scrolling line graph on a plain Canvas -
        deliberately not matplotlib/pyqtgraph, to keep this project at
        zero non-stdlib dependencies beyond pyserial. Y axis is fixed at
        0..y_max (the tool's own known setpoint ceiling, e.g. 450 for the
        soldering iron) rather than auto-scaling to whatever's been seen -
        a fixed, predictable scale is easier to read at a glance than one
        that keeps shifting. Returns add_point(value); each call appends
        one value and redraws, dropping the oldest once max_points is
        reached (a rolling window, not an ever-growing history - this is
        for watching a live trend, not logging one)."""
        canvas = tk.Canvas(parent, width=width, height=height, bg="#1a1a1a", highlightthickness=1,
                           highlightbackground="#444")
        canvas.pack(fill="x", padx=4, pady=(2, 6))
        values = []

        # Gridlines created once, not recreated on every point - only the
        # data line itself actually changes from one add_point() call to
        # the next.
        for frac in (0.0, 0.5, 1.0):
            y = height - frac * height
            canvas.create_line(0, y, width, y, fill="#333")
        line_id = canvas.create_line(0, 0, 0, 0, fill="#00ADB5", width=2, smooth=False)
        canvas.itemconfigure(line_id, state="hidden")  # nothing to show until there are >=2 points

        def add_point(value):
            values.append(max(0, min(y_max, value)))
            if len(values) > max_points:
                values.pop(0)
            if not canvas.winfo_exists():
                return
            if len(values) < 2:
                return
            step_x = width / max(1, max_points - 1)
            points = []
            for i, v in enumerate(values):
                x = i * step_x
                y = height - (v / y_max) * height
                points.extend([x, y])
            canvas.coords(line_id, *points)
            canvas.itemconfigure(line_id, state="normal")

        return add_point

