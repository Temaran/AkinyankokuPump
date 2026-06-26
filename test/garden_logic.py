"""Host-test mirror of the pure firmware rules in src/GardenLogic.h.

The production firmware uses the C++ header. These Python functions let us run
the same edge-case tests on machines without PlatformIO or a C++ toolchain.
Keep formulas and bounds in sync with src/GardenLogic.h.
"""

from __future__ import annotations


CELL_COUNT = 4
MAX_CALIBRATION_RAW = 1023
ADC_MAX_READING = 1023
ANALOG_REFERENCE_MV = 5000


def clamp_float(value: float, low: float, high: float) -> float:
    if value < low:
        return low
    if value > high:
        return high
    return value


def clamp_int(value: int, low: int, high: int) -> int:
    if value < low:
        return low
    if value > high:
        return high
    return value


def is_valid_cell_index(index: int) -> bool:
    return 0 <= index < CELL_COUNT


def is_valid_threshold_config(start_percent: int, stop_percent: int) -> bool:
    return 0 <= start_percent <= 100 and 0 <= stop_percent <= 100 and stop_percent >= start_percent


def is_valid_calibration_config(dry_raw: int, wet_raw: int) -> bool:
    return 0 <= dry_raw < wet_raw <= MAX_CALIBRATION_RAW


def is_valid_zone_sensor(zone_idx: int, sensor_idx: int) -> bool:
    return is_valid_cell_index(zone_idx) and is_valid_cell_index(sensor_idx)


def coerce_zone_sensor(zone_idx: int, sensor_idx: int) -> int:
    return sensor_idx if is_valid_zone_sensor(zone_idx, sensor_idx) else zone_idx


def moisture_byte_from_norm(moisture_norm: float) -> int:
    return clamp_int(int(moisture_norm * 254.0 + 0.5), 0, 254)


def moisture_dot_count(moisture_norm: float, start_threshold_norm: float, stop_threshold_norm: float) -> int:
    if moisture_norm < start_threshold_norm:
        return 0
    if moisture_norm >= stop_threshold_norm:
        return 3
    if stop_threshold_norm <= start_threshold_norm:
        return 1

    midpoint_norm = start_threshold_norm + ((stop_threshold_norm - start_threshold_norm) * 0.5)
    return 1 if moisture_norm < midpoint_norm else 2


def vh400_vwc_percent_from_voltage(voltage: float) -> float:
    if voltage <= 1.1:
        return (10.0 * voltage) - 1.0
    if voltage <= 1.3:
        return (25.0 * voltage) - 17.5
    if voltage <= 1.82:
        return (48.08 * voltage) - 47.5
    if voltage <= 2.2:
        return (26.32 * voltage) - 7.89
    return (62.5 * voltage) - 87.5


def analog_reading_to_millivolts(reading: int) -> int:
    return int(reading * ANALOG_REFERENCE_MV / ADC_MAX_READING)


def count_watering_zones_needed(zone_needs_water: list[bool]) -> int:
    return sum(1 for needs_water in zone_needs_water if needs_water)


def next_watering_zone_after(start_zone: int, zone_needs_water: list[bool]) -> int:
    zone_count = len(zone_needs_water)
    for offset in range(1, zone_count + 1):
        zone_idx = (start_zone + offset + zone_count) % zone_count
        if zone_needs_water[zone_idx]:
            return zone_idx
    return -1


def is_leap_year(year: int) -> bool:
    return (year % 4 == 0 and year % 100 != 0) or year % 400 == 0


def days_in_month(year: int, month: int) -> int:
    days = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
    if month == 2 and is_leap_year(year):
        return 29
    return days[month - 1]


def day_of_week(year: int, month: int, day: int) -> int:
    offsets = [0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4]
    if month < 3:
        year -= 1
    return (year + year // 4 - year // 100 + year // 400 + offsets[month - 1] + day) % 7


def last_sunday_of_month(year: int, month: int) -> int:
    day = days_in_month(year, month)
    while day_of_week(year, month, day) != 0:
        day -= 1
    return day


def unix_time_utc(year: int, month: int, day: int, hour: int, minute: int, second: int) -> int:
    days = 0
    for current_year in range(1970, year):
        days += 366 if is_leap_year(current_year) else 365
    for current_month in range(1, month):
        days += days_in_month(year, current_month)
    days += day - 1
    return (((days * 24) + hour) * 60 + minute) * 60 + second


def is_europe_stockholm_dst(year: int, utc_unix_time: int) -> bool:
    dst_start = unix_time_utc(year, 3, last_sunday_of_month(year, 3), 1, 0, 0)
    dst_end = unix_time_utc(year, 10, last_sunday_of_month(year, 10), 1, 0, 0)
    return dst_start <= utc_unix_time < dst_end


def query_int_value(request_line: str, key: str, fallback: int) -> int:
    value = query_string_raw(request_line, key)
    if value is None:
        return fallback
    try:
        return int(value)
    except ValueError:
        return 0


def hex_nibble(ch: str) -> int:
    if "0" <= ch <= "9":
        return ord(ch) - ord("0")
    if "a" <= ch <= "f":
        return ord(ch) - ord("a") + 10
    if "A" <= ch <= "F":
        return ord(ch) - ord("A") + 10
    return 0


def url_decode(value: str) -> str:
    decoded: list[str] = []
    idx = 0
    while idx < len(value):
        ch = value[idx]
        if ch == "+":
            decoded.append(" ")
        elif ch == "%" and idx + 2 < len(value):
            decoded.append(chr((hex_nibble(value[idx + 1]) << 4) | hex_nibble(value[idx + 2])))
            idx += 2
        else:
            decoded.append(ch)
        idx += 1
    return "".join(decoded)


def query_string_raw(request_line: str, key: str) -> str | None:
    needle = f"{key}="
    start = request_line.find(needle)
    if start < 0:
        return None
    start += len(needle)
    end = request_line.find("&", start)
    if end < 0:
        end = request_line.find(" ", start)
    if end < 0:
        end = len(request_line)
    return request_line[start:end]


def query_string_value(request_line: str, key: str) -> str:
    raw = query_string_raw(request_line, key)
    return "" if raw is None else url_decode(raw)
