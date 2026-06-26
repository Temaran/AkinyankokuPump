from __future__ import annotations

import re
import unittest
from pathlib import Path

import garden_logic as logic


ROOT = Path(__file__).resolve().parents[1]
MAIN_CPP = ROOT / "src" / "main.cpp"
GARDEN_CELL_CPP = ROOT / "src" / "GardenCell.cpp"
GARDEN_LOGIC_H = ROOT / "src" / "GardenLogic.h"


class ValidationTests(unittest.TestCase):
    def test_threshold_validation_accepts_ordered_percentages(self) -> None:
        self.assertTrue(logic.is_valid_threshold_config(0, 0))
        self.assertTrue(logic.is_valid_threshold_config(40, 60))
        self.assertTrue(logic.is_valid_threshold_config(100, 100))

    def test_threshold_validation_rejects_out_of_range_or_inverted_values(self) -> None:
        for start, stop in [(-1, 60), (40, 101), (61, 60), (101, 101)]:
            self.assertFalse(logic.is_valid_threshold_config(start, stop))

    def test_calibration_validation_requires_dry_below_wet(self) -> None:
        self.assertTrue(logic.is_valid_calibration_config(0, 1))
        self.assertTrue(logic.is_valid_calibration_config(324, 1023))
        for dry, wet in [(-1, 10), (10, 10), (11, 10), (0, 1024)]:
            self.assertFalse(logic.is_valid_calibration_config(dry, wet))

    def test_zone_sensor_mapping_validation(self) -> None:
        self.assertTrue(logic.is_valid_zone_sensor(0, 3))
        self.assertTrue(logic.is_valid_zone_sensor(3, 0))
        for zone, sensor in [(-1, 0), (0, -1), (4, 0), (0, 4)]:
            self.assertFalse(logic.is_valid_zone_sensor(zone, sensor))

    def test_invalid_zone_sensor_mapping_falls_back_to_same_zone(self) -> None:
        self.assertEqual(logic.coerce_zone_sensor(2, 0), 0)
        self.assertEqual(logic.coerce_zone_sensor(2, 9), 2)


class MoistureMathTests(unittest.TestCase):
    def test_moisture_byte_reserves_255_for_empty_eeprom(self) -> None:
        self.assertEqual(logic.moisture_byte_from_norm(-0.5), 0)
        self.assertEqual(logic.moisture_byte_from_norm(0.0), 0)
        self.assertEqual(logic.moisture_byte_from_norm(0.5), 127)
        self.assertEqual(logic.moisture_byte_from_norm(1.0), 254)
        self.assertEqual(logic.moisture_byte_from_norm(2.0), 254)

    def test_moisture_dots_follow_start_midpoint_stop_thresholds(self) -> None:
        self.assertEqual(logic.moisture_dot_count(0.39, 0.4, 0.6), 0)
        self.assertEqual(logic.moisture_dot_count(0.40, 0.4, 0.6), 1)
        self.assertEqual(logic.moisture_dot_count(0.50, 0.4, 0.6), 2)
        self.assertEqual(logic.moisture_dot_count(0.60, 0.4, 0.6), 3)

    def test_degenerate_threshold_range_keeps_one_dot_between_start_and_stop(self) -> None:
        self.assertEqual(logic.moisture_dot_count(0.40, 0.4, 0.4), 3)
        self.assertEqual(logic.moisture_dot_count(0.40, 0.4, 0.39), 3)


class Vh400Tests(unittest.TestCase):
    def test_adc_reading_to_millivolts_uses_5v_10bit_reference(self) -> None:
        self.assertEqual(logic.analog_reading_to_millivolts(0), 0)
        self.assertEqual(logic.analog_reading_to_millivolts(1023), 5000)
        self.assertEqual(logic.analog_reading_to_millivolts(512), 2502)

    def test_vh400_piecewise_curve_matches_known_breakpoints(self) -> None:
        self.assertAlmostEqual(logic.vh400_vwc_percent_from_voltage(1.1), 10.0, places=3)
        self.assertAlmostEqual(logic.vh400_vwc_percent_from_voltage(1.3), 15.0, places=3)
        self.assertAlmostEqual(logic.vh400_vwc_percent_from_voltage(1.82), 40.0056, places=3)
        self.assertAlmostEqual(logic.vh400_vwc_percent_from_voltage(2.2), 50.014, places=3)


class SchedulerTests(unittest.TestCase):
    def test_count_watering_zones_needed(self) -> None:
        self.assertEqual(logic.count_watering_zones_needed([False, False, False, False]), 0)
        self.assertEqual(logic.count_watering_zones_needed([True, False, True, False]), 2)

    def test_next_watering_zone_wraps_from_current_zone(self) -> None:
        self.assertEqual(logic.next_watering_zone_after(0, [False, False, True, False]), 2)
        self.assertEqual(logic.next_watering_zone_after(2, [True, False, True, False]), 0)
        self.assertEqual(logic.next_watering_zone_after(-1, [False, True, False, False]), 1)
        self.assertEqual(logic.next_watering_zone_after(1, [False, False, False, False]), -1)


class TimeTests(unittest.TestCase):
    def test_leap_year_rules(self) -> None:
        self.assertTrue(logic.is_leap_year(2024))
        self.assertFalse(logic.is_leap_year(2100))
        self.assertTrue(logic.is_leap_year(2000))

    def test_last_sunday_of_month_for_2026_dst_transitions(self) -> None:
        self.assertEqual(logic.last_sunday_of_month(2026, 3), 29)
        self.assertEqual(logic.last_sunday_of_month(2026, 10), 25)

    def test_stockholm_dst_switches_at_0100_utc(self) -> None:
        before_start = logic.unix_time_utc(2026, 3, 29, 0, 59, 59)
        at_start = logic.unix_time_utc(2026, 3, 29, 1, 0, 0)
        before_end = logic.unix_time_utc(2026, 10, 25, 0, 59, 59)
        at_end = logic.unix_time_utc(2026, 10, 25, 1, 0, 0)

        self.assertFalse(logic.is_europe_stockholm_dst(2026, before_start))
        self.assertTrue(logic.is_europe_stockholm_dst(2026, at_start))
        self.assertTrue(logic.is_europe_stockholm_dst(2026, before_end))
        self.assertFalse(logic.is_europe_stockholm_dst(2026, at_end))


class QueryParsingTests(unittest.TestCase):
    def test_query_int_value_extracts_values_until_ampersand_or_space(self) -> None:
        request = "GET /api/set_threshold?cell=2&start=40&stop=60 HTTP/1.1"
        self.assertEqual(logic.query_int_value(request, "cell", -1), 2)
        self.assertEqual(logic.query_int_value(request, "start", -1), 40)
        self.assertEqual(logic.query_int_value(request, "stop", -1), 60)
        self.assertEqual(logic.query_int_value(request, "missing", 99), 99)

    def test_query_string_value_decodes_plus_and_percent_escape(self) -> None:
        request = "GET /api/set_wifi?ssid=Garden+Pump&password=a%2Bb%20c HTTP/1.1"
        self.assertEqual(logic.query_string_value(request, "ssid"), "Garden Pump")
        self.assertEqual(logic.query_string_value(request, "password"), "a+b c")


class SourceIntegrationTests(unittest.TestCase):
    def test_firmware_uses_shared_logic_header(self) -> None:
        self.assertIn('#include "GardenLogic.h"', MAIN_CPP.read_text())
        self.assertIn('#include "GardenLogic.h"', GARDEN_CELL_CPP.read_text())

    def test_shared_logic_header_contains_expected_public_rules(self) -> None:
        text = GARDEN_LOGIC_H.read_text()
        for name in [
            "IsValidThresholdConfig",
            "IsValidCalibrationConfig",
            "IsValidZoneSensor",
            "MoistureDotCount",
            "Vh400VwcPercentFromVoltage",
            "NextWateringZoneAfter",
            "IsEuropeStockholmDst",
        ]:
            self.assertIn(name, text)

    def test_dashboard_zone_order_matches_physical_layout(self) -> None:
        text = MAIN_CPP.read_text()
        self.assertIn("let order=[0,2,1,3]", text)
        self.assertIn("visualCells(s.zones).map(zoneCard)", text)

    def test_dashboard_does_not_expose_removed_maintenance_controls(self) -> None:
        text = MAIN_CPP.read_text()
        removed_labels = [
            "<h2>Logging</h2>",
            "data gathering</label>",
            "Save I2C clock",
            "Save WiFi",
            "Raw diag</a>",
            "I2C scan</a>",
            "View dump</a>",
            "Download dump",
            "Download then clear",
            "Clear memory",
        ]
        for label in removed_labels:
            self.assertNotIn(label, text)

    def test_dashboard_uses_open_closed_relay_language(self) -> None:
        text = MAIN_CPP.read_text()
        self.assertIn("c.relay?'open':'closed'", text)
        self.assertIn("z.relay?'open':'closed'", text)
        self.assertNotIn("c.relay?'on':'off'", text)
        self.assertNotIn("z.relay?'on':'off'", text)

    def test_irrigation_led_rendering_uses_assigned_zone_sensor(self) -> None:
        text = MAIN_CPP.read_text()
        self.assertIn("GardenLogic::CoerceZoneSensor(updatedCell, Config.zoneSensor[updatedCell])", text)
        self.assertRegex(text, re.compile(r"Cells\[updatedCell\]\.RenderFrom\(LedMatrix,\s*Cells\[sensorIdx\]\)"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
