#!/usr/bin/env python3
"""Validate a WeAct CAN binary log and export it as SavvyCAN CSV."""

import argparse
import csv
import struct
import sys
from pathlib import Path


HEADER = struct.Struct("<8sHHH16sH")
RECORD = struct.Struct("<IIQIBBBB8sHH")
RECORD_MAGIC = 0x314E4143
RECORD_END = 0xA55A


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def decode(input_path: Path, output_path: Path) -> tuple[int, str | None]:
    valid_records = 0
    tail_error = None

    with input_path.open("rb") as source:
        raw_header = source.read(HEADER.size)
        if len(raw_header) != HEADER.size:
            raise ValueError("file does not contain a complete CANLOG1 header")

        magic, version, header_size, record_size, _, stored_crc = HEADER.unpack(raw_header)
        if magic.rstrip(b"\0") != b"CANLOG1":
            raise ValueError("not a CANLOG1 file")
        if version != 1 or header_size != HEADER.size or record_size != RECORD.size:
            raise ValueError("unsupported CAN log version or record size")
        if crc16(raw_header[:-2]) != stored_crc:
            raise ValueError("header CRC is invalid")

        with output_path.open("w", newline="", encoding="ascii") as output:
            writer = csv.writer(output)
            writer.writerow(
                ["Time Stamp", "ID", "Extended", "Dir", "Bus", "LEN", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8"]
            )

            while True:
                raw_record = source.read(RECORD.size)
                if not raw_record:
                    break
                if len(raw_record) != RECORD.size:
                    tail_error = f"ignored {len(raw_record)}-byte incomplete tail"
                    break

                fields = RECORD.unpack(raw_record)
                magic, sequence, timestamp_us, can_id, bus, flags, dlc, _, data, stored_crc, end = fields
                if magic != RECORD_MAGIC or end != RECORD_END or crc16(raw_record[:-4]) != stored_crc:
                    tail_error = f"stopped at invalid record index {valid_records}"
                    break

                dlc = min(dlc, 8)
                data_columns = [f"{value:02X}" for value in data[:dlc]]
                data_columns.extend([""] * (8 - dlc))
                writer.writerow(
                    [
                        timestamp_us,
                        f"{can_id:08X}",
                        "true" if flags & 0x01 else "false",
                        "Tx" if flags & 0x04 else "Rx",
                        bus,
                        dlc,
                        *data_columns,
                        "",  # SavvyCAN's own CSV exporter terminates data rows with a comma.
                    ]
                )
                valid_records += 1

    return valid_records, tail_error


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path, help="CANLOG1 .BIN file")
    parser.add_argument("output", nargs="?", type=Path, help="output CSV path")
    args = parser.parse_args()
    output_path = args.output or args.input.with_suffix(".csv")

    try:
        count, warning = decode(args.input, output_path)
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    print(f"wrote {count} valid records to {output_path}")
    if warning:
        print(f"warning: {warning}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
