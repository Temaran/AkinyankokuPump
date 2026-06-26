#include "FirmwareServices.h"
#include "VH400Sensor.h"
#include "WebUI.h"

namespace GardenPump
{
    void sendHttpHeaders(WiFiClient& client, const char* contentType)
    {
        client.println(F("HTTP/1.1 200 OK"));
        client.print(F("Content-Type: "));
        client.println(contentType);
        client.println(F("Connection: close"));
        client.println();
    }

    void sendJsonStatus(WiFiClient& client)
    {
        accountZoneWaterRuntime();
        sendHttpHeaders(client, "application/json");
        client.print(F("{\"mode\":\""));
        client.print(DataGatheringActive ? F("data gathering") : F("irrigation"));
        client.print(F("\",\"outOfMemory\":"));
        client.print(DataState.outOfMemory ? F("true") : F("false"));
        client.print(F(",\"dataGatheringActive\":"));
        client.print(DataGatheringActive ? F("true") : F("false"));
        client.print(F(",\"simulationEnabled\":"));
        client.print(SimulationEnabled ? F("true") : F("false"));
        client.print(F(",\"operationsStarted\":"));
        client.print(OperationsStarted ? F("true") : F("false"));
        client.print(F(",\"startupTimeWaitTimedOut\":"));
        client.print(StartupTimeWaitTimedOut ? F("true") : F("false"));
        client.print(F(",\"forcedIrrigationZone\":"));
        client.print(ForcedIrrigationZone);
        client.print(F(",\"forcedIrrigationActive\":"));
        client.print(isForcedIrrigationActive() ? F("true") : F("false"));
        client.print(F(",\"activeIrrigationZone\":"));
        client.print(ActiveIrrigationZone);
        client.print(F(",\"zoneSwitchIntervalSeconds\":"));
        client.print(Config.zoneSwitchIntervalSeconds);
        client.print(F(",\"pipeInsideDiameterMm\":"));
        client.print(PipeInsideDiameterMm, 1);
        client.print(F(",\"waterEstimateVelocityMps\":"));
        client.print(WaterEstimateVelocityMps, 1);
        client.print(F(",\"estimatedFlowMlPerSecond\":"));
        client.print(estimatedWaterFlowMlPerSecond(), 1);
        client.print(F(",\"cloudLogConfigured\":"));
        client.print(hasCloudLogConfig() ? F("true") : F("false"));
        client.print(F(",\"cloudLogOk\":"));
        client.print(LastCloudLogOk ? F("true") : F("false"));
        client.print(F(",\"cloudLogHttpStatus\":"));
        client.print(LastCloudLogHttpStatus);
        client.print(F(",\"cloudLogMessage\":\""));
        client.print(LastCloudLogMessage);
        client.print(F("\",\"cloudLogEvaluateIntervalSeconds\":"));
        client.print(CloudLogEvaluateIntervalMs / 1000UL);
        client.print(F(",\"cloudLogHeartbeatSeconds\":"));
        client.print(CloudLogHeartbeatMs / 1000UL);
        client.print(F(",\"minZoneSwitchIntervalSeconds\":"));
        client.print(MinZoneSwitchIntervalSeconds);
        client.print(F(",\"maxZoneSwitchIntervalSeconds\":"));
        client.print(MaxZoneSwitchIntervalSeconds);
        client.print(F(",\"samples\":"));
        client.print(DataState.totalSamples);
        client.print(F(",\"logIntervalSeconds\":"));
        client.print(Config.logIntervalSeconds);
        client.print(F(",\"minLogIntervalSeconds\":"));
        client.print(MinLogIntervalSeconds);
        client.print(F(",\"maxLogIntervalSeconds\":"));
        client.print(MaxLogIntervalSeconds);
        client.print(F(",\"i2cClockHz\":"));
        client.print(Config.i2cClockHz);
        client.print(F(",\"minI2cClockHz\":"));
        client.print(MinI2cClockHz);
        client.print(F(",\"maxI2cClockHz\":"));
        client.print(MaxI2cClockHz);
        client.print(F(",\"timeSynced\":"));
        client.print(TimeSynced ? F("true") : F("false"));
        client.print(F(",\"time\":\""));
        client.print(currentTimeString());
        client.print(F("\",\"timeZone\":\"Europe/Stockholm"));
        client.print(F("\",\"wifiConfigured\":"));
        client.print(hasWifiConfig() ? F("true") : F("false"));
        client.print(F(",\"wifiSsid\":\""));
        client.print(Config.wifiSsid);
        client.print(F("\",\"ip\":\""));
        client.print(WiFi.localIP());
        client.print(F("\",\"rssi\":"));
        client.print(WiFi.RSSI());
        client.print(F(",\"cells\":["));
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            if (cellIdx > 0)
            {
                client.print(',');
            }
            client.print(F("{\"index\":"));
            client.print(cellIdx);
            client.print(F(",\"address\":\"0x"));
            client.print(Cells[cellIdx].GetSensorAddress(), HEX);
            client.print(F("\",\"analogPin\":\""));
            printAnalogPinName(client, Cells[cellIdx].GetAnalogPin());
            client.print(F("\",\"analogPinNumber\":"));
            client.print(Cells[cellIdx].GetAnalogPin());
            client.print(F(",\"sensorSource\":\""));
            if (Config.sensorInputMode[cellIdx] == static_cast<uint8_t>(SensorInputMode::Vh400))
            {
                client.print(F("vh400"));
            }
            else if (Config.sensorInputMode[cellIdx] == static_cast<uint8_t>(SensorInputMode::NotUsed))
            {
                client.print(F("unused"));
            }
            else
            {
                client.print(F("seesaw"));
            }
            client.print(F("\",\"connected\":"));
            client.print(Cells[cellIdx].HasConnected() ? F("true") : F("false"));
            client.print(F(",\"error\":"));
            client.print(Cells[cellIdx].HasError() ? F("true") : F("false"));
            client.print(F(",\"raw\":"));
            client.print(Cells[cellIdx].GetLastCapacitanceReading());
            client.print(F(",\"filteredRaw\":"));
            client.print(Cells[cellIdx].GetFilteredCapacitanceReading());
            client.print(F(",\"voltageMv\":"));
            client.print(Cells[cellIdx].GetLastVoltageMv());
            client.print(F(",\"vwcPercent\":"));
            client.print(Cells[cellIdx].GetLastVwcPercent(), 1);
            client.print(F(",\"vh400ConnectionScore\":"));
            client.print(Cells[cellIdx].GetVh400ConnectionScore());
            client.print(F(",\"readSpread\":"));
            client.print(Cells[cellIdx].GetLastReadSpread());
            client.print(F(",\"acceptedReads\":"));
            client.print(Cells[cellIdx].GetAcceptedReadCount());
            client.print(F(",\"moisture\":"));
            client.print(Cells[cellIdx].GetMoistureByte());
            client.print(F(",\"moisturePercent\":"));
            client.print(Cells[cellIdx].GetMoistnessNorm() * 100.0f, 1);
            client.print(F(",\"simulated\":"));
            client.print(Cells[cellIdx].IsSimulated() ? F("true") : F("false"));
            client.print(F(",\"simulatedMoisturePercent\":"));
            client.print(Cells[cellIdx].GetSimulatedMoisturePercent());
            client.print(F(",\"relay\":"));
            client.print(Cells[cellIdx].IsRelayEnabled() ? F("true") : F("false"));
            client.print(F(",\"needsWater\":"));
            client.print(Cells[cellIdx].ShouldWater() ? F("true") : F("false"));
            client.print(F(",\"startThreshold\":"));
            client.print(Config.startThreshold[cellIdx]);
            client.print(F(",\"stopThreshold\":"));
            client.print(Config.stopThreshold[cellIdx]);
            client.print(F(",\"dryCalibrationRaw\":"));
            client.print(Config.dryCalibrationRaw[cellIdx]);
            client.print(F(",\"wetCalibrationRaw\":"));
            client.print(Config.wetCalibrationRaw[cellIdx]);
            client.print('}');
        }
        client.print(F("],\"zones\":["));
        for (int zoneIdx = 0; zoneIdx < NrCells; ++zoneIdx)
        {
            if (zoneIdx > 0)
            {
                client.print(',');
            }
            client.print(F("{\"index\":"));
            client.print(zoneIdx);
            client.print(F(",\"sensor\":"));
            client.print(Config.zoneSensor[zoneIdx]);
            client.print(F(",\"needsWater\":"));
            client.print(zoneNeedsWater(zoneIdx) ? F("true") : F("false"));
            client.print(F(",\"relay\":"));
            client.print(Cells[zoneIdx].IsRelayEnabled() ? F("true") : F("false"));
            client.print(F(",\"waterOpenSeconds\":"));
            client.print(ZoneWaterOpenMs[zoneIdx] / 1000UL);
            client.print(F(",\"estimatedWaterMl\":"));
            client.print(estimatedZoneWaterMl(zoneIdx), 1);
            client.print('}');
        }
        client.println(F("]}"));
    }

    void sendTextDiagnostics(WiFiClient& client, bool force)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        if (DataState.outOfMemory && !force)
        {
            client.println(F("DIAG skipped: EEPROM log is full. Use DIAG_FORCE or clear the log."));
            return;
        }

        SensorSnapshot snapshot;
        sampleAllCells(snapshot);
        for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
        {
            DataState.latest[cellIdx] = snapshot.moisture[cellIdx];
        }
        printSensorDiagnostics(client, snapshot);
    }

    void sendTextI2CScan(WiFiClient& client)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        scanI2CBus(client);
    }

    void printRawSensorDiagnostics(Print& out)
    {
        out.println(F("Raw sensor diagnostics:"));
        out.print(F("  direct analog scan: A0="));
        out.print(readAnalogSettled(A0));
        out.print(F(" A1="));
        out.print(readAnalogSettled(A1));
        out.print(F(" A2="));
        out.print(readAnalogSettled(A2));
        out.print(F(" A3="));
        out.println(readAnalogSettled(A3));

        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            out.print(F("  cell "));
            out.print(cellIdx);
            out.print(F(" source="));
            printSensorInputMode(out, Cells[cellIdx].GetSensorInputMode());
            out.print(F(" addr=0x"));
            out.print(Cells[cellIdx].GetSensorAddress(), HEX);
            out.print(F(" analog="));
            printAnalogPinName(out, Cells[cellIdx].GetAnalogPin());
            out.print(F("("));
            out.print(Cells[cellIdx].GetAnalogPin());
            out.print(F(")"));
            out.print(F(" connected="));
            out.print(Cells[cellIdx].HasConnected() ? F("yes") : F("no"));

            if (Cells[cellIdx].GetSensorInputMode() == SensorInputMode::Vh400 || Cells[cellIdx].IsSimulated())
            {
                out.print(F(" reads="));
                for (int sampleIdx = 0; sampleIdx < 12; ++sampleIdx)
                {
                    if (sampleIdx > 0)
                    {
                        out.print(',');
                    }
                    out.print(Cells[cellIdx].ReadRawCapacitance());
                    delay(25);
                }
                out.println();
                continue;
            }

            if (!Cells[cellIdx].HasConnected())
            {
                out.println();
                continue;
            }

            out.print(F(" version=0x"));
            out.print(Cells[cellIdx].GetSensorVersion(), HEX);
            out.print(F(" tempC="));
            out.print(Cells[cellIdx].GetSensorTemperatureC(), 1);
            out.print(F(" reads="));
            for (int sampleIdx = 0; sampleIdx < 12; ++sampleIdx)
            {
                if (sampleIdx > 0)
                {
                    out.print(',');
                }
                out.print(Cells[cellIdx].ReadRawCapacitance());
                delay(25);
            }
            out.println();
        }
    }

    void sendRawSensorDiagnostics(WiFiClient& client)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        printRawSensorDiagnostics(client);
    }

    void sendAnalogDiagnostics(WiFiClient& client)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        client.println(F("Analog diagnostics:"));
        client.println(F("  Settled reads, no smoothing/filter/confidence."));
        client.println(F("  Compare these rows against direct multimeter measurements on A0..A3."));

        const int pins[NrCells] = {A0, A1, A2, A3};
        const char* names[NrCells] = {"A0", "A1", "A2", "A3"};

        client.println();
        client.println(F("Single-pin blocks:"));
        for (int pinIdx = 0; pinIdx < NrCells; ++pinIdx)
        {
            client.print(F("  "));
            client.print(names[pinIdx]);
            client.print(F(":"));
            for (int sample = 0; sample < 4; ++sample)
            {
                client.print(' ');
                client.print(readAnalogSettled(pins[pinIdx]));
                delay(2);
            }
            client.println();
        }

        client.println();
        client.println(F("Sequential A0 A1 A2 A3 rows:"));
        for (int row = 0; row < 4; ++row)
        {
            client.print(F("  row "));
            client.print(row);
            client.print(F(": A0="));
            client.print(readAnalogSettled(A0));
            client.print(F(" A1="));
            client.print(readAnalogSettled(A1));
            client.print(F(" A2="));
            client.print(readAnalogSettled(A2));
            client.print(F(" A3="));
            client.println(readAnalogSettled(A3));
            delay(2);
        }

        client.println();
        client.println(F("After reading A2, then target pin:"));
        for (int pinIdx = 0; pinIdx < NrCells; ++pinIdx)
        {
            client.print(F("  A2 -> "));
            client.print(names[pinIdx]);
            client.print(F(":"));
            for (int sample = 0; sample < 4; ++sample)
            {
                readAnalogSettled(A2);
                client.print(' ');
                client.print(readAnalogSettled(pins[pinIdx]));
                delay(2);
            }
            client.println();
        }
    }

    void capturePipelineEntry(PipelineLogEntry& entry)
    {
        entry.timestampMs = millis();
        entry.currentCell = CurrentCell;
        entry.dataGathering = DataGatheringActive;
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            GardenCell& cell = Cells[cellIdx];
            PipelineCellLog& log = entry.cells[cellIdx];
            log = PipelineCellLog{};
            log.readCount = static_cast<uint8_t>(min(cell.GetDebugReadCount(), PipelineLogReadSlots));
            for (int idx = 0; idx < log.readCount; ++idx)
            {
                log.readings[idx] = cell.GetDebugReading(idx);
            }
            log.stableCluster = cell.GetDebugStableCluster();
            log.spread = cell.GetLastReadSpread();
            log.accepted = cell.GetAcceptedReadCount();
            log.selectedRaw = cell.GetDebugSelectedReading();
            log.lastRaw = cell.GetLastCapacitanceReading();
            log.filteredRaw = cell.GetFilteredCapacitanceReading();
            log.voltageMv = cell.GetLastVoltageMv();
            log.vwcPercent = cell.GetLastVwcPercent();
            log.moistureNorm = cell.GetMoistnessNorm();
            log.moistureByte = cell.GetMoistureByte();
            log.latest = DataState.latest[cellIdx];
            log.connected = cell.HasConnected();
            log.error = cell.HasError();
            log.relay = cell.IsRelayEnabled();
            log.source = cell.GetSensorInputMode();
            log.ageMs = entry.timestampMs - cell.GetLastRefreshMs();
        }
    }

    void updatePipelineHistory()
    {
        if ((millis() - LastPipelineLogMs) < PipelineLogIntervalMs)
        {
            return;
        }

        LastPipelineLogMs = millis();
        capturePipelineEntry(PipelineHistory[PipelineHistoryNext]);
        PipelineHistoryNext = (PipelineHistoryNext + 1) % PipelineLogEntries;
        if (PipelineHistoryCount < PipelineLogEntries)
        {
            PipelineHistoryCount++;
        }
    }

    void printPipelineCell(Print& out, int cellIdx, const PipelineCellLog& cell)
    {
        out.print(F("slot "));
        out.print(cellIdx + 1);
        out.print(F(" cell="));
        out.print(cellIdx);
        out.print(F(" source="));
        printSensorInputMode(out, cell.source);
        out.print(F(" ageMs="));
        out.print(cell.ageMs);
        out.print(F(" pin="));
        printAnalogPinName(out, Cells[cellIdx].GetAnalogPin());
        out.print(F("("));
        out.print(Cells[cellIdx].GetAnalogPin());
        out.print(F(") addr=0x"));
        out.println(Cells[cellIdx].GetSensorAddress(), HEX);

        out.print(F("  1 read samples:"));
        if (cell.readCount == 0)
        {
            out.print(F(" none"));
        }
        for (int idx = 0; idx < cell.readCount; ++idx)
        {
            out.print(idx == 0 ? F(" ") : F(","));
            out.print(cell.readings[idx]);
        }
        out.println();

        out.print(F("  2 stable cluster: "));
        out.print(cell.stableCluster ? F("yes") : F("no"));
        out.print(F(" spread="));
        out.print(cell.spread);
        out.print(F(" accepted="));
        out.println(cell.accepted);

        out.print(F("  3 selected raw: "));
        out.print(cell.selectedRaw);
        out.print(F(" lastRaw="));
        out.println(cell.lastRaw);

        out.print(F("  4 filtered raw: "));
        out.print(cell.filteredRaw);
        out.print(F(" hasConnected="));
        out.print(cell.connected ? F("true") : F("false"));
        out.print(F(" hasError="));
        out.println(cell.error ? F("true") : F("false"));

        out.print(F("  5 voltage/VWC: "));
        out.print(cell.voltageMv);
        out.print(F("mV from "));
        out.print(cell.selectedRaw);
        out.print(F("/"));
        out.print(DebugAdcMaxReading);
        out.print(F("*"));
        out.print(DebugAnalogReferenceMv);
        out.print(F("mV "));
        out.print(cell.vwcPercent, 1);
        out.println(F("%"));

        out.print(F("  6 norm/byte: "));
        out.print(cell.moistureNorm, 3);
        out.print(F(" -> "));
        out.println(cell.moistureByte);

        out.print(F("  7 cache/status: latest="));
        out.print(cell.latest);
        out.print(F(" relay="));
        out.println(cell.relay ? F("open") : F("closed"));
    }

    void printPipelineEntry(Print& out, const PipelineLogEntry& entry)
    {
        out.print(F("Pipeline history ms="));
        out.print(entry.timestampMs);
        out.print(F(" currentCell="));
        out.print(entry.currentCell);
        out.print(F(" dataGathering="));
        out.println(entry.dataGathering ? F("true") : F("false"));
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            printPipelineCell(out, cellIdx, entry.cells[cellIdx]);
        }
    }

    void sendPipelineLog(WiFiClient& client)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        updatePipelineHistory();
        client.print(F("Pipeline history count="));
        client.print(PipelineHistoryCount);
        client.print(F(" intervalMs="));
        client.print(PipelineLogIntervalMs);
        client.print(F(" statusPoll=browser fetch /api/status; pipelinePoll=browser fetch /api/pipeline_log"));
        client.println();
        client.println(F("Order: read samples -> stable cluster -> selected raw -> filtered raw -> voltage/VWC -> norm/byte -> cache/status"));
        if (PipelineHistoryCount == 0)
        {
            client.println(F("No entries recorded yet."));
            return;
        }

        for (int entryOffset = 0; entryOffset < PipelineHistoryCount; ++entryOffset)
        {
            const int historyIdx = (PipelineHistoryNext + PipelineLogEntries - 1 - entryOffset) % PipelineLogEntries;
            client.println();
            printPipelineEntry(client, PipelineHistory[historyIdx]);
        }
    }

    void sendPipelineLatest(WiFiClient& client)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        updatePipelineHistory();
        if (PipelineHistoryCount == 0)
        {
            client.println(F("No entries recorded yet."));
            return;
        }

        const int historyIdx = (PipelineHistoryNext + PipelineLogEntries - 1) % PipelineLogEntries;
        printPipelineEntry(client, PipelineHistory[historyIdx]);
    }

    int queryIntValue(const String& requestLine, const char* key, int fallback)
    {
        String needle = String(key) + "=";
        int start = requestLine.indexOf(needle);
        if (start < 0)
        {
            return fallback;
        }
        start += needle.length();
        int end = requestLine.indexOf('&', start);
        if (end < 0)
        {
            end = requestLine.indexOf(' ', start);
        }
        if (end < 0)
        {
            end = requestLine.length();
        }
        return requestLine.substring(start, end).toInt();
    }

    char hexNibble(char ch)
    {
        if (ch >= '0' && ch <= '9')
        {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f')
        {
            return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F')
        {
            return ch - 'A' + 10;
        }
        return 0;
    }

    String urlDecode(String value)
    {
        String decoded = "";
        for (int idx = 0; idx < value.length(); ++idx)
        {
            const char ch = value[idx];
            if (ch == '+')
            {
                decoded += ' ';
            }
            else if (ch == '%' && (idx + 2) < value.length())
            {
                decoded += static_cast<char>((hexNibble(value[idx + 1]) << 4) | hexNibble(value[idx + 2]));
                idx += 2;
            }
            else
            {
                decoded += ch;
            }
        }
        return decoded;
    }

    String queryStringValue(const String& requestLine, const char* key)
    {
        String needle = String(key) + "=";
        int start = requestLine.indexOf(needle);
        if (start < 0)
        {
            return "";
        }
        start += needle.length();
        int end = requestLine.indexOf('&', start);
        if (end < 0)
        {
            end = requestLine.indexOf(' ', start);
        }
        if (end < 0)
        {
            end = requestLine.length();
        }
        return urlDecode(requestLine.substring(start, end));
    }

    void printAnalogPinName(Print& out, int analogPin)
    {
        if (analogPin == A0)
        {
            out.print(F("A0"));
        }
        else if (analogPin == A1)
        {
            out.print(F("A1"));
        }
        else if (analogPin == A2)
        {
            out.print(F("A2"));
        }
        else if (analogPin == A3)
        {
            out.print(F("A3"));
        }
        else if (analogPin == A4)
        {
            out.print(F("A4"));
        }
        else if (analogPin == A5)
        {
            out.print(F("A5"));
        }
        else
        {
            out.print(analogPin);
        }
    }

    int readAnalogSettled(int analogPin)
    {
        return VH400Sensor::ReadAnalogSettled(analogPin);
    }

    void sendThresholdUpdate(WiFiClient& client, const String& requestLine)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        const int cellIdx = queryIntValue(requestLine, "cell", -1);
        const int startPercent = queryIntValue(requestLine, "start", -1);
        const int stopPercent = queryIntValue(requestLine, "stop", -1);

        if (!setCellThresholds(cellIdx, static_cast<uint8_t>(startPercent), static_cast<uint8_t>(stopPercent)))
        {
            client.println(F("Invalid thresholds. Use cell=0..3, start=0..100, stop=0..100, and stop >= start."));
            return;
        }

        client.print(F("Updated cell "));
        client.print(cellIdx);
        client.print(F(" thresholds: start="));
        client.print(startPercent);
        client.print(F("% stop="));
        client.print(stopPercent);
        client.println('%');
    }

    void sendCalibrationUpdate(WiFiClient& client, const String& requestLine)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        const int cellIdx = queryIntValue(requestLine, "cell", -1);
        const int dryRaw = queryIntValue(requestLine, "dry", -1);
        const int wetRaw = queryIntValue(requestLine, "wet", -1);

        if (dryRaw < 0 || wetRaw < 0
            || !setCellCalibration(cellIdx, static_cast<uint16_t>(dryRaw), static_cast<uint16_t>(wetRaw)))
        {
            client.println(F("Invalid calibration. Use cell=0..3 and 0 <= dry < wet <= 1023."));
            return;
        }

        client.print(F("Updated cell "));
        client.print(cellIdx);
        client.print(F(" calibration: dry="));
        client.print(dryRaw);
        client.print(F(" wet="));
        client.println(wetRaw);
    }

    void sendSensorInputModeUpdate(WiFiClient& client, const String& requestLine)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        const int cellIdx = queryIntValue(requestLine, "cell", -1);
        const String source = queryStringValue(requestLine, "source");
        SensorInputMode mode = SensorInputMode::Seesaw;
        if (source == F("vh400"))
        {
            mode = SensorInputMode::Vh400;
        }
        else if (source == F("unused") || source == F("not_used") || source == F("none"))
        {
            mode = SensorInputMode::NotUsed;
        }
        else if (source != F("seesaw"))
        {
            client.println(F("Invalid sensor source. Use source=unused, source=seesaw, or source=vh400."));
            return;
        }

        if (!setCellSensorInputMode(cellIdx, mode))
        {
            client.println(F("Invalid cell. Use cell=0..3."));
            return;
        }

        client.print(F("Updated cell "));
        client.print(cellIdx);
        client.print(F(" sensor source: "));
        if (mode == SensorInputMode::Vh400)
        {
            client.println(F("VH400"));
        }
        else if (mode == SensorInputMode::NotUsed)
        {
            client.println(F("not used"));
        }
        else
        {
            client.println(F("seesaw"));
        }
    }

    void sendLogIntervalUpdate(WiFiClient& client, const String& requestLine)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        const int intervalSeconds = queryIntValue(requestLine, "seconds", -1);
        if (intervalSeconds < 0 || !setLogIntervalSeconds(static_cast<uint32_t>(intervalSeconds)))
        {
            client.print(F("Invalid sample interval. Use "));
            client.print(MinLogIntervalSeconds);
            client.print(F(".."));
            client.print(MaxLogIntervalSeconds);
            client.println(F(" seconds."));
            return;
        }

        client.print(F("Sample interval saved: "));
        client.print(Config.logIntervalSeconds);
        client.println(F(" seconds."));
    }

    void sendZoneSwitchIntervalUpdate(WiFiClient& client, const String& requestLine)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        const int intervalSeconds = queryIntValue(requestLine, "seconds", -1);
        if (intervalSeconds < 0 || !setZoneSwitchIntervalSeconds(static_cast<uint32_t>(intervalSeconds)))
        {
            client.print(F("Invalid zone switch interval. Use "));
            client.print(MinZoneSwitchIntervalSeconds);
            client.print(F(".."));
            client.print(MaxZoneSwitchIntervalSeconds);
            client.println(F(" seconds."));
            return;
        }

        client.print(F("Zone switch interval saved: "));
        client.print(Config.zoneSwitchIntervalSeconds);
        client.println(F(" seconds."));
    }

    void sendZoneSensorUpdate(WiFiClient& client, const String& requestLine)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        const int zoneIdx = queryIntValue(requestLine, "zone", -1);
        const int sensorIdx = queryIntValue(requestLine, "sensor", -1);
        if (!setZoneSensor(zoneIdx, sensorIdx))
        {
            client.println(F("Invalid zone sensor mapping. Use zone=0..3 and sensor=0..3."));
            return;
        }

        client.print(F("Updated zone "));
        client.print(zoneIdx + 1);
        client.print(F(" sensor: slot "));
        client.print(sensorIdx + 1);
        client.println('.');
    }

    void sendI2cClockUpdate(WiFiClient& client, const String& requestLine)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        const int clockHz = queryIntValue(requestLine, "hz", -1);
        if (clockHz < 0 || !setI2cClockHz(static_cast<uint32_t>(clockHz)))
        {
            client.print(F("Invalid I2C clock. Use "));
            client.print(MinI2cClockHz);
            client.print(F(".."));
            client.print(MaxI2cClockHz);
            client.println(F(" Hz."));
            return;
        }

        client.print(F("I2C clock saved and applied: "));
        client.print(Config.i2cClockHz);
        client.println(F(" Hz."));
    }

    void sendForcedIrrigationUpdate(WiFiClient& client, const String& requestLine)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        const int zone = queryIntValue(requestLine, "zone", -2);
        if (!setForcedIrrigationZone(zone))
        {
            client.println(F("Invalid forced irrigation zone. Use zone=-1 for off or zone=0..3."));
            return;
        }

        if (isForcedIrrigationActive())
        {
            client.print(F("Forced irrigation enabled for zone "));
            client.print(ForcedIrrigationZone + 1);
            client.println('.');
        }
        else
        {
            client.println(F("Forced irrigation off."));
        }
    }

    void sendDataGatheringUpdate(WiFiClient& client, const String& requestLine)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        const int enabled = queryIntValue(requestLine, "enabled", -1);
        if (enabled != 0 && enabled != 1)
        {
            client.println(F("Invalid data gathering flag. Use enabled=0 or enabled=1."));
            return;
        }

        setDataGatheringActive(enabled == 1);
        client.println(DataGatheringActive ? F("Data gathering active; irrigation halted.") : F("Data gathering stopped; irrigation active."));
    }

    void sendSimulationUpdate(WiFiClient& client, const String& requestLine)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        const int enabled = queryIntValue(requestLine, "enabled", -1);
        if (enabled != 0 && enabled != 1)
        {
            client.println(F("Invalid simulation flag. Use enabled=0 or enabled=1."));
            return;
        }

        SimulationEnabled = enabled == 1;
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            char key[3] = {'c', static_cast<char>('0' + cellIdx), '\0'};
            const int value = queryIntValue(requestLine, key, SimulatedMoisturePercent[cellIdx]);
            SimulatedMoisturePercent[cellIdx] = static_cast<uint8_t>(constrain(value, 0, 100));
            Cells[cellIdx].SetSimulatedMoisture(SimulationEnabled, SimulatedMoisturePercent[cellIdx]);
        }

        client.print(F("Simulation "));
        client.println(SimulationEnabled ? F("enabled.") : F("disabled."));
    }

    void sendWifiUpdate(WiFiClient& client, const String& requestLine)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        const String ssid = queryStringValue(requestLine, "ssid");
        const String password = queryStringValue(requestLine, "password");
        if (!setWifiConfig(ssid, password))
        {
            client.println(F("Invalid WiFi credentials. SSID max 32 chars, password max 64 chars."));
            return;
        }

        client.println(F("WiFi credentials saved to EEPROM config. Resetting."));
        client.flush();
        NVIC_SystemReset();
        while (true) {}
    }

    void sendCloudLogEndpointUpdate(WiFiClient& client, const String& requestLine)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        const String endpoint = queryStringValue(requestLine, "url");
        if (!setCloudLogEndpoint(endpoint))
        {
            client.print(F("Invalid cloud log endpoint. Use an https URL up to "));
            client.print(CloudLogEndpointMaxLength);
            client.println(F(" chars."));
            return;
        }

        client.println(F("Cloud log endpoint saved."));
    }

    void sendCloudLogTokenUpdate(WiFiClient& client, const String& requestLine)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        const String token = queryStringValue(requestLine, "token");
        if (!setCloudLogToken(token))
        {
            client.print(F("Invalid cloud log token. Use 1.."));
            client.print(CloudLogTokenMaxLength);
            client.println(F(" chars."));
            return;
        }

        client.println(F("Cloud log token saved."));
    }

    void sendCloudLogTest(WiFiClient& client)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        client.println(F("Sending cloud log test row..."));
        const bool ok = sendCloudLogNow(true);
        client.println(ok ? F("Cloud log sent.") : F("Cloud log send failed."));
        printCloudLogStatus(client);
    }

    void sendTextDump(WiFiClient& client, bool eraseAfterDump)
    {
        sendHttpHeaders(client, "text/plain; charset=utf-8");
        dumpLog(client);
        if (eraseAfterDump)
        {
            eraseEntireLog();
            client.println(F("EEPROM erased. Resetting."));
            client.flush();
            NVIC_SystemReset();
            while (true) {}
        }
    }

    void sendNotFound(WiFiClient& client)
    {
        client.println(F("HTTP/1.1 404 Not Found"));
        client.println(F("Content-Type: text/plain; charset=utf-8"));
        client.println(F("Connection: close"));
        client.println();
        client.println(F("Not found"));
    }

    void handleWebClient()
    {
        if (!WebServerStarted)
        {
            return;
        }

        WiFiClient client = WebServer.available();
        if (!client)
        {
            return;
        }

        String requestLine = "";
        bool sawRequestByte = false;
        unsigned long start = millis();
        unsigned long lastByteMs = start;
        while (client.connected())
        {
            if (!client.available())
            {
                const unsigned long now = millis();
                const unsigned long timeout = sawRequestByte ? WebClientLineReadTimeoutMs : WebClientFirstByteTimeoutMs;
                const unsigned long basis = sawRequestByte ? lastByteMs : start;
                if ((now - basis) >= timeout)
                {
                    break;
                }
                delay(1);
                continue;
            }
            const char ch = static_cast<char>(client.read());
            sawRequestByte = true;
            lastByteMs = millis();
            if (ch == '\r')
            {
                continue;
            }
            if (ch == '\n')
            {
                break;
            }
            if (requestLine.length() < 320)
            {
                requestLine += ch;
            }
        }

        while (client.available())
        {
            client.read();
        }

        if (requestLine.startsWith(F("GET / ")) || requestLine.startsWith(F("GET /?")) || requestLine.startsWith(F("GET /HTTP")))
        {
            WebUI::SendHomePage(client);
        }
        else if (requestLine.startsWith(F("GET /api/status")))
        {
            sendJsonStatus(client);
        }
        else if (requestLine.startsWith(F("GET /api/history")))
        {
            sendCloudHistory(client, requestLine);
        }
        else if (requestLine.startsWith(F("GET /api/diag_force")))
        {
            sendTextDiagnostics(client, true);
        }
        else if (requestLine.startsWith(F("GET /api/diag")))
        {
            sendTextDiagnostics(client, false);
        }
        else if (requestLine.startsWith(F("GET /api/raw_diag")))
        {
            sendRawSensorDiagnostics(client);
        }
        else if (requestLine.startsWith(F("GET /api/analog_diag")))
        {
            sendAnalogDiagnostics(client);
        }
        else if (requestLine.startsWith(F("GET /api/pipeline_log")))
        {
            sendPipelineLog(client);
        }
        else if (requestLine.startsWith(F("GET /api/pipeline_latest")))
        {
            sendPipelineLatest(client);
        }
        else if (requestLine.startsWith(F("GET /api/i2c_scan")))
        {
            sendTextI2CScan(client);
        }
        else if (requestLine.startsWith(F("GET /api/set_threshold")))
        {
            sendThresholdUpdate(client, requestLine);
        }
        else if (requestLine.startsWith(F("GET /api/set_calibration")))
        {
            sendCalibrationUpdate(client, requestLine);
        }
        else if (requestLine.startsWith(F("GET /api/set_sensor_source")))
        {
            sendSensorInputModeUpdate(client, requestLine);
        }
        else if (requestLine.startsWith(F("GET /api/set_interval")))
        {
            sendLogIntervalUpdate(client, requestLine);
        }
        else if (requestLine.startsWith(F("GET /api/set_zone_switch_interval")))
        {
            sendZoneSwitchIntervalUpdate(client, requestLine);
        }
        else if (requestLine.startsWith(F("GET /api/set_zone_sensor")))
        {
            sendZoneSensorUpdate(client, requestLine);
        }
        else if (requestLine.startsWith(F("GET /api/set_i2c_clock")))
        {
            sendI2cClockUpdate(client, requestLine);
        }
        else if (requestLine.startsWith(F("GET /api/set_forced_irrigation")))
        {
            sendForcedIrrigationUpdate(client, requestLine);
        }
        else if (requestLine.startsWith(F("GET /api/set_data_gathering")))
        {
            sendDataGatheringUpdate(client, requestLine);
        }
        else if (requestLine.startsWith(F("GET /api/set_simulation")))
        {
            sendSimulationUpdate(client, requestLine);
        }
        else if (requestLine.startsWith(F("GET /api/set_wifi")))
        {
            sendWifiUpdate(client, requestLine);
        }
        else if (requestLine.startsWith(F("GET /api/set_cloud_log_endpoint")))
        {
            sendCloudLogEndpointUpdate(client, requestLine);
        }
        else if (requestLine.startsWith(F("GET /api/set_cloud_log_token")))
        {
            sendCloudLogTokenUpdate(client, requestLine);
        }
        else if (requestLine.startsWith(F("GET /api/test_cloud_log")))
        {
            sendCloudLogTest(client);
        }
        else if (requestLine.startsWith(F("GET /api/dump_no_erase")))
        {
            sendTextDump(client, false);
        }
        else if (requestLine.startsWith(F("GET /api/dump?confirm=yes")))
        {
            sendTextDump(client, true);
        }
        else if (requestLine.startsWith(F("GET /api/clear_log?confirm=yes")))
        {
            sendHttpHeaders(client, "text/plain; charset=utf-8");
            eraseEntireLog();
            client.println(F("EEPROM erased. Resetting."));
            client.flush();
            NVIC_SystemReset();
            while (true) {}
        }
        else if (requestLine.startsWith(F("GET /api/sync_time")))
        {
            sendHttpHeaders(client, "text/plain; charset=utf-8");
            client.println(syncTimeFromNtp() ? F("RTC synced from NTP.") : F("NTP sync failed."));
            client.println(currentTimeString());
        }
        else
        {
            sendNotFound(client);
        }

        delay(1);
        client.stop();
    }
}
