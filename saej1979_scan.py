#!/usr/bin/env python3
"""Read-only SAE J1979 PID scanner for an ELM327-compatible adapter."""

import argparse
import asyncio
import csv
import json
import re
import sys
import time

from elm327_compat_test import (
    BleElmConnection,
    SerialElmConnection,
    TcpElmConnection,
    normalize,
    printable,
)


MODE01_NAMES = {
    0x01: "Monitor status since DTCs cleared",
    0x02: "Freeze DTC",
    0x03: "Fuel system status",
    0x04: "Calculated engine load",
    0x05: "Engine coolant temperature",
    0x06: "Short term fuel trim bank 1",
    0x07: "Long term fuel trim bank 1",
    0x08: "Short term fuel trim bank 2",
    0x09: "Long term fuel trim bank 2",
    0x0A: "Fuel pressure",
    0x0B: "Intake manifold absolute pressure",
    0x0C: "Engine RPM",
    0x0D: "Vehicle speed",
    0x0E: "Timing advance",
    0x0F: "Intake air temperature",
    0x10: "Mass air flow",
    0x11: "Throttle position",
    0x12: "Commanded secondary air status",
    0x13: "Oxygen sensors present",
    0x14: "O2 sensor 1 voltage / trim",
    0x15: "O2 sensor 2 voltage / trim",
    0x16: "O2 sensor 3 voltage / trim",
    0x17: "O2 sensor 4 voltage / trim",
    0x18: "O2 sensor 5 voltage / trim",
    0x19: "O2 sensor 6 voltage / trim",
    0x1A: "O2 sensor 7 voltage / trim",
    0x1B: "O2 sensor 8 voltage / trim",
    0x1C: "OBD standards",
    0x1F: "Run time since engine start",
    0x21: "Distance traveled with MIL on",
    0x2F: "Fuel tank level",
    0x30: "Warm-ups since DTCs cleared",
    0x31: "Distance since DTCs cleared",
    0x33: "Barometric pressure",
    0x42: "Control module voltage",
    0x46: "Ambient air temperature",
    0x49: "Accelerator pedal position D",
    0x4A: "Accelerator pedal position E",
    0x4C: "Commanded throttle actuator",
    0x51: "Fuel type",
    0x5C: "Engine oil temperature",
    0x5E: "Engine fuel rate",
    0x62: "Actual engine percent torque",
    0x63: "Engine reference torque",
    0x7F: "Engine run time",
    0xA6: "Odometer",
    0xD3: "Engine odometer",
}

MODE09_NAMES = {
    0x01: "VIN message count",
    0x02: "Vehicle identification number",
    0x03: "Calibration ID message count",
    0x04: "Calibration ID",
    0x05: "CVN message count",
    0x06: "Calibration verification number",
    0x08: "In-use performance tracking",
    0x0A: "ECU name",
}

SUPPORT_BLOCKS = set(range(0x00, 0xE1, 0x20))
MODE06_SUPPORT_BLOCKS = set(range(0x00, 0xE1, 0x20))


def payload_bytes(payload):
    if payload is None or len(payload) % 2 != 0:
        return []
    return [int(payload[i:i + 2], 16) for i in range(0, len(payload), 2)]


def pct(byte):
    return byte * 100 / 255


def fuel_trim(byte):
    return byte * 100 / 128 - 100


def signed_byte(byte):
    return byte - 256 if byte >= 128 else byte


def u16(data, index=0):
    return (data[index] << 8) | data[index + 1]


def decode_mode01(pid, payload):
    data = payload_bytes(payload)
    if not data:
        return ""

    try:
        if pid == 0x01 and len(data) >= 4:
            return f"MIL={'on' if data[0] & 0x80 else 'off'}, DTC count={data[0] & 0x7F}"
        if pid == 0x02 and len(data) >= 2:
            return f"0x{u16(data):04X}"
        if pid == 0x03 and len(data) >= 2:
            return f"fuel system 1=0x{data[0]:02X}, fuel system 2=0x{data[1]:02X}"
        if pid == 0x04:
            return f"{pct(data[0]):.1f} %"
        if pid in (0x05, 0x0F, 0x46, 0x5C, 0x84):
            return f"{data[0] - 40} deg C"
        if pid in (0x06, 0x07, 0x08, 0x09):
            return f"{fuel_trim(data[0]):.1f} %"
        if pid == 0x0A:
            return f"{data[0] * 3} kPa"
        if pid in (0x0B, 0x33):
            return f"{data[0]} kPa"
        if pid == 0x0C and len(data) >= 2:
            return f"{u16(data) / 4:.0f} rpm"
        if pid in (0x0D, 0xAA):
            return f"{data[0]} km/h"
        if pid == 0x0E:
            return f"{data[0] / 2 - 64:.1f} deg"
        if pid == 0x10 and len(data) >= 2:
            return f"{u16(data) / 100:.2f} g/s"
        if pid in (0x11, 0x2F, 0x45, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x52, 0x5A):
            return f"{pct(data[0]):.1f} %"
        if 0x14 <= pid <= 0x1B and len(data) >= 2:
            return f"{data[0] / 200:.3f} V, trim={fuel_trim(data[1]):.1f} %"
        if pid in (0x1F, 0x21, 0x31) and len(data) >= 2:
            return f"{u16(data)} {'s' if pid == 0x1F else 'km'}"
        if pid == 0x2C:
            return f"{pct(data[0]):.1f} %"
        if pid in (0x30, 0x4D, 0x4E):
            return f"{data[0]}"
        if pid == 0x42 and len(data) >= 2:
            return f"{u16(data) / 1000:.3f} V"
        if pid == 0x44 and len(data) >= 2:
            return f"{u16(data) / 32768:.3f} lambda"
        if pid == 0x51:
            return f"0x{data[0]:02X}"
        if pid == 0x5E and len(data) >= 2:
            return f"{u16(data) / 20:.2f} L/h"
        if pid == 0x62:
            return f"{signed_byte(data[0])} %"
        if pid == 0x63 and len(data) >= 2:
            return f"{u16(data)} Nm"
        if pid in (0xA6, 0xD3) and len(data) >= 4:
            value = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3]
            return f"{value / 10:.1f} km"
    except (IndexError, ValueError):
        return ""

    return ""


def decode_ascii_payload(payload, skip_prefix_bytes=0):
    data = payload_bytes(payload)
    if skip_prefix_bytes:
        data = data[skip_prefix_bytes:]
    text = "".join(chr(byte) for byte in data if 32 <= byte <= 126)
    return text.strip()


def decode_mode09(info, compact):
    if info == 0x02:
        payload = response_payload(compact, 0x09, info, 2)
        return decode_ascii_payload(payload, 1)
    if info in (0x04, 0x0A):
        payload = response_payload(compact, 0x09, info, 2)
        return decode_ascii_payload(payload, 1)
    return ""


def decode_mode02(pid, payload):
    if payload is None or len(payload) < 2:
        return ""

    frame_number = payload[:2]
    value = decode_mode01(pid, payload[2:])
    if value:
        return f"frame {int(frame_number, 16)}: {value}"
    return f"frame {int(frame_number, 16)}"


def response_payload(compact, service, pid, min_data_nibbles=0):
    positive_sid = service + 0x40
    pattern = rf"{positive_sid:02X}{pid:02X}([0-9A-F]{{{min_data_nibbles},}})"
    match = re.search(pattern, compact)
    return match.group(1) if match else None


def supported_from_bitmap(block_pid, bitmap_hex):
    if len(bitmap_hex) < 8:
        return []

    value = int(bitmap_hex[:8], 16)
    supported = []

    for bit_index in range(32):
        if value & (1 << (31 - bit_index)):
            supported.append(block_pid + bit_index + 1)

    return supported


def open_connection(args):
    if args.transport == "serial":
        return SerialElmConnection(args.port, args.baud)

    if args.transport == "tcp":
        return TcpElmConnection(args.host, args.port)

    return BleElmConnection(args.name, args.address)


def send_command(conn, command, timeout):
    conn.drain()
    conn.write(command.encode("ascii") + b"\r")
    raw = conn.read_until_prompt(timeout)
    return printable(raw), normalize(raw)


async def send_ble_command(conn, command, timeout):
    await conn.async_drain()
    await conn.async_write(command.encode("ascii") + b"\r")
    raw = await conn.async_read_until_prompt(timeout)
    return printable(raw), normalize(raw)


def record_result(results, section, command, name, shown, compact, supported=True, value=""):
    status = "OK" if supported and "NODATA" not in compact and "?" not in compact else "NO DATA"
    if "?" in compact:
        status = "ERROR"

    row = {
        "section": section,
        "command": command,
        "name": name,
        "value": value,
        "status": status,
        "response": shown,
    }
    results.append(row)
    value_text = f" {value}" if value else ""
    print(f"{status:<7} {command:<6} {shown:<42} {name}{value_text}")


def discover_mode01(conn, timeout):
    supported = set()
    support_rows = []
    block = 0x00

    while block <= 0xE0:
        command = f"01{block:02X}"
        shown, compact = send_command(conn, command, timeout)
        payload = response_payload(compact, 0x01, block, 8)
        block_supported = []

        if payload is not None:
            block_supported = supported_from_bitmap(block, payload)
            supported.update(pid for pid in block_supported if pid not in SUPPORT_BLOCKS)

        support_rows.append((command, block, shown, compact, block_supported))

        next_block = block + 0x20
        if next_block not in block_supported:
            break

        block = next_block

    return sorted(supported), support_rows


async def discover_mode01_ble(conn, timeout):
    supported = set()
    support_rows = []
    block = 0x00

    while block <= 0xE0:
        command = f"01{block:02X}"
        shown, compact = await send_ble_command(conn, command, timeout)
        payload = response_payload(compact, 0x01, block, 8)
        block_supported = []

        if payload is not None:
            block_supported = supported_from_bitmap(block, payload)
            supported.update(pid for pid in block_supported if pid not in SUPPORT_BLOCKS)

        support_rows.append((command, block, shown, compact, block_supported))

        next_block = block + 0x20
        if next_block not in block_supported:
            break

        block = next_block

    return sorted(supported), support_rows


def discover_mode09(conn, timeout):
    shown, compact = send_command(conn, "0900", timeout)
    payload = response_payload(compact, 0x09, 0x00, 8)
    supported = supported_from_bitmap(0x00, payload) if payload is not None else []
    return [info for info in supported if info != 0x00], shown, compact


async def discover_mode09_ble(conn, timeout):
    shown, compact = await send_ble_command(conn, "0900", timeout)
    payload = response_payload(compact, 0x09, 0x00, 8)
    supported = supported_from_bitmap(0x00, payload) if payload is not None else []
    return [info for info in supported if info != 0x00], shown, compact


def discover_mode02(conn, timeout):
    supported = set()
    support_rows = []
    block = 0x00

    while block <= 0xE0:
        command = f"02{block:02X}"
        shown, compact = send_command(conn, command, timeout)
        payload = response_payload(compact, 0x02, block, 8)
        block_supported = []

        if payload is not None:
            block_supported = supported_from_bitmap(block, payload)
            supported.update(pid for pid in block_supported if pid not in SUPPORT_BLOCKS)

        support_rows.append((command, block, shown, compact, block_supported))

        next_block = block + 0x20
        if next_block not in block_supported:
            break

        block = next_block

    return sorted(supported), support_rows


async def discover_mode02_ble(conn, timeout):
    supported = set()
    support_rows = []
    block = 0x00

    while block <= 0xE0:
        command = f"02{block:02X}"
        shown, compact = await send_ble_command(conn, command, timeout)
        payload = response_payload(compact, 0x02, block, 8)
        block_supported = []

        if payload is not None:
            block_supported = supported_from_bitmap(block, payload)
            supported.update(pid for pid in block_supported if pid not in SUPPORT_BLOCKS)

        support_rows.append((command, block, shown, compact, block_supported))

        next_block = block + 0x20
        if next_block not in block_supported:
            break

        block = next_block

    return sorted(supported), support_rows


def discover_mode06(conn, timeout):
    supported = set()
    support_rows = []
    block = 0x00

    while block <= 0xE0:
        command = f"06{block:02X}"
        shown, compact = send_command(conn, command, timeout)
        payload = response_payload(compact, 0x06, block, 8)
        block_supported = []

        if payload is not None:
            block_supported = supported_from_bitmap(block, payload)
            supported.update(mid for mid in block_supported if mid not in MODE06_SUPPORT_BLOCKS)

        support_rows.append((command, block, shown, compact, block_supported))

        next_block = block + 0x20
        if next_block not in block_supported:
            break

        block = next_block

    return sorted(supported), support_rows


async def discover_mode06_ble(conn, timeout):
    supported = set()
    support_rows = []
    block = 0x00

    while block <= 0xE0:
        command = f"06{block:02X}"
        shown, compact = await send_ble_command(conn, command, timeout)
        payload = response_payload(compact, 0x06, block, 8)
        block_supported = []

        if payload is not None:
            block_supported = supported_from_bitmap(block, payload)
            supported.update(mid for mid in block_supported if mid not in MODE06_SUPPORT_BLOCKS)

        support_rows.append((command, block, shown, compact, block_supported))

        next_block = block + 0x20
        if next_block not in block_supported:
            break

        block = next_block

    return sorted(supported), support_rows


def init_adapter(conn, timeout, header):
    for command in ["ATZ", "ATE0", "ATL0", "ATS0", "ATH0", "ATSP6", f"ATSH{header}"]:
        shown, compact = send_command(conn, command, timeout)
        print(f"INIT    {command:<6} {shown}")
        time.sleep(0.05)


async def init_adapter_ble(conn, timeout, header):
    for command in ["ATZ", "ATE0", "ATL0", "ATS0", "ATH0", "ATSP6", f"ATSH{header}"]:
        shown, compact = await send_ble_command(conn, command, timeout)
        print(f"INIT    {command:<6} {shown}")
        await asyncio.sleep(0.05)


def run_scan(conn, args):
    results = []
    init_adapter(conn, args.timeout, args.header)

    print("\n--- MODE 01 SUPPORT ---")
    supported_pids, support_rows = discover_mode01(conn, args.timeout)
    for command, block, shown, compact, block_supported in support_rows:
        name = f"Supported PIDs {block + 1:02X}-{block + 0x20:02X}"
        record_result(results, "mode01-support", command, name, shown, compact)

    print("\n--- MODE 01 SUPPORTED PIDS ---")
    for pid in supported_pids:
        command = f"01{pid:02X}"
        shown, compact = send_command(conn, command, args.timeout)
        name = MODE01_NAMES.get(pid, "SAE J1979 PID")
        payload = response_payload(compact, 0x01, pid)
        value = decode_mode01(pid, payload)
        record_result(results, "mode01", command, name, shown, compact, value=value)
        time.sleep(args.delay)

    if args.mode02:
        print("\n--- MODE 02 FREEZE FRAME SUPPORT ---")
        supported_pids, support_rows = discover_mode02(conn, args.timeout)
        for command, block, shown, compact, block_supported in support_rows:
            name = f"Freeze-frame PIDs {block + 1:02X}-{block + 0x20:02X}"
            record_result(results, "mode02-support", command, name, shown, compact)

        print("\n--- MODE 02 FREEZE FRAME DATA ---")
        for pid in supported_pids:
            command = f"02{pid:02X}00"
            shown, compact = send_command(conn, command, args.timeout)
            name = MODE01_NAMES.get(pid, "SAE J1979 freeze-frame PID")
            payload = response_payload(compact, 0x02, pid)
            value = decode_mode02(pid, payload)
            record_result(results, "mode02", command, name, shown, compact, value=value)
            time.sleep(args.delay)

    if args.mode06:
        print("\n--- MODE 06 MONITOR SUPPORT ---")
        supported_mids, support_rows = discover_mode06(conn, args.timeout)
        for command, block, shown, compact, block_supported in support_rows:
            name = f"Monitor IDs {block + 1:02X}-{block + 0x20:02X}"
            record_result(results, "mode06-support", command, name, shown, compact)

        print("\n--- MODE 06 MONITOR RESULTS ---")
        for mid in supported_mids:
            command = f"06{mid:02X}"
            shown, compact = send_command(conn, command, args.timeout)
            name = f"On-board monitor test ID ${mid:02X}"
            record_result(results, "mode06", command, name, shown, compact)
            time.sleep(args.delay)

    if args.mode09:
        print("\n--- MODE 09 INFO TYPES ---")
        supported_infos, shown, compact = discover_mode09(conn, args.timeout)
        record_result(results, "mode09-support", "0900", "Supported info types", shown, compact)

        for info in supported_infos:
            command = f"09{info:02X}"
            shown, compact = send_command(conn, command, args.timeout)
            name = MODE09_NAMES.get(info, "SAE J1979 info type")
            value = decode_mode09(info, compact)
            record_result(results, "mode09", command, name, shown, compact, value=value)
            time.sleep(args.delay)

    if args.dtc:
        print("\n--- DTC READS ---")
        for command, name in [("03", "Stored DTCs"), ("07", "Pending DTCs"), ("0A", "Permanent DTCs")]:
            shown, compact = send_command(conn, command, args.timeout)
            record_result(results, "dtc", command, name, shown, compact)
            time.sleep(args.delay)

    return results


async def run_ble_scan(conn, args):
    results = []
    await conn.connect()
    try:
        await init_adapter_ble(conn, args.timeout, args.header)

        print("\n--- MODE 01 SUPPORT ---")
        supported_pids, support_rows = await discover_mode01_ble(conn, args.timeout)
        for command, block, shown, compact, block_supported in support_rows:
            name = f"Supported PIDs {block + 1:02X}-{block + 0x20:02X}"
            record_result(results, "mode01-support", command, name, shown, compact)

        print("\n--- MODE 01 SUPPORTED PIDS ---")
        for pid in supported_pids:
            command = f"01{pid:02X}"
            shown, compact = await send_ble_command(conn, command, args.timeout)
            name = MODE01_NAMES.get(pid, "SAE J1979 PID")
            payload = response_payload(compact, 0x01, pid)
            value = decode_mode01(pid, payload)
            record_result(results, "mode01", command, name, shown, compact, value=value)
            await asyncio.sleep(args.delay)

        if args.mode02:
            print("\n--- MODE 02 FREEZE FRAME SUPPORT ---")
            supported_pids, support_rows = await discover_mode02_ble(conn, args.timeout)
            for command, block, shown, compact, block_supported in support_rows:
                name = f"Freeze-frame PIDs {block + 1:02X}-{block + 0x20:02X}"
                record_result(results, "mode02-support", command, name, shown, compact)

            print("\n--- MODE 02 FREEZE FRAME DATA ---")
            for pid in supported_pids:
                command = f"02{pid:02X}00"
                shown, compact = await send_ble_command(conn, command, args.timeout)
                name = MODE01_NAMES.get(pid, "SAE J1979 freeze-frame PID")
                payload = response_payload(compact, 0x02, pid)
                value = decode_mode02(pid, payload)
                record_result(results, "mode02", command, name, shown, compact, value=value)
                await asyncio.sleep(args.delay)

        if args.mode06:
            print("\n--- MODE 06 MONITOR SUPPORT ---")
            supported_mids, support_rows = await discover_mode06_ble(conn, args.timeout)
            for command, block, shown, compact, block_supported in support_rows:
                name = f"Monitor IDs {block + 1:02X}-{block + 0x20:02X}"
                record_result(results, "mode06-support", command, name, shown, compact)

            print("\n--- MODE 06 MONITOR RESULTS ---")
            for mid in supported_mids:
                command = f"06{mid:02X}"
                shown, compact = await send_ble_command(conn, command, args.timeout)
                name = f"On-board monitor test ID ${mid:02X}"
                record_result(results, "mode06", command, name, shown, compact)
                await asyncio.sleep(args.delay)

        if args.mode09:
            print("\n--- MODE 09 INFO TYPES ---")
            supported_infos, shown, compact = await discover_mode09_ble(conn, args.timeout)
            record_result(results, "mode09-support", "0900", "Supported info types", shown, compact)

            for info in supported_infos:
                command = f"09{info:02X}"
                shown, compact = await send_ble_command(conn, command, args.timeout)
                name = MODE09_NAMES.get(info, "SAE J1979 info type")
                value = decode_mode09(info, compact)
                record_result(results, "mode09", command, name, shown, compact, value=value)
                await asyncio.sleep(args.delay)

        if args.dtc:
            print("\n--- DTC READS ---")
            for command, name in [("03", "Stored DTCs"), ("07", "Pending DTCs"), ("0A", "Permanent DTCs")]:
                shown, compact = await send_ble_command(conn, command, args.timeout)
                record_result(results, "dtc", command, name, shown, compact)
                await asyncio.sleep(args.delay)
    finally:
        await conn.async_close()

    return results


def write_outputs(results, args):
    if args.csv:
        with open(args.csv, "w", newline="", encoding="utf-8") as out_file:
            writer = csv.DictWriter(out_file, fieldnames=["section", "command", "name", "value", "status", "response"])
            writer.writeheader()
            writer.writerows(results)

    if args.json:
        with open(args.json, "w", encoding="utf-8") as out_file:
            json.dump(results, out_file, indent=2)


def main(argv):
    parser = argparse.ArgumentParser(description="Read-only SAE J1979 PID scanner")
    parser.add_argument("--timeout", type=float, default=1.5)
    parser.add_argument("--delay", type=float, default=0.05)
    parser.add_argument("--header", default="7DF", help="request header, usually 7DF or 7E0")
    parser.add_argument("--all-readonly", action="store_true", help="enable Mode 02, Mode 06, Mode 09, and DTC reads")
    parser.add_argument("--baseline-only", action="store_true", help="only scan Mode 01 live-data support and supported PIDs")
    parser.add_argument("--mode02", action="store_true", help="also scan supported Mode 02 freeze-frame PIDs")
    parser.add_argument("--mode06", action="store_true", help="also scan supported Mode 06 monitor IDs")
    parser.add_argument("--mode09", action="store_true", help="also scan supported Mode 09 info types")
    parser.add_argument("--dtc", action="store_true", help="also read DTC services 03, 07, and 0A")
    parser.add_argument("--csv", help="write results to CSV")
    parser.add_argument("--json", help="write results to JSON")

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

    selected_extra_groups = args.mode02 or args.mode06 or args.mode09 or args.dtc
    if args.all_readonly and args.baseline_only:
        parser.error("--all-readonly and --baseline-only cannot be used together")

    if args.all_readonly or not (args.baseline_only or selected_extra_groups):
        args.mode02 = True
        args.mode06 = True
        args.mode09 = True
        args.dtc = True

    conn = open_connection(args)

    try:
        if args.transport == "ble":
            results = asyncio.run(run_ble_scan(conn, args))
        else:
            results = run_scan(conn, args)
    finally:
        if args.transport != "ble":
            conn.close()

    write_outputs(results, args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
