#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import time
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover
    raise SystemExit("pyserial is required. Install it with: pip install pyserial") from exc

BEGIN = "AKINYANKOKUPUMP_DUMP_BEGIN"
END = "AKINYANKOKUPUMP_DUMP_END"
MONIKER = "AkinyankokuPump"
KNOWN_KEYWORDS = (
    "arduino",
    "uno",
    "ra4",
    "renesas",
    "usb serial",
    "wchusbserial",
    "ch340",
    "cp210",
)
MIN_REAL_TIMESTAMP = 1704067200  # 2024-01-01T00:00:00Z


def format_session_header(session_count: int, ts: int, interval_seconds: int) -> str:
    if ts >= MIN_REAL_TIMESTAMP:
        try:
            start = dt.datetime.fromtimestamp(ts).strftime("%Y-%m-%d %H:%M:%S")
            return f"{start} interval={interval_seconds}s"
        except (OverflowError, OSError, ValueError):
            pass
    return f"session={session_count} interval={interval_seconds}s"


def next_output_path(documents: Path) -> Path:
    date_part = time.strftime("%Y-%m-%d_%H-%M-%S")
    return documents / MONIKER / f"log_{date_part}.txt"


def ranked_ports() -> list[str]:
    ports = list(list_ports.comports())
    if not ports:
        raise SystemExit("No serial ports found.")

    def score(port) -> tuple[int, str]:
        haystack = " ".join(
            filter(
                None,
                [
                    port.device,
                    getattr(port, "description", None),
                    getattr(port, "manufacturer", None),
                    getattr(port, "product", None),
                    getattr(port, "interface", None),
                ],
            )
        ).lower()
        s = sum(1 for kw in KNOWN_KEYWORDS if kw in haystack)
        if getattr(port, "vid", None) == 0x2341:  # Arduino SA
            s += 5
        return (-s, port.device)

    return [p.device for p in sorted(ports, key=score)]


def try_capture_once(port: str, baud: int, timeout: float, command: str) -> list[str] | None:
    in_dump = False
    session_count = 0
    lines_out: list[str] = []

    try:
        with serial.Serial(port, baudrate=baud, timeout=0.5) as ser:
            time.sleep(2.0)
            ser.reset_input_buffer()
            ser.reset_output_buffer()
            ser.write((command + "\n").encode("utf-8"))
            ser.flush()

            start = time.time()
            while True:
                raw = ser.readline()
                if not raw:
                    if time.time() - start > timeout:
                        return None
                    continue

                text = raw.decode("utf-8", errors="replace").strip()
                if not text:
                    continue

                print(f"[{port}] {text}")

                if text == BEGIN:
                    in_dump = True
                    session_count = 0
                    lines_out.clear()
                    start = time.time()
                    continue

                if not in_dump:
                    continue

                if text == END:
                    return lines_out

                if text.startswith("FORMAT "):
                    continue

                if text.startswith("SESSION "):
                    parts = text.split()
                    if len(parts) < 3:
                        lines_out.append(f"# MALFORMED {text}")
                        continue
                    ts = int(parts[1])
                    interval_seconds = int(parts[2])
                    session_count += 1
                    if session_count > 1:
                        lines_out.append("")
                    lines_out.append(format_session_header(session_count, ts, interval_seconds))
                    continue

                if text.startswith("SAMPLE "):
                    _, payload = text.split(maxsplit=1)
                    lines_out.append(payload)
                    continue

                if text.startswith("SAMPLE2 "):
                    parts = text.split()
                    if len(parts) != 8:
                        lines_out.append(f"# MALFORMED {text}")
                        continue

                    moisture = " ".join(parts[1:4])
                    raw_values = " ".join(parts[4:7])
                    status_mask = int(parts[7])
                    connected_mask = status_mask & 0x0F
                    error_mask = (status_mask >> 4) & 0x0F
                    lines_out.append(
                        f"moisture={moisture} raw={raw_values} "
                        f"connected=0x{connected_mask:01X} error=0x{error_mask:01X}"
                    )
                    continue

                if text.startswith("WARN "):
                    lines_out.append(f"# {text}")
                    continue
    except serial.SerialException:
        return None

    return None


def capture_dump(port: str | None, baud: int, timeout: float, output_path: Path, command: str) -> str:
    ports_to_try = [port] if port else ranked_ports()
    if port:
        print(f"Connecting to {port} at {baud} baud.")
    else:
        print("Auto-detecting board port...")
        print("Candidate ports: " + ", ".join(ports_to_try))

    for candidate in ports_to_try:
        print(f"Trying {candidate}...")
        lines_out = try_capture_once(candidate, baud, timeout, command)
        if lines_out is None:
            continue

        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text("\n".join(lines_out).rstrip() + "\n", encoding="utf-8")
        print(f"Saved dump to {output_path}")
        return candidate

    tried = ", ".join(ports_to_try)
    raise SystemExit(f"Could not detect a responding AkinyankokuPump board on: {tried}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Capture a garden pump EEPROM dump and save it under Documents/AkinyankokuPump.")
    parser.add_argument("--port", help="Serial port, for example COM6 or /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=8.0, help="Seconds to wait per port for the dump to begin")
    parser.add_argument("--output", type=Path, help="Optional explicit output file path")
    parser.add_argument("--no-erase", action="store_true", help="Use DUMP_NO_ERASE instead of DUMP")
    args = parser.parse_args()

    documents = Path.home() / "Documents"
    output_path = args.output or next_output_path(documents)
    command = "DUMP_NO_ERASE" if args.no_erase else "DUMP"
    chosen_port = capture_dump(args.port, args.baud, args.timeout, output_path, command)
    print(f"Completed using port {chosen_port}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
