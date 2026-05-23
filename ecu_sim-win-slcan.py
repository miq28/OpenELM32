import can
import time
import math
import random

bus = can.interface.Bus(interface="slcan", channel="COM9", bitrate=500000)

print("Realistic ECU simulator started")

start_time = time.time()

VIN = "JT2BG22K1V0123456"


def build_msg(arbid, data):
    return can.Message(arbitration_id=arbid, data=data, is_extended_id=False)


def send_msg(msg, delay=None):
    if delay is None:
        delay = random.uniform(0.005, 0.03)

    time.sleep(delay)
    bus.send(msg)
    print(f"TX: {msg}")


def send_functional_responses(responses):
    for response_id, data in responses:
        send_msg(build_msg(response_id, data), 0.001)


while True:

    msg = bus.recv()

    if msg is None:
        continue

    now = time.time() - start_time

    print(f"RX: {msg}")

    functional_request = msg.arbitration_id == 0x7DF

    if msg.arbitration_id == 0x7DF:
        response_id = 0x7E8
    elif msg.arbitration_id == 0x7E0:
        response_id = 0x7E8
    elif msg.arbitration_id == 0x7E1:
        response_id = 0x7E9
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
            send_functional_responses(
                [
                    (0x7E8, [0x06, 0x41, 0x00, 0x98, 0x19, 0x80, 0x11, 0x00]),
                    (0x7E9, [0x06, 0x41, 0x00, 0x98, 0x18, 0x00, 0x01, 0x00]),
                ]
            )
            continue

        resp = build_msg(response_id, [0x06, 0x41, 0x00, 0x98, 0x19, 0x80, 0x11, 0x00])

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

        resp = build_msg(response_id, [0x06, 0x41, 0x01, 0x00, 0x07, 0x65, 0x04, 0x00])

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
    # MODE 09 PID 0A - ECU name
    # --------------------------------------------

    elif data[:3] == [0x02, 0x09, 0x0A]:

        resp = build_msg(
            response_id,
            [0x07, 0x49, 0x0A, 0x01, ord("E"), ord("S"), ord("P"), ord("3")],
        )

    # --------------------------------------------
    # MODE 03/07/0A DTC responses
    # --------------------------------------------

    elif data[:2] == [0x01, 0x03]:

        resp = build_msg(response_id, [0x05, 0x43, 0x01, 0x23, 0x04, 0x56, 0x00, 0x00])

    elif data[:2] == [0x01, 0x07]:

        resp = build_msg(response_id, [0x03, 0x47, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00])

    elif data[:2] == [0x01, 0x0A]:

        resp = build_msg(response_id, [0x03, 0x4A, 0x04, 0x20, 0x00, 0x00, 0x00, 0x00])

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
