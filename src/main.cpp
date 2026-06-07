#include <Arduino.h>
#include <EEPROM.h>
#include <RTC.h>
#include <Wire.h>
#include <WiFiS3.h>
#include <WiFiUdp.h>
#include "Arduino_LED_Matrix.h"

#include "GardenCell.h"

namespace
{
    constexpr int NrCells = 4;
    constexpr int DiagnosticCells = 3;
    constexpr uint32_t ConfigMarker = 0x47434647; // GCFG
    constexpr uint16_t ConfigStartAddress = 0;
    constexpr uint16_t LogStartAddress = 192;
    constexpr int WifiSsidMaxLength = 32;
    constexpr int WifiPasswordMaxLength = 64;
    constexpr uint32_t SessionMarkerV1 = 0xDEADBEEF;
    constexpr uint32_t SessionMarkerV2 = 0xD1A6BEEF;
    constexpr uint32_t EmptyWord = 0xFFFFFFFF;
    constexpr int LegacySampleBytes = NrCells;
    constexpr int DiagnosticSampleBytes = DiagnosticCells + (DiagnosticCells * 2) + 1;
    constexpr unsigned long IntroAnimTimeoutMs = 4500;
    constexpr unsigned long UpdateDelayMs = 10;
    constexpr unsigned long LogIntervalMs = 60UL * 1000UL;
    constexpr uint32_t LogIntervalSeconds = 60UL;
    constexpr unsigned long StatsScreenPeriodMs = 2500;
    constexpr unsigned long WifiRetryMs = 30000;
    constexpr unsigned long NtpRetryMs = 300000;
    constexpr unsigned long NtpStartupRetryMs = 10000;
    constexpr unsigned long StartupTimeSyncTimeoutMs = 60000;
    constexpr uint16_t HttpPort = 80;
    constexpr uint16_t NtpLocalPort = 2390;
    constexpr int NtpPacketSize = 48;
    constexpr int ModeSensePin = 13;
    const char WifiHostname[] = "garden-pump";

    constexpr uint8_t kFontDigits[10][5] = {
        {0b111, 0b101, 0b101, 0b101, 0b111},
        {0b010, 0b110, 0b010, 0b010, 0b111},
        {0b111, 0b001, 0b111, 0b100, 0b111},
        {0b111, 0b001, 0b111, 0b001, 0b111},
        {0b101, 0b101, 0b111, 0b001, 0b001},
        {0b111, 0b100, 0b111, 0b001, 0b111},
        {0b111, 0b100, 0b111, 0b101, 0b111},
        {0b111, 0b001, 0b001, 0b001, 0b001},
        {0b111, 0b101, 0b111, 0b101, 0b111},
        {0b111, 0b101, 0b111, 0b001, 0b111},
    };

    constexpr uint8_t kFontC[5] = {0b111, 0b100, 0b100, 0b100, 0b111};
    constexpr uint8_t kFontH[5] = {0b101, 0b101, 0b111, 0b101, 0b101};
    constexpr uint8_t kFontL[5] = {0b100, 0b100, 0b100, 0b100, 0b111};
    constexpr uint8_t kFontM[5] = {0b101, 0b111, 0b111, 0b101, 0b101};
    constexpr uint8_t kFontO[5] = {0b111, 0b101, 0b101, 0b101, 0b111};
    constexpr uint8_t kFontI[5] = {0b111, 0b010, 0b010, 0b010, 0b111};
    constexpr uint8_t kFontR[5] = {0b110, 0b101, 0b110, 0b101, 0b101};
    constexpr uint8_t kFontG[5] = {0b111, 0b100, 0b101, 0b101, 0b111};
    constexpr uint8_t kFontD[5] = {0b110, 0b101, 0b101, 0b101, 0b110};
    constexpr uint8_t kFontP[5] = {0b110, 0b101, 0b110, 0b100, 0b100};

    enum class OperationMode : uint8_t
    {
        Irrigation = 0,
        DataGathering = 1,
    };

    struct GatherState
    {
        bool initialized = false;
        bool outOfMemory = false;
        uint16_t nextLogAddress = 0;
        uint32_t totalSamples = 0;
        uint8_t latest[NrCells] = {0, 0, 0, 0};
        uint8_t minValues[NrCells] = {254, 254, 254, 254};
        uint8_t maxValues[NrCells] = {0, 0, 0, 0};
        unsigned long lastWriteMs = 0;
        uint8_t currentStatsScreen = 0;
        unsigned long lastStatsScreenSwapMs = 0;
    };

    struct SensorSnapshot
    {
        uint8_t moisture[DiagnosticCells] = {0, 0, 0};
        uint16_t raw[DiagnosticCells] = {0, 0, 0};
        uint8_t statusMask = 0;
    };

    struct PumpConfig
    {
        uint32_t marker = ConfigMarker;
        uint8_t version = 1;
        uint8_t startThreshold[NrCells] = {40, 40, 40, 40};
        uint8_t stopThreshold[NrCells] = {60, 60, 60, 60};
        char wifiSsid[WifiSsidMaxLength + 1] = {};
        char wifiPassword[WifiPasswordMaxLength + 1] = {};
        uint8_t checksum = 0;
    };

    GardenCell Cells[NrCells];
    ArduinoLEDMatrix LedMatrix;
    WiFiServer WebServer(HttpPort);
    WiFiUDP Udp;
    IPAddress NtpServer(162, 159, 200, 123);
    byte NtpPacket[NtpPacketSize];

    void renderDumpDisplay();
    void dumpLogToSerial();
    void dumpLog(Print& out);
    void eraseEntireLog();
    void loadConfig();
    void saveConfig();
    void applyConfig();
    uint8_t configChecksum(const PumpConfig& config);
    bool setCellThresholds(int cellIdx, uint8_t startPercent, uint8_t stopPercent);
    bool hasWifiConfig();
    bool setWifiConfig(const String& ssid, const String& password);
    void clearWifiConfig();
    void printWifiConfigHelp(Print& out);
    void processSerialCommand(String command);
    void eraseLogAndReset();
    void scanI2CBus();
    void scanI2CBus(Print& out);
    void handleWifi();
    void handleWebClient();
    void connectWifiIfNeeded();
    bool syncTimeFromNtp();
    bool startupTimeWaitFinished();
    void startOperationsIfReady();
    void sendNtpPacket();
    void printWifiStatus();
    void sendHttpHeaders(WiFiClient& client, const char* contentType);
    void sendHomePage(WiFiClient& client);
    void sendJsonStatus(WiFiClient& client);
    void sendTextDiagnostics(WiFiClient& client, bool force);
    void sendTextDump(WiFiClient& client, bool eraseAfterDump);
    void sendTextI2CScan(WiFiClient& client);
    void sendThresholdUpdate(WiFiClient& client, const String& requestLine);
    int queryIntValue(const String& requestLine, const char* key, int fallback);
    String queryStringValue(const String& requestLine, const char* key);
    void sendWifiUpdate(WiFiClient& client, const String& requestLine);
    void sendNotFound(WiFiClient& client);
    void printSensorDiagnostics(Print& out, const SensorSnapshot& snapshot);
    void printSensorDiagnostics();
    void printSensorDiagnostics(const SensorSnapshot& snapshot);
    GatherState DataState;
    PumpConfig Config;
    OperationMode CurrentMode = OperationMode::Irrigation;
    int CurrentCell = 0;
    unsigned long IntroAnimEndsAtMs = IntroAnimTimeoutMs;
    bool IntroFinished = false;
    String SerialCommandBuffer;
    int WifiStatus = WL_IDLE_STATUS;
    bool WebServerStarted = false;
    bool UdpStarted = false;
    bool TimeSynced = false;
    bool OperationsStarted = false;
    bool StartupTimeWaitTimedOut = false;
    unsigned long LastWifiAttemptMs = 0;
    unsigned long LastNtpAttemptMs = 0;
    unsigned long StartupStartedAtMs = 0;

    int logEndAddress()
    {
        return EEPROM.length();
    }

    int logStartAddress()
    {
        return LogStartAddress;
    }

    uint8_t configChecksum(const PumpConfig& config)
    {
        uint8_t sum = 0;
        sum ^= static_cast<uint8_t>(config.marker);
        sum ^= static_cast<uint8_t>(config.marker >> 8);
        sum ^= static_cast<uint8_t>(config.marker >> 16);
        sum ^= static_cast<uint8_t>(config.marker >> 24);
        sum ^= config.version;
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            sum ^= config.startThreshold[cellIdx];
            sum ^= static_cast<uint8_t>(config.stopThreshold[cellIdx] << 1);
        }
        for (int idx = 0; idx <= WifiSsidMaxLength; ++idx)
        {
            sum ^= static_cast<uint8_t>(config.wifiSsid[idx] + idx);
        }
        for (int idx = 0; idx <= WifiPasswordMaxLength; ++idx)
        {
            sum ^= static_cast<uint8_t>(config.wifiPassword[idx] + (idx * 3));
        }
        return sum;
    }

    void applyConfig()
    {
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            Cells[cellIdx].SetWateringThresholds(
                static_cast<float>(Config.startThreshold[cellIdx]) / 100.0f,
                static_cast<float>(Config.stopThreshold[cellIdx]) / 100.0f);
        }
    }

    void saveConfig()
    {
        Config.marker = ConfigMarker;
        Config.version = 1;
        Config.checksum = configChecksum(Config);
        EEPROM.put(ConfigStartAddress, Config);
    }

    void loadConfig()
    {
        EEPROM.get(ConfigStartAddress, Config);
        const bool valid = Config.marker == ConfigMarker
            && Config.version == 1
            && Config.checksum == configChecksum(Config);
        if (!valid)
        {
            Config = PumpConfig{};
            saveConfig();
        }

        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            Config.startThreshold[cellIdx] = constrain(Config.startThreshold[cellIdx], 0, 100);
            Config.stopThreshold[cellIdx] = constrain(Config.stopThreshold[cellIdx], 0, 100);
            if (Config.stopThreshold[cellIdx] < Config.startThreshold[cellIdx])
            {
                Config.stopThreshold[cellIdx] = Config.startThreshold[cellIdx];
            }
        }
        Config.wifiSsid[WifiSsidMaxLength] = '\0';
        Config.wifiPassword[WifiPasswordMaxLength] = '\0';
        applyConfig();
    }

    bool setCellThresholds(int cellIdx, uint8_t startPercent, uint8_t stopPercent)
    {
        if (cellIdx < 0 || cellIdx >= NrCells || startPercent > 100 || stopPercent > 100 || stopPercent < startPercent)
        {
            return false;
        }

        Config.startThreshold[cellIdx] = startPercent;
        Config.stopThreshold[cellIdx] = stopPercent;
        applyConfig();
        saveConfig();
        return true;
    }

    bool hasWifiConfig()
    {
        return Config.wifiSsid[0] != '\0' && Config.wifiPassword[0] != '\0';
    }

    bool setWifiConfig(const String& ssid, const String& password)
    {
        if (ssid.length() == 0 || ssid.length() > WifiSsidMaxLength
            || password.length() == 0 || password.length() > WifiPasswordMaxLength)
        {
            return false;
        }

        memset(Config.wifiSsid, 0, sizeof(Config.wifiSsid));
        memset(Config.wifiPassword, 0, sizeof(Config.wifiPassword));
        ssid.toCharArray(Config.wifiSsid, sizeof(Config.wifiSsid));
        password.toCharArray(Config.wifiPassword, sizeof(Config.wifiPassword));
        saveConfig();
        return true;
    }

    void clearWifiConfig()
    {
        memset(Config.wifiSsid, 0, sizeof(Config.wifiSsid));
        memset(Config.wifiPassword, 0, sizeof(Config.wifiPassword));
        saveConfig();
    }

    void printWifiConfigHelp(Print& out)
    {
        out.println(F("WiFi config commands:"));
        out.println(F("  SET_WIFI <ssid> <password>"));
        out.println(F("  CLEAR_WIFI"));
        out.println(F("  WIFI_STATUS"));
    }

    void writePixel(int x, int y, bool on)
    {
        const uint8_t value = on ? 255 : 0;
        LedMatrix.set(x, y, value, value, value);
    }

    void clearMatrix()
    {
        for (int x = 0; x < 12; ++x)
        {
            for (int y = 0; y < 8; ++y)
            {
                writePixel(x, y, false);
            }
        }
    }

    void drawGlyph3x5(int startX, int startY, const uint8_t glyph[5])
    {
        for (int row = 0; row < 5; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                const bool on = (glyph[row] & (1 << (2 - col))) != 0;
                writePixel(startX + col, startY + row, on);
            }
        }
    }

    void drawDigit3x5(int startX, int startY, int digit)
    {
        digit = constrain(digit, 0, 9);
        drawGlyph3x5(startX, startY, kFontDigits[digit]);
    }

    void drawCountScreen(uint32_t count)
    {
        clearMatrix();

        const uint32_t shown = count % 1000UL;
        const int hundreds = static_cast<int>((shown / 100UL) % 10UL);
        const int tens = static_cast<int>((shown / 10UL) % 10UL);
        const int ones = static_cast<int>(shown % 10UL);

        drawDigit3x5(0, 1, hundreds);
        drawDigit3x5(4, 1, tens);
        drawDigit3x5(8, 1, ones);

        if (count >= 1000UL)
        {
            writePixel(11, 0, true);
        }
    }

    void drawBarScreen(const uint8_t label[5], const uint8_t values[NrCells])
    {
        clearMatrix();
        drawGlyph3x5(0, 1, label);

        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            const int barX = 4 + cellIdx * 2;
            const int barHeight = map(values[cellIdx], 0, 254, 0, 5);
            for (int y = 0; y < barHeight; ++y)
            {
                writePixel(barX, 5 - y, true);
            }
        }
    }

    void drawText3x5(const uint8_t left[5], const uint8_t middle[5], const uint8_t right[5])
    {
        clearMatrix();
        drawGlyph3x5(0, 1, left);
        drawGlyph3x5(4, 1, middle);
        drawGlyph3x5(8, 1, right);
    }

    uint32_t readU32(int address)
    {
        uint32_t value = 0;
        EEPROM.get(address, value);
        return value;
    }

    void writeU32(int address, uint32_t value)
    {
        EEPROM.put(address, value);
    }

    void writeBytes(int address, const uint8_t* data, int count)
    {
        for (int idx = 0; idx < count; ++idx)
        {
            EEPROM.update(address + idx, data[idx]);
        }
    }

    uint16_t readU16(int address)
    {
        uint16_t value = 0;
        EEPROM.get(address, value);
        return value;
    }

    void writeU16(int address, uint16_t value)
    {
        EEPROM.put(address, value);
    }

    bool isGatherModePinActive()
    {
        return digitalRead(ModeSensePin) == LOW;
    }

    OperationMode readRequestedMode()
    {
        return isGatherModePinActive() ? OperationMode::DataGathering : OperationMode::Irrigation;
    }

    void printModeHelp()
    {
        Serial.println();
        Serial.println(F("Garden pump controls:"));
        Serial.println(F("  D13 shorted to GND = data gathering mode"));
        Serial.println(F("  D13 floating/high  = irrigation mode"));
        Serial.println(F("  DUMP = dump EEPROM over USB, erase it, then reset"));
        Serial.println(F("  DUMP_NO_ERASE = dump EEPROM over USB and keep the data"));
        Serial.println(F("  DIAG = print live sensor diagnostics"));
        Serial.println(F("  SET_WIFI <ssid> <password> = save WiFi credentials to EEPROM config"));
        Serial.println(F("  CLEAR_WIFI = remove saved WiFi credentials"));
        Serial.println(F("  WIFI_STATUS = print WiFi config and connection status"));
        Serial.println(F("  r = reboot"));
        Serial.println();
    }

    uint32_t getCurrentTimestamp()
    {
        RTCTime currentTime;
        RTC.getTime(currentTime);
        return currentTime.getUnixTime();
    }

    String currentTimeString()
    {
        RTCTime currentTime;
        if (!RTC.getTime(currentTime))
        {
            return String("unavailable");
        }
        return currentTime.toString();
    }

    void connectWifiIfNeeded()
    {
        if (!hasWifiConfig())
        {
            return;
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            return;
        }

        if ((millis() - LastWifiAttemptMs) < WifiRetryMs && LastWifiAttemptMs != 0)
        {
            return;
        }

        LastWifiAttemptMs = millis();
        if (WiFi.status() == WL_NO_MODULE)
        {
            Serial.println(F("WiFi module not available."));
            return;
        }

        Serial.print(F("Connecting to WiFi SSID: "));
        Serial.println(Config.wifiSsid);
        WiFi.setHostname(WifiHostname);
        WifiStatus = WiFi.begin(Config.wifiSsid, Config.wifiPassword);
        if (WifiStatus == WL_CONNECTED)
        {
            WebServer.begin();
            WebServerStarted = true;
            if (!UdpStarted)
            {
                Udp.begin(NtpLocalPort);
                UdpStarted = true;
            }
            printWifiStatus();
            syncTimeFromNtp();
        }
        else
        {
            Serial.println(F("WiFi not connected yet."));
        }
    }

    void sendNtpPacket()
    {
        memset(NtpPacket, 0, NtpPacketSize);
        NtpPacket[0] = 0b11100011;
        NtpPacket[1] = 0;
        NtpPacket[2] = 6;
        NtpPacket[3] = 0xEC;
        NtpPacket[12] = 49;
        NtpPacket[13] = 0x4E;
        NtpPacket[14] = 49;
        NtpPacket[15] = 52;

        Udp.beginPacket(NtpServer, 123);
        Udp.write(NtpPacket, NtpPacketSize);
        Udp.endPacket();
    }

    bool syncTimeFromNtp()
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            return false;
        }

        if (!UdpStarted)
        {
            Udp.begin(NtpLocalPort);
            UdpStarted = true;
        }

        LastNtpAttemptMs = millis();
        sendNtpPacket();
        delay(1000);

        if (!Udp.parsePacket())
        {
            Serial.println(F("NTP sync failed: no packet."));
            return false;
        }

        Udp.read(NtpPacket, NtpPacketSize);
        const unsigned long highWord = word(NtpPacket[40], NtpPacket[41]);
        const unsigned long lowWord = word(NtpPacket[42], NtpPacket[43]);
        const unsigned long secsSince1900 = (highWord << 16) | lowWord;
        const unsigned long unixTime = secsSince1900 - 2208988800UL;
        RTCTime rtcTime(static_cast<time_t>(unixTime));
        RTC.setTime(rtcTime);
        TimeSynced = true;
        Serial.print(F("RTC synced from NTP: "));
        Serial.println(currentTimeString());
        return true;
    }

    void printWifiStatus()
    {
        Serial.print(F("SSID: "));
        Serial.println(WiFi.SSID());
        Serial.print(F("IP Address: "));
        Serial.println(WiFi.localIP());
        Serial.print(F("RSSI: "));
        Serial.print(WiFi.RSSI());
        Serial.println(F(" dBm"));
        Serial.print(F("Garden pump dashboard: http://"));
        Serial.println(WiFi.localIP());
        Serial.print(F("Hostname, if supported by router: http://"));
        Serial.println(WifiHostname);
    }

    bool hasRoomForBytes(int byteCount)
    {
        return (DataState.nextLogAddress + byteCount) <= logEndAddress();
    }

    void updateStatsFromSample(const uint8_t sample[NrCells])
    {
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            DataState.latest[cellIdx] = sample[cellIdx];
            DataState.minValues[cellIdx] = min(DataState.minValues[cellIdx], sample[cellIdx]);
            DataState.maxValues[cellIdx] = max(DataState.maxValues[cellIdx], sample[cellIdx]);
        }
    }

    uint8_t cellMask(int cellIdx)
    {
        return static_cast<uint8_t>(1U << cellIdx);
    }

    uint8_t connectedMask(const SensorSnapshot& snapshot)
    {
        return snapshot.statusMask & 0x0F;
    }

    uint8_t errorMask(const SensorSnapshot& snapshot)
    {
        return (snapshot.statusMask >> 4) & 0x0F;
    }

    void sampleAllCells(SensorSnapshot& outSnapshot)
    {
        outSnapshot = SensorSnapshot{};
        for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
        {
            Cells[cellIdx].RefreshSensor();
            Cells[cellIdx].ForceDefaultSolenoidState();
            outSnapshot.moisture[cellIdx] = Cells[cellIdx].GetMoistureByte();

            const int raw = Cells[cellIdx].GetLastCapacitanceReading();
            outSnapshot.raw[cellIdx] = static_cast<uint16_t>(constrain(raw, 0, 65535));
            if (Cells[cellIdx].HasConnected())
            {
                outSnapshot.statusMask |= cellMask(cellIdx);
            }
            if (Cells[cellIdx].HasError())
            {
                outSnapshot.statusMask |= static_cast<uint8_t>(cellMask(cellIdx) << 4);
            }
        }
    }

    void sampleAllCells(uint8_t outSample[NrCells])
    {
        SensorSnapshot snapshot;
        sampleAllCells(snapshot);
        for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
        {
            outSample[cellIdx] = snapshot.moisture[cellIdx];
        }
        for (int cellIdx = DiagnosticCells; cellIdx < NrCells; ++cellIdx)
        {
            outSample[cellIdx] = 0;
        }
    }

    void writeDiagnosticSample(int address, const SensorSnapshot& snapshot)
    {
        writeBytes(address, snapshot.moisture, DiagnosticCells);
        for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
        {
            writeU16(address + DiagnosticCells + (cellIdx * 2), snapshot.raw[cellIdx]);
        }
        EEPROM.update(address + DiagnosticCells + (DiagnosticCells * 2), snapshot.statusMask);
    }

    void readDiagnosticSample(int address, SensorSnapshot& snapshot)
    {
        snapshot = SensorSnapshot{};
        EEPROM.get(address, snapshot.moisture);
        for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
        {
            snapshot.raw[cellIdx] = readU16(address + DiagnosticCells + (cellIdx * 2));
        }
        snapshot.statusMask = EEPROM.read(address + DiagnosticCells + (DiagnosticCells * 2));
    }

    void appendSample(const SensorSnapshot& snapshot)
    {
        if (!hasRoomForBytes(DiagnosticSampleBytes))
        {
            DataState.outOfMemory = true;
            return;
        }

        writeDiagnosticSample(DataState.nextLogAddress, snapshot);
        DataState.nextLogAddress += DiagnosticSampleBytes;
        DataState.totalSamples++;
        uint8_t statSample[NrCells] = {};
        for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
        {
            statSample[cellIdx] = snapshot.moisture[cellIdx];
        }
        updateStatsFromSample(statSample);
        DataState.lastWriteMs = millis();

        Serial.print(F("Logged sample #"));
        Serial.print(DataState.totalSamples);
        Serial.print(F(" : "));
        for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
        {
            if (cellIdx > 0)
            {
                Serial.print(F(", "));
            }
            Serial.print(snapshot.moisture[cellIdx]);
        }
        Serial.println();
        printSensorDiagnostics(snapshot);
    }

    void startNewGatheringSession()
    {
        SensorSnapshot firstSample;
        sampleAllCells(firstSample);

        if (!hasRoomForBytes(12 + DiagnosticSampleBytes))
        {
            DataState.outOfMemory = true;
            return;
        }

        writeU32(DataState.nextLogAddress, SessionMarkerV2);
        writeU32(DataState.nextLogAddress + 4, getCurrentTimestamp());
        writeU32(DataState.nextLogAddress + 8, LogIntervalSeconds);
        DataState.nextLogAddress += 12;
        appendSample(firstSample);
    }

    void scanExistingLog()
    {
        DataState = GatherState{};
        DataState.nextLogAddress = logStartAddress();
        int sampleBytes = LegacySampleBytes;

        while ((DataState.nextLogAddress + 4) <= logEndAddress())
        {
            const uint32_t word = readU32(DataState.nextLogAddress);
            if (word == EmptyWord)
            {
                break;
            }

            if (word == SessionMarkerV1 || word == SessionMarkerV2)
            {
                if ((DataState.nextLogAddress + 12) > logEndAddress())
                {
                    break;
                }
                sampleBytes = (word == SessionMarkerV2) ? DiagnosticSampleBytes : LegacySampleBytes;
                DataState.nextLogAddress += 12;
                continue;
            }

            if (!hasRoomForBytes(sampleBytes))
            {
                break;
            }

            if (sampleBytes == DiagnosticSampleBytes)
            {
                SensorSnapshot snapshot;
                readDiagnosticSample(DataState.nextLogAddress, snapshot);
                uint8_t sample[NrCells] = {};
                for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
                {
                    sample[cellIdx] = snapshot.moisture[cellIdx];
                }
                updateStatsFromSample(sample);
            }
            else
            {
                uint8_t sample[NrCells] = {};
                EEPROM.get(DataState.nextLogAddress, sample);
                updateStatsFromSample(sample);
            }
            DataState.totalSamples++;
            DataState.nextLogAddress += sampleBytes;
        }

        DataState.initialized = true;
        DataState.outOfMemory = !hasRoomForBytes(12 + DiagnosticSampleBytes);
        DataState.lastWriteMs = millis();
        DataState.lastStatsScreenSwapMs = millis();
    }

    void beginGatherMode()
    {
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            Cells[cellIdx].ForceDefaultSolenoidState();
        }

        scanExistingLog();
        if (!DataState.outOfMemory)
        {
            startNewGatheringSession();
        }

        Serial.println(F("Entered data gathering mode."));
        Serial.print(F("Next log address: "));
        Serial.println(DataState.nextLogAddress);
        Serial.print(F("Existing total sample count: "));
        Serial.println(DataState.totalSamples);
    }

    void enterIrrigationMode()
    {
        CurrentMode = OperationMode::Irrigation;
        Serial.println(F("Entered irrigation mode."));
    }

    void enterGatherMode()
    {
        CurrentMode = OperationMode::DataGathering;
        beginGatherMode();
    }

    void applyRequestedMode(OperationMode requestedMode)
    {
        if (CurrentMode == requestedMode)
        {
            return;
        }

        if (requestedMode == OperationMode::DataGathering)
        {
            enterGatherMode();
        }
        else
        {
            enterIrrigationMode();
        }
    }

    void performDump(bool eraseAfterDump)
    {
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            Cells[cellIdx].ForceDefaultSolenoidState();
        }

        LedMatrix.beginDraw();
        renderDumpDisplay();
        LedMatrix.endDraw();

        Serial.println(eraseAfterDump
            ? F("Serial command DUMP received. Dumping EEPROM, erasing it, then resetting." )
            : F("Serial command DUMP_NO_ERASE received. Dumping EEPROM without erasing."));
        dumpLogToSerial();

        if (eraseAfterDump)
        {
            eraseEntireLog();
            Serial.println(F("EEPROM erased."));
            Serial.flush();
            NVIC_SystemReset();
            while (true) {}
        }
    }

    void eraseLogAndReset()
    {
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            Cells[cellIdx].ForceDefaultSolenoidState();
        }

        eraseEntireLog();
        Serial.println(F("EEPROM erased."));
        Serial.flush();
        NVIC_SystemReset();
        while (true) {}
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

            if (SerialCommandBuffer.length() < 48)
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

    void updateModeFromHardware()
    {
        applyRequestedMode(readRequestedMode());
    }

    void sendHttpHeaders(WiFiClient& client, const char* contentType)
    {
        client.println(F("HTTP/1.1 200 OK"));
        client.print(F("Content-Type: "));
        client.println(contentType);
        client.println(F("Connection: close"));
        client.println();
    }

    void sendHomePage(WiFiClient& client)
    {
        sendHttpHeaders(client, "text/html; charset=utf-8");
        client.println(F("<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"));
        client.println(F("<title>Garden Pump</title><style>body{font-family:system-ui;margin:0;background:#f5f7f4;color:#172018}header{background:#234b36;color:white;padding:16px 20px}main{padding:16px;max-width:960px;margin:auto}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:10px}.card{background:white;border:1px solid #d9dfd7;border-radius:8px;padding:12px}.big{font-size:28px;font-weight:700}.bar{height:10px;background:#dfe6dc;border-radius:999px;overflow:hidden}.fill{height:100%;background:#2f7d4f;width:0}button,a.btn{display:inline-block;margin:4px 4px 4px 0;padding:8px 10px;border:1px solid #9cad9b;border-radius:6px;background:white;color:#172018;text-decoration:none}pre{white-space:pre-wrap;background:#111;color:#eee;padding:12px;border-radius:8px;overflow:auto}</style></head><body>"));
        client.println(F("<header><h1>Garden Pump</h1><div id=\"sub\">Loading...</div></header><main>"));
        client.println(F("<div class=\"grid\"><div class=\"card\"><div>Mode</div><div class=\"big\" id=\"mode\">-</div></div><div class=\"card\"><div>Samples</div><div class=\"big\" id=\"samples\">-</div></div><div class=\"card\"><div>Clock</div><div id=\"clock\">-</div></div><div class=\"card\"><div>WiFi</div><div id=\"wifi\">-</div></div></div>"));
        client.println(F("<h2>WiFi</h2><div class=\"card\"><label>SSID <input id=\"wifiSsid\"></label><label> Password <input id=\"wifiPassword\" type=\"password\"></label><button id=\"saveWifi\">Save WiFi</button></div>"));
        client.println(F("<h2>Sensors</h2><div class=\"grid\" id=\"sensors\"></div><h2>Controls</h2>"));
        client.println(F("<a class=\"btn\" href=\"/api/diag\">DIAG</a><a class=\"btn\" href=\"/api/i2c_scan\">I2C scan</a><a class=\"btn\" href=\"/api/dump_no_erase\">View dump</a><button class=\"btn\" id=\"downloadDump\">Download dump</button><button class=\"btn\" id=\"downloadThenClear\">Download then clear</button><button class=\"btn\" id=\"clearLog\">Clear memory</button><a class=\"btn\" href=\"/api/sync_time\">Sync time</a>"));
        client.println(F("<h2>Output</h2><pre id=\"out\"></pre><script>async function status(){let r=await fetch('/api/status');let s=await r.json();mode.textContent=s.operationsStarted?s.mode:'waiting';samples.textContent=s.samples;clock.textContent=s.time+(s.timeSynced?'':' (not synced)');wifi.textContent=(s.wifiConfigured?s.wifiSsid:'not configured')+' '+s.ip+' '+s.rssi+' dBm';if(!wifiSsid.value)wifiSsid.value=s.wifiSsid;let state=!s.operationsStarted?'waiting for clock':(s.outOfMemory?'log full':'logging available');if(s.startupTimeWaitTimedOut)state+=' (time sync timed out)';sub.textContent='http://'+s.ip+' - '+state;sensors.innerHTML=s.cells.map(c=>`<div class=\"card\"><b>Cell ${c.index}</b><div>addr ${c.address}</div><div>${c.connected?'connected':'not connected'} / ${c.error?'error':'ok'}</div><div>raw ${c.raw}</div><div>relay ${c.relay?'on':'off'}</div><div class=\"bar\"><div class=\"fill\" style=\"width:${Math.max(0,Math.min(100,c.moisture/254*100))}%\"></div></div><div>moisture ${c.moisture}</div><label>start % <input id=\"st${c.index}\" type=\"number\" min=\"0\" max=\"100\" value=\"${c.startThreshold}\"></label><label> stop % <input id=\"sp${c.index}\" type=\"number\" min=\"0\" max=\"100\" value=\"${c.stopThreshold}\"></label><button onclick=\"setThreshold(${c.index})\">Save</button></div>`).join('')}async function setThreshold(i){let st=document.getElementById('st'+i).value,sp=document.getElementById('sp'+i).value;let r=await fetch(`/api/set_threshold?cell=${i}&start=${st}&stop=${sp}`);out.textContent=await r.text();status()}async function setWifi(){if(!confirm('Save WiFi credentials to EEPROM and reset?'))return;let u='/api/set_wifi?ssid='+encodeURIComponent(wifiSsid.value)+'&password='+encodeURIComponent(wifiPassword.value);let r=await fetch(u);out.textContent=await r.text()}async function load(p){let r=await fetch(p);out.textContent=await r.text();status()}function saveText(t){let stamp=new Date().toISOString().replace(/[:.]/g,'-');let a=document.createElement('a');a.href=URL.createObjectURL(new Blob([t],{type:'text/plain'}));a.download=`garden-pump-dump-${stamp}.txt`;a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000)}async function downloadDumpFile(){out.textContent='Preparing dump...';let r=await fetch('/api/dump_no_erase');let t=await r.text();out.textContent=t;saveText(t);status()}async function clearMemory(){if(!confirm('Clear EEPROM log memory? This cannot be undone.'))return;out.textContent='Clearing memory. The board will reset...';try{let r=await fetch('/api/clear_log?confirm=yes');out.textContent=await r.text()}catch(e){out.textContent='Clear command sent. Waiting for board to restart...'}setTimeout(status,8000)}async function downloadAndClear(){if(!confirm('Download the current dump and then clear EEPROM log memory?'))return;await downloadDumpFile();await clearMemory()}document.querySelectorAll('a.btn').forEach(a=>a.onclick=e=>{e.preventDefault();load(a.getAttribute('href'))});saveWifi.onclick=setWifi;downloadDump.onclick=downloadDumpFile;clearLog.onclick=clearMemory;downloadThenClear.onclick=downloadAndClear;status();setInterval(status,5000)</script>"));
        client.println(F("</main></body></html>"));
    }

    void sendJsonStatus(WiFiClient& client)
    {
        sendHttpHeaders(client, "application/json");
        client.print(F("{\"mode\":\""));
        client.print(CurrentMode == OperationMode::DataGathering ? F("data gathering") : F("irrigation"));
        client.print(F("\",\"outOfMemory\":"));
        client.print(DataState.outOfMemory ? F("true") : F("false"));
        client.print(F(",\"operationsStarted\":"));
        client.print(OperationsStarted ? F("true") : F("false"));
        client.print(F(",\"startupTimeWaitTimedOut\":"));
        client.print(StartupTimeWaitTimedOut ? F("true") : F("false"));
        client.print(F(",\"samples\":"));
        client.print(DataState.totalSamples);
        client.print(F(",\"timeSynced\":"));
        client.print(TimeSynced ? F("true") : F("false"));
        client.print(F(",\"time\":\""));
        client.print(currentTimeString());
        client.print(F("\",\"wifiConfigured\":"));
        client.print(hasWifiConfig() ? F("true") : F("false"));
        client.print(F(",\"wifiSsid\":\""));
        client.print(Config.wifiSsid);
        client.print(F("\",\"ip\":\""));
        client.print(WiFi.localIP());
        client.print(F("\",\"rssi\":"));
        client.print(WiFi.RSSI());
        client.print(F(",\"cells\":["));
        for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
        {
            if (cellIdx > 0)
            {
                client.print(',');
            }
            client.print(F("{\"index\":"));
            client.print(cellIdx);
            client.print(F(",\"address\":\"0x"));
            client.print(Cells[cellIdx].GetSensorAddress(), HEX);
            client.print(F("\",\"connected\":"));
            client.print(Cells[cellIdx].HasConnected() ? F("true") : F("false"));
            client.print(F(",\"error\":"));
            client.print(Cells[cellIdx].HasError() ? F("true") : F("false"));
            client.print(F(",\"raw\":"));
            client.print(Cells[cellIdx].GetLastCapacitanceReading());
            client.print(F(",\"moisture\":"));
            client.print(DataState.latest[cellIdx]);
            client.print(F(",\"relay\":"));
            client.print(Cells[cellIdx].ShouldWater() ? F("true") : F("false"));
            client.print(F(",\"startThreshold\":"));
            client.print(Config.startThreshold[cellIdx]);
            client.print(F(",\"stopThreshold\":"));
            client.print(Config.stopThreshold[cellIdx]);
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
        unsigned long start = millis();
        while (client.connected() && (millis() - start) < 1000)
        {
            if (!client.available())
            {
                delay(1);
                continue;
            }
            const char ch = static_cast<char>(client.read());
            if (ch == '\r')
            {
                continue;
            }
            if (ch == '\n')
            {
                break;
            }
            if (requestLine.length() < 96)
            {
                requestLine += ch;
            }
        }

        while (client.available())
        {
            client.read();
        }

        if (requestLine.startsWith(F("GET / ")) || requestLine.startsWith(F("GET /HTTP")))
        {
            sendHomePage(client);
        }
        else if (requestLine.startsWith(F("GET /api/status")))
        {
            sendJsonStatus(client);
        }
        else if (requestLine.startsWith(F("GET /api/diag_force")))
        {
            sendTextDiagnostics(client, true);
        }
        else if (requestLine.startsWith(F("GET /api/diag")))
        {
            sendTextDiagnostics(client, false);
        }
        else if (requestLine.startsWith(F("GET /api/i2c_scan")))
        {
            sendTextI2CScan(client);
        }
        else if (requestLine.startsWith(F("GET /api/set_threshold")))
        {
            sendThresholdUpdate(client, requestLine);
        }
        else if (requestLine.startsWith(F("GET /api/set_wifi")))
        {
            sendWifiUpdate(client, requestLine);
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

    void handleWifi()
    {
        connectWifiIfNeeded();
        if (WiFi.status() == WL_CONNECTED)
        {
            handleWebClient();
            const unsigned long retryMs = OperationsStarted ? NtpRetryMs : NtpStartupRetryMs;
            if (!TimeSynced && (millis() - LastNtpAttemptMs) > retryMs)
            {
                syncTimeFromNtp();
            }
        }
    }

    bool startupTimeWaitFinished()
    {
        if (TimeSynced)
        {
            return true;
        }

        if ((millis() - StartupStartedAtMs) >= StartupTimeSyncTimeoutMs)
        {
            if (!StartupTimeWaitTimedOut)
            {
                StartupTimeWaitTimedOut = true;
                Serial.println(F("Time sync startup timeout reached; starting normal operation without valid wall-clock time."));
            }
            return true;
        }

        return false;
    }

    void startOperationsIfReady()
    {
        if (OperationsStarted || !startupTimeWaitFinished())
        {
            return;
        }

        CurrentMode = readRequestedMode();
        Serial.print(F("Starting operation in mode from D13: "));
        Serial.println(CurrentMode == OperationMode::DataGathering ? F("data gathering") : F("irrigation"));

        if (CurrentMode == OperationMode::DataGathering)
        {
            beginGatherMode();
        }
        else
        {
            enterIrrigationMode();
        }
        OperationsStarted = true;
    }

    void renderGatherModeDisplay()
    {
        if (DataState.outOfMemory)
        {
            drawText3x5(kFontO, kFontO, kFontM);
            return;
        }

        if ((millis() - DataState.lastStatsScreenSwapMs) >= StatsScreenPeriodMs)
        {
            DataState.currentStatsScreen = (DataState.currentStatsScreen + 1) % 3;
            DataState.lastStatsScreenSwapMs = millis();
        }

        switch (DataState.currentStatsScreen)
        {
            case 0:
                drawCountScreen(DataState.totalSamples);
                break;
            case 1:
                drawBarScreen(kFontH, DataState.maxValues);
                break;
            case 2:
            default:
                drawBarScreen(kFontL, DataState.minValues);
                break;
        }
    }

    void renderDumpDisplay()
    {
        drawText3x5(kFontD, kFontM, kFontP);
    }

    void eraseEntireLog()
    {
        for (int address = logStartAddress(); address < EEPROM.length(); ++address)
        {
            EEPROM.update(address, 0xFF);
        }
    }

    void dumpLogToSerial()
    {
        dumpLog(Serial);
    }

    void dumpLog(Print& out)
    {
        out.println(F("AKINYANKOKUPUMP_DUMP_BEGIN"));
        out.println(F("FORMAT 2"));

        int address = logStartAddress();
        int sampleBytes = LegacySampleBytes;
        while ((address + 4) <= logEndAddress())
        {
            const uint32_t word = readU32(address);
            if (word == EmptyWord)
            {
                break;
            }

            if (word == SessionMarkerV1 || word == SessionMarkerV2)
            {
                if ((address + 12) > logEndAddress())
                {
                    out.println(F("WARN TRUNCATED_SESSION_HEADER"));
                    break;
                }

                const uint32_t timestamp = readU32(address + 4);
                const uint32_t intervalSeconds = readU32(address + 8);
                sampleBytes = (word == SessionMarkerV2) ? DiagnosticSampleBytes : LegacySampleBytes;
                out.print(F("SESSION "));
                out.print(timestamp);
                out.print(' ');
                out.println(intervalSeconds);
                address += 12;
                continue;
            }

            if ((address + sampleBytes) > logEndAddress())
            {
                out.println(F("WARN TRUNCATED_SAMPLE"));
                break;
            }

            if (sampleBytes == DiagnosticSampleBytes)
            {
                SensorSnapshot snapshot;
                readDiagnosticSample(address, snapshot);
                out.print(F("SAMPLE2 "));
                out.print(snapshot.moisture[0]);
                out.print(' ');
                out.print(snapshot.moisture[1]);
                out.print(' ');
                out.print(snapshot.moisture[2]);
                out.print(' ');
                out.print(snapshot.raw[0]);
                out.print(' ');
                out.print(snapshot.raw[1]);
                out.print(' ');
                out.print(snapshot.raw[2]);
                out.print(' ');
                out.println(snapshot.statusMask);
            }
            else
            {
                uint8_t sample[NrCells] = {};
                EEPROM.get(address, sample);
                out.print(F("SAMPLE "));
                out.print(sample[0]);
                out.print(' ');
                out.print(sample[1]);
                out.print(' ');
                out.print(sample[2]);
                out.print(' ');
                out.println(sample[3]);
            }
            address += sampleBytes;
        }

        out.println(F("AKINYANKOKUPUMP_DUMP_END"));
        out.flush();
    }

    void printSensorDiagnostics(const SensorSnapshot& snapshot)
    {
        printSensorDiagnostics(Serial, snapshot);
    }

    void printSensorDiagnostics(Print& out, const SensorSnapshot& snapshot)
    {
        out.println(F("Sensor diagnostics:"));
        for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
        {
            out.print(F("  cell "));
            out.print(cellIdx);
            out.print(F(" addr=0x"));
            out.print(Cells[cellIdx].GetSensorAddress(), HEX);
            out.print(F(" connected="));
            out.print((connectedMask(snapshot) & cellMask(cellIdx)) ? F("yes") : F("no"));
            out.print(F(" error="));
            out.print((errorMask(snapshot) & cellMask(cellIdx)) ? F("yes") : F("no"));
            out.print(F(" raw="));
            out.print(snapshot.raw[cellIdx]);
            out.print(F(" norm="));
            out.print(Cells[cellIdx].GetMoistnessNorm(), 3);
            out.print(F(" byte="));
            out.println(snapshot.moisture[cellIdx]);
        }
    }

    void printSensorDiagnostics()
    {
        SensorSnapshot snapshot;
        sampleAllCells(snapshot);
        printSensorDiagnostics(snapshot);
    }

}

void setup()
{
    Serial.begin(115200);
    Wire.begin();
    RTC.begin();
    LedMatrix.begin();

    pinMode(ModeSensePin, INPUT_PULLUP);

    Cells[0].Initialize(0, 0, 0, 0x36);
    Cells[1].Initialize(7, 0, 1, 0x37);
    Cells[2].Initialize(0, 5, 2, 0x38);
    Cells[3].Initialize(7, 5, 3, 0x39);
    loadConfig();

    printModeHelp();
    StartupStartedAtMs = millis();
    Serial.println(F("Waiting for WiFi/NTP before starting normal operation."));
    connectWifiIfNeeded();

    LedMatrix.loadSequence(LEDMATRIX_ANIMATION_STARTUP);
    LedMatrix.play(true);
}

void loop()
{
    handleSerialCommands();
    handleWifi();

    if (!OperationsStarted)
    {
        startOperationsIfReady();
        delay(UpdateDelayMs);
        return;
    }

    updateModeFromHardware();

    if (!IntroFinished)
    {
        if (millis() < IntroAnimEndsAtMs)
        {
            delay(1);
            return;
        }
        IntroFinished = true;
        LedMatrix.clear();
    }

    if (CurrentMode == OperationMode::Irrigation)
    {
        LedMatrix.beginDraw();
        Cells[CurrentCell].Update(LedMatrix);
        CurrentCell = (CurrentCell + 1) % NrCells;
        LedMatrix.endDraw();
        delay(UpdateDelayMs);
        return;
    }

    if (DataState.outOfMemory)
    {
        LedMatrix.beginDraw();
        renderGatherModeDisplay();
        LedMatrix.endDraw();
        delay(UpdateDelayMs);
        return;
    }

    Cells[CurrentCell].RefreshSensor();
    Cells[CurrentCell].ForceDefaultSolenoidState();
    DataState.latest[CurrentCell] = Cells[CurrentCell].GetMoistureByte();
    CurrentCell = (CurrentCell + 1) % NrCells;

    if ((millis() - DataState.lastWriteMs) >= LogIntervalMs)
    {
        SensorSnapshot snapshot;
        sampleAllCells(snapshot);
        appendSample(snapshot);
    }

    LedMatrix.beginDraw();
    renderGatherModeDisplay();
    LedMatrix.endDraw();

    delay(UpdateDelayMs);
}
