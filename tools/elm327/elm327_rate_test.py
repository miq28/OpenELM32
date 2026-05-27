#!/usr/bin/env python3
"""Measure ELM327 request/response rate for one command over a transport."""

import argparse
import asyncio
import statistics
import time

from elm327_compat_test import (
    BleElmConnection,
    SerialElmConnection,
    TcpElmConnection,
    normalize,
    printable,
)


INIT_COMMANDS = ["ATZ", "ATE0", "ATL0", "ATS0", "ATH0", "ATSP6"]


def response_ok(compact, expected):
    if not compact or "NODATA" in compact or "?" in compact:
        return False
    return expected.upper() in compact if expected else compact.endswith(">")


def summarize(name, command, latencies, failures):
    total = sum(latencies)
    count = len(latencies)
    rate = count / total if total > 0 else 0.0
    sorted_ms = [value * 1000.0 for value in sorted(latencies)]
    avg = statistics.mean(sorted_ms) if sorted_ms else 0.0
    median = statistics.median(sorted_ms) if sorted_ms else 0.0
    p95 = sorted_ms[int((len(sorted_ms) - 1) * 0.95)] if sorted_ms else 0.0

    print(f"\n=== {name} rate ===")
    print(f"command       : {command}")
    print(f"successful    : {count}")
    print(f"failures      : {failures}")
    print(f"elapsed       : {total:.3f} s")
    print(f"rate          : {rate:.1f} pids/s")
    print(f"latency avg   : {avg:.1f} ms")
    print(f"latency median: {median:.1f} ms")
    print(f"latency p95   : {p95:.1f} ms")


def send_command(conn, command, timeout):
    conn.write(command.encode("ascii") + b"\r")
    raw = conn.read_until_prompt(timeout)
    return printable(raw), normalize(raw)


def init_adapter(conn, timeout, quiet, header):
    conn.drain()
    for command in [*INIT_COMMANDS, f"ATSH{header}"]:
        shown, _compact = send_command(conn, command, timeout)
        if not quiet:
            print(f"INIT {command:<7} {shown}")


def run_rate(conn, args, name):
    failures = 0
    latencies = []

    init_adapter(conn, args.timeout, args.quiet, args.header)
    conn.drain()

    for index in range(args.count):
        start = time.perf_counter()
        shown, compact = send_command(conn, args.command, args.timeout)
        elapsed = time.perf_counter() - start

        ok = response_ok(compact, args.expect)
        if ok:
            latencies.append(elapsed)
        else:
            failures += 1

        if args.verbose:
            status = "OK" if ok else "FAIL"
            print(f"{status} {index + 1:04d} {elapsed * 1000:.1f} ms {shown}")

        if args.delay > 0:
            time.sleep(args.delay)

    summarize(name, args.command, latencies, failures)


async def send_ble_command(conn, command, timeout):
    await conn.async_write(command.encode("ascii") + b"\r")
    raw = await conn.async_read_until_prompt(timeout)
    return printable(raw), normalize(raw)


async def init_ble_adapter(conn, timeout, quiet, header):
    await conn.async_drain()
    for command in [*INIT_COMMANDS, f"ATSH{header}"]:
        shown, _compact = await send_ble_command(conn, command, timeout)
        if not quiet:
            print(f"INIT {command:<7} {shown}")


async def run_ble_rate(conn, args):
    failures = 0
    latencies = []

    await conn.connect()
    try:
        await init_ble_adapter(conn, args.timeout, args.quiet, args.header)
        await conn.async_drain()

        for index in range(args.count):
            start = time.perf_counter()
            shown, compact = await send_ble_command(conn, args.command, args.timeout)
            elapsed = time.perf_counter() - start

            ok = response_ok(compact, args.expect)
            if ok:
                latencies.append(elapsed)
            else:
                failures += 1

            if args.verbose:
                status = "OK" if ok else "FAIL"
                print(f"{status} {index + 1:04d} {elapsed * 1000:.1f} ms {shown}")

            if args.delay > 0:
                await asyncio.sleep(args.delay)
    finally:
        await conn.async_close()

    summarize("ble", args.command, latencies, failures)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Measure ELM327 PID request rate")
    parser.add_argument("--count", type=int, default=100)
    parser.add_argument("--command", default="010C")
    parser.add_argument("--expect", default="410C")
    parser.add_argument("--header", default="7DF", help="CAN request header, for example 7DF or 7E0")
    parser.add_argument("--timeout", type=float, default=1.0)
    parser.add_argument("--delay", type=float, default=0.0, help="extra delay between requests")
    parser.add_argument("--quiet", action="store_true", help="hide init command responses")
    parser.add_argument("--verbose", action="store_true", help="print each response")

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

    if args.count <= 0:
        parser.error("--count must be greater than zero")

    if args.transport == "serial":
        conn = SerialElmConnection(args.port, args.baud)
        try:
            run_rate(conn, args, "serial")
        finally:
            conn.close()
    elif args.transport == "tcp":
        conn = TcpElmConnection(args.host, args.port)
        try:
            run_rate(conn, args, "tcp")
        finally:
            conn.close()
    else:
        conn = BleElmConnection(args.name, args.address)
        asyncio.run(run_ble_rate(conn, args))


if __name__ == "__main__":
    main()
