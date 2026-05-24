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
    ("ATI", [r"ELM327|OBDLINK"]),
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

FORMATTING_SEQUENCE = [
    ("At Dp", [r"CAN11/500|ISO15765-4"]),
    ("AT SP 0", [r"OK"]),
    ("01 00", [r"4100|NO DATA"]),
    ("AT H 1", [r"OK"]),
    ("01 00", [r"7E8.*4100|4100|NO DATA"]),
]

IDENTITY_SEQUENCE = [
    ("STI", [r"STN"]),
    ("VTI", [r"OBDLINK|ELM327|ESP32|WEACT"]),
    ("AT@2", [r"WEACT|ESP32|CAN|OBD"]),
    ("ATRV", [r"\d+\.\d+V"]),
    ("ATIGN", [r"OK"]),
]

OBDLINK_SEQUENCE = [
    ("STI", [r"STN\d+V\d+\.\d+\.\d+"]),
    ("STIX", [r"STN\d+V\d+\.\d+\.\d+"]),
    ("STDI", [r"OBDLINK"]),
    ("STDIX", [r"OBDLINK.*STN.*OBDSOLUTIONS|STN.*OBDLINK.*OBDSOLUTIONS"]),
    ("STMFR", [r"OBDSOLUTIONS"]),
    ("STSN", [r"\d{8,}"]),
    ("STPR", [r"6"]),
    ("STPRS", [r"CAN11/500|ISO15765-4"]),
    ("STSLCS", [r"UART_SLEEP.*OBD_SLEEP.*VOLT_SLEEP"]),
]

DTC_SEQUENCE = [
    ("ATH0", [r"OK"]),
    ("ATS0", [r"OK"]),
    ("ATSH7DF", [r"OK"]),
    ("03", [r"43012304|NO DATA"]),
    ("07", [r"47012304|NO DATA"]),
    ("0A", [r"4A012304|NO DATA"]),
    ("ATSH7E0", [r"OK"]),
]

FREEZE_FRAME_SEQUENCE = [
    ("ATH0", [r"OK"]),
    ("ATS0", [r"OK"]),
    ("ATSH7DF", [r"OK"]),
    ("0101", [r"410181"]),
    ("0200", [r"4200"]),
    ("020200", [r"4202002304|42022304"]),
    ("020400", [r"4204"]),
    ("020500", [r"4205"]),
    ("020C00", [r"420C"]),
    ("020D00", [r"420D"]),
    ("ATSH7E0", [r"OK"]),
]

MULTI_ECU_SEQUENCE = [
    ("ATH1", [r"OK"]),
    ("ATSH7DF", [r"OK"]),
    ("0100", [r"7E806410098198011.*7E906410098180001|7E906410098180001.*7E806410098198011"]),
    ("ATSH7E0", [r"OK"]),
    ("ATH0", [r"OK"]),
]


class ElmConnection:
    def write(self, data):
        raise NotImplementedError

    def read_until_prompt(self, timeout):
        raise NotImplementedError

    def drain(self, duration=0.15):
        pass

    def close(self):
        pass


class TcpElmConnection(ElmConnection):
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=5.0)
        self.sock.settimeout(0.05)
        self.buffer = bytearray()

    def write(self, data):
        self.sock.sendall(data)

    def read_until_prompt(self, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if b">" in self.buffer:
                index = self.buffer.index(b">") + 1
                data = bytes(self.buffer[:index])
                del self.buffer[:index]
                return data

            try:
                data = self.sock.recv(256)
                if data:
                    self.buffer.extend(data)
            except socket.timeout:
                pass

        data = bytes(self.buffer)
        self.buffer.clear()
        return data

    def drain(self, duration=0.15):
        deadline = time.monotonic() + duration
        self.buffer.clear()
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(256)
                if data:
                    self.buffer.clear()
                    deadline = time.monotonic() + duration
            except socket.timeout:
                pass

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

    def drain(self, duration=0.15):
        deadline = time.monotonic() + duration
        self.serial.reset_input_buffer()
        while time.monotonic() < deadline:
            if self.serial.read(256):
                self.serial.reset_input_buffer()
                deadline = time.monotonic() + duration

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
        self.loop = None
        self.queue = None

    async def connect(self):
        self.loop = asyncio.get_running_loop()
        self.queue = asyncio.Queue()
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
        if self.loop is not None and self.queue is not None:
            self.loop.call_soon_threadsafe(self.queue.put_nowait, bytes(data))

    async def async_write(self, data):
        await self.client.write_gatt_char(OBDLINK_WRITE_UUID, data, response=False)

    async def async_read_until_prompt(self, timeout):
        deadline = time.monotonic() + timeout
        chunks = []
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            try:
                data = await asyncio.wait_for(self.queue.get(), timeout=remaining)
            except asyncio.TimeoutError:
                break

            chunks.append(data)
            if b">" in data:
                break

        return b"".join(chunks)

    async def async_drain(self, duration=0.15):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            try:
                await asyncio.wait_for(self.queue.get(), timeout=0.01)
                deadline = time.monotonic() + duration
            except asyncio.TimeoutError:
                pass

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


def run_sequence(
    conn,
    timeout,
    include_vin=False,
    include_invalid=False,
    include_formatting=False,
    include_identity=False,
    include_obdlink=False,
    include_dtc=False,
    include_freeze_frame=False,
    include_multi_ecu=False,
):
    failures = 0
    sequence = list(DEFAULT_SEQUENCE)

    if include_vin:
        sequence.extend(VIN_SEQUENCE)

    if include_invalid:
        sequence.extend(INVALID_SEQUENCE)

    if include_formatting:
        sequence.extend(FORMATTING_SEQUENCE)

    if include_identity:
        sequence.extend(IDENTITY_SEQUENCE)

    if include_obdlink:
        sequence.extend(OBDLINK_SEQUENCE)

    if include_dtc:
        sequence.extend(DTC_SEQUENCE)

    if include_freeze_frame:
        sequence.extend(FREEZE_FRAME_SEQUENCE)

    if include_multi_ecu:
        sequence.extend(MULTI_ECU_SEQUENCE)

    for command, patterns in sequence:
        conn.drain()
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


async def run_ble_sequence(
    conn,
    timeout,
    include_vin=False,
    include_invalid=False,
    include_formatting=False,
    include_identity=False,
    include_obdlink=False,
    include_dtc=False,
    include_freeze_frame=False,
    include_multi_ecu=False,
):
    failures = 0
    sequence = list(DEFAULT_SEQUENCE)

    if include_vin:
        sequence.extend(VIN_SEQUENCE)

    if include_invalid:
        sequence.extend(INVALID_SEQUENCE)

    if include_formatting:
        sequence.extend(FORMATTING_SEQUENCE)

    if include_identity:
        sequence.extend(IDENTITY_SEQUENCE)

    if include_obdlink:
        sequence.extend(OBDLINK_SEQUENCE)

    if include_dtc:
        sequence.extend(DTC_SEQUENCE)

    if include_freeze_frame:
        sequence.extend(FREEZE_FRAME_SEQUENCE)

    if include_multi_ecu:
        sequence.extend(MULTI_ECU_SEQUENCE)

    await conn.connect()
    try:
        await conn.async_drain()
        for command, patterns in sequence:
            await conn.async_drain()
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
    serial_parser.add_argument("--formatting", action="store_true", help="also test spaced and mixed-case ELM commands")
    serial_parser.add_argument("--identity", action="store_true", help="also test adapter identity and capability probes")
    serial_parser.add_argument("--obdlink", action="store_true", help="also test public OBDLink/STN FRPM identity probes")
    serial_parser.add_argument("--dtc", action="store_true", help="also test OBD DTC services 03, 07, and 0A")
    serial_parser.add_argument("--freeze-frame", action="store_true", help="also test OBD freeze-frame service 02")
    serial_parser.add_argument("--multi-ecu", action="store_true", help="also test multiple ECU responses to a functional request")

    tcp_parser = subparsers.add_parser("tcp")
    tcp_parser.add_argument("--host", required=True)
    tcp_parser.add_argument("--port", type=int, default=35000)
    tcp_parser.add_argument("--vin", action="store_true", help="also test ISO-TP VIN response with 0902")
    tcp_parser.add_argument("--invalid", action="store_true", help="also test invalid-command rejection")
    tcp_parser.add_argument("--formatting", action="store_true", help="also test spaced and mixed-case ELM commands")
    tcp_parser.add_argument("--identity", action="store_true", help="also test adapter identity and capability probes")
    tcp_parser.add_argument("--obdlink", action="store_true", help="also test public OBDLink/STN FRPM identity probes")
    tcp_parser.add_argument("--dtc", action="store_true", help="also test OBD DTC services 03, 07, and 0A")
    tcp_parser.add_argument("--freeze-frame", action="store_true", help="also test OBD freeze-frame service 02")
    tcp_parser.add_argument("--multi-ecu", action="store_true", help="also test multiple ECU responses to a functional request")

    ble_parser = subparsers.add_parser("ble")
    ble_parser.add_argument("--name")
    ble_parser.add_argument("--address")
    ble_parser.add_argument("--vin", action="store_true", help="also test ISO-TP VIN response with 0902")
    ble_parser.add_argument("--invalid", action="store_true", help="also test invalid-command rejection")
    ble_parser.add_argument("--formatting", action="store_true", help="also test spaced and mixed-case ELM commands")
    ble_parser.add_argument("--identity", action="store_true", help="also test adapter identity and capability probes")
    ble_parser.add_argument("--obdlink", action="store_true", help="also test public OBDLink/STN FRPM identity probes")
    ble_parser.add_argument("--dtc", action="store_true", help="also test OBD DTC services 03, 07, and 0A")
    ble_parser.add_argument("--freeze-frame", action="store_true", help="also test OBD freeze-frame service 02")
    ble_parser.add_argument("--multi-ecu", action="store_true", help="also test multiple ECU responses to a functional request")

    parser.add_argument("--timeout", type=float, default=1.5)

    args = parser.parse_args(argv)

    if args.transport == "ble":
        conn = BleElmConnection(args.name, args.address)
        failures = asyncio.run(
            run_ble_sequence(
                conn,
                args.timeout,
                args.vin,
                args.invalid,
                args.formatting,
                args.identity,
                args.obdlink,
                args.dtc,
                args.freeze_frame,
                args.multi_ecu,
            )
        )
        return 1 if failures else 0

    if args.transport == "serial":
        conn = SerialElmConnection(args.port, args.baud)
    else:
        conn = TcpElmConnection(args.host, args.port)

    try:
        failures = run_sequence(
            conn,
            args.timeout,
            args.vin,
            args.invalid,
            args.formatting,
            args.identity,
            args.obdlink,
            args.dtc,
            args.freeze_frame,
            args.multi_ecu,
        )
    finally:
        conn.close()

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
