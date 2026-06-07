# EEPROM dump workflow

## Firmware behavior
- D13 connected to GND: data gathering mode
- Logging interval currently set to 60 seconds for bring-up/testing
- D13 floating/high: irrigation mode
- Serial command `DUMP`: dump EEPROM to USB serial, erase EEPROM, then reset
- Serial command `DUMP_NO_ERASE`: dump EEPROM to USB serial and keep the data
- Serial command `DIAG`: print live sensor diagnostics without changing EEPROM
- Serial command `I2C_SCAN`: scan the I2C bus and print responding addresses
- Serial commands `CLEAR_LOG` / `ERASE_LOG`: erase EEPROM and reset
- Serial command `DIAG_FORCE`: run live diagnostics even if the log is full
- Data gathering stores diagnostics for the first three sensors with every sample: moisture bytes, raw capacitance readings, connected mask, and error mask
- EEPROM addresses `0..191` are reserved for persistent pump settings. Log data starts at address `192`.
- Wi-Fi credentials and per-cell watering thresholds are stored in the reserved EEPROM settings block. Threshold defaults are start `40%`, stop `60%`.
- Clearing memory from the web dashboard or serial commands erases only the log area, not saved settings.
- The firmware connects to Wi-Fi, starts a local dashboard, and syncs the RTC from NTP when network access is available. If NTP has not synced yet, timestamps are not meaningful.
- On boot, normal irrigation/data-gathering operation is deferred until NTP sync succeeds. If internet time is unavailable after 60 seconds, operation starts anyway and the dashboard marks the clock as unsynced/timed out.

## Web dashboard
Wi-Fi credentials are stored in the reserved EEPROM config block, not in source code. Configure them over Serial:

```text
SET_WIFI your-ssid your-password
```

The board saves the values and resets. Use `WIFI_STATUS` to check the saved SSID/connection state, and `CLEAR_WIFI` to remove saved credentials.

The board connects to that Wi-Fi network, asks DHCP for hostname `garden-pump`, prints its IP address on Serial, and hosts a dashboard at:

`http://<board-ip>/`

Many routers also make the dashboard available at:

`http://garden-pump/`

If that name does not resolve, reserve the board's IP address in the router DHCP settings and keep using the fixed IP.

Useful endpoints:
- `/api/status`: JSON status with mode, sample count, time sync state, sensor values, and relay states
- `/api/set_wifi?ssid=your-ssid&password=your-password`: update persistent Wi-Fi credentials when already connected
- `/api/set_threshold?cell=0&start=40&stop=60`: update a cell's persistent watering thresholds
- `/api/diag`: live sensor diagnostics
- `/api/diag_force`: diagnostics even when the EEPROM log is full
- `/api/i2c_scan`: I2C bus scan
- `/api/dump_no_erase`: raw dump without erasing
- `/api/sync_time`: retry NTP time sync
- `/api/clear_log?confirm=yes`: erase EEPROM and reset

The dashboard includes:
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
