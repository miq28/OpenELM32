# OpenELM32

Firmware for ESP32-based CAN/RS485 boards that exposes an ELM327/OBDLink-compatible adapter over BLE, WiFi/TCP, and optional USB serial. The current focus is practical compatibility with OBD apps while keeping the ESP32 close to an adapter role: app commands are translated to CAN, and ECU behavior comes from a real vehicle or the included Python simulator.

This project is based on [collin80/ESP32RET](https://github.com/collin80/ESP32RET). Credit goes to Collin Kidder and the original ESP32RET contributors for the base firmware, CAN infrastructure, console, and MIT-licensed foundation.

## Current Scope

- ELM327-style command interface for common OBD apps.
- OBDLink/STN identity and compatibility probes used by apps such as OBDLink, Torque, OBD Auto Doctor, and Car Scanner.
- BLE ELM service using NimBLE, including OBDLink-style `FFF0/FFF1/FFF2` and generic serial `FFE0/FFE1` services.
- WiFi/TCP ELM327 server on port `35000`.
- Optional USB serial ELM327 mode with configurable baud.
- CAN 11-bit 500 kbit ISO 15765-4 request/response path.
- ISO-TP multi-frame ECU response assembly for responses such as VIN.
- LAWICEL/SLCAN-style CAN access for simulator and tool workflows.
- RS485 debug/console output for app-facing tests where USB serial is used by an OBD app.
- Runtime profiles and app presets for switching between quiet OBD mode and development/debug mode.

This is still firmware for testing and development. It is not a certified diagnostic interface.

## Supported Hardware

The active PlatformIO environments are:

| Environment | Board target | Notes |
| --- | --- | --- |
| `waveshare-esp32-s3-rs485-can` | Waveshare ESP32-S3 RS485/CAN | Default PlatformIO environment. |
| `weact-studio-can485-v1` | WeAct Studio CAN485 V1 | ESP32 CAN/RS485 board used for most recent OBD app testing. |

The original ESP32RET project supported other boards. This fork's README only documents the boards currently represented in `platformio.ini`.

## Build

This project uses PlatformIO, not the Arduino IDE.

```powershell
platformio run -e weact-studio-can485-v1
platformio run -e waveshare-esp32-s3-rs485-can
```

Upload and monitor ports are normally set locally in `platformio.ini` or passed on the command line.

```powershell
platformio run -e weact-studio-can485-v1 -t upload
platformio device monitor -b 1000000
```

## Runtime Modes

Use the serial console to switch the firmware between app-facing and development behavior.

Recommended app-facing mode:

```text
APP=OBD
```

USB serial ELM327 presets:

```text
APP=SERIAL115200
APP=SERIAL1000000
```

Development/debug preset:

```text
APP=DEV
```

Useful individual controls:

```text
PROFILE=OBD
PROFILE=DEV
ELM327SERIAL=1
ELM327SERIAL=0
SERBAUD=115200
SERBAUD=1000000
CANSTAT=0
CANSTAT=1
DEBUG=1
DEBUGSER=0
DEBUG485=1
BTNAME=WEACT_CAN485_8CE0
STATUS=1
```

For OBD app compatibility tests, keep app traffic clean: `PROFILE=OBD`, `CANSTAT=0`, debug enabled to RS485, and debug disabled on USB serial unless the test specifically needs USB debug.

## App Transports

| Transport | Purpose | Notes |
| --- | --- | --- |
| BLE | Primary mobile app path | Advertises OBDLink-style BLE services and reports the adapter model as `OBDLink CX` to apps. |
| WiFi/TCP | Network ELM327 path | ELM server listens on TCP port `35000`. |
| USB serial | PC app path | Enable with `ELM327SERIAL=1`; use `APP=SERIAL115200` for apps that cannot open 1 Mbit serial. This preset disables RS485 debug. |
| RS485 | Debug and console | Preferred debug output when USB serial is being used by an OBD app. |

## ECU Simulator

Use `ecu_sim-win-slcan.py` when testing OBD responses without a real car. It provides CAN-side ECU behavior for smoke tests, VIN, DTC/freeze-frame checks, and ATRV/PID `0142` voltage behavior.

```powershell
python ecu_sim-win-slcan.py
```

Specific simulator baud:

```powershell
python ecu_sim-win-slcan.py --baud 2000000
```

Expected simulator VIN:

```text
JT2BG22K1V0123456
```

## Testing

See [TESTING.md](TESTING.md) for the current manual and script test workflow.

Common smoke tests:

```powershell
python elm327_compat_test.py serial --port COM5 --baud 1000000 --vin --invalid
python elm327_compat_test.py tcp --host 192.168.1.242 --port 35000 --vin --invalid
python elm327_compat_test.py ble --address e0:8c:fe:a8:94:be --vin --invalid
```

Broader regression:

```powershell
python run_elm327_tests.py --serial COM5 --serial-baud 1000000 --tcp 192.168.1.242 --ble e0:8c:fe:a8:94:be --vin --invalid --formatting --identity --obdlink --dtc --freeze-frame --multi-ecu
```

Run one transport at a time when debugging app behavior. Serial, TCP, and BLE share the same emulator state, so concurrent app sessions can create misleading failures.

## OBDLink Compatibility Notes

The firmware intentionally reports OBDLink-compatible identity strings for app compatibility. BLE advertising name and app-reported model are separate:

- BLE broadcast name comes from `BTNAME` or app command `STBTDN`.
- Adapter/model identity remains `OBDLink CX` for commands such as `ATI`, `AT@1`, `STDI`, and Device Information reads.
- `AT@2` and `STDIX` can expose the configured broadcast name for diagnostics.

The OBDLink app can send broadcast-name changes with `STBTDN`, for example:

```text
STBTDN jontor%5s%5R
```

The OBDLink app enforces names shorter than 20 characters after suffix expansion. If a bad name is saved, reset it from the console:

```text
BTNAME=WEACT_CAN485_8CE0
```

## Documentation

- [TESTING.md](TESTING.md): current test workflow and app compatibility checks.
- [ELM327_COMMANDS.md](ELM327_COMMANDS.md): ELM327/OBDLink command compatibility matrix and roadmap.
- `ecu_sim-win-slcan.py`: Windows SLCAN ECU simulator used for app-facing regression tests.

## License and Attribution

This fork keeps the original MIT license. See [LICENSE](LICENSE).

Original project: [collin80/ESP32RET](https://github.com/collin80/ESP32RET)

Original copyright:

```text
Copyright (c) 2014-2020 Collin Kidder, Michael Neuweiler
```

Substantial parts of the CAN, console, configuration, and firmware structure originate from ESP32RET. This fork adds and changes behavior around ELM327/OBDLink compatibility, BLE app behavior, simulator-driven OBD testing, and board-specific runtime workflows.
