#!/usr/bin/env python3
"""Run ELM327 compatibility checks sequentially across transports."""

import argparse
import subprocess
import sys
import time


def run_command(name, command, allow_retry=False):
    print(f"\n=== {name} ===")
    print(" ".join(command))

    result = subprocess.run(command)

    if result.returncode != 0 and allow_retry:
        print(f"{name}: first run failed; retrying after serial warm-up")
        time.sleep(1.0)
        result = subprocess.run(command)

    status = "PASS" if result.returncode == 0 else "FAIL"
    return name, status, result.returncode


def main(argv):
    parser = argparse.ArgumentParser(description="Run ELM327 transport tests in sequence")
    parser.add_argument("--serial", help="serial port, for example COM5")
    parser.add_argument("--serial-baud", type=int, default=1000000)
    parser.add_argument("--tcp", help="TCP/WiFi host")
    parser.add_argument("--tcp-port", type=int, default=35000)
    parser.add_argument("--ble", help="BLE address")
    parser.add_argument("--vin", action="store_true")
    parser.add_argument("--invalid", action="store_true")
    parser.add_argument("--formatting", action="store_true")
    parser.add_argument("--identity", action="store_true")
    parser.add_argument("--obdlink", action="store_true")
    parser.add_argument("--dtc", action="store_true")
    parser.add_argument("--freeze-frame", action="store_true")
    parser.add_argument("--multi-ecu", action="store_true")
    parser.add_argument("--timeout", type=float, default=1.5)

    args = parser.parse_args(argv)

    tests = []
    common = ["--timeout", str(args.timeout)]
    flags = []
    if args.vin:
        flags.append("--vin")
    if args.invalid:
        flags.append("--invalid")
    if args.formatting:
        flags.append("--formatting")
    if args.identity:
        flags.append("--identity")
    if args.obdlink:
        flags.append("--obdlink")
    if args.dtc:
        flags.append("--dtc")
    if args.freeze_frame:
        flags.append("--freeze-frame")
    if args.multi_ecu:
        flags.append("--multi-ecu")

    # Serial first: opening USB serial can reset the ESP32 and produce boot noise.
    if args.serial:
        tests.append((
            "serial",
            [
                sys.executable,
                "elm327_compat_test.py",
                *common,
                "serial",
                "--port",
                args.serial,
                "--baud",
                str(args.serial_baud),
                *flags,
            ],
            True,
        ))

    if args.tcp:
        tests.append((
            "tcp",
            [
                sys.executable,
                "elm327_compat_test.py",
                *common,
                "tcp",
                "--host",
                args.tcp,
                "--port",
                str(args.tcp_port),
                *flags,
            ],
            False,
        ))

    if args.ble:
        tests.append((
            "ble",
            [
                sys.executable,
                "elm327_compat_test.py",
                *common,
                "ble",
                "--address",
                args.ble,
                *flags,
            ],
            False,
        ))

    if not tests:
        parser.error("provide at least one of --serial, --tcp, or --ble")

    results = [run_command(name, command, retry) for name, command, retry in tests]

    print("\n=== Summary ===")
    for name, status, _code in results:
        print(f"{name:<8} {status}")

    return 0 if all(code == 0 for _name, _status, code in results) else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
