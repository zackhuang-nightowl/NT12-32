#!/usr/bin/env python3
"""Pull nvr_settings.db from NVR via serial console (COM6 @ 115200)."""
import base64
import re
import sys
import time
from pathlib import Path

import serial

PORT = "COM6"
BAUD = 115200
OUT_DIR = Path(__file__).resolve().parent.parent / "requirement" / "device_dump"
DB_PATH = "/flash/nvrcfg/nvr_settings.db"
ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
START_MARK = "__NVR_DB_B64_START__"
END_MARK = "__NVR_DB_B64_END__"


def strip_ansi(s):
    return ANSI_RE.sub("", s)


def wake_shell(ser):
    ser.reset_input_buffer()
    for seq in (b"\x03", b"\r\n", b"\r\n", b"\r\n"):
        ser.write(seq)
        ser.flush()
        time.sleep(0.4)
    time.sleep(0.5)
    ser.reset_input_buffer()


def read_until(ser, predicate, timeout=180.0):
    buf = ""
    end = time.time() + timeout
    while time.time() < end:
        chunk = ser.read(4096 if ser.in_waiting else 256)
        if chunk:
            buf += chunk.decode("utf-8", errors="replace")
            if predicate(strip_ansi(buf)):
                return strip_ansi(buf)
        else:
            time.sleep(0.05)
    raise TimeoutError(f"timeout after {len(buf)} bytes, tail={buf[-300:]!r}")


def pull_base64(ser, db_path):
    # Separate echoes from payload: START/END on their own echo lines.
    ser.write((f"echo {START_MARK}\r\n").encode())
    ser.flush()
    read_until(ser, lambda b: START_MARK in b, timeout=10.0)

    ser.write((f"base64 {db_path}\r\n").encode())
    ser.flush()

    ser.write((f"echo {END_MARK}\r\n").encode())
    ser.flush()

    out = read_until(
        ser,
        lambda b: b.rfind(END_MARK) > b.find(START_MARK) and b.count(END_MARK) >= 1,
        timeout=180.0,
    )

    i = out.rfind(START_MARK)
    j = out.rfind(END_MARK)
    if i < 0 or j < 0 or j <= i:
        Path(OUT_DIR / "serial_raw.txt").write_text(out, encoding="utf-8", errors="replace")
        raise RuntimeError("markers missing; raw saved")

    block = out[i + len(START_MARK) : j]
    b64 = []
    for line in block.splitlines():
        line = line.strip()
        if not line or line.startswith("echo ") or line.startswith("base64 "):
            continue
        if line.startswith("root@XVR:"):
            continue
        if re.fullmatch(r"[A-Za-z0-9+/=]+", line):
            b64.append(line)

    if not b64:
        Path(OUT_DIR / "serial_raw.txt").write_text(out, encoding="utf-8", errors="replace")
        raise RuntimeError("no base64 parsed; raw saved")

    data = base64.b64decode("".join(b64), validate=False)
    return data


def dump_caps(db_file):
    import sqlite3

    con = sqlite3.connect(str(db_file))
    cur = con.cursor()
    print("\n=== camera (chn=6 -> GUI ch7) ===")
    cols = [d[1] for d in cur.execute("PRAGMA table_info(camera)").fetchall()]
    row = cur.execute("SELECT * FROM camera WHERE chn=6").fetchone()
    if row:
        for k, v in zip(cols, row):
            print(f"  {k}: {v}")
    else:
        print("  (no row for chn=6)")

    print("\n=== camera_capability (non-empty) ===")
    rows = cur.execute(
        "SELECT chn, signal, caps_json FROM camera_capability WHERE caps_json!='' ORDER BY chn"
    ).fetchall()
    for chn, sig, caps in rows:
        print(f"\n--- chn={chn} (GUI ch{chn+1}) signal={sig} ---")
        print(caps)
    con.close()


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    print(f"Opening {PORT} @ {BAUD} ...")
    ser = serial.Serial(PORT, BAUD, timeout=1)
    try:
        wake_shell(ser)
        ser.write((f"wc -c {DB_PATH}\r\n").encode())
        ser.flush()
        time.sleep(1.0)
        print(strip_ansi(ser.read(ser.in_waiting).decode("utf-8", errors="replace"))[-120:])

        print(f"Transferring {DB_PATH} (expect ~118784 bytes, ~60-90s) ...")
        data = pull_base64(ser, DB_PATH)
        out_file = OUT_DIR / "nvr_settings.db"
        out_file.write_bytes(data)
        print(f"Saved {len(data)} bytes -> {out_file}")
        dump_caps(out_file)
        return 0
    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
