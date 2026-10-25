#!/usr/bin/env python3
"""Run the Tab5 FreeType comparison, verify cleanup, optionally leave it open."""
import argparse
import re
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("--size", type=int, default=24)
    parser.add_argument("--leave-open", action="store_true")
    args = parser.parse_args()
    if not 12 <= args.size <= 48:
        parser.error("--size must be 12..48")

    link = serial.Serial(port=None, baudrate=115200, timeout=0.1, exclusive=True)
    link.dtr = link.rts = False
    link.port = args.port
    link.open()

    def read_until(token, timeout=45):
        data = bytearray()
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            data.extend(link.read(max(1, link.in_waiting)))
            if token in data:
                return data.decode(errors="replace").replace("\r", "")
        raise TimeoutError(f"waiting for {token!r}; tail={data[-800:]!r}")

    def command(text, timeout=45):
        link.write(text.encode() + b"\r")
        return read_until(b"\n> ", timeout)

    def free_psram(text):
        match = re.search(r"psram\s+total \d+ KB\s+used \d+ KB\s+free (\d+) KB", text)
        if not match:
            raise RuntimeError(f"cannot parse PSRAM: {text!r}")
        return int(match.group(1))

    try:
        link.write(b"\x03\r")
        read_until(b"> ", 45)
        # Opening USB serial can reset Tab5.  The first prompt can precede the
        # queued Ctrl-C, so synchronize once more with an already-running shell.
        time.sleep(0.2)
        link.reset_input_buffer()
        link.write(b"\x03\r")
        read_until(b"> ", 10)
        listing = command("ls /assets/fonts/BIZUDGothic-Regular.ttf")
        if "4667380" not in listing:
            raise RuntimeError(f"pinned TTF missing or wrong size: {listing!r}")
        before = free_psram(command("mem"))
        link.write(f"fontbench {args.size}\r".encode())
        result = read_until(b"FONTBENCH ", 60) + read_until(b"\n", 10)
        pattern = (r"FONTBENCH (\d+) px: load=(\d+) ms first=(\d+) us warm=(\d+) us "
                   r"misses=(\d+) hits=(\d+) raster=(\d+) us font=(\d+) cache=(\d+)")
        match = re.search(pattern, result)
        if not match:
            raise RuntimeError(f"benchmark receipt missing: {result!r}")
        values = list(map(int, match.groups()))
        if values[0] != args.size or values[4] == 0 or values[5] == 0 or values[7] != 4667380:
            raise RuntimeError(f"invalid benchmark counters: {values}")
        time.sleep(2)
        link.write(b"q")
        read_until(b"\n> ", 20)
        after = free_psram(command("mem"))
        if abs(after - before) > 32:
            raise RuntimeError(f"PSRAM not reclaimed: before={before} KiB after={after} KiB")
        print("FREETYPE DEVICE PASS: "
              f"{values[0]}px load {values[1]} ms; first {values[2]} us; "
              f"warm {values[3]} us; raster {values[6]} us; "
              f"miss/hit {values[4]}/{values[5]}; cache {values[8] // 1024} KiB; "
              f"PSRAM restored to {after} KiB")
        if args.leave_open:
            link.write(f"fontbench {args.size}\r".encode())
            read_until(b"FONTBENCH ", 60)
            print("Comparison screen left open; press Q on Tab5 to return.")
    finally:
        link.close()


if __name__ == "__main__":
    main()

