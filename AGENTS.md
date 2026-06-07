# Local Agent Notes

## Garden pump EEPROM dump

- Board environment: `uno_r4_wifi` in `platformio.ini`.
- Serial baud: `115200`.
- Wi-Fi credentials live in the reserved EEPROM config block, not in source. Configure over Serial with `SET_WIFI <ssid> <password>`. Use `WIFI_STATUS` to inspect and `CLEAR_WIFI` to remove.
- The firmware requests DHCP hostname `garden-pump`, starts a local HTTP dashboard on port 80 after Wi-Fi connects, and prints the board IP on Serial. Try `http://garden-pump/`; if the router does not resolve DHCP hostnames, use a router DHCP reservation for a fixed IP.
- Web endpoints:
  - `/api/status`: JSON status, sensor readings, relay states, clock sync state
  - `/api/set_wifi?ssid=your-ssid&password=your-password`: update persistent Wi-Fi credentials when already connected
  - `/api/set_threshold?cell=0&start=40&stop=60`: update persistent watering thresholds
  - `/api/diag`: live diagnostics unless EEPROM is full
  - `/api/diag_force`: live diagnostics even if EEPROM is full
  - `/api/i2c_scan`: scan I2C devices
  - `/api/dump_no_erase`: raw EEPROM dump without erasing
  - `/api/sync_time`: retry NTP sync
  - `/api/clear_log?confirm=yes`: erase EEPROM and reset
- RTC is synced from NTP when Wi-Fi works. If NTP has not synced, session timestamps are not meaningful.
- EEPROM layout: addresses `0..191` are reserved for persistent pump config; log data starts at address `192`.
- Persistent config currently stores Wi-Fi credentials and per-cell watering thresholds. Threshold defaults: start `40%`, stop `60%`.
- Log clearing commands erase only the log area from address `192` onward and preserve config settings.
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
