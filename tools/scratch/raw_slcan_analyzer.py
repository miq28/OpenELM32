#!/usr/bin/env python3
import argparse
import threading
import time
import serial


def slcan_setup(ser, bitrate):
    ser.write(b"C\r")

    speed_map = {
        10000: b"S0\r",
        20000: b"S1\r",
        50000: b"S2\r",
        100000: b"S3\r",
        125000: b"S4\r",
        250000: b"S5\r",
        500000: b"S6\r",
        800000: b"S7\r",
        1000000: b"S8\r",
    }

    if bitrate not in speed_map:
        raise ValueError(f"Unsupported SLCAN bitrate: {bitrate}")

    ser.write(speed_map[bitrate])
    ser.write(b"O\r")
    ser.flush()


def parse_slcan_frame(line: bytes):
    if not line:
        return None

    text = line.decode("ascii", errors="ignore").strip()
    if not text:
        return None

    frame_type = text[0]

    try:
        if frame_type == "t":
            if len(text) < 5:
                return None

            can_id = int(text[1:4], 16)
            dlc = int(text[4], 16)
            data_hex = text[5:5 + dlc * 2]

            if len(data_hex) != dlc * 2:
                return None

            data = bytes.fromhex(data_hex) if data_hex else b""
            return can_id, False, False, dlc, data

        if frame_type == "T":
            if len(text) < 10:
                return None

            can_id = int(text[1:9], 16)
            dlc = int(text[9], 16)
            data_hex = text[10:10 + dlc * 2]

            if len(data_hex) != dlc * 2:
                return None

            data = bytes.fromhex(data_hex) if data_hex else b""
            return can_id, True, False, dlc, data

        if frame_type == "r":
            if len(text) < 5:
                return None

            can_id = int(text[1:4], 16)
            dlc = int(text[4], 16)
            return can_id, False, True, dlc, b""

        if frame_type == "R":
            if len(text) < 10:
                return None

            can_id = int(text[1:9], 16)
            dlc = int(text[9], 16)
            return can_id, True, True, dlc, b""

    except ValueError:
        return None

    return None


def match_filter(can_id, filter_text):
    if not filter_text:
        return True

    parts = filter_text.split(",")

    for part in parts:
        part = part.strip().upper()

        if not part:
            continue

        if "-" in part:
            start_str, end_str = part.split("-", 1)
            start_id = int(start_str, 16)
            end_id = int(end_str, 16)

            if start_id <= can_id <= end_id:
                return True
        else:
            single_id = int(part, 16)

            if can_id == single_id:
                return True

    return False


def input_thread(ser):
    print("Type OBD PID like 0100 / 010C / 010D, or raw SLCAN like t7DF802010C0000000000")

    while True:
        try:
            cmd = input("> ").strip()
        except EOFError:
            break

        if not cmd:
            continue

        if cmd.startswith(("t", "T", "r", "R", "C", "O", "L", "S")):
            ser.write((cmd + "\r").encode("ascii"))
            ser.flush()
            print(f"[TX RAW] {cmd}", flush=True)
            continue

        try:
            payload = bytes.fromhex(cmd)

            if len(payload) > 7:
                print("Payload too long for single-frame OBD request", flush=True)
                continue

            data = bytes([len(payload)]) + payload
            data = data.ljust(8, b"\x00")

            frame = "t7DF8" + data.hex().upper()

            ser.write((frame + "\r").encode("ascii"))
            ser.flush()

            print(f"[TX OBD] {cmd.upper()} -> {frame}", flush=True)

        except ValueError:
            print("Invalid input", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--comport", required=True)
    parser.add_argument("--bitrate", type=int, default=500000)
    parser.add_argument("--tty-baudrate", type=int, default=2000000)
    parser.add_argument("--setup", action="store_true")

    parser.add_argument("--print-frames", action="store_true")
    parser.add_argument("--raw", action="store_true")
    parser.add_argument("--no-stat", action="store_true")

    parser.add_argument(
        "--filter-id",
        nargs="?",
        const="7E8-7EF",
        default=None,
        type=str,
        help="CAN ID filter. Examples: --filter-id, --filter-id 7E8, --filter-id 7E8-7EF, --filter-id 100-1FF,7E8",
    )

    args = parser.parse_args()

    ser = serial.Serial(
        args.comport,
        baudrate=args.tty_baudrate,
        timeout=0,
        write_timeout=0,
    )

    if args.setup:
        slcan_setup(ser, args.bitrate)
        time.sleep(0.1)
        ser.reset_input_buffer()

    print("Raw SLCAN analyzer started.")
    print("Press Ctrl+C to stop.")

    threading.Thread(
        target=input_thread,
        args=(ser,),
        daemon=True,
    ).start()

    start_time = time.perf_counter()
    buf = bytearray()

    total_frames = 0
    total_bytes = 0
    bad_lines = 0

    stat_frames = 0
    stat_bytes = 0
    last_stat_ns = time.perf_counter_ns()

    try:
        while True:
            chunk = ser.read(8192)

            if chunk:
                buf.extend(chunk)

                while b"\r" in buf:
                    line, _, rest = buf.partition(b"\r")
                    buf = bytearray(rest)

                    if args.raw:
                        print(f"RAW: {line!r}", flush=True)

                    parsed = parse_slcan_frame(line)

                    if parsed is None:
                        bad_lines += 1
                        continue

                    can_id, extended, rtr, dlc, data = parsed

                    total_frames += 1
                    stat_frames += 1
                    total_bytes += len(data)
                    stat_bytes += len(data)

                    if not match_filter(can_id, args.filter_id):
                        continue

                    if args.print_frames:
                        elapsed = time.perf_counter() - start_time
                        id_fmt = f"{can_id:08X}" if extended else f"{can_id:03X}"
                        data_fmt = " ".join(f"{b:02X}" for b in data)

                        flags = []
                        if extended:
                            flags.append("EXT")
                        if rtr:
                            flags.append("RTR")

                        flag_text = ",".join(flags) if flags else "STD"

                        print(
                            f"+{elapsed:10.6f}s {id_fmt} [{dlc}] {data_fmt} {flag_text}",
                            flush=True,
                        )

            now = time.perf_counter_ns()

            if not args.no_stat and now - last_stat_ns >= 1_000_000_000:
                elapsed = (now - last_stat_ns) / 1_000_000_000
                fps = stat_frames / elapsed
                kbps_payload = (stat_bytes * 8 / elapsed) / 1000

                print(
                    f"[RX STAT] {fps:.0f} fps "
                    f"payload:{kbps_payload:.1f} kbps "
                    f"total:{total_frames} "
                    f"bad:{bad_lines}",
                    flush=True,
                )

                stat_frames = 0
                stat_bytes = 0
                last_stat_ns = now

    except KeyboardInterrupt:
        print("\nStopped.")
        print(f"Total frames: {total_frames}")
        print(f"Bad lines: {bad_lines}")

    finally:
        ser.close()


if __name__ == "__main__":
    main()