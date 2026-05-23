#!/usr/bin/env python3
"""
Small ELM327 compatibility smoke test for this firmware.

Examples:
  python elm327_compat_test.py serial --port COM5 --baud 1000000
  python elm327_compat_test.py tcp --host 192.168.4.1 --port 35000
  python elm327_compat_test.py ble --name WEACT_CAN485_8CE0
"""

import argparse
import asyncio
import re
import socket
import sys
import time


OBDLINK_WRITE_UUID = "0000fff2-0000-1000-8000-00805f9b34fb"
OBDLINK_NOTIFY_UUID = "0000fff1-0000-1000-8000-00805f9b34fb"


DEFAULT_SEQUENCE = [
    ("ATZ", [r"ELM327"]),
    ("ATI", [r"ELM327"]),
    ("AT@1", [r"OBDLink|ELM327|ESP32|WEACT"]),
    ("ATE0", [r"OK"]),
    ("ATL0", [r"OK"]),
    ("ATS0", [r"OK"]),
    ("ATH0", [r"OK"]),
    ("ATSP0", [r"OK"]),
    ("ATDPN", [r"A?6"]),
    ("ATDP", [r"CAN11/500|ISO15765-4"]),
    ("0100", [r"4100|NO DATA"]),
    ("ATH1", [r"OK"]),
    ("0100", [r"7E8.*4100|4100|NO DATA"]),
]

VIN_SEQUENCE = [
    ("ATH0", [r"OK"]),
    ("ATS0", [r"OK"]),
    ("ATSP6", [r"OK"]),
    ("0902", [r"490201[0-9A-F]{34}|NO DATA"]),
]

INVALID_SEQUENCE = [
    ("?", [r"^\?>$"]),
    ("HELLO", [r"^\?>$"]),
    ("010G", [r"^\?>$"]),
    ("12345678", [r"^\?>$"]),
]


class ElmConnection:
    def write(self, data):
        raise NotImplementedError

    def read_until_prompt(self, timeout):
        raise NotImplementedError

    def close(self):
        pass


class TcpElmConnection(ElmConnection):
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=5.0)
        self.sock.settimeout(0.05)

    def write(self, data):
        self.sock.sendall(data)

    def read_until_prompt(self, timeout):
        deadline = time.monotonic() + timeout
        chunks = []
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(256)
                if data:
                    chunks.append(data)
                    if b">" in data:
                        break
            except socket.timeout:
                pass
        return b"".join(chunks)

    def close(self):
        self.sock.close()


class SerialElmConnection(ElmConnection):
    def __init__(self, port, baud):
        try:
            import serial
        except ImportError as exc:
            raise SystemExit("pyserial is required: pip install pyserial") from exc

        self.serial = serial.Serial(port, baudrate=baud, timeout=0.05, write_timeout=1.0)
        time.sleep(0.2)
        self.serial.reset_input_buffer()

    def write(self, data):
        self.serial.write(data)
        self.serial.flush()

    def read_until_prompt(self, timeout):
        deadline = time.monotonic() + timeout
        chunks = []
        while time.monotonic() < deadline:
            data = self.serial.read(256)
            if data:
                chunks.append(data)
                if b">" in data:
                    break
        return b"".join(chunks)

    def close(self):
        self.serial.close()


class BleElmConnection(ElmConnection):
    def __init__(self, name=None, address=None, timeout=8.0):
        try:
            from bleak import BleakClient, BleakScanner
        except ImportError as exc:
            raise SystemExit("bleak is required: pip install bleak") from exc

        if not name and not address:
            raise SystemExit("BLE requires --name or --address")

        self.BleakClient = BleakClient
        self.BleakScanner = BleakScanner
        self.name = name
        self.address = address
        self.timeout = timeout
        self.client = None
        self.buffer = bytearray()

    async def connect(self):
        target = self.address

        if target is None:
            devices = await self.BleakScanner.discover(timeout=self.timeout)
            for device in devices:
                if device.name == self.name:
                    target = device.address
                    break

        if target is None:
            raise SystemExit(f"BLE device not found: {self.name}")

        self.client = self.BleakClient(target)
        await self.client.connect()
        await self.client.start_notify(OBDLINK_NOTIFY_UUID, self._on_notify)

    def _on_notify(self, _sender, data):
        self.buffer.extend(data)

    async def async_write(self, data):
        await self.client.write_gatt_char(OBDLINK_WRITE_UUID, data, response=False)

    async def async_read_until_prompt(self, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if b">" in self.buffer:
                break
            await asyncio.sleep(0.01)

        data = bytes(self.buffer)
        self.buffer.clear()
        return data

    async def async_close(self):
        if self.client is not None and self.client.is_connected:
            await self.client.stop_notify(OBDLINK_NOTIFY_UUID)
            await self.client.disconnect()


def printable(raw):
    text = raw.decode("ascii", errors="replace")
    return text.replace("\r", "\\r").replace("\n", "\\n")


def normalize(raw):
    text = raw.decode("ascii", errors="replace")
    return re.sub(r"\s+", "", text).upper()


def run_sequence(conn, timeout, include_vin=False, include_invalid=False):
    failures = 0
    sequence = list(DEFAULT_SEQUENCE)

    if include_vin:
        sequence.extend(VIN_SEQUENCE)

    if include_invalid:
        sequence.extend(INVALID_SEQUENCE)

    for command, patterns in sequence:
        conn.write(command.encode("ascii") + b"\r")
        raw = conn.read_until_prompt(timeout)
        shown = printable(raw)
        compact = normalize(raw)

        ok = any(re.search(pattern, compact, re.IGNORECASE) for pattern in patterns)
        status = "PASS" if ok else "FAIL"
        print(f"{status} {command:<6} {shown}")

        if not ok:
            failures += 1

        time.sleep(0.05)

    return failures


async def run_ble_sequence(conn, timeout, include_vin=False, include_invalid=False):
    failures = 0
    sequence = list(DEFAULT_SEQUENCE)

    if include_vin:
        sequence.extend(VIN_SEQUENCE)

    if include_invalid:
        sequence.extend(INVALID_SEQUENCE)

    await conn.connect()
    try:
        for command, patterns in sequence:
            await conn.async_write(command.encode("ascii") + b"\r")
            raw = await conn.async_read_until_prompt(timeout)
            shown = printable(raw)
            compact = normalize(raw)

            ok = any(re.search(pattern, compact, re.IGNORECASE) for pattern in patterns)
            status = "PASS" if ok else "FAIL"
            print(f"{status} {command:<6} {shown}")

            if not ok:
                failures += 1

            await asyncio.sleep(0.05)
    finally:
        await conn.async_close()

    return failures


def main(argv):
    parser = argparse.ArgumentParser(description="ELM327 compatibility smoke test")
    subparsers = parser.add_subparsers(dest="transport", required=True)

    serial_parser = subparsers.add_parser("serial")
    serial_parser.add_argument("--port", required=True)
    serial_parser.add_argument("--baud", type=int, default=1000000)
    serial_parser.add_argument("--vin", action="store_true", help="also test ISO-TP VIN response with 0902")
    serial_parser.add_argument("--invalid", action="store_true", help="also test invalid-command rejection")

    tcp_parser = subparsers.add_parser("tcp")
    tcp_parser.add_argument("--host", required=True)
    tcp_parser.add_argument("--port", type=int, default=35000)
    tcp_parser.add_argument("--vin", action="store_true", help="also test ISO-TP VIN response with 0902")
    tcp_parser.add_argument("--invalid", action="store_true", help="also test invalid-command rejection")

    ble_parser = subparsers.add_parser("ble")
    ble_parser.add_argument("--name")
    ble_parser.add_argument("--address")
    ble_parser.add_argument("--vin", action="store_true", help="also test ISO-TP VIN response with 0902")
    ble_parser.add_argument("--invalid", action="store_true", help="also test invalid-command rejection")

    parser.add_argument("--timeout", type=float, default=1.5)

    args = parser.parse_args(argv)

    if args.transport == "ble":
        conn = BleElmConnection(args.name, args.address)
        failures = asyncio.run(run_ble_sequence(conn, args.timeout, args.vin, args.invalid))
        return 1 if failures else 0

    if args.transport == "serial":
        conn = SerialElmConnection(args.port, args.baud)
    else:
        conn = TcpElmConnection(args.host, args.port)

    try:
        failures = run_sequence(conn, args.timeout, args.vin, args.invalid)
    finally:
        conn.close()

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
