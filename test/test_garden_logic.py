from __future__ import annotations

import re
import unittest
from pathlib import Path

import garden_logic as logic


ROOT = Path(__file__).resolve().parents[1]
MAIN_CPP = ROOT / "src" / "main.cpp"
GARDEN_CELL_CPP = ROOT / "src" / "GardenCell.cpp"
GARDEN_LOGIC_H = ROOT / "src" / "GardenLogic.h"
WEB_UI_CPP = ROOT / "src" / "WebUI.cpp"
HTTP_API_CPP = ROOT / "src" / "HttpApi.cpp"
FIRMWARE_STATE_H = ROOT / "src" / "FirmwareState.h"


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


class BaseSensorTests(unittest.TestCase):
    def test_capacitance_reading_validation(self) -> None:
        self.assertFalse(logic.is_valid_capacitance_reading(0))
        self.assertTrue(logic.is_valid_capacitance_reading(1))
        self.assertTrue(logic.is_valid_capacitance_reading(1023))
        self.assertFalse(logic.is_valid_capacitance_reading(1024))

    def test_stable_cluster_uses_tightest_three_readings(self) -> None:
        stable, median, spread = logic.find_stable_cluster_median([900, 405, 400, 410, 950])
        self.assertTrue(stable)
        self.assertEqual(median, 405)
        self.assertEqual(spread, 10)

    def test_unstable_cluster_reports_best_spread_but_not_stable(self) -> None:
        stable, median, spread = logic.find_stable_cluster_median([100, 250, 420])
        self.assertFalse(stable)
        self.assertEqual(median, 250)
        self.assertEqual(spread, 320)


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

    def test_sensor_code_is_split_into_dedicated_modules(self) -> None:
        for path in [
            ROOT / "src" / "BaseSensor.h",
            ROOT / "src" / "BaseSensor.cpp",
            ROOT / "src" / "SeesawSensor.h",
            ROOT / "src" / "SeesawSensor.cpp",
            ROOT / "src" / "VH400Sensor.h",
            ROOT / "src" / "VH400Sensor.cpp",
        ]:
            self.assertTrue(path.exists(), f"{path} should exist")

        cell_text = GARDEN_CELL_CPP.read_text()
        self.assertIn('#include "SeesawSensor.h"', cell_text)
        self.assertIn('#include "VH400Sensor.h"', cell_text)

    def test_firmware_subsystems_are_split_out_of_main(self) -> None:
        expected_modules = [
            "FirmwareState.h",
            "FirmwareServices.h",
            "PumpConfigStore.cpp",
            "IrrigationScheduler.cpp",
            "EepromLog.cpp",
            "NetworkTime.cpp",
            "SerialCommands.cpp",
            "HttpApi.cpp",
            "CloudLogger.cpp",
            "LedStatusDisplay.cpp",
            "Diagnostics.cpp",
        ]
        for name in expected_modules:
            self.assertTrue((ROOT / "src" / name).exists(), f"{name} should exist")

        main_text = MAIN_CPP.read_text()
        self.assertLess(len(main_text.splitlines()), 220)
        self.assertNotIn("void sendHttpHeaders", main_text)
        self.assertNotIn("uint8_t configChecksum", main_text)
        self.assertNotIn("void processSerialCommand", main_text)

    def test_web_ui_is_split_out_of_main(self) -> None:
        self.assertTrue((ROOT / "src" / "WebUI.h").exists())
        self.assertTrue(WEB_UI_CPP.exists())
        self.assertTrue(HTTP_API_CPP.exists())
        self.assertIn('#include "WebUI.h"', HTTP_API_CPP.read_text())
        self.assertIn("WebUI::SendHomePage(client)", HTTP_API_CPP.read_text())
        self.assertNotIn("void sendHomePage", MAIN_CPP.read_text())

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
        text = WEB_UI_CPP.read_text()
        self.assertIn("let order=[0,2,1,3]", text)
        self.assertIn("visualCells(s.zones).map(zoneCard)", text)

    def test_dashboard_does_not_expose_removed_maintenance_controls(self) -> None:
        text = WEB_UI_CPP.read_text()
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
        text = WEB_UI_CPP.read_text()
        self.assertIn("c.relay?'open':'closed'", text)
        self.assertIn("z.relay?'open':'closed'", text)
        self.assertIn("Relay status", text)
        self.assertNotIn("c.relay?'on':'off'", text)
        self.assertNotIn("z.relay?'on':'off'", text)

    def test_stats_tab_uses_uplot_with_expected_default_series(self) -> None:
        text = WEB_UI_CPP.read_text()
        self.assertIn("data-tab=\\\"stats\\\"", text)
        self.assertIn("uPlot.iife.min.js", text)
        self.assertIn("fetch('/api/history?limit='+rangeLimit())", text)
        self.assertNotIn("logToken", text)
        self.assertNotIn("script.google.com/macros", text)
        self.assertIn("{id:'s1',label:'Sensor 1',key:'Sensor 1',scale:'pct',stroke:'#2f7d4f',show:true}", text)
        self.assertIn("{id:'s2',label:'Sensor 2',key:'Sensor 2',scale:'pct',stroke:'#1565c0',show:true}", text)
        self.assertIn("{id:'s3',label:'Sensor 3',key:'Sensor 3',scale:'pct',stroke:'#8e44ad',show:false}", text)
        self.assertIn("{id:'s4',label:'Sensor 4',key:'Sensor 4',scale:'pct',stroke:'#b26a00',show:false}", text)
        for zone in range(1, 5):
            self.assertIn(f"{{id:'w{zone}',label:'Zone {zone} water'", text)
            self.assertIn(f"{{id:'r{zone}',label:'Zone {zone} relay'", text)

    def test_irrigation_zones_report_runtime_water_estimate(self) -> None:
        state = FIRMWARE_STATE_H.read_text()
        scheduler = (ROOT / "src" / "IrrigationScheduler.cpp").read_text()
        api = HTTP_API_CPP.read_text()
        ui = WEB_UI_CPP.read_text()

        self.assertIn("PipeInsideDiameterMm = 4.2f", state)
        self.assertIn("WaterEstimateVelocityMps = 1.0f", state)
        self.assertIn("ZoneWaterOpenMs[zoneIdx] += elapsedMs", scheduler)
        self.assertIn("accountZoneWaterRuntime();", api)
        self.assertIn("waterOpenSeconds", api)
        self.assertIn("estimatedWaterMl", api)
        self.assertIn("water estimate", ui)
        self.assertIn("water runtime", ui)

    def test_irrigation_led_rendering_uses_assigned_zone_sensor(self) -> None:
        text = MAIN_CPP.read_text()
        self.assertIn("for (int zoneIdx = 0; zoneIdx < NrCells; ++zoneIdx)", text)
        self.assertIn("GardenLogic::CoerceZoneSensor(zoneIdx, Config.zoneSensor[zoneIdx])", text)
        self.assertRegex(text, re.compile(r"Cells\[zoneIdx\]\.RenderFrom\(LedMatrix,\s*Cells\[sensorIdx\]\)"))

    def test_watering_animation_is_time_based_one_revolution_per_second(self) -> None:
        text = GARDEN_CELL_CPP.read_text()
        self.assertIn("kWateringAnimationRevolutionMs = 1000", text)
        self.assertIn("millis() / frameMs", text)
        self.assertNotIn("_currentAnimFrame = (_currentAnimFrame + 1)", text)

    def test_web_client_waits_for_first_byte_but_keeps_line_reads_short(self) -> None:
        state = FIRMWARE_STATE_H.read_text()
        text = HTTP_API_CPP.read_text()
        self.assertIn("WebClientFirstByteTimeoutMs = 1000", state)
        self.assertIn("WebClientLineReadTimeoutMs = 50", state)
        self.assertIn("sawRequestByte ? WebClientLineReadTimeoutMs : WebClientFirstByteTimeoutMs", text)
        self.assertIn('requestLine.startsWith(F("GET /?"))', text)

    def test_cloud_logging_uses_smart_minute_evaluation_and_ten_minute_heartbeat(self) -> None:
        state = FIRMWARE_STATE_H.read_text()
        services = (ROOT / "src" / "FirmwareServices.h").read_text()
        logger = (ROOT / "src" / "CloudLogger.cpp").read_text()
        network = (ROOT / "src" / "NetworkTime.cpp").read_text()
        serial = (ROOT / "src" / "SerialCommands.cpp").read_text()
        api = HTTP_API_CPP.read_text()

        self.assertIn("ConfigVersion = 7", state)
        self.assertIn("LogStartAddress = 512", state)
        self.assertIn("CloudLogEvaluateIntervalMs = 60UL * 1000UL", state)
        self.assertIn("CloudLogHeartbeatMs = 10UL * 60UL * 1000UL", state)
        self.assertIn("CloudLogMoistureDeltaPercent = 1.0f", state)
        self.assertIn("CloudLogWaterDeltaMl = 100.0f", state)
        self.assertIn("struct PumpConfigV6", state)
        self.assertIn("char cloudLogEndpoint", state)
        self.assertIn("char cloudLogToken", state)
        self.assertIn("bool sendCloudLogNow(bool force)", services)
        self.assertIn("void updateCloudLogger()", services)
        self.assertIn("snapshot.relayMask != LastCloudLogSnapshot.relayMask", logger)
        self.assertIn("CloudLogHeartbeatMs", logger)
        self.assertIn("sendHttpsPostJson(Config.cloudLogEndpoint, body)", logger)
        self.assertIn("streamHttpsGetBody(cloudHistoryUrl(limit), client, 2)", logger)
        self.assertIn("updateCloudLogger();", network)
        self.assertIn("SET_LOG_ENDPOINT <https-url>", serial)
        self.assertIn("LOG_TEST", serial)
        self.assertIn('requestLine.startsWith(F("GET /api/history"))', api)


if __name__ == "__main__":
    unittest.main(verbosity=2)
