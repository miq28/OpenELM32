import can
import time
import math
import random
import argparse

parser = argparse.ArgumentParser(description="OBD ECU simulator over SLCAN")
parser.add_argument("--port", "--comport", default="COM9", help="SLCAN serial port")
parser.add_argument("--channel", dest="port", help=argparse.SUPPRESS)
parser.add_argument(
    "--serial-baud",
    "--baud",
    "--tty-baudrate",
    dest="serial_baud",
    type=int,
    default=115200,
    help="SLCAN adapter serial baud rate",
)
parser.add_argument(
    "--protocol",
    type=int,
    choices=[6, 7, 8, 9],
    default=6,
    help="ELM protocol: 6=CAN11/500, 7=CAN29/500, 8=CAN11/250, 9=CAN29/250",
)
parser.add_argument("--bitrate", type=int, help="override CAN bitrate")
parser.add_argument(
    "--multi-ecu",
    action="store_true",
    help="reply from two ECUs to functional PID 0100 requests",
)
args = parser.parse_args()

PROTOCOLS = {
    6: {
        "name": "ISO 15765-4 CAN 11/500",
        "bitrate": 500000,
        "extended": False,
        "functional_request_id": 0x7DF,
        "physical_request_ids": {0x7E0: 0x7E8, 0x7E1: 0x7E9},
        "functional_response_ids": [0x7E8, 0x7E9],
    },
    7: {
        "name": "ISO 15765-4 CAN 29/500",
        "bitrate": 500000,
        "extended": True,
        "functional_request_id": 0x18DB33F1,
        "physical_request_ids": {
            0x18DA10F1: 0x18DAF110,
            0x18DA11F1: 0x18DAF111,
        },
        "functional_response_ids": [0x18DAF110, 0x18DAF111],
    },
    8: {
        "name": "ISO 15765-4 CAN 11/250",
        "bitrate": 250000,
        "extended": False,
        "functional_request_id": 0x7DF,
        "physical_request_ids": {0x7E0: 0x7E8, 0x7E1: 0x7E9},
        "functional_response_ids": [0x7E8, 0x7E9],
    },
    9: {
        "name": "ISO 15765-4 CAN 29/250",
        "bitrate": 250000,
        "extended": True,
        "functional_request_id": 0x18DB33F1,
        "physical_request_ids": {
            0x18DA10F1: 0x18DAF110,
            0x18DA11F1: 0x18DAF111,
        },
        "functional_response_ids": [0x18DAF110, 0x18DAF111],
    },
}

protocol = PROTOCOLS[args.protocol]
bitrate = args.bitrate or protocol["bitrate"]

bus = can.interface.Bus(
    interface="slcan",
    channel=args.port,
    bitrate=bitrate,
    ttyBaudrate=args.serial_baud,
)

print(
    f"Realistic ECU simulator started: {protocol['name']} on {args.port} "
    f"CAN @ {bitrate}, serial @ {args.serial_baud}"
)

start_time = time.time()

VIN = "JT2BG22K1V0123456"
DTC_P2304 = [0x23, 0x04]
dtcs_cleared = False


def build_msg(arbid, data):
    return can.Message(
        arbitration_id=arbid,
        data=data,
        is_extended_id=protocol["extended"],
    )


def send_msg(msg, delay=None):
    if delay is None:
        delay = random.uniform(0.005, 0.03)

    time.sleep(delay)
    bus.send(msg)
    print(f"TX: {msg}")


def send_functional_responses(responses):
    for response_id, data in responses:
        send_msg(build_msg(response_id, data), 0.001)


def build_dtc_response(response_id, service):
    if dtcs_cleared:
        return build_msg(response_id, [0x02, 0x40 | service, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])

    return build_msg(response_id, [0x05, 0x40 | service, 0x01, *DTC_P2304, 0x00, 0x00, 0x00])


while True:

    msg = bus.recv()

    if msg is None:
        continue

    now = time.time() - start_time

    print(f"RX: {msg}")

    if msg.is_extended_id != protocol["extended"]:
        continue

    functional_request = msg.arbitration_id == protocol["functional_request_id"]

    if functional_request:
        response_id = protocol["functional_response_ids"][0]
    elif msg.arbitration_id in protocol["physical_request_ids"]:
        response_id = protocol["physical_request_ids"][msg.arbitration_id]
    else:
        continue

    data = list(msg.data)

    resp = None

    # --------------------------------------------
    # simulated engine behavior
    # --------------------------------------------

    # RPM oscillates between ~750-2500
    rpm = int(1100 + math.sin(now * 0.4) * 700 + random.randint(-40, 40))

    rpm = max(720, rpm)

    # speed loosely follows rpm
    speed = int((rpm - 700) / 45)

    speed += random.randint(-2, 2)

    speed = max(0, min(speed, 130))

    # coolant warms up over time
    coolant_c = min(92, int(25 + now / 8))

    # throttle %
    throttle = int(12 + abs(math.sin(now * 0.7)) * 35)

    # engine load %
    engine_load = int(18 + abs(math.sin(now * 0.3)) * 55)

    # --------------------------------------------
    # MODE 01 PID 00
    # --------------------------------------------

    if data[:3] == [0x02, 0x01, 0x00]:

        if functional_request:
            primary_id = protocol["functional_response_ids"][0]
            secondary_id = protocol["functional_response_ids"][1]
            responses = [
                (primary_id, [0x06, 0x41, 0x00, 0x98, 0x19, 0x80, 0x11, 0x00]),
            ]

            if args.multi_ecu:
                responses.append(
                    (secondary_id, [0x06, 0x41, 0x00, 0x98, 0x18, 0x00, 0x01, 0x00])
                )

            send_functional_responses(responses)
            continue

        resp = build_msg(response_id, [0x06, 0x41, 0x00, 0x98, 0x19, 0x80, 0x11, 0x00])

    # --------------------------------------------
    # MODE 02 freeze frame data
    # --------------------------------------------

    elif len(data) >= 3 and data[1] == 0x02 and data[2] == 0x00:

        # Include PID 02 so apps can request the DTC that caused the freeze frame.
        resp = build_msg(response_id, [0x06, 0x42, 0x00, 0xD8, 0x19, 0x80, 0x11, 0x00])

    elif len(data) >= 3 and data[1] == 0x02 and data[2] == 0x02:

        resp = build_msg(response_id, [0x05, 0x42, 0x02, 0x00, *DTC_P2304, 0x00, 0x00])

    elif len(data) >= 3 and data[1] == 0x02 and data[2] == 0x01:

        mil_and_count = 0x00 if dtcs_cleared else 0x81
        resp = build_msg(response_id, [0x07, 0x42, 0x01, 0x00, mil_and_count, 0x07, 0x65, 0x04])

    elif len(data) >= 3 and data[1] == 0x02 and data[2] == 0x04:

        raw = int(engine_load * 255 / 100)
        resp = build_msg(response_id, [0x04, 0x42, 0x04, 0x00, raw, 0x00, 0x00, 0x00])

    elif len(data) >= 3 and data[1] == 0x02 and data[2] == 0x05:

        resp = build_msg(
            response_id, [0x04, 0x42, 0x05, 0x00, coolant_c + 40, 0x00, 0x00, 0x00]
        )

    elif len(data) >= 3 and data[1] == 0x02 and data[2] == 0x0C:

        raw = rpm * 4
        resp = build_msg(
            response_id,
            [0x05, 0x42, 0x0C, 0x00, (raw >> 8) & 0xFF, raw & 0xFF, 0x00, 0x00],
        )

    elif len(data) >= 3 and data[1] == 0x02 and data[2] == 0x0D:

        resp = build_msg(response_id, [0x04, 0x42, 0x0D, 0x00, speed, 0x00, 0x00, 0x00])

    elif len(data) >= 3 and data[1] == 0x02 and data[2] == 0x10:

        raw = 650
        resp = build_msg(
            response_id,
            [0x05, 0x42, 0x10, 0x00, (raw >> 8) & 0xFF, raw & 0xFF, 0x00, 0x00],
        )

    elif len(data) >= 3 and data[1] == 0x02 and data[2] == 0x11:

        raw = int(throttle * 255 / 100)
        resp = build_msg(response_id, [0x04, 0x42, 0x11, 0x00, raw, 0x00, 0x00, 0x00])

    elif len(data) >= 3 and data[1] == 0x02 and data[2] == 0x1C:

        resp = build_msg(response_id, [0x04, 0x42, 0x1C, 0x00, 0x06, 0x00, 0x00, 0x00])

    # --------------------------------------------
    # RPM
    # --------------------------------------------

    elif data[:3] == [0x02, 0x01, 0x0C]:

        raw = rpm * 4

        resp = build_msg(
            response_id,
            [0x04, 0x41, 0x0C, (raw >> 8) & 0xFF, raw & 0xFF, 0x00, 0x00, 0x00],
        )

    # --------------------------------------------
    # SPEED
    # --------------------------------------------

    elif data[:3] == [0x02, 0x01, 0x0D]:

        resp = build_msg(response_id, [0x03, 0x41, 0x0D, speed, 0x00, 0x00, 0x00, 0x00])

    # --------------------------------------------
    # COOLANT TEMP
    # formula = A - 40
    # --------------------------------------------

    elif data[:3] == [0x02, 0x01, 0x05]:

        resp = build_msg(
            response_id, [0x03, 0x41, 0x05, coolant_c + 40, 0x00, 0x00, 0x00, 0x00]
        )

    # --------------------------------------------
    # THROTTLE POSITION
    # --------------------------------------------

    elif data[:3] == [0x02, 0x01, 0x11]:

        raw = int(throttle * 255 / 100)

        resp = build_msg(response_id, [0x03, 0x41, 0x11, raw, 0x00, 0x00, 0x00, 0x00])

    # --------------------------------------------
    # ENGINE LOAD
    # --------------------------------------------

    elif data[:3] == [0x02, 0x01, 0x04]:

        raw = int(engine_load * 255 / 100)

        resp = build_msg(response_id, [0x03, 0x41, 0x04, raw, 0x00, 0x00, 0x00, 0x00])

    # --------------------------------------------
    # VIN (ISO-TP multi-frame)
    # --------------------------------------------

    elif data[:3] == [0x02, 0x09, 0x02]:

        vin_bytes = [ord(c) for c in VIN]

        frames = [
            [0x10, 0x14, 0x49, 0x02, 0x01, *vin_bytes[0:3]],
            [0x21, *vin_bytes[3:10]],
            [0x22, *vin_bytes[10:17]],
        ]

        for f in frames:
            while len(f) < 8:
                f.append(0x00)

            resp = build_msg(response_id, f)

            time.sleep(0.01)
            bus.send(resp)

            print(f"TX: {resp}")

        continue

    # --------------------------------------------
    # UDS READ DATA BY IDENTIFIER - OBD Auto Doctor probe
    # --------------------------------------------

    elif data[:4] == [0x03, 0x22, 0xF8, 0x02]:

        resp = build_msg(response_id, [0x04, 0x62, 0xF8, 0x02, 0x00, 0x00, 0x00, 0x00])

    # --------------------------------------------
    # MODE 01 PID 01 - Monitor status since DTCs cleared
    # --------------------------------------------

    elif data[:3] == [0x02, 0x01, 0x01]:

        mil_and_count = 0x00 if dtcs_cleared else 0x81
        resp = build_msg(response_id, [0x06, 0x41, 0x01, mil_and_count, 0x07, 0x65, 0x04, 0x00])

    # --------------------------------------------
    # MODE 01 PID 10 - MAF air flow rate
    # formula = ((A * 256) + B) / 100
    # --------------------------------------------

    elif data[:3] == [0x02, 0x01, 0x10]:

        maf = int(max(200, min(12000, rpm * 3)))

        resp = build_msg(
            response_id,
            [0x04, 0x41, 0x10, (maf >> 8) & 0xFF, maf & 0xFF, 0x00, 0x00, 0x00],
        )

    # --------------------------------------------
    # MODE 01 PID 42 - Control module voltage
    # formula = ((A * 256) + B) / 1000 V
    # --------------------------------------------

    elif data[:3] == [0x02, 0x01, 0x42]:

        millivolts = 14088

        resp = build_msg(
            response_id,
            [
                0x04,
                0x41,
                0x42,
                (millivolts >> 8) & 0xFF,
                millivolts & 0xFF,
                0x00,
                0x00,
                0x00,
            ],
        )

    # --------------------------------------------
    # MODE 01 PID 1C - OBD standards this vehicle conforms to
    # 0x06 = EOBD + OBD-II
    # --------------------------------------------

    elif data[:3] == [0x02, 0x01, 0x1C]:

        resp = build_msg(response_id, [0x03, 0x41, 0x1C, 0x06, 0x00, 0x00, 0x00, 0x00])

    # --------------------------------------------
    # MODE 01 PID 20 - Supported PIDs 21-40
    # Bit 0x80000000 means PID 21 supported.
    # Keep simple but valid.
    # --------------------------------------------

    elif data[:3] == [0x02, 0x01, 0x20]:

        resp = build_msg(response_id, [0x06, 0x41, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00])

    # --------------------------------------------
    # MODE 06 MID 00 - Supported monitor IDs 01-20.
    # Advertise a few monitor IDs so the read-only scanner can exercise Mode 06.
    # --------------------------------------------

    elif data[:3] == [0x02, 0x06, 0x00]:

        resp = build_msg(response_id, [0x06, 0x46, 0x00, 0x8C, 0x00, 0x00, 0x00, 0x00])

    # --------------------------------------------
    # MODE 06 monitor results.
    # Response bytes after 46 MID are synthetic but shaped like common TID/value data.
    # --------------------------------------------

    elif data[:3] == [0x02, 0x06, 0x01]:

        resp = build_msg(response_id, [0x06, 0x46, 0x01, 0x01, 0x00, 0x64, 0x00, 0xC8])

    elif data[:3] == [0x02, 0x06, 0x05]:

        resp = build_msg(response_id, [0x06, 0x46, 0x05, 0x01, 0x00, 0x32, 0x00, 0x96])

    elif data[:3] == [0x02, 0x06, 0x06]:

        resp = build_msg(response_id, [0x06, 0x46, 0x06, 0x01, 0x00, 0x28, 0x00, 0x80])

    # --------------------------------------------
    # MODE 08 TID 00 - Supported control IDs 01-20
    # Keep simple: no Mode 08 controls advertised.
    # --------------------------------------------

    elif data[:3] == [0x02, 0x08, 0x00]:

        resp = build_msg(response_id, [0x06, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])

    # --------------------------------------------
    # MODE 09 PID 00 - Supported info types 01-20
    # Advertise VIN (02) and ECU name (0A).
    # --------------------------------------------

    elif data[:3] == [0x02, 0x09, 0x00]:

        resp = build_msg(response_id, [0x06, 0x49, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00])

    # --------------------------------------------
    # MODE 09 PID 0A - ECU name
    # --------------------------------------------

    elif data[:3] == [0x02, 0x09, 0x0A]:

        resp = build_msg(
            response_id,
            [0x07, 0x49, 0x0A, 0x01, ord("E"), ord("S"), ord("P"), ord("3")],
        )

    # --------------------------------------------
    # MODE 03/04/07/0A DTC responses
    # --------------------------------------------

    elif data[:2] == [0x01, 0x03]:

        resp = build_dtc_response(response_id, 0x03)

    elif data[:2] == [0x01, 0x04]:

        dtcs_cleared = True
        resp = build_msg(response_id, [0x01, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])

    elif data[:2] == [0x01, 0x07]:

        resp = build_dtc_response(response_id, 0x07)

    elif data[:2] == [0x01, 0x0A]:

        resp = build_dtc_response(response_id, 0x0A)

    # --------------------------------------------
    # unsupported PID
    # --------------------------------------------

    elif len(data) >= 3 and data[1] == 0x01:

        requested_pid = data[2]

        resp = build_msg(
            response_id, [0x03, 0x7F, 0x01, 0x12, requested_pid, 0x00, 0x00, 0x00]
        )

    # --------------------------------------------
    # send response
    # --------------------------------------------

    if resp:

        send_msg(resp)
