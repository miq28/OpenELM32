#!/usr/bin/env python3
"""Guarded OBD-II Mode 04 clear-DTC utility.

This is intentionally separate from the SAE J1979 scanner because Mode 04
changes vehicle state. By default it only prints the commands it would send.
"""

import argparse
import asyncio
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "elm327"))

from elm327_compat_test import (
    BleElmConnection,
    SerialElmConnection,
    TcpElmConnection,
    normalize,
    printable,
)


CONFIRM_PHRASE = "CLEAR-DTC"


def open_connection(args):
    if args.transport == "serial":
        return SerialElmConnection(args.port, args.baud)

    if args.transport == "tcp":
        return TcpElmConnection(args.host, args.port)

    return BleElmConnection(args.name, args.address)


def command_plan(header):
    return [
        ("ATZ", "Reset ELM emulator state"),
        ("ATE0", "Disable command echo for cleaner responses"),
        ("ATL0", "Disable linefeed characters"),
        ("ATS0", "Disable spaces in hex responses"),
        ("ATH0", "Disable CAN headers in app-facing responses"),
        ("ATSP6", "Select ISO 15765-4 CAN 11-bit 500 kbit/s"),
        (f"ATSH{header}", f"Set request header to {header}"),
        ("03", "Read stored emission-related DTCs before clear"),
        ("07", "Read pending emission-related DTCs before clear"),
        ("0A", "Read permanent emission-related DTCs before clear"),
        ("04", "Clear/reset emission-related DTC information"),
        ("03", "Read stored DTCs after clear"),
        ("07", "Read pending DTCs after clear"),
        ("0A", "Read permanent DTCs after clear"),
    ]


def print_dry_run(args):
    print("DRY RUN: no commands sent.")
    print("This command plan clears DTCs only if run with --execute and the confirmation phrase.")
    print()
    for command, purpose in command_plan(args.header):
        print(f"{command:<8} {purpose}")


def response_status(command, compact):
    if command == "04" and "44" in compact:
        return "CLEARED"
    if "?" in compact:
        return "ERROR"
    if "NODATA" in compact:
        return "NO DATA"
    return "OK"


def send_command(conn, command, purpose, timeout):
    conn.drain()
    conn.write(command.encode("ascii") + b"\r")
    raw = conn.read_until_prompt(timeout)
    shown = printable(raw)
    compact = normalize(raw)
    print(f"{response_status(command, compact):<8} {command:<8} {shown:<32} {purpose}")
    return compact


async def send_ble_command(conn, command, purpose, timeout):
    await conn.async_drain()
    await conn.async_write(command.encode("ascii") + b"\r")
    raw = await conn.async_read_until_prompt(timeout)
    shown = printable(raw)
    compact = normalize(raw)
    print(f"{response_status(command, compact):<8} {command:<8} {shown:<32} {purpose}")
    return compact


def run_clear(conn, args):
    print("WARNING: sending Mode 04 can clear DTCs, freeze-frame data, and readiness-related state.")
    print("Reading DTCs before and after clear.")
    print()

    for command, purpose in command_plan(args.header):
        send_command(conn, command, purpose, args.timeout)
        time.sleep(args.delay)


async def run_ble_clear(conn, args):
    print("WARNING: sending Mode 04 can clear DTCs, freeze-frame data, and readiness-related state.")
    print("Reading DTCs before and after clear.")
    print()

    await conn.connect()
    try:
        for command, purpose in command_plan(args.header):
            await send_ble_command(conn, command, purpose, args.timeout)
            await asyncio.sleep(args.delay)
    finally:
        await conn.async_close()


def main(argv):
    parser = argparse.ArgumentParser(description="Guarded OBD-II Mode 04 clear-DTC utility")
    parser.add_argument("--timeout", type=float, default=1.5)
    parser.add_argument("--delay", type=float, default=0.05)
    parser.add_argument("--header", default="7DF", help="request header, usually 7DF or 7E0")
    parser.add_argument("--execute", action="store_true", help="actually send the clear-DTC command")
    parser.add_argument("--confirm", help=f"must be exactly {CONFIRM_PHRASE} when using --execute")

    subparsers = parser.add_subparsers(dest="transport", required=True)

    serial_parser = subparsers.add_parser("serial")
    serial_parser.add_argument("--port", required=True)
    serial_parser.add_argument("--baud", type=int, default=115200)

    tcp_parser = subparsers.add_parser("tcp")
    tcp_parser.add_argument("--host", required=True)
    tcp_parser.add_argument("--port", type=int, default=35000)

    ble_parser = subparsers.add_parser("ble")
    ble_parser.add_argument("--name")
    ble_parser.add_argument("--address")

    args = parser.parse_args(argv)

    if not args.execute:
        print_dry_run(args)
        return 0

    if args.confirm != CONFIRM_PHRASE:
        raise SystemExit(f"--execute requires --confirm {CONFIRM_PHRASE}")

    conn = open_connection(args)

    try:
        if args.transport == "ble":
            asyncio.run(run_ble_clear(conn, args))
        else:
            run_clear(conn, args)
    finally:
        if args.transport != "ble":
            conn.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
