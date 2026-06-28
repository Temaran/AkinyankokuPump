from __future__ import annotations

import re
import unittest
from pathlib import Path

import garden_logic as logic


ROOT = Path(__file__).resolve().parents[1]
MAIN_CPP = ROOT / "src" / "main.cpp"
GARDEN_CELL_CPP = ROOT / "src" / "GardenCell.cpp"
GARDEN_LOGIC_H = ROOT / "src" / "GardenLogic.h"
LED_STATUS_DISPLAY_CPP = ROOT / "src" / "LedStatusDisplay.cpp"
WEB_UI_CPP = ROOT / "src" / "WebUI.cpp"
HTTP_API_CPP = ROOT / "src" / "HttpApi.cpp"
FIRMWARE_STATE_H = ROOT / "src" / "FirmwareState.h"
RUNTIME_DIAGNOSTICS_CPP = ROOT / "src" / "RuntimeDiagnostics.cpp"


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

    def test_watering_request_uses_hysteresis_band(self) -> None:
        self.assertTrue(logic.should_request_water(0.19, 0.20, 0.40, False))
        self.assertFalse(logic.should_request_water(0.31, 0.20, 0.40, False))
        self.assertTrue(logic.should_request_water(0.31, 0.20, 0.40, True))
        self.assertTrue(logic.should_request_water(0.39, 0.20, 0.40, True))
        self.assertFalse(logic.should_request_water(0.40, 0.20, 0.40, True))
        self.assertFalse(logic.should_request_water(0.61, 0.40, 0.60, True))


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
            "ShouldRequestWater",
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

    def test_dashboard_uses_clear_threshold_hysteresis_labels(self) -> None:
        text = WEB_UI_CPP.read_text()
        self.assertIn("Start below", text)
        self.assertIn("Stop above", text)
        self.assertNotIn("Low marker", text)
        self.assertNotIn("Stop watering", text)
        self.assertNotIn("Start Watering", text)
        self.assertNotIn("Stop Watering", text)

    def test_watering_state_uses_hysteresis_band(self) -> None:
        text = GARDEN_CELL_CPP.read_text()
        self.assertIn("GardenLogic::ShouldRequestWater(", text)
        self.assertIn("_startWateringThresholdNorm", text)
        self.assertIn("_stopWateringThresholdNorm", text)
        self.assertIn("_shouldWater);", text)

    def test_stats_tab_uses_uplot_with_expected_default_series(self) -> None:
        text = WEB_UI_CPP.read_text()
        self.assertIn("data-tab=\\\"stats\\\"", text)
        self.assertIn("uPlot.iife.min.js", text)
        self.assertIn("fetch('/api/history?limit='+rangeLimit())", text)
        self.assertIn("await waitForDashboardPolls()", text)
        self.assertIn("statsBusy||!dashboard.classList.contains('active')", text)
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
        self.assertIn("SpigotFlowCalibrationVolumeMl = 700.0f", state)
        self.assertIn("SpigotFlowCalibrationSeconds = 11.0f", state)
        self.assertIn("TenMeterFlowCalibrationVolumeMl = 700.0f", state)
        self.assertIn("TenMeterFlowCalibrationSeconds = 23.5f", state)
        self.assertIn("DripperSectionLengthM = 7.0f", state)
        self.assertIn("DripperCount = 10", state)
        self.assertIn("DripperSectionFullFlowEquivalentM = 2.7f", state)
        self.assertIn("WaterEstimateFlowMlPerMinute = 1620.0f", state)
        self.assertIn("WaterEstimateFlowMlPerSecond = WaterEstimateFlowMlPerMinute / 60.0f", state)
        self.assertIn("WaterEstimateVelocityMps = 1.9f", state)
        self.assertIn("ZoneWaterOpenMs[zoneIdx] += elapsedMs", scheduler)
        self.assertIn("return WaterEstimateFlowMlPerSecond", scheduler)
        self.assertIn("accountZoneWaterRuntime();", api)
        self.assertIn("waterOpenSeconds", api)
        self.assertIn("estimatedWaterMl", api)
        self.assertIn("water estimate", ui)
        self.assertIn("water runtime", ui)
        self.assertIn("flowMlps", ui)

    def test_irrigation_led_rendering_uses_assigned_zone_sensor(self) -> None:
        text = LED_STATUS_DISPLAY_CPP.read_text()
        self.assertIn("for (int zoneIdx = 0; zoneIdx < NrCells; ++zoneIdx)", text)
        self.assertIn("GardenLogic::CoerceZoneSensor(zoneIdx, Config.zoneSensor[zoneIdx])", text)
        self.assertRegex(
            text,
            re.compile(
                r"Cells\[zoneIdx\]\.RenderFrameToBuffer\([^;]+Cells\[sensorIdx\],\s*frameIdx\)"
            ),
        )

    def test_watering_animation_uses_atomic_double_buffered_sequence(self) -> None:
        display = LED_STATUS_DISPLAY_CPP.read_text()
        logger = (ROOT / "src" / "CloudLogger.cpp").read_text()
        http = HTTP_API_CPP.read_text()
        network = (ROOT / "src" / "NetworkTime.cpp").read_text()
        main = MAIN_CPP.read_text()

        self.assertIn(
            "IrrigationAnimationFrames[2][GardenWateringAnimLength][4]", display
        )
        self.assertIn(
            "nextBuffer = 1 - ActiveIrrigationAnimationBuffer", display
        )
        self.assertIn("ArduinoLEDMatrix::loadPixelsToBuffer(", display)
        self.assertIn("noInterrupts();", display)
        self.assertIn(
            "LedMatrix.loadSequence(IrrigationAnimationFrames[nextBuffer]);", display
        )
        self.assertIn("LedMatrix.play(true);", display)
        self.assertIn("ActiveIrrigationAnimationBuffer = nextBuffer;", display)
        self.assertIn("interrupts();", display)

        swap = display.split("void updateIrrigationDisplayAnimation()", 1)[1]
        build = swap.split("noInterrupts();", 1)[0]
        self.assertIn("IrrigationAnimationFrames[nextBuffer][frameIdx]", build)
        self.assertNotIn("IrrigationAnimationFrames[ActiveIrrigationAnimationBuffer]", build)
        self.assertLess(
            swap.index("ArduinoLEDMatrix::loadPixelsToBuffer("),
            swap.index("noInterrupts();"),
        )
        self.assertLess(swap.index("noInterrupts();"), swap.index("LedMatrix.loadSequence("))
        self.assertLess(
            swap.index("ActiveIrrigationAnimationBuffer = nextBuffer;"),
            swap.index("interrupts();"),
        )
        self.assertIn("updateIrrigationDisplayAnimation();", main)
        self.assertNotIn("RenderFrom(", main)
        for text in (logger, http, network, main):
            self.assertNotIn("serviceIrrigationDisplay", text)

    def test_other_display_modes_take_and_release_animation_ownership(self) -> None:
        services = (ROOT / "src" / "FirmwareServices.h").read_text()
        eeprom = (ROOT / "src" / "EepromLog.cpp").read_text()

        self.assertIn("void updateIrrigationDisplayAnimation();", services)
        self.assertIn("void invalidateIrrigationDisplayAnimation();", services)
        self.assertIn("void stopIrrigationDisplayAnimation();", services)

        enter_gather = eeprom.split("void enterGatherMode()", 1)[1].split("}", 1)[0]
        enter_irrigation = eeprom.split("void enterIrrigationMode()", 1)[1].split("}", 1)[0]
        perform_dump = eeprom.split("void performDump(bool eraseAfterDump)", 1)[1]
        self.assertIn("stopIrrigationDisplayAnimation();", enter_gather)
        self.assertIn("invalidateIrrigationDisplayAnimation();", enter_irrigation)
        self.assertLess(
            perform_dump.index("stopIrrigationDisplayAnimation();"),
            perform_dump.index("dumpLogToSerial();"),
        )

    def test_matrix_driver_calls_share_one_translation_unit(self) -> None:
        display = LED_STATUS_DISPLAY_CPP.read_text()
        services = (ROOT / "src" / "FirmwareServices.h").read_text()
        main = MAIN_CPP.read_text()
        eeprom = (ROOT / "src" / "EepromLog.cpp").read_text()

        self.assertIn("ArduinoLEDMatrix LedMatrix;", display)
        self.assertIn("void initializeLedStatusDisplay();", services)
        self.assertIn("void playStartupDisplayAnimation();", services)
        self.assertIn("initializeLedStatusDisplay();", main)
        self.assertIn("playStartupDisplayAnimation();", main)
        self.assertNotIn("LedMatrix.", main)
        self.assertNotIn("LedMatrix.", eeprom)
        self.assertIn("LedMatrix.begin();", display)
        self.assertIn("LedMatrix.loadSequence(LEDMATRIX_ANIMATION_STARTUP);", display)
        self.assertIn("LedMatrix.beginDraw();", display)
        self.assertIn("LedMatrix.endDraw();", display)

    def test_retained_runtime_diagnostics_record_the_last_stage(self) -> None:
        runtime = RUNTIME_DIAGNOSTICS_CPP.read_text()
        state = FIRMWARE_STATE_H.read_text()
        services = (ROOT / "src" / "FirmwareServices.h").read_text()
        main = MAIN_CPP.read_text()

        self.assertIn("RetainedRuntimeDiagnosticsEnabled = true", state)
        self.assertIn("RuntimeBackupRegisterOffset = 64", state)
        self.assertIn("enum class RuntimeStage", state)
        self.assertIn("struct RuntimeMetrics", state)
        self.assertIn("R_SYSTEM->VBTBKR", runtime)
        self.assertIn("RuntimeBackupVersion", runtime)
        self.assertIn("BackupChecksumOffset", runtime)
        self.assertIn("backupChecksum()", runtime)
        self.assertIn("R_SYSTEM->PRCR = BackupRegisterUnlock", runtime)
        self.assertIn("R_SYSTEM->PRCR = BackupRegisterLock", runtime)
        self.assertIn("HardwareWatchdogEnabled", state)
        self.assertIn("void initializeRuntimeDiagnostics();", services)
        self.assertIn("void setRuntimeStage(RuntimeStage stage);", services)
        self.assertIn("void printRuntimeDiagnostics(Print& out);", services)
        self.assertIn("initializeRuntimeDiagnostics();", main)
        self.assertIn("beginControlLoopIteration();", main)
        self.assertIn("completeControlLoopIteration();", main)
        self.assertIn("setRuntimeStage(RuntimeStage::SensorRead);", main)
        self.assertIn("setRuntimeStage(RuntimeStage::Irrigation);", main)

    def test_runtime_diagnostics_are_reported_over_serial_and_http(self) -> None:
        serial = (ROOT / "src" / "SerialCommands.cpp").read_text()
        api = HTTP_API_CPP.read_text()

        self.assertIn("RUNTIME_STATUS", serial)
        self.assertIn("printRuntimeDiagnostics(Serial);", serial)
        self.assertIn('\\"runtimeStage\\":\\"', api)
        self.assertIn('\\"previousResetStage\\":\\"', api)
        self.assertIn('\\"maxLoopMs\\":', api)
        self.assertIn("printRuntimeDiagnostics(client);", api)

    def test_watchdog_is_enabled_only_after_display_startup(self) -> None:
        runtime = RUNTIME_DIAGNOSTICS_CPP.read_text()
        state = FIRMWARE_STATE_H.read_text()
        services = (ROOT / "src" / "FirmwareServices.h").read_text()
        main = MAIN_CPP.read_text()

        self.assertIn("WatchdogTimeoutMs = 5000", state)
        self.assertIn("HardwareWatchdogEnabled = true", state)
        self.assertIn("bool watchdogEnabled = false", state)
        self.assertIn("#include <WDT.h>", runtime)
        self.assertIn("void initializeWatchdog()", runtime)
        self.assertIn("if (!HardwareWatchdogEnabled)", runtime)
        self.assertIn("WDT.begin(WatchdogTimeoutMs)", runtime)
        self.assertIn("void watchdogProgress()", runtime)
        self.assertIn("WDT.refresh();", runtime)
        self.assertIn("void initializeWatchdog();", services)
        self.assertIn("void watchdogProgress();", services)
        self.assertIn("initializeWatchdog();", main)
        self.assertGreater(
            main.index("initializeWatchdog();"),
            main.index("playStartupDisplayAnimation();"),
        )

    def test_watchdog_recovery_test_records_stage_and_fails_relays_safe(self) -> None:
        runtime = RUNTIME_DIAGNOSTICS_CPP.read_text()
        state = FIRMWARE_STATE_H.read_text()
        services = (ROOT / "src" / "FirmwareServices.h").read_text()
        serial = (ROOT / "src" / "SerialCommands.cpp").read_text()
        main = MAIN_CPP.read_text()

        self.assertIn("extern const int RelayPins[NrCells];", state)
        self.assertIn("WatchdogTest = 15", state)
        self.assertIn('case RuntimeStage::WatchdogTest: return F("watchdog-test")', runtime)
        self.assertIn("void initializeRelayOutputsSafe()", runtime)
        self.assertIn("digitalWrite(RelayPins[idx], LOW);", runtime)
        self.assertIn("void runWatchdogRecoveryTest()", runtime)
        watchdog_test = runtime.split("void runWatchdogRecoveryTest()", 1)[1]
        self.assertIn("setRuntimeStage(RuntimeStage::WatchdogTest);", watchdog_test)
        self.assertIn("initializeRelayOutputsSafe();", watchdog_test)
        self.assertIn("while (true)", watchdog_test)
        self.assertNotIn("watchdogProgress();", watchdog_test.split("}", 1)[0])
        self.assertIn("void initializeRelayOutputsSafe();", services)
        self.assertIn("void runWatchdogRecoveryTest();", services)
        self.assertIn("WATCHDOG_TEST", serial)
        setup = main.split("void setup()", 1)[1].split("void loop()", 1)[0]
        self.assertLess(
            setup.index("initializeRelayOutputsSafe();"),
            setup.index("Serial.begin(115200);"),
        )

    def test_long_synchronous_waits_report_watchdog_progress(self) -> None:
        logger = (ROOT / "src" / "CloudLogger.cpp").read_text()
        http = HTTP_API_CPP.read_text()
        network = (ROOT / "src" / "NetworkTime.cpp").read_text()
        main = MAIN_CPP.read_text()

        self.assertGreaterEqual(logger.count("watchdogProgress();"), 8)
        self.assertGreaterEqual(network.count("watchdogProgress();"), 4)
        self.assertIn("watchdogProgress();", http)
        self.assertIn("watchdogProgress();", main)
        self.assertEqual(
            logger.count(
                "const bool connected = remote.connect(parsed.host.c_str(), 443);"
            ),
            2,
        )
        self.assertGreaterEqual(
            logger.count(
                "const bool connected = remote.connect(parsed.host.c_str(), 443);\n"
                "            watchdogProgress();"
            ),
            2,
        )
        self.assertRegex(
            network,
            re.compile(
                r"watchdogProgress\(\);\s*"
                r"WifiStatus = WiFi\.begin\([^)]+\);\s*"
                r"watchdogProgress\(\);"
            ),
        )

    def test_tls_connect_uses_bounded_interrupt_driven_watchdog_grace(self) -> None:
        runtime = RUNTIME_DIAGNOSTICS_CPP.read_text()
        state = FIRMWARE_STATE_H.read_text()
        services = (ROOT / "src" / "FirmwareServices.h").read_text()
        logger = (ROOT / "src" / "CloudLogger.cpp").read_text()

        self.assertIn("WatchdogNetworkGraceMs = 30000UL", state)
        self.assertIn("WatchdogGraceTimerHz = 2.0f", state)
        self.assertIn("#include <FspTimer.h>", runtime)
        self.assertIn("FspTimer WatchdogGraceTimer", runtime)
        self.assertIn("volatile uint32_t WatchdogGraceTicksRemaining", runtime)
        self.assertIn("watchdogGraceTimerCallback", runtime)
        self.assertIn("--WatchdogGraceTicksRemaining;", runtime)
        self.assertIn("FspTimer::get_available_timer(type)", runtime)
        self.assertIn("WatchdogGraceTimer.setup_overflow_irq()", runtime)
        self.assertIn("WatchdogGraceTimer.open()", runtime)
        self.assertIn("WatchdogGraceTimer.start()", runtime)
        self.assertIn("bool beginWatchdogNetworkGrace();", services)
        self.assertIn("void endWatchdogNetworkGrace();", services)
        self.assertEqual(logger.count("beginWatchdogNetworkGrace()"), 2)
        self.assertEqual(logger.count("endWatchdogNetworkGrace();"), 2)
        self.assertEqual(
            logger.count('setCloudLogMessage(F("watchdog grace unavailable"))'),
            1,
        )
        for section_name in ("CloudConnect", "HistoryConnect"):
            section = logger.split(f"RuntimeStage::{section_name}", 1)[1]
            self.assertLess(
                section.index("beginWatchdogNetworkGrace()"),
                section.index("remote.connect("),
            )
            self.assertLess(
                section.index("remote.connect("),
                section.index("endWatchdogNetworkGrace();"),
            )

    def test_web_client_waits_for_first_byte_but_keeps_line_reads_short(self) -> None:
        state = FIRMWARE_STATE_H.read_text()
        text = HTTP_API_CPP.read_text()
        self.assertIn("WebClientFirstByteTimeoutMs = 1000", state)
        self.assertIn("WebClientLineReadTimeoutMs = 50", state)
        self.assertIn("sawRequestByte ? WebClientLineReadTimeoutMs : WebClientFirstByteTimeoutMs", text)
        self.assertIn('requestLine.startsWith(F("GET /?"))', text)

    def test_web_server_starts_when_wifi_is_already_connected(self) -> None:
        network = (ROOT / "src" / "NetworkTime.cpp").read_text()

        connected_branch = network.split(
            "if (WiFi.status() == WL_CONNECTED)", 1
        )[1].split("return;", 1)[0]
        self.assertIn("recoverNetworkServicesIfNeeded();", connected_branch)
        self.assertIn("WebServer.begin();", network)
        self.assertIn("Udp.begin(NtpLocalPort);", network)

    def test_network_services_recover_from_stale_esp_sockets(self) -> None:
        state = FIRMWARE_STATE_H.read_text()
        services = (ROOT / "src" / "FirmwareServices.h").read_text()
        network = (ROOT / "src" / "NetworkTime.cpp").read_text()
        logger = (ROOT / "src" / "CloudLogger.cpp").read_text()
        runtime = RUNTIME_DIAGNOSTICS_CPP.read_text()
        api = HTTP_API_CPP.read_text()

        self.assertIn("NetworkServiceStartFailureLimit = 3", state)
        self.assertIn("uint32_t webServerRestartCount = 0", state)
        self.assertIn("uint32_t networkServiceStartFailureCount = 0", state)
        self.assertIn("unsigned long lastWebRequestMs = 0", state)
        self.assertIn("void stopNetworkServices();", services)

        self.assertIn("WebServer.end();", network)
        self.assertIn("Udp.stop();", network)
        self.assertIn("WebServerStarted = false;", network)
        self.assertIn("UdpStarted = false;", network)
        self.assertIn("WebServerStarted = static_cast<bool>(WebServer);", network)
        self.assertIn("Runtime.webServerRestartCount++;", network)
        self.assertIn("Runtime.networkServiceStartFailureCount++;", network)
        self.assertNotIn("NetworkServiceRefreshMs", network)
        self.assertNotIn("requestNetworkServiceRefresh();", api)

        disconnected = network.split(
            "if (WiFi.status() == WL_CONNECTED)", 1
        )[1].split("LastWifiAttemptMs = millis();", 1)[0]
        self.assertIn("stopNetworkServices();", disconnected)

        recovery = logger.split("Runtime.consecutiveCloudFailures >= 3", 1)[1]
        self.assertLess(
            recovery.index("stopNetworkServices();"),
            recovery.index("WiFi.disconnect();"),
        )
        self.assertNotIn("WebServerStarted = false;", recovery)
        self.assertNotIn("UdpStarted = false;", recovery)

        self.assertIn("Runtime.lastWebRequestMs = millis();", runtime)
        self.assertIn('\\"webServerRestarts\\":', api)
        self.assertIn('\\"networkServiceStartFailures\\":', api)
        self.assertIn('\\"lastWebRequestMs\\":', api)

    def test_network_work_is_bounded_and_runs_after_irrigation_display(self) -> None:
        state = FIRMWARE_STATE_H.read_text()
        network = (ROOT / "src" / "NetworkTime.cpp").read_text()
        main = MAIN_CPP.read_text()

        self.assertIn("WifiConnectTimeoutMs = 1500", state)
        self.assertIn("WifiModemCommandTimeoutMs = 750", state)
        self.assertIn("CloudLogHttpTimeoutMs = 4000", state)
        self.assertIn("modem.begin(115200, 1)", network)
        self.assertIn("modem.timeout(WifiModemCommandTimeoutMs)", network)
        self.assertIn("WiFi.setTimeout(WifiConnectTimeoutMs)", network)
        self.assertIn("initializeWifiModem();", main)

        setup = main.split("void setup()", 1)[1].split("void loop()", 1)[0]
        self.assertNotIn("connectWifiIfNeeded();", setup)

        irrigation = main.split("if (!DataGatheringActive)", 1)[1].split(
            "return;", 1
        )[0]
        self.assertLess(
            irrigation.index("updateIrrigationDisplayAnimation();"),
            irrigation.index("handleWifi();"),
        )
        self.assertNotIn("LedMatrix.beginDraw();", irrigation)
        self.assertNotIn("LedMatrix.endDraw();", irrigation)

    def test_dashboard_does_not_queue_pipeline_polling(self) -> None:
        ui = WEB_UI_CPP.read_text()

        self.assertIn("setInterval(status,5000)", ui)
        self.assertNotIn("setInterval(pipelineLog", ui)

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
        self.assertIn("CloudLogHeartbeatMs = 9UL * 60UL * 1000UL", state)
        self.assertIn("CloudLogStartupDelayMs = 60UL * 1000UL", state)
        self.assertIn("CloudLogMoistureDeltaPercent = 1.0f", state)
        self.assertIn("CloudLogWaterDeltaMl = 100.0f", state)
        self.assertIn("CloudLogResponseTimeoutMs = 12000UL", state)
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
        self.assertIn("SYNC_TIME", serial)
        self.assertIn("Test cloud log", WEB_UI_CPP.read_text())
        self.assertIn("cloudLog.textContent", WEB_UI_CPP.read_text())
        self.assertIn("void sendCloudLogTest(WiFiClient& client)", services)
        self.assertIn('requestLine.startsWith(F("GET /api/test_cloud_log"))', api)
        self.assertIn('requestLine.startsWith(F("GET /api/history"))', api)
        self.assertIn("Token configured:", logger)
        self.assertIn("Time synced:", logger)
        self.assertIn("Connectivity firmware:", logger)
        self.assertIn("WIFI_FIRMWARE_LATEST_VERSION", logger)
        self.assertIn("Connectivity firmware:", network)
        self.assertIn("WIFI_FIRMWARE_LATEST_VERSION", network)
        self.assertIn("!TimeSynced && !syncTimeFromNtp()", logger)
        self.assertIn("(millis() - StartupStartedAtMs) < CloudLogStartupDelayMs", logger)
        self.assertNotIn("GoogleTrustServicesRootR1", logger)
        self.assertNotIn("remote.setCACert(", logger)
        self.assertGreaterEqual(logger.count("WiFiSSLClient remote;"), 2)
        self.assertIn("#include <Modem.h>", logger)
        self.assertIn("class ScopedCloudModemTimeout", logger)
        self.assertIn("modem.timeout(CloudLogHttpTimeoutMs);", logger)
        self.assertIn("modem.timeout(WifiModemCommandTimeoutMs);", logger)
        self.assertIn("remote.setConnectionTimeout(CloudLogHttpTimeoutMs);", logger)
        self.assertIn('command.indexOf(F("[I]"))', serial)

    def test_repeated_cloud_failures_reset_the_wifi_session(self) -> None:
        logger = (ROOT / "src" / "CloudLogger.cpp").read_text()

        self.assertIn("Runtime.consecutiveCloudFailures >= 3", logger)
        recovery = logger.split("Runtime.consecutiveCloudFailures >= 3", 1)[1]
        self.assertIn("watchdogProgress();", recovery)
        self.assertIn("stopNetworkServices();", recovery)
        self.assertIn("WiFi.disconnect();", recovery)
        self.assertIn("LastWifiAttemptMs = 0;", recovery)
        self.assertIn("Runtime.consecutiveCloudFailures = 0;", recovery)
        self.assertIn('setCloudLogMessage(F("wifi reset after failures"))', recovery)

    def test_cloud_logger_uses_connectivity_firmware_ca_bundle(self) -> None:
        logger = (ROOT / "src" / "CloudLogger.cpp").read_text()

        self.assertNotIn("-----BEGIN CERTIFICATE-----", logger)
        self.assertIn("remote.setConnectionTimeout(CloudLogHttpTimeoutMs);", logger)

    def test_cloud_history_decodes_chunked_google_response(self) -> None:
        logger = (ROOT / "src" / "CloudLogger.cpp").read_text()

        self.assertIn('headerValue(line, F("Transfer-Encoding:"))', logger)
        self.assertIn("streamChunkedHttpBody(remote, client)", logger)
        self.assertIn("strtoul(sizeLine.c_str(), nullptr, 16)", logger)
        self.assertIn("chunkBytes == 0", logger)
        self.assertIn("HttpHeaderLineMaxLength = 768", logger)
        self.assertIn('remote.println(F("Accept-Encoding: identity"))', logger)

    def test_cloud_logging_waits_for_initial_sensor_readings(self) -> None:
        logger = (ROOT / "src" / "CloudLogger.cpp").read_text()

        self.assertIn("bool cloudLogReadingsReady()", logger)
        self.assertIn("GetSensorInputMode() == SensorInputMode::NotUsed", logger)
        self.assertIn("Cells[cellIdx].GetLastRefreshMs() == 0", logger)
        self.assertIn('setCloudLogMessage(F("sensor readings not ready"))', logger)
        self.assertIn("if (!cloudLogReadingsReady())", logger)


if __name__ == "__main__":
    unittest.main(verbosity=2)
