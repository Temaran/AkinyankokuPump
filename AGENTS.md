# Local Agent Notes

## Core workflow

- Before changing firmware behavior, add or update tests under `test/` for the behavior being touched.
- After every code change, run the host-side regression suite from the repo root:

```powershell
.venv\Scripts\python.exe test\run_tests.py
```

- Do not call work complete until the test suite passes, or explicitly report why it could not be run.
- The Python tests mirror pure firmware rules in `src/GardenLogic.h` and include source-level checks for the embedded dashboard. Keep the tests and `GardenLogic.h` in sync when refactoring logic.
- If PlatformIO becomes available, also run the firmware build for `uno_r4_wifi` before release-level changes.

## Garden pump EEPROM dump

- Board environment: `uno_r4_wifi` in `platformio.ini`.
- Serial baud: `115200`.
- Wi-Fi credentials live in the reserved EEPROM config block, not in source. Configure over Serial with `SET_WIFI <ssid> <password>`. Use `WIFI_STATUS` to inspect and `CLEAR_WIFI` to remove.
- The firmware requests DHCP hostname `garden-pump`, starts a local HTTP dashboard on port 80 after Wi-Fi connects, and prints the board IP on Serial. Try `http://garden-pump/`; if the router does not resolve DHCP hostnames, use a router DHCP reservation for a fixed IP.
- Web endpoints:
  - `/api/status`: JSON status, sensor readings, relay states, clock sync state
  - `/api/set_wifi?ssid=your-ssid&password=your-password`: update persistent Wi-Fi credentials when already connected
  - `/api/set_threshold?cell=0&start=40&stop=60`: update persistent watering thresholds
  - `/api/set_calibration?cell=0&dry=430&wet=1023`: update persistent Seesaw raw dry/wet calibration
  - `/api/set_sensor_source?cell=0&source=vh400`: update persistent sensor source. Use `source=seesaw` or `source=vh400`.
  - `/api/set_interval?seconds=60`: update persistent sample interval
  - `/api/set_i2c_clock?hz=10000`: update and immediately apply persistent I2C clock
  - `/api/set_forced_irrigation?zone=-1`: runtime forced irrigation override. Use `zone=0..3` for zones 1..4. Not persisted; clears on reboot.
  - `/api/set_simulation?enabled=1&c0=30&c1=40&c2=50&c3=60`: runtime simulated sensor moisture percentages. Not persisted; clears on reboot.
  - `/api/diag`: live diagnostics unless EEPROM is full
  - `/api/raw_diag`: untouched burst reads, seesaw version, and temperature for each sensor
  - `/api/diag_force`: live diagnostics even if EEPROM is full
  - `/api/i2c_scan`: scan I2C devices
  - `/api/dump_no_erase`: raw EEPROM dump without erasing
  - `/api/sync_time`: retry NTP sync
  - `/api/clear_log?confirm=yes`: erase EEPROM and reset
- RTC is synced from NTP when Wi-Fi works. The dashboard and serial human-readable time are displayed as Europe/Stockholm with CET/CEST applied. If NTP has not synced, session timestamps are not meaningful.
- EEPROM layout: addresses `0..191` are reserved for persistent pump config; log data starts at address `192`.
- Persistent config currently stores Wi-Fi credentials, per-cell watering thresholds, per-cell Seesaw raw dry/wet calibration, per-cell sensor source, sample interval, and I2C clock. Threshold defaults: start watering at `40%`, stop at `60%`; calibration defaults: dry `324`, wet `1023`; sensor source default: `seesaw`; sample interval default: `60` seconds; I2C clock default: `10000` Hz.
- VH400 sensor mode reads analog pins `A0..A3` for cells `0..3`. The firmware assumes the Arduino analog reference is `5V` and converts the VH400 `0..3V` output to VWC using Vegetronix's published piecewise equations. Wire VH400 bare to GND, red to a valid sensor supply, and black output to the matching analog input.
- VH400 uses the shared watering thresholds directly as moisture percentages from `0..100`. Seesaw uses raw dry/wet calibration to turn capacitance into the same moisture percentage scale.
- Physical/display slot layout is `1 3` on the top row and `2 4` on the bottom row. VH400 analog mapping is slot 1 -> `A0`, slot 2 -> `A1`, slot 3 -> `A2`, and slot 4 -> `A3`. The Arduino LED matrix API uses row-major coordinates with `y=0` at the top, so top-row slots use the smaller LED Y offset in firmware.
- Open VH400 analog inputs can float and follow other active analog channels through ADC mux settling/crosstalk. Firmware requests 10-bit ADC reads and discards settling reads, but reliable disconnected detection may still need weak pulldowns on each signal input.
- Serial command `SET_CALIBRATION <cell> <dryRaw> <wetRaw>` saves per-cell Seesaw raw calibration. Use `0 <= dryRaw < wetRaw <= 1023`.
- Serial command `SET_SENSOR_SOURCE <cell> <seesaw|vh400>` saves the per-cell sensor source.
- Serial command `SET_SIMULATION <0|1> [c0 c1 c2 c3]` controls runtime simulated moisture percentages for relay testing. Not persisted; clears on reboot.
- Serial command `SET_INTERVAL <seconds>` saves the sample interval to EEPROM config. Valid range: `10..86400` seconds.
- Serial command `SET_I2C_CLOCK <hz>` saves and applies the I2C clock. Valid range: `1000..400000` Hz.
- Serial command `SET_FORCED_ZONE <-1..3>` controls the runtime forced irrigation override. `-1` disables it; `0..3` forces one solenoid open and closes all others.
- Log clearing commands erase only the log area from address `192` onward and preserve config settings.
- Normal live/data-gathering operation polls one sensor per loop cycle. EEPROM logging uses cached rolling values and does not poll every sensor in a burst.
- Firmware command `DUMP` dumps EEPROM, erases it with `eraseEntireLog()`, then resets the board. Do not use it when the user asks to preserve data.
- Firmware command `DUMP_NO_ERASE` dumps EEPROM and keeps the EEPROM contents intact.
- PC helper: `tools/capture_dump.py`.
- To dump without erasing and save the decoded output inside this repo, run:

```powershell
uv run tools/capture_dump.py --no-erase --output dumps/latest-no-erase.txt
```

- Current observed Arduino board port on this Windows machine after switching to a good USB cable: `COM4`. `COM3` was a misleading/nonresponsive USB serial device.
- Use `--port COM4` to avoid probing unrelated ports:

```powershell
.venv\Scripts\python.exe tools\capture_dump.py --no-erase --port COM4 --output dumps\latest-no-erase.txt --timeout 20
```

- If PlatformIO monitor opens with `Terminal on COM3 | 115200 8-N-1` but the capture script gets no `AKINYANKOKUPUMP_DUMP_BEGIN`, the port is open but the Garden Pump firmware is not answering serial commands. Close any open monitor first, then try reset/replug and rerun the command above.
- The Uno R4 RTC is not set by this firmware, so session timestamps emitted by the board are fake. The script waits for `AKINYANKOKUPUMP_DUMP_BEGIN` and `AKINYANKOKUPUMP_DUMP_END`, labels sessions by sequence number, and converts `SAMPLE2` records into lines like:

```text
session=1 interval=60s
```

and:

```text
moisture=24 52 0 raw=390 467 0 connected=0x3 error=0x4
```

- Live diagnostics can be requested with `DIAG`; it does not modify EEPROM.
- Raw sensor bus diagnostics can be requested with `RAW_DIAG`; it prints repeated untouched `touchRead(0)` values before filtering/normalization.
