# =============================================================================
# URTC Tester - CANBusMonitor: the one background thread reading frames
# off the transport, dispatching to registered handlers/sniffers
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0 - see LICENSE
# =============================================================================
import threading
import time

from tester_config import _

class CANBusMonitor:
    """Owns the one and only background thread reading frames off the
    transport, dispatching each received frame to whichever callbacks are
    currently registered for its CAN ID. This exists because a live
    tester - unlike the flasher's one-request-at-a-time protocol - needs
    to watch several different telemetry IDs at once (temperature, RPM,
    endstops...) without them stepping on each other, and a serial port or
    raw socket can't safely be read from two places at once. Callbacks run
    on this background thread - they must only touch Tkinter state via
    root.after(), never touch widgets directly.
    """
    def __init__(self, transport, log):
        self.transport = transport
        self.log = log
        self._handlers = {}  # can_id -> list of callback(data) functions
        self._sniffers = []  # callback(can_id, data) functions - called for EVERY frame, regardless of ID
        self._running = False
        self._thread = None
        self._lock = threading.Lock()

    def register(self, can_id, callback):
        with self._lock:
            self._handlers.setdefault(can_id, []).append(callback)

    def unregister(self, can_id, callback):
        with self._lock:
            if can_id in self._handlers and callback in self._handlers[can_id]:
                self._handlers[can_id].remove(callback)
                if not self._handlers[can_id]:
                    del self._handlers[can_id]

    def register_sniffer(self, callback):
        """Registers a callback(can_id, data) that fires for every single
        frame this monitor sees, regardless of ID - for the raw bus
        monitor panel. Separate from the per-ID handlers above; neither
        mechanism affects the other."""
        with self._lock:
            self._sniffers.append(callback)

    def unregister_sniffer(self, callback):
        with self._lock:
            if callback in self._sniffers:
                self._sniffers.remove(callback)

    def clear_all(self):
        with self._lock:
            self._handlers = {}

    def start(self):
        self._running = True
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def stop(self):
        self._running = False
        if self._thread is not None:
            self._thread.join(timeout=1.0)

    def _loop(self):
        while self._running:
            try:
                frame = self.transport.read_frame(timeout=0.1)
            except Exception as e:
                self.log(_("LOG_BUS_READ_ERROR", e=e))
                time.sleep(0.2)
                continue
            if frame is None:
                continue
            can_id, data = frame
            with self._lock:
                callbacks = list(self._handlers.get(can_id, []))
                sniffers = list(self._sniffers)
            for callback in callbacks:
                try:
                    callback(data)
                except Exception as e:
                    self.log(_("LOG_HANDLER_ERROR", can_id=can_id, e=e))
            for sniffer in sniffers:
                try:
                    sniffer(can_id, data)
                except Exception as e:
                    self.log(_("LOG_SNIFFER_ERROR", e=e))

    def send(self, can_id, data):
        self.transport.send_frame(can_id, data)

    def wait_for_one(self, can_id, timeout=1.5, send_after_register=None):
        """One-off synchronous wait for a single frame on can_id, layered
        on top of the same registration mechanism everything else uses -
        never calls read_frame() directly, so this never races against
        the background thread's own reading.

        send_after_register, when given, is called with no arguments
        right after the callback is registered but before waiting - this
        closes the window where a fast enough response could otherwise
        arrive and get processed by the background thread before
        anything was listening for it, which would cause a false timeout
        on an otherwise-successful exchange. A caller that sends its
        request frame itself before calling this method instead of using
        send_after_register is still safe, just without that race closed
        for
        them specifically) since this parameter is optional."""
        result = {"data": None}
        event = threading.Event()

        def _capture(data):
            result["data"] = data
            self.unregister(can_id, _capture)  # right here, not just in the
            # outer finally below - closes the narrow window where a second
            # frame on the same can_id could arrive and get dispatched
            # before the main thread wakes from event.wait(), overwriting
            # result with the wrong frame's data. Safe from inside this
            # callback since _loop dispatches over a copied handler list,
            # not the live one this mutates.
            event.set()

        self.register(can_id, _capture)
        try:
            if send_after_register is not None:
                send_after_register()
            event.wait(timeout=timeout)
        finally:
            self.unregister(can_id, _capture)
        return result["data"]



