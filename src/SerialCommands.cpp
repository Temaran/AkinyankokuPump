#include "FirmwareServices.h"

#include <Wire.h>

namespace GardenPump
{
    void printModeHelp()
    {
        Serial.println();
        Serial.println(F("Garden pump controls:"));
        Serial.println(F("  Irrigation starts by default"));
        Serial.println(F("  SET_DATA_GATHERING <0|1> = runtime logging mode; disables irrigation while active"));
        Serial.println(F("  DUMP = dump EEPROM over USB, erase it, then reset"));
        Serial.println(F("  DUMP_NO_ERASE = dump EEPROM over USB and keep the data"));
        Serial.println(F("  DIAG = print live sensor diagnostics"));
        Serial.println(F("  RAW_DIAG = print untouched sensor burst reads"));
        Serial.println(F("  SET_INTERVAL <seconds> = save sample interval to EEPROM config"));
        Serial.println(F("  SET_I2C_CLOCK <hz> = save and apply I2C clock to EEPROM config"));
        Serial.println(F("  SET_CALIBRATION <cell> <dryRaw> <wetRaw> = save raw moisture calibration"));
        Serial.println(F("  SET_SENSOR_SOURCE <cell> <unused|seesaw|vh400> = save sensor input source"));
        Serial.println(F("  SET_ZONE_SENSOR <zone> <sensor> = save which sensor controls an irrigation zone"));
        Serial.println(F("  SET_ZONE_SWITCH_INTERVAL <seconds> = save irrigation zone swap interval"));
        Serial.println(F("  SET_LOG_ENDPOINT <https-url> = save Google Apps Script log endpoint"));
        Serial.println(F("  SET_LOG_TOKEN <token> = save cloud log secret token"));
        Serial.println(F("  LOG_STATUS = print cloud log config and last send result"));
        Serial.println(F("  LOG_TEST = send one cloud log row now"));
        Serial.println(F("  CLEAR_LOG_ENDPOINT = remove cloud logging endpoint and token"));
        Serial.println(F("  SYNC_TIME = retry NTP time sync"));
        Serial.println(F("  SET_SIMULATION <0|1> [c0 c1 c2 c3] = runtime simulated moisture percent"));
        Serial.println(F("  SET_FORCED_ZONE <-1..3> = runtime irrigation override; -1 disables it"));
        Serial.println(F("  SET_WIFI <ssid> <password> = save WiFi credentials to EEPROM config"));
        Serial.println(F("  CLEAR_WIFI = remove saved WiFi credentials"));
        Serial.println(F("  WIFI_STATUS = print WiFi config and connection status"));
        Serial.println(F("  r = reboot"));
        Serial.println();
    }

    void processSerialCommand(String command)
    {
        command.trim();
        String commandUpper = command;
        commandUpper.toUpperCase();

        if (commandUpper.length() == 0)
        {
            return;
        }

        if (command.startsWith(F("[")) &&
            (command.indexOf(F("[E]")) >= 0 ||
             command.indexOf(F("[W]")) >= 0 ||
             command.indexOf(F("[I]")) >= 0))
        {
            return;
        }

        if (commandUpper == F("R"))
        {
            NVIC_SystemReset();
            while (true) {}
        }

        if (commandUpper == F("DUMP"))
        {
            performDump(true);
            return;
        }

        if (commandUpper == F("DUMP_NO_ERASE"))
        {
            performDump(false);
            return;
        }

        if (commandUpper == F("ERASE_LOG") || commandUpper == F("CLEAR_LOG"))
        {
            eraseLogAndReset();
            return;
        }

        if (commandUpper == F("WIFI_STATUS"))
        {
            Serial.print(F("WiFi credentials configured: "));
            Serial.println(hasWifiConfig() ? F("yes") : F("no"));
            if (hasWifiConfig())
            {
                Serial.print(F("SSID: "));
                Serial.println(Config.wifiSsid);
            }
            Serial.print(F("WiFi status: "));
            Serial.println(WiFi.status());
            if (WiFi.status() == WL_CONNECTED)
            {
                printWifiStatus();
            }
            else
            {
                printWifiConfigHelp(Serial);
            }
            return;
        }

        if (commandUpper == F("CLEAR_WIFI"))
        {
            clearWifiConfig();
            Serial.println(F("WiFi credentials cleared. Resetting."));
            Serial.flush();
            NVIC_SystemReset();
            while (true) {}
        }

        if (commandUpper == F("LOG_STATUS"))
        {
            printCloudLogStatus(Serial);
            return;
        }

        if (commandUpper == F("LOG_TEST"))
        {
            Serial.println(F("Sending cloud log test row..."));
            Serial.println(sendCloudLogNow(true) ? F("Cloud log sent.") : F("Cloud log send failed."));
            printCloudLogStatus(Serial);
            return;
        }

        if (commandUpper == F("CLEAR_LOG_ENDPOINT"))
        {
            clearCloudLogConfig();
            LastCloudLogSnapshot = CloudLogSnapshot{};
            Serial.println(F("Cloud log endpoint and token cleared."));
            return;
        }

        if (commandUpper == F("SYNC_TIME"))
        {
            Serial.println(syncTimeFromNtp() ? F("RTC synced from NTP.") : F("NTP sync failed."));
            Serial.println(currentTimeString());
            return;
        }

        if (commandUpper.startsWith(F("SET_LOG_ENDPOINT ")))
        {
            const int firstSpace = command.indexOf(' ');
            String endpoint = command.substring(firstSpace + 1);
            endpoint.trim();
            if (!setCloudLogEndpoint(endpoint))
            {
                Serial.print(F("Invalid endpoint. Use an https URL up to "));
                Serial.print(CloudLogEndpointMaxLength);
                Serial.println(F(" chars."));
                return;
            }

            LastCloudLogSnapshot = CloudLogSnapshot{};
            Serial.println(F("Cloud log endpoint saved."));
            return;
        }

        if (commandUpper.startsWith(F("SET_LOG_TOKEN ")))
        {
            const int firstSpace = command.indexOf(' ');
            String token = command.substring(firstSpace + 1);
            token.trim();
            if (!setCloudLogToken(token))
            {
                Serial.print(F("Invalid token. Use 1.."));
                Serial.print(CloudLogTokenMaxLength);
                Serial.println(F(" chars."));
                return;
            }

            LastCloudLogSnapshot = CloudLogSnapshot{};
            Serial.println(F("Cloud log token saved."));
            return;
        }

        if (commandUpper.startsWith(F("SET_INTERVAL ")))
        {
            const int firstSpace = command.indexOf(' ');
            const uint32_t intervalSeconds = static_cast<uint32_t>(command.substring(firstSpace + 1).toInt());
            if (!setLogIntervalSeconds(intervalSeconds))
            {
                Serial.print(F("Invalid sample interval. Use "));
                Serial.print(MinLogIntervalSeconds);
                Serial.print(F(".."));
                Serial.print(MaxLogIntervalSeconds);
                Serial.println(F(" seconds."));
                return;
            }

            Serial.print(F("Sample interval saved: "));
            Serial.print(Config.logIntervalSeconds);
            Serial.println(F(" seconds."));
            return;
        }

        if (commandUpper.startsWith(F("SET_I2C_CLOCK ")))
        {
            const int firstSpace = command.indexOf(' ');
            const uint32_t clockHz = static_cast<uint32_t>(command.substring(firstSpace + 1).toInt());
            if (!setI2cClockHz(clockHz))
            {
                Serial.print(F("Invalid I2C clock. Use "));
                Serial.print(MinI2cClockHz);
                Serial.print(F(".."));
                Serial.print(MaxI2cClockHz);
                Serial.println(F(" Hz."));
                return;
            }

            Serial.print(F("I2C clock saved and applied: "));
            Serial.print(Config.i2cClockHz);
            Serial.println(F(" Hz."));
            return;
        }

        if (commandUpper.startsWith(F("SET_ZONE_SWITCH_INTERVAL ")))
        {
            const int firstSpace = command.indexOf(' ');
            const uint32_t intervalSeconds = static_cast<uint32_t>(command.substring(firstSpace + 1).toInt());
            if (!setZoneSwitchIntervalSeconds(intervalSeconds))
            {
                Serial.print(F("Invalid zone switch interval. Use "));
                Serial.print(MinZoneSwitchIntervalSeconds);
                Serial.print(F(".."));
                Serial.print(MaxZoneSwitchIntervalSeconds);
                Serial.println(F(" seconds."));
                return;
            }

            Serial.print(F("Zone switch interval saved: "));
            Serial.print(Config.zoneSwitchIntervalSeconds);
            Serial.println(F(" seconds."));
            return;
        }

        if (commandUpper.startsWith(F("SET_ZONE_SENSOR ")))
        {
            const int firstSpace = command.indexOf(' ');
            const int secondSpace = command.indexOf(' ', firstSpace + 1);
            if (firstSpace < 0 || secondSpace < 0)
            {
                Serial.println(F("Usage: SET_ZONE_SENSOR <zone> <sensor>"));
                return;
            }

            const int zoneIdx = command.substring(firstSpace + 1, secondSpace).toInt();
            const int sensorIdx = command.substring(secondSpace + 1).toInt();
            if (!setZoneSensor(zoneIdx, sensorIdx))
            {
                Serial.println(F("Invalid zone sensor mapping. Use zone 0..3 and sensor 0..3."));
                return;
            }

            Serial.print(F("Zone "));
            Serial.print(zoneIdx);
            Serial.print(F(" now uses sensor "));
            Serial.print(sensorIdx);
            Serial.println('.');
            return;
        }

        if (commandUpper.startsWith(F("SET_CALIBRATION ")))
        {
            const int firstSpace = command.indexOf(' ');
            const int secondSpace = command.indexOf(' ', firstSpace + 1);
            const int thirdSpace = command.indexOf(' ', secondSpace + 1);
            if (firstSpace < 0 || secondSpace < 0 || thirdSpace < 0)
            {
                Serial.println(F("Usage: SET_CALIBRATION <cell> <dryRaw> <wetRaw>"));
                return;
            }

            const int cellIdx = command.substring(firstSpace + 1, secondSpace).toInt();
            const uint16_t dryRaw = static_cast<uint16_t>(command.substring(secondSpace + 1, thirdSpace).toInt());
            const uint16_t wetRaw = static_cast<uint16_t>(command.substring(thirdSpace + 1).toInt());
            if (!setCellCalibration(cellIdx, dryRaw, wetRaw))
            {
                Serial.println(F("Invalid calibration. Use cell=0..3 and 0 <= dryRaw < wetRaw <= 1023."));
                return;
            }

            Serial.print(F("Calibration saved for cell "));
            Serial.print(cellIdx);
            Serial.print(F(": dry="));
            Serial.print(dryRaw);
            Serial.print(F(" wet="));
            Serial.println(wetRaw);
            return;
        }

        if (commandUpper.startsWith(F("SET_SENSOR_SOURCE ")))
        {
            const int firstSpace = command.indexOf(' ');
            const int secondSpace = command.indexOf(' ', firstSpace + 1);
            if (firstSpace < 0 || secondSpace < 0)
            {
                Serial.println(F("Usage: SET_SENSOR_SOURCE <cell> <unused|seesaw|vh400>"));
                return;
            }

            const int cellIdx = command.substring(firstSpace + 1, secondSpace).toInt();
            String source = command.substring(secondSpace + 1);
            source.trim();
            source.toLowerCase();
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
                Serial.println(F("Invalid sensor source. Use unused, seesaw, or vh400."));
                return;
            }

            if (!setCellSensorInputMode(cellIdx, mode))
            {
                Serial.println(F("Invalid cell. Use 0..3."));
                return;
            }

            Serial.print(F("Sensor source saved for cell "));
            Serial.print(cellIdx);
            Serial.print(F(": "));
            if (mode == SensorInputMode::Vh400)
            {
                Serial.println(F("VH400"));
            }
            else if (mode == SensorInputMode::NotUsed)
            {
                Serial.println(F("not used"));
            }
            else
            {
                Serial.println(F("seesaw"));
            }
            return;
        }

        if (commandUpper.startsWith(F("SET_FORCED_ZONE ")))
        {
            const int firstSpace = command.indexOf(' ');
            const int zone = command.substring(firstSpace + 1).toInt();
            if (!setForcedIrrigationZone(zone))
            {
                Serial.println(F("Invalid forced irrigation zone. Use -1 for off or 0..3."));
                return;
            }

            if (isForcedIrrigationActive())
            {
                Serial.print(F("Forced irrigation enabled for zone "));
                Serial.print(ForcedIrrigationZone + 1);
                Serial.println('.');
            }
            else
            {
                Serial.println(F("Forced irrigation off."));
            }
            return;
        }

        if (commandUpper.startsWith(F("SET_SIMULATION ")))
        {
            int positions[5] = {-1, -1, -1, -1, -1};
            positions[0] = command.indexOf(' ');
            for (int idx = 1; idx < 5; ++idx)
            {
                if (positions[idx - 1] < 0)
                {
                    break;
                }
                positions[idx] = command.indexOf(' ', positions[idx - 1] + 1);
            }

            const int enabled = command.substring(positions[0] + 1, positions[1] < 0 ? command.length() : positions[1]).toInt();
            if (enabled != 0 && enabled != 1)
            {
                Serial.println(F("Invalid simulation flag. Use SET_SIMULATION 0 or 1."));
                return;
            }

            SimulationEnabled = enabled == 1;
            for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
            {
                if (positions[cellIdx + 1] >= 0)
                {
                    const int end = (cellIdx + 2) < 5 ? positions[cellIdx + 2] : -1;
                    const int value = command.substring(
                        positions[cellIdx + 1] + 1,
                        end < 0 ? command.length() : end).toInt();
                    SimulatedMoisturePercent[cellIdx] = static_cast<uint8_t>(constrain(value, 0, 100));
                }
                Cells[cellIdx].SetSimulatedMoisture(SimulationEnabled, SimulatedMoisturePercent[cellIdx]);
            }

            Serial.print(F("Simulation "));
            Serial.println(SimulationEnabled ? F("enabled.") : F("disabled."));
            return;
        }

        if (commandUpper.startsWith(F("SET_WIFI ")))
        {
            const int firstSpace = command.indexOf(' ');
            const int secondSpace = command.indexOf(' ', firstSpace + 1);
            if (firstSpace < 0 || secondSpace < 0)
            {
                printWifiConfigHelp(Serial);
                return;
            }

            const String ssid = command.substring(firstSpace + 1, secondSpace);
            const String password = command.substring(secondSpace + 1);
            if (!setWifiConfig(ssid, password))
            {
                Serial.println(F("Invalid WiFi credentials length."));
                printWifiConfigHelp(Serial);
                return;
            }

            Serial.println(F("WiFi credentials saved to EEPROM config. Resetting."));
            Serial.flush();
            NVIC_SystemReset();
            while (true) {}
        }

        if (commandUpper == F("DIAG"))
        {
            if (DataState.outOfMemory)
            {
                Serial.println(F("DIAG skipped: EEPROM log is full; live I2C reads are disabled so DUMP_NO_ERASE remains responsive."));
                return;
            }

            SensorSnapshot snapshot;
            sampleAllCells(snapshot);
            for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
            {
                DataState.latest[cellIdx] = snapshot.moisture[cellIdx];
            }
            printSensorDiagnostics(snapshot);
            return;
        }

        if (commandUpper == F("I2C_SCAN"))
        {
            scanI2CBus();
            return;
        }

        if (commandUpper == F("RAW_DIAG"))
        {
            printRawSensorDiagnostics(Serial);
            return;
        }

        if (commandUpper == F("DIAG_FORCE"))
        {
            SensorSnapshot snapshot;
            sampleAllCells(snapshot);
            for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
            {
                DataState.latest[cellIdx] = snapshot.moisture[cellIdx];
            }
            printSensorDiagnostics(snapshot);
            return;
        }

        if (commandUpper.startsWith(F("SET_DATA_GATHERING ")))
        {
            const int firstSpace = command.indexOf(' ');
            const int enabled = command.substring(firstSpace + 1).toInt();
            if (enabled != 0 && enabled != 1)
            {
                Serial.println(F("Invalid data gathering flag. Use SET_DATA_GATHERING 0 or 1."));
                return;
            }

            setDataGatheringActive(enabled == 1);
            Serial.println(DataGatheringActive ? F("Data gathering active.") : F("Data gathering stopped."));
            return;
        }

        Serial.print(F("Unknown command: "));
        Serial.println(command);
    }

    void scanI2CBus()
    {
        scanI2CBus(Serial);
    }

    void scanI2CBus(Print& out)
    {
        int found = 0;
        out.println(F("I2C scan begin"));
        for (uint8_t address = 1; address < 127; ++address)
        {
            Wire.beginTransmission(address);
            const uint8_t error = Wire.endTransmission();
            if (error == 0)
            {
                out.print(F("  found 0x"));
                if (address < 16)
                {
                    out.print('0');
                }
                out.println(address, HEX);
                found++;
            }
        }
        out.print(F("I2C scan complete, found "));
        out.print(found);
        out.println(F(" device(s)."));
    }

    void handleSerialCommands()
    {
        while (Serial.available() > 0)
        {
            const char ch = static_cast<char>(Serial.read());
            if (ch == '\n' || ch == '\r')
            {
                processSerialCommand(SerialCommandBuffer);
                SerialCommandBuffer = "";
                continue;
            }

            if (SerialCommandBuffer.length() < 256)
            {
                SerialCommandBuffer += ch;
            }
            else
            {
                SerialCommandBuffer = "";
                Serial.println(F("Serial command too long; buffer cleared."));
            }
        }
    }
}
