#!/usr/bin/env python3
"""
win-gamepad-logger.py

Polls the XInput virtual controller (ViGEm) on Windows and sends button
press/release events with precise timestamps to the Moonlight log server.

Compare server-receive timestamps with TV-side padDebug entries to measure
end-to-end input latency through the Moonlight -> Sunshine -> ViGEm pipeline.

Usage:
    python win-gamepad-logger.py --server http://192.168.1.100:8765/log
    python win-gamepad-logger.py --server http://192.168.1.100:8765/log --controller 0
"""

import ctypes
import time
import urllib.request
import threading
import argparse
import queue
import sys
from datetime import datetime, timezone

POLL_INTERVAL_S = 0.001  # 1ms = 1000Hz polling

BUTTON_NAMES = {
    0x0001: "DPAD_UP",
    0x0002: "DPAD_DOWN",
    0x0004: "DPAD_LEFT",
    0x0008: "DPAD_RIGHT",
    0x0010: "START",
    0x0020: "BACK",
    0x0040: "LS_CLICK",
    0x0080: "RS_CLICK",
    0x0100: "LB",
    0x0200: "RB",
    0x0400: "GUIDE",
    0x1000: "A",
    0x2000: "B",
    0x4000: "X",
    0x8000: "Y",
}

TRIGGER_THRESHOLD = 10  # report trigger changes above this delta


class XINPUT_GAMEPAD(ctypes.Structure):
    _fields_ = [
        ("wButtons",      ctypes.c_ushort),
        ("bLeftTrigger",  ctypes.c_ubyte),
        ("bRightTrigger", ctypes.c_ubyte),
        ("sThumbLX",      ctypes.c_short),
        ("sThumbLY",      ctypes.c_short),
        ("sThumbRX",      ctypes.c_short),
        ("sThumbRY",      ctypes.c_short),
    ]


class XINPUT_STATE(ctypes.Structure):
    _fields_ = [
        ("dwPacketNumber", ctypes.c_ulong),
        ("Gamepad",        XINPUT_GAMEPAD),
    ]


def load_xinput():
    for lib in ("xinput1_4", "xinput1_3", "xinput9_1_0"):
        try:
            return ctypes.windll.LoadLibrary(lib)
        except OSError:
            continue
    sys.exit("[ERROR] No XInput DLL found. Is this Windows?")


def sender_thread(log_queue, server_url):
    """Background thread: drains the queue and POSTs to the log server."""
    seq = [0]
    while True:
        try:
            msg = log_queue.get(timeout=5)
        except queue.Empty:
            continue
        if msg is None:
            break
        seq[0] += 1
        body = f"source=WIN seq={seq[0]} {msg}"
        try:
            req = urllib.request.Request(
                server_url,
                data=body.encode("utf-8"),
                method="POST",
            )
            req.add_header("Content-Type", "text/plain; charset=utf-8")
            with urllib.request.urlopen(req, timeout=2):
                pass
        except Exception as e:
            print(f"[SEND ERR] {e}", flush=True)
        log_queue.task_done()


def now_iso():
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")


def main():
    parser = argparse.ArgumentParser(description="XInput gamepad latency logger")
    parser.add_argument("--server", required=True,
                        help="Log server URL, e.g. http://192.168.1.100:8765/log")
    parser.add_argument("--controller", type=int, default=0,
                        help="XInput controller index (0-3)")
    args = parser.parse_args()

    xinput = load_xinput()
    state  = XINPUT_STATE()

    log_queue = queue.Queue()
    t = threading.Thread(target=sender_thread, args=(log_queue, args.server), daemon=True)
    t.start()

    print(f"Logging to:  {args.server}")
    print(f"Controller:  {args.controller}")
    print(f"Polling at:  {int(1/POLL_INTERVAL_S)} Hz")
    print("Waiting for controller... Press Ctrl+C to stop.\n")

    prev_buttons = 0
    prev_lt      = 0
    prev_rt      = 0
    was_connected = False

    # startup message
    log_queue.put(f"WIN_LOGGER_START server={args.server} controller={args.controller}")

    try:
        while True:
            result = xinput.XInputGetState(args.controller, ctypes.byref(state))

            if result != 0:
                # ERROR_DEVICE_NOT_CONNECTED = 1167
                if was_connected:
                    ts = now_iso()
                    msg = f"ts_win={ts} WIN_CONTROLLER_DISCONNECTED"
                    print(f"  {msg}", flush=True)
                    log_queue.put(msg)
                    was_connected = False
                    prev_buttons = prev_lt = prev_rt = 0
                time.sleep(POLL_INTERVAL_S)
                continue

            if not was_connected:
                ts = now_iso()
                msg = f"ts_win={ts} WIN_CONTROLLER_CONNECTED"
                print(f"  {msg}", flush=True)
                log_queue.put(msg)
                was_connected = True

            gp = state.Gamepad
            curr_buttons = gp.wButtons
            curr_lt      = gp.bLeftTrigger
            curr_rt      = gp.bRightTrigger

            btn_changed = curr_buttons ^ prev_buttons
            lt_changed  = abs(int(curr_lt) - int(prev_lt)) > TRIGGER_THRESHOLD
            rt_changed  = abs(int(curr_rt) - int(prev_rt)) > TRIGGER_THRESHOLD

            if btn_changed or lt_changed or rt_changed:
                # Record timestamp immediately before anything else
                ts = now_iso()

                parts = [f"ts_win={ts}"]

                pressed  = [n for m, n in BUTTON_NAMES.items() if btn_changed & m and     curr_buttons & m]
                released = [n for m, n in BUTTON_NAMES.items() if btn_changed & m and not curr_buttons & m]

                if pressed:
                    parts.append("pressed="  + "+".join(pressed))
                if released:
                    parts.append("released=" + "+".join(released))
                if lt_changed:
                    parts.append(f"LT={curr_lt}")
                if rt_changed:
                    parts.append(f"RT={curr_rt}")

                msg = " ".join(parts)
                print(f"  {msg}", flush=True)
                log_queue.put(msg)

                prev_buttons = curr_buttons
                if lt_changed:
                    prev_lt = curr_lt
                if rt_changed:
                    prev_rt = curr_rt

            time.sleep(POLL_INTERVAL_S)

    except KeyboardInterrupt:
        print("\nStopped.")
        log_queue.put(f"ts_win={now_iso()} WIN_LOGGER_STOP")
        log_queue.join()


if __name__ == "__main__":
    main()
