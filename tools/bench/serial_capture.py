#!/usr/bin/env python3
"""
serial_capture.py — bench capture for the RAK3212's native USB-Serial-JTAG port (ESP32-S3).

Why this exists (bench-proven 2026-09-24): the USB-Serial-JTAG peripheral interprets the CDC
control lines like the classic auto-reset circuit — DTR=0 with RTS=1 is "reset", DTR=1 with
RTS=0 is "boot strap". pyserial's open() applies the DTR state before the RTS state, so the
usual "dtr=False, rts=False before open" recipe passes through DTR=0/RTS=1 and RESETS THE CHIP
on every open (observed 11/11 times, reset cause USB_UART_CHIP_RESET). After a reset the CDC
port drops, so reopening turns into an endless self-inflicted "boot loop".

Safe sequence: pre-set BOTH lines asserted (no transition through DTR=0/RTS=1 inside open()),
open, then drop RTS first and DTR last. Hold the port once; never reopen automatically.

Usage:
  tools/bench/serial_capture.py [--port GLOB] [--seconds N] [--send LINE ... --after S] [--out FILE] [--reset]
  --reset: deliberately reset the chip on open, then reopen once (safely) to catch the boot log.
  python3 needs pyserial (pip install pyserial / brew python3 has it on this machine).
"""
import argparse, glob, sys, time
import serial


def open_without_reset(port: str, baud: int) -> serial.Serial:
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, baud, 0.2
    s.dtr = True
    s.rts = True          # applied inside open(): DTR first, then RTS — both asserted, no reset
    s.open()
    s.rts = False         # DTR=1,RTS=0 → no reset
    s.dtr = False         # DTR=0,RTS=0 → no reset
    return s


def open_with_reset(port: str, baud: int) -> serial.Serial:
    """Deliberately reset the chip on open (pyserial's default order passes DTR=0/RTS=1)."""
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, baud, 0.2
    s.dtr = False
    s.rts = False
    s.open()
    return s


def wait_for_port(pattern: str, timeout: float) -> str:
    deadline = time.time() + timeout
    while time.time() < deadline:
        ports = sorted(glob.glob(pattern))
        if ports:
            return ports[0]
        time.sleep(0.1)
    return ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/cu.usbmodem*", help="port or glob (default: /dev/cu.usbmodem*)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=15)
    ap.add_argument("--send", action="append", default=[], help="line to send once (repeatable; sent in order, 0.4 s apart)")
    ap.add_argument("--after", type=float, default=3.0, help="seconds to wait before --send")
    ap.add_argument("--out", help="also append raw lines to this file")
    ap.add_argument("--reset", action="store_true",
                    help="reset the chip first (reset-on-open), then reopen ONCE safely after the "
                         "USB re-enumeration to catch the boot log; the ROM lines are lost")
    a = ap.parse_args()

    ports = sorted(glob.glob(a.port))
    if not ports:
        print(f"no port matches {a.port}", file=sys.stderr)
        return 2
    if a.reset:
        r = open_with_reset(ports[0], a.baud)
        print(f"[capture] reset {r.port} via DTR/RTS; waiting for re-enumeration")
        t_reset = time.time()
        # read until the port drops (the reset re-enumerates the CDC device)
        while time.time() - t_reset < 3.0:
            try:
                r.read(64)
            except (serial.SerialException, OSError):
                break
        try:
            r.close()
        except Exception:
            pass
        time.sleep(0.3)
        port = wait_for_port(a.port, 10.0)
        if not port:
            print("[capture] port did not come back after reset", file=sys.stderr)
            return 3
        s = open_without_reset(port, a.baud)
        print(f"[capture] reopened {s.port} {time.time()-t_reset:.1f} s after reset (safe order)")
    else:
        s = open_without_reset(ports[0], a.baud)
    out = open(a.out, "a") if a.out else None
    print(f"[capture] {s.port} @ {a.baud}, DTR/RTS low, {a.seconds:.0f} s")
    t0, buf, sent, n = time.time(), b"", False, 0
    while time.time() - t0 < a.seconds:
        try:
            if a.send and not sent and time.time() - t0 >= a.after:
                for line_to_send in a.send:
                    s.write((line_to_send + "\n").encode())
                    print(f"[capture] sent {line_to_send!r}")
                    time.sleep(0.4)
                sent = True
            chunk = s.read(256)
        except (serial.SerialException, OSError):
            print(f"[capture] port dropped at t={time.time()-t0:.1f}s (chip reset). Not reopening: "
                  "a reopen would reset the chip again — rerun the capture instead.")
            break
        if chunk:
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("utf-8", "replace").rstrip("\r")
                n += 1
                print("  | " + text)
                if out:
                    out.write(text + "\n")
    s.close()
    if out:
        out.close()
    print(f"[capture] done, {n} lines")
    return 0


if __name__ == "__main__":
    sys.exit(main())
