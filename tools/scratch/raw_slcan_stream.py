# raw_slcan_stream.py
import argparse
import time
import serial

def slcan_setup(ser, bitrate):
    ser.write(b"C\r")  # close

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
        raise ValueError("Unsupported SLCAN bitrate")

    ser.write(speed_map[bitrate])
    ser.write(b"O\r")  # open
    ser.flush()

def make_frame(can_id, counter):
    data = [
        counter & 0xFF,
        (counter >> 8) & 0xFF,
        (counter >> 16) & 0xFF,
        (counter >> 24) & 0xFF,
        0x44,
        0x55,
        0x66,
        0x77,
    ]

    data_hex = "".join(f"{b:02X}" for b in data)
    return f"t{can_id:03X}8{data_hex}\r".encode()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--comport", required=True)
    parser.add_argument("--bitrate", type=int, default=500000)
    parser.add_argument("--tty-baudrate", type=int, default=2000000)
    parser.add_argument("--fps", type=int, default=1000)
    parser.add_argument("--locked-id", type=lambda x: int(x, 0), default=-1)
    parser.add_argument("--setup", action="store_true")

    args = parser.parse_args()

    ser = serial.Serial(
        args.comport,
        baudrate=args.tty_baudrate,
        timeout=0,
        write_timeout=0,
    )

    if args.setup:
        slcan_setup(ser, args.bitrate)

    interval_ns = int(1_000_000_000 / args.fps)

    counter = 0
    current_id = 0
    frame_count = 0
    last_tx_ns = time.perf_counter_ns()
    last_stat_ns = last_tx_ns

    print("Raw SLCAN stream started.")
    print("Press Ctrl+C to stop.")

    try:
        while True:
            now = time.perf_counter_ns()

            if now - last_tx_ns < interval_ns:
                continue

            can_id = args.locked_id if args.locked_id >= 0 else 0x100 + current_id
            frame = make_frame(can_id, counter)

            try:
                ser.write(frame)
                frame_count += 1
            except serial.SerialTimeoutException:
                pass

            counter = (counter + 1) & 0xFFFFFFFF
            current_id = (current_id + 1) % 10
            last_tx_ns += interval_ns

            if now - last_stat_ns >= 1_000_000_000:
                print(f"[TX STAT] {frame_count} fps")
                frame_count = 0
                last_stat_ns = now

    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        ser.close()

if __name__ == "__main__":
    main()