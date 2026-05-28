#!/usr/bin/env python3
import argparse
import time
import can
import random
from dataclasses import dataclass


@dataclass
class AppState:
    running: bool = True
    delay_us: int = 0
    target_fps: int = 0
    locked_id: int = -1
    extended: bool = False


class CANStreamSimulator:
    def __init__(self, bus: can.BusABC, state: AppState):
        self.bus = bus
        self.state = state

        self.current_id = 0
        self.counter = 0

        self.last_frame_ns = 0
        self.last_fps_ns = time.monotonic_ns()
        self.frame_count = 0

        self.last_10ms_ns = 0
        self.last_20ms_ns = 0
        self.last_1000ms_ns = 0

        self.rpm = 800
        self.speed = 0

        self.id_base = self.get_generator_id_base()

    def get_generator_id_base(self) -> int:
        # Similar idea to ESP MAC hash, but PC-side.
        slot = random.randint(0, 95)
        return 0x100 + (slot * 0x10)

    def send_msg(self, arbitration_id: int, data: bytes, extended: bool = False):
        msg = can.Message(
            arbitration_id=arbitration_id,
            data=data,
            is_extended_id=extended,
            is_remote_frame=False,
        )

        try:
            self.bus.send(msg, timeout=0)
            self.frame_count += 1
        except can.CanError:
            pass

    def print_fps(self):
        now = time.monotonic_ns()
        elapsed = now - self.last_fps_ns

        if elapsed >= 1_000_000_000:
            fps = self.frame_count * 1_000_000_000 / elapsed
            print(f"[TX STAT] {fps:.0f} fps")
            self.frame_count = 0
            self.last_fps_ns = now

    def generator_loop(self):
        now = time.monotonic_ns()

        if not self.state.running:
            return

        if self.state.delay_us > 0:
            interval_ns = self.state.delay_us * 1000
            if now - self.last_frame_ns < interval_ns:
                return

        elif self.state.target_fps > 0:
            interval_ns = int(1_000_000_000 / self.state.target_fps)
            if now - self.last_frame_ns < interval_ns:
                return

        if self.state.locked_id >= 0:
            arbitration_id = self.state.locked_id
        else:
            arbitration_id = self.id_base + self.current_id

        if self.state.extended:
            arbitration_id |= 0x100

        data = bytes(
            [
                (self.counter >> 0) & 0xFF,
                (self.counter >> 8) & 0xFF,
                (self.counter >> 16) & 0xFF,
                (self.counter >> 24) & 0xFF,
                0x44,
                0x55,
                0x66,
                0x77,
            ]
        )

        self.send_msg(arbitration_id, data, self.state.extended)

        self.counter = (self.counter + 1) & 0xFFFFFFFF
        self.last_frame_ns = now

        if self.state.locked_id < 0:
            self.current_id = (self.current_id + 1) % 10

    def ecu_loop(self):
        now = time.monotonic_ns()

        if not self.state.running:
            return

        if now - self.last_10ms_ns >= 10_000_000:
            self.last_10ms_ns = now

            self.rpm += 10
            if self.rpm > 4000:
                self.rpm = 800

            data = bytes(
                [
                    self.rpm & 0xFF,
                    (self.rpm >> 8) & 0xFF,
                ]
            )

            self.send_msg(0x100, data)

        if now - self.last_20ms_ns >= 20_000_000:
            self.last_20ms_ns = now

            self.speed += 1
            if self.speed > 120:
                self.speed = 0

            data = bytes(
                [
                    self.speed & 0xFF,
                    (self.speed >> 8) & 0xFF,
                ]
            )

            self.send_msg(0x200, data)

        if now - self.last_1000ms_ns >= 1_000_000_000:
            self.last_1000ms_ns = now
            self.send_msg(0x300, bytes([0xAA]))

    def run(self, mode: str):
        print(f"CAN simulator started. Mode: {mode}")
        print("Press Ctrl+C to stop.")

        try:
            while True:
                if mode == "generator":
                    self.generator_loop()
                elif mode == "ecu":
                    self.ecu_loop()
                else:
                    raise ValueError(f"Unknown mode: {mode}")

                self.print_fps()

        except KeyboardInterrupt:
            print("\nStopped.")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--interface", required=True, help="socketcan, slcan, pcan, kvaser, etc."
    )
    parser.add_argument(
        "--comport", required=True, help="can0, COM9, /dev/ttyUSB0, etc."
    )
    parser.add_argument("--bitrate", type=int, default=500000)

    parser.add_argument("--mode", choices=["generator", "ecu"], default="generator")
    parser.add_argument("--fps", type=int, default=0)
    parser.add_argument("--delay-us", type=int, default=0)
    parser.add_argument("--locked-id", type=lambda x: int(x, 0), default=-1)
    parser.add_argument("--extended", action="store_true")
    parser.add_argument("--tty-baudrate", type=int, default=2000000)

    args = parser.parse_args()

    bus = can.Bus(
        interface=args.interface,
        channel=args.comport,
        bitrate=args.bitrate,
        tty_baudrate=args.tty_baudrate,
    )

    state = AppState(
        running=True,
        delay_us=args.delay_us,
        target_fps=args.fps,
        locked_id=args.locked_id,
        extended=args.extended,
    )

    sim = CANStreamSimulator(bus, state)
    sim.run(args.mode)


if __name__ == "__main__":
    main()
