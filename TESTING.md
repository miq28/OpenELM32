# Testing

This project uses the ESP32 firmware as an ELM327-compatible adapter. ECU behavior should normally be supplied by a real ECU or by `ecu_sim-win-slcan.py` on the CAN side.

See `ELM327_COMMANDS.md` for the ELM compatibility matrix and diagnostics roadmap.

## Setup

1. Flash the ESP32 firmware.
2. Start the CAN-side ECU simulator when testing OBD responses:

   ```powershell
   python ecu_sim-win-slcan.py
   ```

3. For USB serial ELM327 mode, enable it once from the serial console:

   ```text
   ELM327SERIAL=1
   ```

4. Match the configured USB baud:

   ```text
   SERBAUD=1000000
   SERBAUD=115200
   ```

Use `SERBAUD=115200` for applications that cannot open the adapter at `1000000`.

## Smoke Tests

Run one transport at a time. The ELM emulator has shared state, so running serial, TCP, and BLE tests at the same time can create misleading failures.

Serial:

```powershell
python elm327_compat_test.py serial --port COM5 --baud 1000000 --vin --invalid
```

TCP/WiFi:

```powershell
python elm327_compat_test.py tcp --host 192.168.1.242 --port 35000 --vin --invalid
```

BLE:

```powershell
python elm327_compat_test.py ble --address e0:8c:fe:a8:94:be --vin --invalid
```

Expected VIN response from `ecu_sim-win-slcan.py`:

```text
4902014A5432424732324B315630313233343536
```

This decodes to `JT2BG22K1V0123456`.

## App Compatibility

| App | Serial | WiFi/TCP | BLE | Notes |
| --- | --- | --- | --- | --- |
| OBD Auto Doctor | Works | Works | Works | Serial may require `SERBAUD=115200`. |
| Car Scanner | Works | Works | Works | Validate after firmware changes. |
| Torque | Not primary | Works | Works | BLE tested historically. |
| OBDWiz | Unsupported | Unsupported | Unsupported | Rejects the adapter during OBDLink vendor validation. |

## Release Checklist

Before committing firmware behavior changes:

1. Build locally with PlatformIO.
2. Flash the ESP32.
3. Run at least one `elm327_compat_test.py` transport with `--vin --invalid`.
4. Before a release, run serial, TCP/WiFi, and BLE smoke tests.
5. Check one real app that matters for the change.
6. Confirm RS485 debug output still works; RS485 is the dedicated debug channel.

## Useful Console Commands

```text
ELM327SERIAL=1
ELM327SERIAL=0
SERBAUD=115200
SERBAUD=1000000
PROFILE=OBD
PROFILE=DEV
CONSOLECAN=0
CONSOLECAN=1
DEBUG=1
DEBUGSER=0
DEBUG485=1
```
