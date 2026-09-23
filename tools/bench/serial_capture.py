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
  tools/bench/serial_capture.py [--port GLOB] [--seconds N] [--send LINE --after S] [--out FILE]
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


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/cu.usbmodem*", help="port or glob (default: /dev/cu.usbmodem*)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=15)
    ap.add_argument("--send", help="one line to send once")
    ap.add_argument("--after", type=float, default=3.0, help="seconds to wait before --send")
    ap.add_argument("--out", help="also append raw lines to this file")
    a = ap.parse_args()

    ports = sorted(glob.glob(a.port))
    if not ports:
        print(f"no port matches {a.port}", file=sys.stderr)
        return 2
    s = open_without_reset(ports[0], a.baud)
    out = open(a.out, "a") if a.out else None
    print(f"[capture] {s.port} @ {a.baud}, DTR/RTS low, {a.seconds:.0f} s")
    t0, buf, sent, n = time.time(), b"", False, 0
    while time.time() - t0 < a.seconds:
        try:
            if a.send and not sent and time.time() - t0 >= a.after:
                s.write((a.send + "\n").encode())
                sent = True
                print(f"[capture] sent {a.send!r}")
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
