import can

bus = can.interface.Bus(
    interface="slcan",
    channel="COM9",
    bitrate=500000
)

print("ECU simulator started")

while True:
    msg = bus.recv()

    if msg is None:
        continue

    print(f"RX: {msg}")

    # support both functional and physical addressing
    if msg.arbitration_id == 0x7DF:
        response_id = 0x7E8
    elif msg.arbitration_id == 0x7E0:
        response_id = 0x7E8
    else:
        continue

    data = list(msg.data)

    resp = None

    # =========================================================
    # MODE 01 PID 00
    # =========================================================
    if data[:3] == [0x02, 0x01, 0x00]:

        resp = can.Message(
            arbitration_id=response_id,
            data=[
                0x06,
                0x41,
                0x00,
                0xBE,
                0x3F,
                0xA8,
                0x13,
                0xAA
            ],
            is_extended_id=False
        )

    # =========================================================
    # MODE 01 PID 20
    # =========================================================
    elif data[:3] == [0x02, 0x01, 0x20]:

        resp = can.Message(
            arbitration_id=response_id,
            data=[
                0x06,
                0x41,
                0x20,
                0x00,
                0x00,
                0x00,
                0x01,
                0xAA
            ],
            is_extended_id=False
        )

    # =========================================================
    # RPM
    # =========================================================
    elif data[:3] == [0x02, 0x01, 0x0C]:

        rpm = 850
        raw = rpm * 4

        resp = can.Message(
            arbitration_id=response_id,
            data=[
                0x04,
                0x41,
                0x0C,
                (raw >> 8) & 0xFF,
                raw & 0xFF,
                0xAA,
                0xAA,
                0xAA
            ],
            is_extended_id=False
        )

    # =========================================================
    # SPEED
    # =========================================================
    elif data[:3] == [0x02, 0x01, 0x0D]:

        resp = can.Message(
            arbitration_id=response_id,
            data=[
                0x03,
                0x41,
                0x0D,
                40,
                0xAA,
                0xAA,
                0xAA,
                0xAA
            ],
            is_extended_id=False
        )

    # =========================================================
    # COOLANT TEMP
    # =========================================================
    elif data[:3] == [0x02, 0x01, 0x05]:

        resp = can.Message(
            arbitration_id=response_id,
            data=[
                0x03,
                0x41,
                0x05,
                130,
                0xAA,
                0xAA,
                0xAA,
                0xAA
            ],
            is_extended_id=False
        )

    # =========================================================
    # MODE 09 PID 00
    # =========================================================
    elif data[:3] == [0x02, 0x09, 0x00]:

        resp = can.Message(
            arbitration_id=response_id,
            data=[
                0x06,
                0x49,
                0x00,
                0x40,
                0x00,
                0x00,
                0x00,
                0xAA
            ],
            is_extended_id=False
        )

    # =========================================================
    # MODE 09 PID 02 VIN
    # =========================================================
    elif data[:3] == [0x02, 0x09, 0x02]:

        resp = can.Message(
            arbitration_id=response_id,
            data=[
                0x03,
                0x7F,
                0x09,
                0x11,
                0xAA,
                0xAA,
                0xAA,
                0xAA
            ],
            is_extended_id=False
        )

    # =========================================================
    # UDS NEGATIVE RESPONSE
    # =========================================================
    elif data[:4] == [0x03, 0x22, 0xF8, 0x02]:

        # negative response: request out of range
        resp = can.Message(
            arbitration_id=response_id,
            data=[
                0x03,
                0x7F,
                0x22,
                0x31,
                0xAA,
                0xAA,
                0xAA,
                0xAA
            ],
            is_extended_id=False
        )

    # =========================================================
    # SEND IF MATCHED
    # =========================================================
    if resp is not None:

        bus.send(resp)

        print(f"TX: {resp}")