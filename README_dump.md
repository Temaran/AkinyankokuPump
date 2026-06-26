# EEPROM dump workflow

## Firmware behavior
- D13 connected to GND: data gathering mode
- Logging interval is stored in the reserved EEPROM settings block. Default is 60 seconds; valid range is 10 seconds to 86400 seconds.
- D13 floating/high: irrigation mode
- Serial command `DUMP`: dump EEPROM to USB serial, erase EEPROM, then reset
- Serial command `DUMP_NO_ERASE`: dump EEPROM to USB serial and keep the data
- Serial command `DIAG`: print live sensor diagnostics without changing EEPROM
- Serial command `RAW_DIAG`: print untouched burst reads, seesaw version, and temperature for each sensor
- Serial command `I2C_SCAN`: scan the I2C bus and print responding addresses
- Serial command `SET_FORCED_ZONE -1`: disable runtime forced irrigation. Use `SET_FORCED_ZONE 0` through `SET_FORCED_ZONE 3` to force one zone open and all others closed until reboot or override off.
- Serial command `SET_SENSOR_SOURCE 0 vh400`: save a cell's sensor source. Use `seesaw` for the I2C path or `vh400` for analog A0-A3.
- Serial command `SET_SIMULATION 1 30 40 50 60`: enable runtime simulated moisture percentages for cells 0-3. Use `SET_SIMULATION 0` to return to real sensors.
- Serial commands `CLEAR_LOG` / `ERASE_LOG`: erase EEPROM and reset
- Serial command `DIAG_FORCE`: run live diagnostics even if the log is full
- Data gathering stores diagnostics for the first three sensors with every sample: moisture bytes, raw capacitance readings, connected mask, and error mask
- Normal live/data-gathering operation polls one sensor per loop cycle. EEPROM logging records the latest cached rolling values instead of polling all sensors in a burst.
- EEPROM addresses `0..191` are reserved for persistent pump settings. Log data starts at address `192`.
- Wi-Fi credentials, per-cell watering thresholds, per-cell Seesaw raw dry/wet calibration, per-cell sensor source, logging interval, and I2C clock are stored in the reserved EEPROM settings block. Threshold defaults are start watering at `40%`, stop at `60%`; raw calibration defaults are dry `324`, wet `1023`; sensor source defaults to `seesaw`; I2C clock default is `10000` Hz.
- Clearing memory from the web dashboard or serial commands erases only the log area, not saved settings.
- VH400 mode reads cells 0-3 from analog pins `A0..A3`. The firmware assumes a `5V` analog reference and converts the VH400 `0..3V` output using Vegetronix's published piecewise voltage-to-VWC equations.
- VH400 uses the shared watering thresholds directly as moisture percentages from `0..100`. Seesaw uses raw dry/wet calibration to turn capacitance into the same moisture percentage scale.
- The box/display layout is slot `1 3` on the top row and slot `2 4` on the bottom row. VH400 analog mapping is slot 1 -> `A0`, slot 2 -> `A1`, slot 3 -> `A2`, and slot 4 -> `A3`. The Arduino LED matrix API uses row-major coordinates with `y=0` at the top, so top-row slots use the smaller LED Y offset in firmware.
- Open VH400 analog inputs can float and follow other active analog channels through ADC mux settling/crosstalk. Firmware requests 10-bit ADC reads and discards settling reads, but reliable disconnected detection may still need weak pulldowns on each signal input.
- The firmware connects to Wi-Fi, starts a local dashboard, and syncs the RTC from NTP when network access is available. The dashboard and serial human-readable time are displayed as Europe/Stockholm with CET/CEST applied. If NTP has not synced yet, timestamps are not meaningful.
- On boot, normal irrigation/data-gathering operation is deferred until NTP sync succeeds. If internet time is unavailable after 60 seconds, operation starts anyway and the dashboard marks the clock as unsynced/timed out.

## Web dashboard
Wi-Fi credentials are stored in the reserved EEPROM config block, not in source code. Configure them over Serial:

```text
SET_WIFI your-ssid your-password
```

The board saves the values and resets. Use `WIFI_STATUS` to check the saved SSID/connection state, and `CLEAR_WIFI` to remove saved credentials.

Set the persistent sample interval over Serial with:

```text
SET_INTERVAL 60
```

Set the persistent I2C clock over Serial with:

```text
SET_I2C_CLOCK 10000
```

Set a cell's persistent Seesaw raw calibration over Serial with:

```text
SET_CALIBRATION 0 430 1023
```

Set a cell's persistent sensor source over Serial with:

```text
SET_SENSOR_SOURCE 0 vh400
```

Use `seesaw` to switch that cell back to the I2C seesaw path.

The board connects to that Wi-Fi network, asks DHCP for hostname `garden-pump`, prints its IP address on Serial, and hosts a dashboard at:

`http://<board-ip>/`

Many routers also make the dashboard available at:

`http://garden-pump/`

If that name does not resolve, reserve the board's IP address in the router DHCP settings and keep using the fixed IP.

Useful endpoints:
- `/api/status`: JSON status with mode, sample count, time sync state, sensor values, and relay states
- `/api/set_wifi?ssid=your-ssid&password=your-password`: update persistent Wi-Fi credentials when already connected
- `/api/set_threshold?cell=0&start=40&stop=60`: update a cell's persistent watering thresholds
- `/api/set_calibration?cell=0&dry=430&wet=1023`: update a cell's persistent Seesaw raw dry/wet calibration
- `/api/set_sensor_source?cell=0&source=vh400`: update a cell's persistent sensor source
- `/api/set_interval?seconds=60`: update the persistent sample interval
- `/api/set_i2c_clock?hz=10000`: update and immediately apply the persistent I2C clock
- `/api/set_forced_irrigation?zone=-1`: runtime forced irrigation override. Use `zone=0..3` for zones 1..4. This is not stored in EEPROM and clears on reboot.
- `/api/set_simulation?enabled=1&c0=30&c1=40&c2=50&c3=60`: runtime simulated moisture percentages. This is not stored in EEPROM and clears on reboot.
- `/api/diag`: live sensor diagnostics
- `/api/raw_diag`: untouched burst reads from each sensor
- `/api/analog_diag`: repeated raw settled reads from A0..A3 for VH400 wiring diagnostics
- `/api/diag_force`: diagnostics even when the EEPROM log is full
- `/api/i2c_scan`: I2C bus scan
- `/api/dump_no_erase`: raw dump without erasing
- `/api/sync_time`: retry NTP time sync
- `/api/clear_log?confirm=yes`: erase EEPROM and reset

The dashboard includes:
- Per-cell sensor source radio buttons for `seesaw` or `VH400`
- Simple per-cell `0..100%` watering thresholds for all sensor sources; raw dry/wet calibration controls are shown only for Seesaw cells
- A simulation panel for runtime moisture percentages so relay behavior can be tested without real sensor input
- "View dump": fetches `/api/dump_no_erase` into the output panel
- "Download dump": saves `/api/dump_no_erase` as a timestamped text file in the browser
- "Clear memory": calls `/api/clear_log?confirm=yes`, erases EEPROM, and resets the board
- "Download then clear": downloads the no-erase dump first, then clears EEPROM after confirmation

## PC-side capture
The Arduino cannot directly write a file into your Documents folder. The included script opens the serial port, sends the dump command, decodes sample records, and saves the result as:

`Documents/AkinyankokuPump/log_YYYY-MM-DD_HH-MM-SS.txt`

## Usage
1. Install pyserial into the project environment:
   - `uv add pyserial`
   - or `uv pip install pyserial`
2. Connect the board over USB.
3. Run either the included VS Code tasks or:
   - `uv run tools/capture_dump.py`
4. The script sends `DUMP`, captures the decoded dump, and saves it into your Documents folder.

To dump without erasing afterward:
- `uv run tools/capture_dump.py --no-erase`

To check live sensor status from a serial monitor while the board is on the bench, send:
- `DIAG`

It prints each connected/debugged cell's I2C address, connection status, error status, raw capacitance reading, normalized value, and logged byte value.
The deployed data log stores the same connection/error/raw-reading evidence with every sample, so the dump remains useful after you bring the board back inside.

## Output format
The saved file looks like:

```text
session=1 interval=60s
moisture=24 52 0 raw=390 467 0 connected=0x3 error=0x4
moisture=11 15 0 raw=354 365 0 connected=0x3 error=0x4

session=2 interval=3600s
moisture=24 52 0 raw=390 467 0 connected=0x3 error=0x4
moisture=11 15 0 raw=354 365 0 connected=0x3 error=0x4
```


VS Code tasks are included in `.vscode/tasks.json`:
- `Pump: Dump`
- `Pump: Dump (No Erase)`

Dump files are saved under `Documents/AkinyankokuPump/` as `log_YYYY-MM-DD_HH-MM-SS.txt`.
The capture script will auto-detect the board by probing available serial ports; use `--port` to override if needed.
