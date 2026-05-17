import can

bus = can.interface.Bus(channel="can0", interface="socketcan")

print("ECU simulator started")

while True:
    msg = bus.recv()

    if msg is None:
        continue

    print(f"RX: {msg}")

    if msg.arbitration_id == 0x7DF:

        data = list(msg.data)

        # 0100
        if data[:3] == [0x02, 0x01, 0x00]:

            resp = can.Message(
                arbitration_id=0x7E8,
                data=[
                    0x06,
                    0x41,
                    0x00,
                    0xBE,
                    0x3F,
                    0xA8,
                    0x13,
                    0xAA,
                ],
                is_extended_id=False,
            )

            bus.send(resp)

            print(f"TX: {resp}")
