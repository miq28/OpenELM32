# Testing

This project uses the ESP32 firmware as an ELM327-compatible adapter. ECU behavior should normally be supplied by a real ECU or by `ecu_sim-win-slcan.py` on the CAN side.

See `ELM327_COMMANDS.md` for the ELM compatibility matrix and diagnostics roadmap.

Build and flash testing is intentionally left to the person with the hardware attached. When changing firmware behavior, update this file with the manual checks that proved the change.

## Setup

1. Flash the ESP32 firmware.
2. Start the CAN-side ECU simulator when testing OBD responses:

   ```powershell
   python ecu_sim-win-slcan.py
   ```

   To run the simulator at a specific serial baud:

   ```powershell
   python ecu_sim-win-slcan.py --baud 2000000
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

The console presets are faster when switching test modes:

```text
APP=OBD
APP=SERIAL115200
APP=SERIAL1000000
APP=DEV
```

Recommended OBD app testing baseline:

```text
PROFILE=OBD
CANSTAT=0
DEBUG=1
DEBUGSER=0
DEBUG485=1
```

`CANSTAT=1` is useful during development, but keep it off for normal OBD app validation unless the test specifically needs CAN throughput stats.

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

Fuller regression when an app-facing change touches identity, OBDLink probes, DTCs, freeze-frame data, or multi-ECU responses:

```powershell
python run_elm327_tests.py --serial COM5 --serial-baud 1000000 --tcp 192.168.1.242 --ble e0:8c:fe:a8:94:be --vin --invalid --formatting --identity --obdlink --dtc --freeze-frame --multi-ecu
```

Simulator-only DTC clear validation:

```powershell
python elm327_compat_test.py serial --port COM5 --baud 1000000 --dtc --clear-dtc
```

Use `--clear-dtc` only with `ecu_sim-win-slcan.py` while validating this branch. On a real vehicle, OBD service `04` can erase diagnostic trouble codes, freeze-frame data, and readiness-related diagnostic state.

Expected VIN response from `ecu_sim-win-slcan.py`:

```text
4902014A5432424732324B315630313233343536
```

This decodes to `JT2BG22K1V0123456`.

Expected ATRV behavior with the simulator:

```text
ATRV -> firmware sends PID 0142 -> simulator returns 0142 voltage bytes -> app sees a voltage like 14.1V
```

If the simulator is not running or a real ECU does not support PID `0142`, verify that the app-facing response still remains prompt-terminated and does not stall.

## App Compatibility

| App | Serial | WiFi/TCP | BLE | Notes |
| --- | --- | --- | --- | --- |
| OBD Auto Doctor | Works | Works | Works | Serial may require `SERBAUD=115200`. |
| Car Scanner | Works | Works | Works | Validate after firmware changes. |
| Torque | Not primary | Works | Works | BLE tested historically. |
| OBDWiz | Unsupported | Unsupported | Unsupported | Rejects the adapter during OBDLink vendor validation. |
| OBDLink app | Not primary | Not primary | Works with current BLE path | Use for OBDLink/ST command compatibility checks. |

## OBDLink BLE Checks

The advertised BLE name can be changed by the OBDLink app with `STBTDN`. The app enforces a broadcast name shorter than 20 characters, so use 19 characters or fewer after suffix expansion.

Useful identity checks:

```text
ATI    -> OBDLink CX
AT@1   -> OBDLink CX
AT@2   -> configured broadcast name
STI    -> STN2310 v5.6.19
STMFR  -> OBD Solutions LLC
STDI   -> OBDLink CX r1.0.0
STDIX  -> OBDLink CX details and current BT Dev Name
STSN   -> 231012345678
```

Example broadcast-name write sent by the OBDLink app:

```text
STBTDN jontor%5s%5R
```

`%Ns` appends the last `N` serial-number characters. `%Nr` appends the last `N` Bluetooth/MAC-derived characters. The new name is persisted and takes effect after a reboot/power cycle.

If the name is changed accidentally, reset it from the serial console and reboot:

```text
BTNAME=WEACT_CAN485_8CE0
```

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
CANSTAT=0
CANSTAT=1
DEBUG=1
DEBUGSER=0
DEBUG485=1
APP=OBD
APP=SERIAL115200
APP=SERIAL1000000
APP=DEV
BTNAME=WEACT_CAN485_8CE0
```
