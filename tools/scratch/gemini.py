import can
import time

# Initialize CAN interface
bus = can.interface.Bus(
    interface="slcan",
    channel="COM9",
    bitrate=500000
)

print("ECU simulator started")

# Define a valid 17-character VIN (Must be exactly 17 bytes)
VIN_STR = "1YVHP82D395113426"  
VIN_BYTES = [ord(c) for c in VIN_STR]

while True:
    msg = bus.recv()

    if msg is None:
        continue

    print(f"RX: {msg}")

    # Support both functional (0x7DF) and physical (0x7E0) addressing
    if msg.arbitration_id not in [0x7DF, 0x7E0]:
        continue

    data = list(msg.data)
    resp = None

    # =========================================================
    # MODE 01 PID 00 - Supported PIDs [01-20]
    # =========================================================
    if data[:3] == [0x02, 0x01, 0x00]:
        resp = can.Message(
            arbitration_id=0x7E8,
            data=[0x06, 0x41, 0x00, 0xBE, 0x3F, 0xA8, 0x13, 0xAA],
            is_extended_id=False
        )
        bus.send(resp)
        print(f"TX: {resp}")

    # =========================================================
    # MODE 01 PID 20 - Supported PIDs [21-40]
    # =========================================================
    elif data[:3] == [0x02, 0x01, 0x20]:
        resp = can.Message(
            arbitration_id=0x7E8,
            data=[0x06, 0x41, 0x20, 0x00, 0x00, 0x00, 0x01, 0xAA],
            is_extended_id=False
        )
        bus.send(resp)
        print(f"TX: {resp}")

    # =========================================================
    # MODE 01 PID 0C - Engine RPM
    # =========================================================
    elif data[:3] == [0x02, 0x01, 0x0C]:
        rpm = 850
        raw = rpm * 4
        resp = can.Message(
            arbitration_id=0x7E8,
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
        bus.send(resp)
        print(f"TX: {resp}")

    # =========================================================
    # MODE 01 PID 0D - Vehicle Speed
    # =========================================================
    elif data[:3] == [0x02, 0x01, 0x0D]:
        resp = can.Message(
            arbitration_id=0x7E8,
            data=[0x03, 0x41, 0x0D, 40, 0xAA, 0xAA, 0xAA, 0xAA],
            is_extended_id=False
        )
        bus.send(resp)
        print(f"TX: {resp}")

    # =========================================================
    # MODE 01 PID 05 - Engine Coolant Temperature
    # =========================================================
    elif data[:3] == [0x02, 0x01, 0x05]:
        resp = can.Message(
            arbitration_id=0x7E8,
            data=[0x03, 0x41, 0x05, 130, 0xAA, 0xAA, 0xAA, 0xAA],
            is_extended_id=False
        )
        bus.send(resp)
        print(f"TX: {resp}")

    # =========================================================
    # MODE 09 PID 00 - Supported PIDs [01-20]
    # =========================================================
    elif data[:3] == [0x02, 0x09, 0x00]:
        resp = can.Message(
            arbitration_id=0x7E8,
            data=[0x06, 0x49, 0x00, 0x40, 0x00, 0x00, 0x00, 0xAA],
            is_extended_id=False
        )
        bus.send(resp)
        print(f"TX: {resp}")

    # =========================================================
    # MODE 09 PID 02 - Burst Multi-Frame Standard VIN
    # =========================================================
    elif data[:3] == [0x02, 0x09, 0x02]:
        print(" -> Mode 09 VIN Request received. Sending Burst ISO-TP Frames...")
        
        # 1. First Frame (0x10). Total ISO-TP length = 20 bytes (0x14)
        # Payload: 1 byte number of items (0x01), 2 bytes headers (0x49, 0x02), first 3 bytes of VIN
        ff_msg = can.Message(
            arbitration_id=0x7E8,
            data=[0x10, 20, 0x49, 0x02, 0x01] + VIN_BYTES[0:3],
            is_extended_id=False
        )
        bus.send(ff_msg)
        time.sleep(0.005) # 5ms safe delay for the ELM emulator to process
        
        # 2. Consecutive Frame 1 (0x21). Next 7 bytes of VIN
        cf1 = can.Message(
            arbitration_id=0x7E8,
            data=[0x21] + VIN_BYTES[3:10],
            is_extended_id=False
        )
        bus.send(cf1)
        time.sleep(0.005)
        
        # 3. Consecutive Frame 2 (0x22). Last 7 bytes of VIN
        cf2 = can.Message(
            arbitration_id=0x7E8,
            data=[0x22] + VIN_BYTES[10:17],
            is_extended_id=False
        )
        bus.send(cf2)
        print(" -> Sent Burst Mode 09 VIN.")

    # =========================================================
    # MODE 22 PID F802 - Send Clean UDS Negative Response Code
    # =========================================================
    elif data[:4] == [0x03, 0x22, 0xF8, 0x02]:
        print(" -> Mode 22 F802 Received. Rejecting with NRC 31 (Unsupported)...")
        
        resp = can.Message(
            arbitration_id=0x7E8,
            data=[
                0x03,  # Data length: 3 bytes follow
                0x7F,  # 0x7F = Negative Response Identifier
                0x22,  # Rejecting Mode 22
                0x31,  # NRC 0x31 = Request Out Of Range / Not Supported
                0xAA, 0xAA, 0xAA, 0xAA  # Dead padding bytes
            ],
            is_extended_id=False
        )
        bus.send(resp)
        print(f"TX: {resp}")

