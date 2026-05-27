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

`APP=SERIAL115200` keeps RS485 debug off by default. Re-enable it with `DEBUG485=1` only when you need a separate debug console.

To reset saved settings to the same default state used by a newly flashed board:

```text
RESETCONFIG=1
```

Power cycle after clearing config.
This command also works over USB while USB ELM327 mode is active.

Recommended OBD app testing baseline:

```text
PROFILE=OBD
CANSTAT=0
DEBUG=1
DEBUGSER=0
DEBUG485=0
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

Include OBDLink/STN batched command parsing when changing `STBC`, pipe-delimited command handling, or response formatting:

```powershell
python run_elm327_tests.py --serial COM5 --serial-baud 1000000 --tcp 192.168.1.242 --ble e0:8c:fe:a8:94:be --obdlink --batching
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

## SAE J1979 PID Scan

`saej1979_scan.py` is a read-only scanner for real vehicles. It first asks the ECU which PID/info blocks are supported, then queries only the advertised read-only items.
Common Mode 01 and Mode 02 values are decoded with SAE J1979 formulas, with raw responses kept in the output for unsupported or unknown PIDs.
With no group flags it runs the same maximum read-only sweep as `--all-readonly`.

Maximum read-only scan:

```powershell
python saej1979_scan.py --csv crv2013-saej1979.csv serial --port COM4 --baud 115200
```

Explicit maximum read-only scan:

```powershell
python saej1979_scan.py --all-readonly --csv crv2013-saej1979.csv serial --port COM4 --baud 115200
```

Only scan Mode 01 baseline live-data support:

```powershell
python saej1979_scan.py --baseline-only serial --port COM4 --baud 115200
```

TCP/WiFi:

```powershell
python saej1979_scan.py --csv crv2013-saej1979.csv tcp --host 192.168.1.242
```

BLE:

```powershell
python saej1979_scan.py --csv crv2013-saej1979.csv ble --address e0:8c:fe:a8:94:be
```

The scanner covers Mode `01`, Mode `02`, Mode `06`, Mode `09`, and DTC reads `03`/`07`/`0A`. It does not send Mode `04`, Mode `08`, or write/control services.

## ELM327 Transport Rate Test

`elm327_rate_test.py` measures raw ELM request/response rate without app polling behavior. Use it with the Python ECU simulator to compare USB serial, WiFi/TCP, and BLE using the same PID command.

Serial:

```powershell
python elm327_rate_test.py --count 100 --quiet serial --port COM4 --baud 115200
```

TCP/WiFi:

```powershell
python elm327_rate_test.py --count 100 --quiet tcp --host 192.168.1.242
```

BLE:

```powershell
python elm327_rate_test.py --count 100 --quiet ble --address e0:8c:fe:a8:94:be
```

The default command is `010C` and expected response marker is `410C`. Use `--command` and `--expect` to test a different PID.
Use `--header 7E0` to benchmark a physical ECU request instead of the default functional broadcast header `7DF`.

Fast physical Mode 01 polling can be enabled from the console:

```text
ELMFASTPOLL=1
```

With `ELMFASTPOLL=1`, physical-header Mode 01 single-frame replies return immediately after the first valid ECU response instead of waiting for the normal multi-ECU quiet window. Functional broadcast requests such as `7DF`, multi-frame replies, VIN, DTC, and other modes keep the normal behavior.

Compare before and after with:

```powershell
python elm327_rate_test.py --count 100 --quiet --header 7E0 ble --address e0:8c:fe:a8:94:be
```

Disable it again with:

```text
ELMFASTPOLL=0
```

## Clear DTC Utility

`obd_clear_dtc.py` is intentionally separate from the read-only scanner. By default it performs a dry run and only prints the command plan.
The command plan includes a short purpose for each command before anything is sent.

Dry run:

```powershell
python obd_clear_dtc.py serial --port COM4 --baud 115200
```

Execute clear DTC intentionally:

```powershell
python obd_clear_dtc.py --execute --confirm CLEAR-DTC serial --port COM4 --baud 115200
```

Mode `04` can clear diagnostic trouble codes, freeze-frame data, and readiness-related diagnostic state.

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
STSN   -> 12-digit OBDLink-style serial with a MAC-derived numeric suffix
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

1. Build locally with pioarduino.
2. Flash the ESP32.
3. Run at least one `elm327_compat_test.py` transport with `--vin --invalid`.
4. Before a release, run serial, TCP/WiFi, and BLE smoke tests.
5. Check one real app that matters for the change.
6. Confirm RS485 debug output still works; RS485 is the dedicated debug channel.

## Crash Debug Builds

Normal builds use `CORE_DEBUG_LEVEL=0`, normal optimization, and include debug symbols in the local ELF so the VS Code upload button still produces decodable crash traces.

For a one-off core-log build:

```powershell
$env:OPENELM_CORE_DEBUG_LEVEL='5'; platformio run -e waveshare-esp32-s3-rs485-can
```

For deeper crash tracing with lower optimization:

```powershell
$env:OPENELM_DEBUG_BUILD='1'; platformio run -e waveshare-esp32-s3-rs485-can
```

If verbose ESP/Arduino core logs are needed too:

```powershell
$env:OPENELM_DEBUG_BUILD='1'; $env:OPENELM_CORE_DEBUG_LEVEL='5'; platformio run -e waveshare-esp32-s3-rs485-can
```

Clear `OPENELM_CORE_DEBUG_LEVEL` and `OPENELM_DEBUG_BUILD` before normal release builds.

Panic backtraces are emitted by the ESP-IDF panic handler on the configured console UART, not through OpenELM32's RS485 debug logger. For USB-serial ELM testing, use the coredump-enabled partition tables to capture intermittent crashes in flash.

After a crash, keep the matching `.pio/build/<env>/firmware.elf` from the flashed build. For Waveshare:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\esp-coredump.exe" --chip esp32s3 --port COM8 --baud 921600 info_corefile --off 0x811000 --save-core waveshare-crash.core .pio\build\waveshare-esp32-s3-rs485-can\firmware.elf
```

Linux:

```bash
~/.platformio/penv/bin/esp-coredump --chip esp32s3 --port /dev/ttyACM0 --baud 921600 info_corefile --off 0x811000 --save-core waveshare-crash.core .pio/build/waveshare-esp32-s3-rs485-can/firmware.elf
```

For WeAct:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\esp-coredump.exe" --chip esp32 --port COM8 --baud 921600 info_corefile --off 0x420000 --save-core weact-crash.core .pio\build\weact-studio-can485-v1\firmware.elf
```

Linux:

```bash
~/.platformio/penv/bin/esp-coredump --chip esp32 --port /dev/ttyUSB0 --baud 921600 info_corefile --off 0x420000 --save-core weact-crash.core .pio/build/weact-studio-can485-v1/firmware.elf
```

Change `COM8`, `/dev/ttyACM0`, or `/dev/ttyUSB0` to the board's USB port. On Linux, add the user to `dialout` if serial access is denied. Decode before rebuilding when possible, because the ELF should match the crashed firmware. This is useful for panics, asserts, and watchdog crashes; it will not explain a plain power-on reset, USB reset, or brownout as well.

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
