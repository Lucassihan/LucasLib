#!/usr/bin/env python3
"""
Capture a LucasLib debugger trace off the V5 brain's USB serial — reliably.

Why this exists: the VS Code / VEX serial monitor drops bytes under the load of a
full trace dump (you lose the header + early ticks). Reading the port directly
and writing to a file doesn't drop, and this also:
  - strips the ~LLT| markers,
  - keeps only COMPLETE #LUCASLIB_TRACE...#END blocks,
  - survives the USB re-enumeration that happens when the program (re)starts,
  - writes the latest complete trace to a file you can import.

Usage:
    pip install pyserial          # one-time
    python3 capture-trace.py      # auto-detects /dev/cu.usbmodem*
    python3 capture-trace.py /dev/cu.usbmodem102 trace.txt   # explicit

Leave it running, then run your auton. Each time a complete trace comes through
it overwrites the output file and prints a line. Ctrl-C when you're done.
"""

import sys
import glob
import time

try:
    import serial  # pyserial
except ImportError:
    sys.exit("pyserial not installed — run:  pip install pyserial")

MARK = "~LLT|"


def find_port() -> str | None:
    cands = sorted(glob.glob("/dev/cu.usbmodem*")) or sorted(glob.glob("/dev/tty.usbmodem*"))
    return cands[-1] if cands else None


def main() -> None:
    port = sys.argv[1] if len(sys.argv) > 1 else None
    out = sys.argv[2] if len(sys.argv) > 2 else "trace.txt"

    print(f"LucasLib trace capture -> {out}")
    print("Run your auton; waiting for a complete dump. Ctrl-C to stop.\n")

    buf: list[str] = []
    in_trace = False
    count = 0

    while True:
        p = port or find_port()
        if not p:
            time.sleep(0.5)
            continue
        try:
            with serial.Serial(p, 115200, timeout=1) as ser:
                print(f"reading {p} …")
                while True:
                    raw = ser.readline().decode("ascii", "replace")
                    i = raw.find(MARK)
                    if i < 0:
                        continue
                    line = raw[i + len(MARK):].rstrip("\r\n")
                    if line.startswith("#LUCASLIB_TRACE"):
                        buf = [line]
                        in_trace = True
                    elif in_trace and line.startswith("#END"):
                        with open(out, "w") as f:
                            f.write("\n".join(buf) + "\n")
                        count += 1
                        print(f"  ✓ captured complete trace #{count} "
                              f"({len(buf)} lines) -> {out}")
                        in_trace = False
                    elif in_trace:
                        buf.append(line)
        except serial.SerialException:
            # program restart re-enumerates USB; reopen and keep going
            print("  … port dropped (program restart?), reopening")
            time.sleep(0.5)
        except KeyboardInterrupt:
            print(f"\nDone — {count} trace(s) captured to {out}")
            return


if __name__ == "__main__":
    main()
