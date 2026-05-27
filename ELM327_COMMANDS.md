# ELM327 Compatibility Matrix

This project emulates enough ELM327-compatible behavior to work with common OBD apps over serial, WiFi/TCP, and BLE. Implementation should stay driven by real app behavior and regression tests; third-party reference manuals are not redistributed in this repository.

## Current Scope

| Area | Status | Notes |
| --- | --- | --- |
| Core ELM prompt/echo/line ending behavior | Supported | Covered by `elm327_compat_test.py`. |
| Serial, WiFi/TCP, BLE transports | Supported | BLE app behavior still depends heavily on the Windows BLE stack. |
| CAN 11-bit 500 kbit protocol reporting | Supported | `ATDP`, `ATDPN`, `ATSP0`, and `ATSP6` are covered. |
| Single-frame OBD requests | Supported | Examples: `0100`, `0101`, standard mode/PID requests. |
| Multi-frame ISO-TP responses | Supported | Receive-side assembly is covered by `0902` VIN tests. |
| Invalid ELM command rejection | Supported | Non-AT/non-hex malformed commands return `?`. |
| Mixed case and spaced commands | Supported | Covered by `--formatting`. |
| Adapter identity probes | Supported/stubbed | Covered by `--identity`. |
| Request-side ISO-TP segmentation | Not implemented | Needed for long UDS requests sent from app to ECU. |
| Full UDS diagnostics | Not implemented | Should be added selectively, not as a full stack first. |
| DTC convenience behavior | Partial/pass-through | Firmware passes app requests to CAN. The simulator covers `03`, `04`, `07`, and `0A` for safe testing. |
| Non-CAN ELM protocols | Not planned | Not useful for this ESP32 CAN-focused adapter. |

## Implemented or Stubbed Commands

| Command | Current behavior | Implementation note |
| --- | --- | --- |
| `ATZ` | Resets emulator state and reports `ELM327 v1.4b` | Supported. |
| `ATI` | Reports `OpenELM32` | Supported for app compatibility. |
| `AT@1` | Reports `OpenELM32` | Adapter identity. |
| `AT@2` | Reports configured broadcast name | Useful for diagnostics. |
| `STI` | Reports OpenELM32 firmware identity | Compatibility probe. |
| `VTI` | Reports OpenELM32 identity | Compatibility probe. |
| `ATE0/1` | Echo off/on | Supported. |
| `ATL0/1` | Linefeed off/on | Supported. |
| `ATS0/1` | Spaces off/on | Supported. |
| `ATH0/1` | Headers off/on | Supported. |
| `ATD` | Defaults | Supported. |
| `ATD0/1` | DLC display off/on | Supported/stored. |
| `ATSH...` | Set CAN request ID | Supported for 11-bit CAN IDs. |
| `ATSP0`, `ATSP6` | Select automatic/CAN 11/500 | Supported. |
| `ATDP`, `ATDPN` | Report protocol name/number | Supported. |
| `ATTP...` | Try protocol | Stubbed OK, sets physical ECU addressing. |
| `ATRV` | Requests OBD PID `0142` and reports voltage such as `14.1V` | Falls back to prompt-terminated app-facing behavior if no ECU voltage is available. |
| `03`, `07`, `0A` | Pass-through DTC read services | Covered by simulator/test flag `--dtc`. |
| `04` | Pass-through clear-DTC service | Covered by simulator/test flag `--clear-dtc`; use with care on real vehicles. |
| `ATAT...`, `ATM...`, `ATST...`, `ATSW...` | Timing/memory parameters | Stubbed OK. |
| `ATCAF...`, `ATAL`, `ATCEA...`, `ATPC`, `ATWS`, `ATCTM...`, `ATFI...` | Common compatibility commands | Stubbed OK. |
| Unknown `AT...` | Returns `OK` | App-friendly fallback. |
| Invalid non-AT command | Returns `?` | Prevents bogus CAN TX. |

## Recommended Roadmap

1. **Keep ELM command compatibility tested**
   - Expand `elm327_compat_test.py` only when a real app sends a new probe.
   - Prefer harmless stubs for commands that apps only use for capability discovery.

2. **Complete ISO-TP transport**
   - Current: receive multi-frame ECU responses and send flow-control frames.
   - Next: segment long app requests into ISO-TP first/consecutive frames.
   - This enables longer UDS requests that do not fit in one CAN frame.

3. **Add targeted UDS support**
   - Start with pass-through UDS services used by diagnostics tools:
     `0x10` diagnostic session control, `0x22` read data by identifier, `0x19` read DTC information, and `0x14` clear DTC information.
   - Keep the ESP32 acting as adapter first. Let the ECU or simulator decide positive/negative responses.

4. **Expand DTC-focused test scenarios**
   - `ecu_sim-win-slcan.py` has known DTC responses and simulator-only Mode 04 clear state.
   - Keep OBD services `03`, `04`, `07`, and `0A` covered before adding UDS service `19`.
   - Verify app output with OBD Auto Doctor and Car Scanner.

5. **Only then consider higher-level diagnostics features**
   - DBC/cantools decoding belongs in tooling or optional debug output, not the core ELM adapter path.
   - `python-can-isotp` is useful for simulator/test development before duplicating logic in firmware.

## Test Commands

Baseline all-transport regression:

```powershell
python run_elm327_tests.py --serial COM5 --tcp 192.168.1.242 --ble e0:8c:fe:a8:94:be --vin --invalid --formatting --identity
```

DTC simulator checks:

```powershell
python run_elm327_tests.py --serial COM5 --tcp 192.168.1.242 --vin --dtc
python run_elm327_tests.py --serial COM5 --tcp 192.168.1.242 --vin --clear-dtc
```
