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
    constexpr uint8_t ConfigVersion = 6;
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
    constexpr unsigned long UpdateDelayMs = 2;
    constexpr uint32_t DefaultLogIntervalSeconds = 60UL;
    constexpr uint32_t MinLogIntervalSeconds = 10UL;
    constexpr uint32_t MaxLogIntervalSeconds = 24UL * 60UL * 60UL;
    constexpr uint32_t DefaultZoneSwitchIntervalSeconds = 60UL;
    constexpr uint32_t MinZoneSwitchIntervalSeconds = 10UL;
    constexpr uint32_t MaxZoneSwitchIntervalSeconds = 24UL * 60UL * 60UL;
    constexpr uint32_t DefaultI2cClockHz = 10000UL;
    constexpr uint32_t MinI2cClockHz = 1000UL;
    constexpr uint32_t MaxI2cClockHz = 400000UL;
    constexpr uint16_t DefaultDryCalibrationRaw = 324;
    constexpr uint16_t DefaultWetCalibrationRaw = 1023;
    constexpr uint16_t MinCalibrationRaw = 0;
    constexpr uint16_t MaxCalibrationRaw = 1023;
    constexpr unsigned long StatsScreenPeriodMs = 2500;
    constexpr unsigned long WifiRetryMs = 30000;
    constexpr unsigned long NtpRetryMs = 300000;
    constexpr unsigned long NtpStartupRetryMs = 10000;
    constexpr unsigned long StartupTimeSyncTimeoutMs = 60000;
    constexpr uint16_t HttpPort = 80;
    constexpr uint16_t NtpLocalPort = 2390;
    constexpr int NtpPacketSize = 48;
    constexpr int DebugAdcMaxReading = 1023;
    constexpr int DebugAnalogReferenceMv = 5000;
    constexpr int AnalogSettlingReadCount = 6;
    constexpr int AnalogSettlingDelayUs = 250;
    constexpr unsigned long PipelineLogIntervalMs = 1000;
    constexpr int PipelineLogEntries = 12;
    constexpr int PipelineLogReadSlots = 5;
    const char WifiHostname[] = "garden-pump";
    constexpr int AnalogPins[NrCells] = {A0, A1, A2, A3};

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

    struct PipelineCellLog
    {
        int readings[PipelineLogReadSlots] = {};
        uint8_t readCount = 0;
        bool stableCluster = false;
        int spread = 0;
        int accepted = 0;
        int selectedRaw = 0;
        int lastRaw = 0;
        int filteredRaw = 0;
        int voltageMv = 0;
        float vwcPercent = 0.0f;
        float moistureNorm = 0.0f;
        uint8_t moistureByte = 0;
        uint8_t latest = 0;
        bool connected = false;
        bool error = false;
        bool relay = false;
        SensorInputMode source = SensorInputMode::Seesaw;
        unsigned long ageMs = 0;
    };

    struct PipelineLogEntry
    {
        unsigned long timestampMs = 0;
        int currentCell = 0;
        bool dataGathering = false;
        PipelineCellLog cells[NrCells];
    };

    struct PumpConfig
    {
        uint32_t marker = ConfigMarker;
        uint8_t version = ConfigVersion;
        uint8_t startThreshold[NrCells] = {40, 40, 40, 40};
        uint8_t stopThreshold[NrCells] = {60, 60, 60, 60};
        char wifiSsid[WifiSsidMaxLength + 1] = {};
        char wifiPassword[WifiPasswordMaxLength + 1] = {};
        uint32_t logIntervalSeconds = DefaultLogIntervalSeconds;
        uint32_t i2cClockHz = DefaultI2cClockHz;
        uint16_t dryCalibrationRaw[NrCells] = {
            DefaultDryCalibrationRaw,
            DefaultDryCalibrationRaw,
            DefaultDryCalibrationRaw,
            DefaultDryCalibrationRaw,
        };
        uint16_t wetCalibrationRaw[NrCells] = {
            DefaultWetCalibrationRaw,
            DefaultWetCalibrationRaw,
            DefaultWetCalibrationRaw,
            DefaultWetCalibrationRaw,
        };
        uint8_t sensorInputMode[NrCells] = {
            static_cast<uint8_t>(SensorInputMode::Seesaw),
            static_cast<uint8_t>(SensorInputMode::Seesaw),
            static_cast<uint8_t>(SensorInputMode::Seesaw),
            static_cast<uint8_t>(SensorInputMode::Seesaw),
        };
        uint8_t zoneSensor[NrCells] = {0, 1, 2, 3};
        uint32_t zoneSwitchIntervalSeconds = DefaultZoneSwitchIntervalSeconds;
        uint8_t checksum = 0;
    };

    struct PumpConfigV5
    {
        uint32_t marker = ConfigMarker;
        uint8_t version = 5;
        uint8_t startThreshold[NrCells] = {40, 40, 40, 40};
        uint8_t stopThreshold[NrCells] = {60, 60, 60, 60};
        char wifiSsid[WifiSsidMaxLength + 1] = {};
        char wifiPassword[WifiPasswordMaxLength + 1] = {};
        uint32_t logIntervalSeconds = DefaultLogIntervalSeconds;
        uint32_t i2cClockHz = DefaultI2cClockHz;
        uint16_t dryCalibrationRaw[NrCells] = {
            DefaultDryCalibrationRaw,
            DefaultDryCalibrationRaw,
            DefaultDryCalibrationRaw,
            DefaultDryCalibrationRaw,
        };
        uint16_t wetCalibrationRaw[NrCells] = {
            DefaultWetCalibrationRaw,
            DefaultWetCalibrationRaw,
            DefaultWetCalibrationRaw,
            DefaultWetCalibrationRaw,
        };
        uint8_t sensorInputMode[NrCells] = {
            static_cast<uint8_t>(SensorInputMode::Seesaw),
            static_cast<uint8_t>(SensorInputMode::Seesaw),
            static_cast<uint8_t>(SensorInputMode::Seesaw),
            static_cast<uint8_t>(SensorInputMode::Seesaw),
        };
        uint8_t checksum = 0;
    };

    struct PumpConfigV4
    {
        uint32_t marker = ConfigMarker;
        uint8_t version = 4;
        uint8_t startThreshold[NrCells] = {40, 40, 40, 40};
        uint8_t stopThreshold[NrCells] = {60, 60, 60, 60};
        char wifiSsid[WifiSsidMaxLength + 1] = {};
        char wifiPassword[WifiPasswordMaxLength + 1] = {};
        uint32_t logIntervalSeconds = DefaultLogIntervalSeconds;
        uint32_t i2cClockHz = DefaultI2cClockHz;
        uint16_t dryCalibrationRaw[NrCells] = {
            DefaultDryCalibrationRaw,
            DefaultDryCalibrationRaw,
            DefaultDryCalibrationRaw,
            DefaultDryCalibrationRaw,
        };
        uint16_t wetCalibrationRaw[NrCells] = {
            DefaultWetCalibrationRaw,
            DefaultWetCalibrationRaw,
            DefaultWetCalibrationRaw,
            DefaultWetCalibrationRaw,
        };
        uint8_t checksum = 0;
    };

    struct PumpConfigV3
    {
        uint32_t marker = ConfigMarker;
        uint8_t version = 3;
        uint8_t startThreshold[NrCells] = {40, 40, 40, 40};
        uint8_t stopThreshold[NrCells] = {60, 60, 60, 60};
        char wifiSsid[WifiSsidMaxLength + 1] = {};
        char wifiPassword[WifiPasswordMaxLength + 1] = {};
        uint32_t logIntervalSeconds = DefaultLogIntervalSeconds;
        uint32_t i2cClockHz = DefaultI2cClockHz;
        uint8_t checksum = 0;
    };

    struct PumpConfigV2
    {
        uint32_t marker = ConfigMarker;
        uint8_t version = 2;
        uint8_t startThreshold[NrCells] = {40, 40, 40, 40};
        uint8_t stopThreshold[NrCells] = {60, 60, 60, 60};
        char wifiSsid[WifiSsidMaxLength + 1] = {};
        char wifiPassword[WifiPasswordMaxLength + 1] = {};
        uint32_t logIntervalSeconds = DefaultLogIntervalSeconds;
        uint8_t checksum = 0;
    };

    struct PumpConfigV1
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
    void applyI2cClock();
    uint8_t configChecksum(const PumpConfig& config);
    uint8_t configChecksumV5(const PumpConfigV5& config);
    uint8_t configChecksumV4(const PumpConfigV4& config);
    uint8_t configChecksumV3(const PumpConfigV3& config);
    uint8_t configChecksumV2(const PumpConfigV2& config);
    uint8_t configChecksumV1(const PumpConfigV1& config);
    bool setCellThresholds(int cellIdx, uint8_t startPercent, uint8_t stopPercent);
    bool setCellCalibration(int cellIdx, uint16_t dryRaw, uint16_t wetRaw);
    bool setCellSensorInputMode(int cellIdx, SensorInputMode mode);
    bool setLogIntervalSeconds(uint32_t intervalSeconds);
    bool setZoneSwitchIntervalSeconds(uint32_t intervalSeconds);
    bool setZoneSensor(int zoneIdx, int sensorIdx);
    bool setI2cClockHz(uint32_t clockHz);
    bool setForcedIrrigationZone(int zone);
    bool setDataGatheringActive(bool active);
    void updateIrrigationScheduler();
    void applyRelayOutputs(int activeZone);
    bool zoneNeedsWater(int zoneIdx);
    bool isForcedIrrigationActive();
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
    void printRawSensorDiagnostics(Print& out);
    void sendRawSensorDiagnostics(WiFiClient& client);
    void sendAnalogDiagnostics(WiFiClient& client);
    void sendPipelineLog(WiFiClient& client);
    void sendPipelineLatest(WiFiClient& client);
    void sendTextDump(WiFiClient& client, bool eraseAfterDump);
    void sendTextI2CScan(WiFiClient& client);
    void sendThresholdUpdate(WiFiClient& client, const String& requestLine);
    void sendCalibrationUpdate(WiFiClient& client, const String& requestLine);
    void sendSensorInputModeUpdate(WiFiClient& client, const String& requestLine);
    void sendLogIntervalUpdate(WiFiClient& client, const String& requestLine);
    void sendZoneSwitchIntervalUpdate(WiFiClient& client, const String& requestLine);
    void sendZoneSensorUpdate(WiFiClient& client, const String& requestLine);
    void sendI2cClockUpdate(WiFiClient& client, const String& requestLine);
    void sendForcedIrrigationUpdate(WiFiClient& client, const String& requestLine);
    void sendDataGatheringUpdate(WiFiClient& client, const String& requestLine);
    void sendSimulationUpdate(WiFiClient& client, const String& requestLine);
    int queryIntValue(const String& requestLine, const char* key, int fallback);
    String queryStringValue(const String& requestLine, const char* key);
    void printAnalogPinName(Print& out, int analogPin);
    int readAnalogSettled(int analogPin);
    void sendWifiUpdate(WiFiClient& client, const String& requestLine);
    void sendNotFound(WiFiClient& client);
    void printSensorDiagnostics(Print& out, const SensorSnapshot& snapshot);
    void printSensorDiagnostics();
    void printSensorDiagnostics(const SensorSnapshot& snapshot);
    void updatePipelineHistory();
    void capturePipelineEntry(PipelineLogEntry& entry);
    void printPipelineEntry(Print& out, const PipelineLogEntry& entry);
    GatherState DataState;
    PumpConfig Config;
    PipelineLogEntry PipelineHistory[PipelineLogEntries];
    int PipelineHistoryNext = 0;
    int PipelineHistoryCount = 0;
    unsigned long LastPipelineLogMs = 0;
    int CurrentCell = 0;
    int ForcedIrrigationZone = -1;
    int ActiveIrrigationZone = -1;
    unsigned long LastZoneSwitchMs = 0;
    bool SimulationEnabled = false;
    uint8_t SimulatedMoisturePercent[NrCells] = {30, 30, 30, 30};
    bool DataGatheringActive = false;
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
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 8);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 16);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 24);
        sum ^= static_cast<uint8_t>(config.i2cClockHz);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 8);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 16);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 24);
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            sum ^= static_cast<uint8_t>(config.dryCalibrationRaw[cellIdx]);
            sum ^= static_cast<uint8_t>(config.dryCalibrationRaw[cellIdx] >> 8);
            sum ^= static_cast<uint8_t>(config.wetCalibrationRaw[cellIdx]);
            sum ^= static_cast<uint8_t>(config.wetCalibrationRaw[cellIdx] >> 8);
            sum ^= static_cast<uint8_t>(config.sensorInputMode[cellIdx] + (cellIdx * 7));
            sum ^= static_cast<uint8_t>(config.zoneSensor[cellIdx] + (cellIdx * 11));
        }
        sum ^= static_cast<uint8_t>(config.zoneSwitchIntervalSeconds);
        sum ^= static_cast<uint8_t>(config.zoneSwitchIntervalSeconds >> 8);
        sum ^= static_cast<uint8_t>(config.zoneSwitchIntervalSeconds >> 16);
        sum ^= static_cast<uint8_t>(config.zoneSwitchIntervalSeconds >> 24);
        return sum;
    }

    uint8_t configChecksumV5(const PumpConfigV5& config)
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
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 8);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 16);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 24);
        sum ^= static_cast<uint8_t>(config.i2cClockHz);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 8);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 16);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 24);
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            sum ^= static_cast<uint8_t>(config.dryCalibrationRaw[cellIdx]);
            sum ^= static_cast<uint8_t>(config.dryCalibrationRaw[cellIdx] >> 8);
            sum ^= static_cast<uint8_t>(config.wetCalibrationRaw[cellIdx]);
            sum ^= static_cast<uint8_t>(config.wetCalibrationRaw[cellIdx] >> 8);
            sum ^= static_cast<uint8_t>(config.sensorInputMode[cellIdx] + (cellIdx * 7));
        }
        return sum;
    }

    uint8_t configChecksumV4(const PumpConfigV4& config)
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
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 8);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 16);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 24);
        sum ^= static_cast<uint8_t>(config.i2cClockHz);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 8);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 16);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 24);
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            sum ^= static_cast<uint8_t>(config.dryCalibrationRaw[cellIdx]);
            sum ^= static_cast<uint8_t>(config.dryCalibrationRaw[cellIdx] >> 8);
            sum ^= static_cast<uint8_t>(config.wetCalibrationRaw[cellIdx]);
            sum ^= static_cast<uint8_t>(config.wetCalibrationRaw[cellIdx] >> 8);
        }
        return sum;
    }

    uint8_t configChecksumV3(const PumpConfigV3& config)
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
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 8);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 16);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 24);
        sum ^= static_cast<uint8_t>(config.i2cClockHz);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 8);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 16);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 24);
        return sum;
    }

    uint8_t configChecksumV2(const PumpConfigV2& config)
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
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 8);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 16);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 24);
        return sum;
    }

    uint8_t configChecksumV1(const PumpConfigV1& config)
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
            Cells[cellIdx].SetMoistureCalibration(
                Config.dryCalibrationRaw[cellIdx],
                Config.wetCalibrationRaw[cellIdx]);
            Cells[cellIdx].SetSensorInputMode(static_cast<SensorInputMode>(Config.sensorInputMode[cellIdx]));
            Cells[cellIdx].SetSimulatedMoisture(SimulationEnabled, SimulatedMoisturePercent[cellIdx]);
        }
    }

    void applyI2cClock()
    {
        Wire.setClock(Config.i2cClockHz);
    }

    void saveConfig()
    {
        Config.marker = ConfigMarker;
        Config.version = ConfigVersion;
        Config.checksum = configChecksum(Config);
        EEPROM.put(ConfigStartAddress, Config);
    }

    void loadConfig()
    {
        EEPROM.get(ConfigStartAddress, Config);
        const bool valid = Config.marker == ConfigMarker
            && Config.version == ConfigVersion
            && Config.checksum == configChecksum(Config);
        if (!valid)
        {
            PumpConfigV5 oldConfigV5;
            EEPROM.get(ConfigStartAddress, oldConfigV5);
            const bool validV5 = oldConfigV5.marker == ConfigMarker
                && oldConfigV5.version == 5
                && oldConfigV5.checksum == configChecksumV5(oldConfigV5);

            PumpConfigV4 oldConfigV4;
            EEPROM.get(ConfigStartAddress, oldConfigV4);
            const bool validV4 = oldConfigV4.marker == ConfigMarker
                && oldConfigV4.version == 4
                && oldConfigV4.checksum == configChecksumV4(oldConfigV4);

            PumpConfigV3 oldConfigV3;
            EEPROM.get(ConfigStartAddress, oldConfigV3);
            const bool validV3 = oldConfigV3.marker == ConfigMarker
                && oldConfigV3.version == 3
                && oldConfigV3.checksum == configChecksumV3(oldConfigV3);

            PumpConfigV2 oldConfigV2;
            EEPROM.get(ConfigStartAddress, oldConfigV2);
            const bool validV2 = oldConfigV2.marker == ConfigMarker
                && oldConfigV2.version == 2
                && oldConfigV2.checksum == configChecksumV2(oldConfigV2);

            PumpConfigV1 oldConfig;
            EEPROM.get(ConfigStartAddress, oldConfig);
            const bool validV1 = oldConfig.marker == ConfigMarker
                && oldConfig.version == 1
                && oldConfig.checksum == configChecksumV1(oldConfig);

            Config = PumpConfig{};
            if (validV5)
            {
                memcpy(Config.startThreshold, oldConfigV5.startThreshold, sizeof(Config.startThreshold));
                memcpy(Config.stopThreshold, oldConfigV5.stopThreshold, sizeof(Config.stopThreshold));
                memcpy(Config.wifiSsid, oldConfigV5.wifiSsid, sizeof(Config.wifiSsid));
                memcpy(Config.wifiPassword, oldConfigV5.wifiPassword, sizeof(Config.wifiPassword));
                Config.logIntervalSeconds = oldConfigV5.logIntervalSeconds;
                Config.i2cClockHz = oldConfigV5.i2cClockHz;
                memcpy(Config.dryCalibrationRaw, oldConfigV5.dryCalibrationRaw, sizeof(Config.dryCalibrationRaw));
                memcpy(Config.wetCalibrationRaw, oldConfigV5.wetCalibrationRaw, sizeof(Config.wetCalibrationRaw));
                memcpy(Config.sensorInputMode, oldConfigV5.sensorInputMode, sizeof(Config.sensorInputMode));
            }
            else if (validV4)
            {
                memcpy(Config.startThreshold, oldConfigV4.startThreshold, sizeof(Config.startThreshold));
                memcpy(Config.stopThreshold, oldConfigV4.stopThreshold, sizeof(Config.stopThreshold));
                memcpy(Config.wifiSsid, oldConfigV4.wifiSsid, sizeof(Config.wifiSsid));
                memcpy(Config.wifiPassword, oldConfigV4.wifiPassword, sizeof(Config.wifiPassword));
                Config.logIntervalSeconds = oldConfigV4.logIntervalSeconds;
                Config.i2cClockHz = oldConfigV4.i2cClockHz;
                memcpy(Config.dryCalibrationRaw, oldConfigV4.dryCalibrationRaw, sizeof(Config.dryCalibrationRaw));
                memcpy(Config.wetCalibrationRaw, oldConfigV4.wetCalibrationRaw, sizeof(Config.wetCalibrationRaw));
            }
            else if (validV3)
            {
                memcpy(Config.startThreshold, oldConfigV3.startThreshold, sizeof(Config.startThreshold));
                memcpy(Config.stopThreshold, oldConfigV3.stopThreshold, sizeof(Config.stopThreshold));
                memcpy(Config.wifiSsid, oldConfigV3.wifiSsid, sizeof(Config.wifiSsid));
                memcpy(Config.wifiPassword, oldConfigV3.wifiPassword, sizeof(Config.wifiPassword));
                Config.logIntervalSeconds = oldConfigV3.logIntervalSeconds;
                Config.i2cClockHz = oldConfigV3.i2cClockHz;
            }
            else if (validV2)
            {
                memcpy(Config.startThreshold, oldConfigV2.startThreshold, sizeof(Config.startThreshold));
                memcpy(Config.stopThreshold, oldConfigV2.stopThreshold, sizeof(Config.stopThreshold));
                memcpy(Config.wifiSsid, oldConfigV2.wifiSsid, sizeof(Config.wifiSsid));
                memcpy(Config.wifiPassword, oldConfigV2.wifiPassword, sizeof(Config.wifiPassword));
                Config.logIntervalSeconds = oldConfigV2.logIntervalSeconds;
                Config.i2cClockHz = DefaultI2cClockHz;
            }
            else if (validV1)
            {
                memcpy(Config.startThreshold, oldConfig.startThreshold, sizeof(Config.startThreshold));
                memcpy(Config.stopThreshold, oldConfig.stopThreshold, sizeof(Config.stopThreshold));
                memcpy(Config.wifiSsid, oldConfig.wifiSsid, sizeof(Config.wifiSsid));
                memcpy(Config.wifiPassword, oldConfig.wifiPassword, sizeof(Config.wifiPassword));
                Config.logIntervalSeconds = DefaultLogIntervalSeconds;
                Config.i2cClockHz = DefaultI2cClockHz;
            }
        }

        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            Config.startThreshold[cellIdx] = constrain(Config.startThreshold[cellIdx], 0, 100);
            Config.stopThreshold[cellIdx] = constrain(Config.stopThreshold[cellIdx], 0, 100);
            if (Config.stopThreshold[cellIdx] < Config.startThreshold[cellIdx])
            {
                Config.stopThreshold[cellIdx] = Config.startThreshold[cellIdx];
            }
            Config.dryCalibrationRaw[cellIdx] = constrain(
                Config.dryCalibrationRaw[cellIdx],
                MinCalibrationRaw,
                static_cast<uint16_t>(MaxCalibrationRaw - 1));
            Config.wetCalibrationRaw[cellIdx] = constrain(
                Config.wetCalibrationRaw[cellIdx],
                static_cast<uint16_t>(Config.dryCalibrationRaw[cellIdx] + 1),
                MaxCalibrationRaw);
            if (Config.sensorInputMode[cellIdx] != static_cast<uint8_t>(SensorInputMode::Seesaw)
                && Config.sensorInputMode[cellIdx] != static_cast<uint8_t>(SensorInputMode::Vh400)
                && Config.sensorInputMode[cellIdx] != static_cast<uint8_t>(SensorInputMode::NotUsed))
            {
                Config.sensorInputMode[cellIdx] = static_cast<uint8_t>(SensorInputMode::Seesaw);
            }
            if (Config.zoneSensor[cellIdx] >= NrCells)
            {
                Config.zoneSensor[cellIdx] = cellIdx;
            }
        }
        Config.wifiSsid[WifiSsidMaxLength] = '\0';
        Config.wifiPassword[WifiPasswordMaxLength] = '\0';
        if (Config.logIntervalSeconds < MinLogIntervalSeconds || Config.logIntervalSeconds > MaxLogIntervalSeconds)
        {
            Config.logIntervalSeconds = DefaultLogIntervalSeconds;
        }
        if (Config.i2cClockHz < MinI2cClockHz || Config.i2cClockHz > MaxI2cClockHz)
        {
            Config.i2cClockHz = DefaultI2cClockHz;
        }
        if (Config.zoneSwitchIntervalSeconds < MinZoneSwitchIntervalSeconds
            || Config.zoneSwitchIntervalSeconds > MaxZoneSwitchIntervalSeconds)
        {
            Config.zoneSwitchIntervalSeconds = DefaultZoneSwitchIntervalSeconds;
        }
        saveConfig();
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

    bool setCellCalibration(int cellIdx, uint16_t dryRaw, uint16_t wetRaw)
    {
        if (cellIdx < 0 || cellIdx >= NrCells || dryRaw >= wetRaw || wetRaw > MaxCalibrationRaw)
        {
            return false;
        }

        Config.dryCalibrationRaw[cellIdx] = dryRaw;
        Config.wetCalibrationRaw[cellIdx] = wetRaw;
        applyConfig();
        saveConfig();
        return true;
    }

    bool setCellSensorInputMode(int cellIdx, SensorInputMode mode)
    {
        if (cellIdx < 0 || cellIdx >= NrCells
            || (mode != SensorInputMode::Seesaw && mode != SensorInputMode::Vh400 && mode != SensorInputMode::NotUsed))
        {
            return false;
        }

        Config.sensorInputMode[cellIdx] = static_cast<uint8_t>(mode);
        Cells[cellIdx].SetSensorInputMode(mode);
        Cells[cellIdx].SetSimulatedMoisture(SimulationEnabled, SimulatedMoisturePercent[cellIdx]);
        saveConfig();
        return true;
    }

    bool setZoneSensor(int zoneIdx, int sensorIdx)
    {
        if (zoneIdx < 0 || zoneIdx >= NrCells || sensorIdx < 0 || sensorIdx >= NrCells)
        {
            return false;
        }

        Config.zoneSensor[zoneIdx] = static_cast<uint8_t>(sensorIdx);
        saveConfig();
        LastZoneSwitchMs = 0;
        return true;
    }

    bool setZoneSwitchIntervalSeconds(uint32_t intervalSeconds)
    {
        if (intervalSeconds < MinZoneSwitchIntervalSeconds || intervalSeconds > MaxZoneSwitchIntervalSeconds)
        {
            return false;
        }

        Config.zoneSwitchIntervalSeconds = intervalSeconds;
        saveConfig();
        return true;
    }

    bool setI2cClockHz(uint32_t clockHz)
    {
        if (clockHz < MinI2cClockHz || clockHz > MaxI2cClockHz)
        {
            return false;
        }

        Config.i2cClockHz = clockHz;
        applyI2cClock();
        saveConfig();
        return true;
    }

    bool setForcedIrrigationZone(int zone)
    {
        if (zone < -1 || zone >= NrCells)
        {
            return false;
        }

        ForcedIrrigationZone = zone;
        ActiveIrrigationZone = zone;
        LastZoneSwitchMs = zone >= 0 ? millis() : 0;
        if (DataGatheringActive)
        {
            ForcedIrrigationZone = -1;
            ActiveIrrigationZone = -1;
            applyRelayOutputs(-1);
            return true;
        }

        applyRelayOutputs(ActiveIrrigationZone);
        return true;
    }

    bool isForcedIrrigationActive()
    {
        return !DataGatheringActive && ForcedIrrigationZone >= 0 && ForcedIrrigationZone < NrCells;
    }

    bool zoneNeedsWater(int zoneIdx)
    {
        if (zoneIdx < 0 || zoneIdx >= NrCells)
        {
            return false;
        }

        const int sensorIdx = Config.zoneSensor[zoneIdx];
        if (sensorIdx < 0 || sensorIdx >= NrCells)
        {
            return false;
        }

        return Cells[sensorIdx].ShouldWater();
    }

    int countWateringZonesNeeded()
    {
        int count = 0;
        for (int zoneIdx = 0; zoneIdx < NrCells; ++zoneIdx)
        {
            if (zoneNeedsWater(zoneIdx))
            {
                count++;
            }
        }

        return count;
    }

    int nextWateringZoneAfter(int startZone)
    {
        for (int offset = 1; offset <= NrCells; ++offset)
        {
            const int zoneIdx = (startZone + offset + NrCells) % NrCells;
            if (zoneNeedsWater(zoneIdx))
            {
                return zoneIdx;
            }
        }

        return -1;
    }

    void applyRelayOutputs(int activeZone)
    {
        for (int zoneIdx = 0; zoneIdx < NrCells; ++zoneIdx)
        {
            Cells[zoneIdx].SetSolenoidOutput(zoneIdx == activeZone);
        }
    }

    void updateIrrigationScheduler()
    {
        if (DataGatheringActive)
        {
            ForcedIrrigationZone = -1;
            ActiveIrrigationZone = -1;
            applyRelayOutputs(-1);
            return;
        }

        const unsigned long now = millis();
        const unsigned long intervalMs = Config.zoneSwitchIntervalSeconds * 1000UL;
        if (LastZoneSwitchMs == 0)
        {
            LastZoneSwitchMs = now;
        }

        if (isForcedIrrigationActive())
        {
            ActiveIrrigationZone = ForcedIrrigationZone;
            if ((now - LastZoneSwitchMs) < intervalMs)
            {
                applyRelayOutputs(ActiveIrrigationZone);
                return;
            }

            ForcedIrrigationZone = -1;
        }

        const int neededCount = countWateringZonesNeeded();
        if (neededCount == 0)
        {
            ActiveIrrigationZone = -1;
            applyRelayOutputs(-1);
            return;
        }

        const bool activeStillNeedsWater = zoneNeedsWater(ActiveIrrigationZone);
        const bool switchDue = (now - LastZoneSwitchMs) >= intervalMs;
        if (!activeStillNeedsWater || (neededCount > 1 && switchDue))
        {
            ActiveIrrigationZone = nextWateringZoneAfter(ActiveIrrigationZone);
            LastZoneSwitchMs = now;
        }

        applyRelayOutputs(ActiveIrrigationZone);
    }

    bool setLogIntervalSeconds(uint32_t intervalSeconds)
    {
        if (intervalSeconds < MinLogIntervalSeconds || intervalSeconds > MaxLogIntervalSeconds)
        {
            return false;
        }

        Config.logIntervalSeconds = intervalSeconds;
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

    unsigned long logIntervalMs()
    {
        return static_cast<unsigned long>(Config.logIntervalSeconds) * 1000UL;
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
        Serial.println(F("  SET_SIMULATION <0|1> [c0 c1 c2 c3] = runtime simulated moisture percent"));
        Serial.println(F("  SET_FORCED_ZONE <-1..3> = runtime irrigation override; -1 disables it"));
        Serial.println(F("  SET_WIFI <ssid> <password> = save WiFi credentials to EEPROM config"));
        Serial.println(F("  CLEAR_WIFI = remove saved WiFi credentials"));
        Serial.println(F("  WIFI_STATUS = print WiFi config and connection status"));
        Serial.println(F("  r = reboot"));
        Serial.println();
    }

    bool isLeapYear(int year)
    {
        return ((year % 4) == 0 && (year % 100) != 0) || (year % 400) == 0;
    }

    int daysInMonth(int year, int month)
    {
        static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month == 2 && isLeapYear(year))
        {
            return 29;
        }
        return days[month - 1];
    }

    int dayOfWeek(int year, int month, int day)
    {
        static const int offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
        if (month < 3)
        {
            --year;
        }
        return (year + year / 4 - year / 100 + year / 400 + offsets[month - 1] + day) % 7;
    }

    int lastSundayOfMonth(int year, int month)
    {
        int day = daysInMonth(year, month);
        while (dayOfWeek(year, month, day) != 0)
        {
            --day;
        }
        return day;
    }

    uint32_t unixTimeUtc(int year, int month, int day, int hour, int minute, int second)
    {
        uint32_t days = 0;
        for (int y = 1970; y < year; ++y)
        {
            days += isLeapYear(y) ? 366UL : 365UL;
        }
        for (int m = 1; m < month; ++m)
        {
            days += daysInMonth(year, m);
        }
        days += static_cast<uint32_t>(day - 1);
        return (((days * 24UL) + static_cast<uint32_t>(hour)) * 60UL + static_cast<uint32_t>(minute)) * 60UL
            + static_cast<uint32_t>(second);
    }

    bool isEuropeStockholmDst(uint32_t utcUnixTime)
    {
        RTCTime utcTime(static_cast<time_t>(utcUnixTime));
        const int year = utcTime.getYear();
        const uint32_t dstStart = unixTimeUtc(year, 3, lastSundayOfMonth(year, 3), 1, 0, 0);
        const uint32_t dstEnd = unixTimeUtc(year, 10, lastSundayOfMonth(year, 10), 1, 0, 0);
        return utcUnixTime >= dstStart && utcUnixTime < dstEnd;
    }

    uint32_t europeStockholmOffsetSeconds(uint32_t utcUnixTime)
    {
        return isEuropeStockholmDst(utcUnixTime) ? 2UL * 60UL * 60UL : 1UL * 60UL * 60UL;
    }

    const __FlashStringHelper* europeStockholmTimeZoneName(uint32_t utcUnixTime)
    {
        return isEuropeStockholmDst(utcUnixTime) ? F("CEST") : F("CET");
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
        const uint32_t utcTimestamp = currentTime.getUnixTime();
        RTCTime localTime(static_cast<time_t>(utcTimestamp + europeStockholmOffsetSeconds(utcTimestamp)));
        String formatted = localTime.toString();
        formatted += ' ';
        formatted += europeStockholmTimeZoneName(utcTimestamp);
        return formatted;
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

    void printSensorInputMode(Print& out, SensorInputMode mode)
    {
        if (mode == SensorInputMode::Vh400)
        {
            out.print(F("vh400"));
        }
        else if (mode == SensorInputMode::NotUsed)
        {
            out.print(F("unused"));
        }
        else
        {
            out.print(F("seesaw"));
        }
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

    void snapshotCachedCells(SensorSnapshot& outSnapshot)
    {
        outSnapshot = SensorSnapshot{};
        for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
        {
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
        if (!hasRoomForBytes(12))
        {
            DataState.outOfMemory = true;
            return;
        }

        writeU32(DataState.nextLogAddress, SessionMarkerV2);
        writeU32(DataState.nextLogAddress + 4, getCurrentTimestamp());
        writeU32(DataState.nextLogAddress + 8, Config.logIntervalSeconds);
        DataState.nextLogAddress += 12;
        DataState.lastWriteMs = millis();
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
        ForcedIrrigationZone = -1;
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
        DataGatheringActive = false;
        Serial.println(F("Entered irrigation mode."));
    }

    void enterGatherMode()
    {
        DataGatheringActive = true;
        beginGatherMode();
    }

    bool setDataGatheringActive(bool active)
    {
        if (DataGatheringActive == active)
        {
            return true;
        }

        if (active)
        {
            enterGatherMode();
        }
        else
        {
            enterIrrigationMode();
        }

        return true;
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
        client.println(F("<title>Garden Pump</title><style>body{font-family:system-ui;margin:0;background:#f5f7f4;color:#172018}header{background:#234b36;color:white;padding:16px 20px}main{padding:16px;max-width:960px;margin:auto}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:10px}.slotgrid{display:grid;grid-template-columns:repeat(2,minmax(220px,1fr));gap:10px;max-width:760px}.card{background:white;border:1px solid #d9dfd7;border-radius:8px;padding:12px}.big{font-size:28px;font-weight:700}.bar{height:10px;background:#dfe6dc;border-radius:999px;overflow:hidden}.fill{height:100%;background:#2f7d4f;width:0}.field{display:block;margin:6px 0}.field input[type=number]{width:72px}.source{display:flex;gap:10px;flex-wrap:wrap;margin:8px 0}.hint{margin:6px 0 12px;color:#4c5a4c}select{max-width:100%}button,a.btn{display:inline-block;margin:4px 4px 4px 0;padding:8px 10px;border:1px solid #9cad9b;border-radius:6px;background:white;color:#172018;text-decoration:none}pre{white-space:pre-wrap;background:#111;color:#eee;padding:12px;border-radius:8px;overflow:auto}@media(max-width:640px){.slotgrid{grid-template-columns:1fr}}</style></head><body>"));
        client.println(F("<header><h1>Garden Pump</h1><div id=\"sub\">Loading...</div></header><main>"));
        client.println(F("<div class=\"grid\"><div class=\"card\"><div>Clock</div><div id=\"clock\">-</div></div><div class=\"card\"><div>WiFi</div><div id=\"wifi\">-</div></div></div>"));
        client.println(F("<h2>Watering zone override</h2><div class=\"card\"><label><input type=\"radio\" name=\"forcedZone\" value=\"-1\"> off</label><label><input type=\"radio\" name=\"forcedZone\" value=\"0\"> zone 1</label><label><input type=\"radio\" name=\"forcedZone\" value=\"1\"> zone 2</label><label><input type=\"radio\" name=\"forcedZone\" value=\"2\"> zone 3</label><label><input type=\"radio\" name=\"forcedZone\" value=\"3\"> zone 4</label><label class=\"field\">Switch interval (seconds) <input id=\"zoneSwitchInterval\" type=\"number\" min=\"10\" max=\"86400\"></label><button id=\"saveZoneSwitchInterval\">Save interval</button></div>"));
        client.println(F("<h2>Irrigation zones</h2><div class=\"slotgrid\" id=\"irrigationZones\"></div>"));
        client.println(F("<h2>Simulation</h2><div class=\"card\" id=\"simulationControls\"><label><input id=\"simulationEnabled\" type=\"checkbox\"> simulate sensor moisture</label><div class=\"slotgrid\" id=\"simulationCells\"></div><button id=\"saveSimulation\">Apply simulation</button></div>"));
        client.println(F("<h2>Sensors</h2><div class=\"hint\">Use Analog diag for raw A0..A3 reads.</div><div class=\"slotgrid\" id=\"sensors\"></div><h2>Controls</h2>"));
        client.println(F("<a class=\"btn\" href=\"/api/diag\">DIAG</a><a class=\"btn\" href=\"/api/analog_diag\">Analog diag</a><a class=\"btn\" href=\"/api/pipeline_log\">Pipeline log</a><a class=\"btn\" href=\"/api/dump_no_erase\">View dump</a><button class=\"btn\" id=\"downloadDump\">Download dump</button><button class=\"btn\" id=\"downloadThenClear\">Download then clear</button><button class=\"btn\" id=\"clearLog\">Clear memory</button><a class=\"btn\" href=\"/api/sync_time\">Sync time</a>"));
        client.println(F("<h2>Output</h2><pre id=\"out\"></pre><h2>Pipeline</h2><button class=\"btn\" id=\"refreshPipeline\">Refresh pipeline</button><pre id=\"pipeline\">Loading...</pre><script>"));
        client.println(F("let forcedBusyUntil=0,sensorBusyUntil=0,zoneBusyUntil=0,simulationBusyUntil=0,statusBusy=false,apiBusy=false,pipelineBusy=false"));
        client.println(F("function esc(v){return encodeURIComponent(v)}"));
        client.println(F("async function api(p){if(apiBusy)return;apiBusy=true;out.textContent='Working...';try{let r=await fetch(p);out.textContent=await r.text()}catch(e){out.textContent='Request failed: '+e}finally{apiBusy=false;status()}}"));
        client.println(F("function card(c){let pct=Math.max(0,Math.min(100,c.moisturePercent||0));let h='<div class=\"card\"><b>Slot '+(c.index+1)+'</b><div>source '+c.sensorSource+' / '+c.analogPin+' ('+c.analogPinNumber+')</div><div>internal cell '+c.index+' addr '+c.address+'</div><div>'+(c.connected?'connected':'not connected')+' / '+(c.error?'error':'ok')+'</div>';"));
        client.println(F("if(c.sensorSource==='vh400'){h+='<div>voltage '+c.voltageMv+' mV / VWC '+c.vwcPercent.toFixed(1)+'%</div><div>confidence '+c.vh400ConnectionScore+' / spread '+c.readSpread+' / accepted '+c.acceptedReads+'</div>'}else if(c.sensorSource==='seesaw'){h+='<div>raw '+c.raw+' / filtered '+c.filteredRaw+'</div><div>spread '+c.readSpread+' / accepted '+c.acceptedReads+'</div>'}else{h+='<div>not installed</div>'}"));
        client.println(F("h+='<div>relay '+(c.relay?'open':'closed')+'</div><div class=\"bar\"><div class=\"fill\" style=\"width:'+pct+'%\"></div></div><div>moisture '+pct.toFixed(1)+'%</div>';"));
        client.println(F("h+='<div class=\"source\"><label><input type=\"radio\" name=\"source'+c.index+'\" value=\"unused\" '+(c.sensorSource==='unused'?'checked':'')+'> not used</label><label><input type=\"radio\" name=\"source'+c.index+'\" value=\"seesaw\" '+(c.sensorSource==='seesaw'?'checked':'')+'> seesaw</label><label><input type=\"radio\" name=\"source'+c.index+'\" value=\"vh400\" '+(c.sensorSource==='vh400'?'checked':'')+'> VH400</label></div>';"));
        client.println(F("h+='<label class=\"field\">Start Watering <input id=\"st'+c.index+'\" type=\"number\" min=\"0\" max=\"100\" value=\"'+c.startThreshold+'\"> %</label><label class=\"field\">Stop Watering <input id=\"sp'+c.index+'\" type=\"number\" min=\"0\" max=\"100\" value=\"'+c.stopThreshold+'\"> %</label><button onclick=\"setThreshold('+c.index+')\">Save watering</button>';"));
        client.println(F("if(c.sensorSource==='seesaw'){h+='<div>calibration raw '+c.dryCalibrationRaw+'..'+c.wetCalibrationRaw+'</div><label>dry <input id=\"dry'+c.index+'\" type=\"number\" min=\"0\" max=\"1022\" placeholder=\"'+c.dryCalibrationRaw+'\"></label><label> wet <input id=\"wet'+c.index+'\" type=\"number\" min=\"1\" max=\"1023\" placeholder=\"'+c.wetCalibrationRaw+'\"></label><button onclick=\"setCalibration('+c.index+')\">Save cal</button><button onclick=\"useCurrentDry('+c.index+','+c.filteredRaw+')\">Use dry</button><button onclick=\"useCurrentWet('+c.index+','+c.filteredRaw+')\">Use wet</button>'}h+='</div>';return h}"));
        client.println(F("function simCard(c){return '<div><label>slot '+(c.index+1)+' <input id=\"sim'+c.index+'\" type=\"range\" min=\"0\" max=\"100\" value=\"'+c.simulatedMoisturePercent+'\"></label><input id=\"simn'+c.index+'\" type=\"number\" min=\"0\" max=\"100\" value=\"'+c.simulatedMoisturePercent+'\"></div>'}"));
        client.println(F("function zoneCard(z){let h='<div class=\"card\"><b>Zone '+(z.index+1)+'</b><div>relay '+(z.relay?'open':'closed')+'</div><div>'+(z.needsWater?'needs water':'not requesting water')+'</div>';h+='<label class=\"field\">Sensor <select name=\"zoneSensor'+z.index+'\">';for(let i=0;i<4;i++)h+='<option value=\"'+i+'\" '+(z.sensor===i?'selected':'')+'>slot '+(i+1)+'</option>';h+='</select></label></div>';return h}"));
        client.println(F("function visualCells(cells){let order=[0,2,1,3];return cells.slice().sort((a,b)=>order.indexOf(a.index)-order.indexOf(b.index))}"));
        client.println(F("async function pipelineLog(){if(pipelineBusy||apiBusy)return;pipelineBusy=true;try{let r=await fetch('/api/pipeline_latest');let t=await r.text();pipeline.textContent=(t+'\\n'+pipeline.textContent).slice(0,16000)}catch(e){pipeline.textContent='Pipeline request failed: '+e+'\\n'+pipeline.textContent}finally{pipelineBusy=false}}async function pipelineHistory(){if(pipelineBusy||apiBusy)return;pipelineBusy=true;try{let r=await fetch('/api/pipeline_log');pipeline.textContent=await r.text()}catch(e){pipeline.textContent='Pipeline history failed: '+e}finally{pipelineBusy=false}}"));
        client.println(F("async function status(){if(statusBusy||apiBusy)return;statusBusy=true;try{let r=await fetch('/api/status');let s=await r.json();let vc=visualCells(s.cells);clock.textContent=s.time+(s.timeSynced?'':' (not synced)');wifi.textContent=(s.wifiConfigured?s.wifiSsid:'not configured')+' '+s.ip+' '+s.rssi+' dBm';if(!zoneSwitchInterval.value)zoneSwitchInterval.value=s.zoneSwitchIntervalSeconds;"));
        client.println(F("if(Date.now()>forcedBusyUntil)document.querySelectorAll('input[name=forcedZone]').forEach(x=>x.checked=Number(x.value)===s.activeIrrigationZone);if(Date.now()>zoneBusyUntil&&!irrigationZones.matches(':focus-within'))irrigationZones.innerHTML=visualCells(s.zones).map(zoneCard).join('');if(Date.now()>simulationBusyUntil&&!simulationControls.matches(':focus-within')){simulationEnabled.checked=s.simulationEnabled;simulationCells.innerHTML=vc.map(simCard).join('');document.querySelectorAll('#simulationCells input[type=range]').forEach(x=>x.oninput=()=>{document.getElementById('simn'+x.id.substring(3)).value=x.value});document.querySelectorAll('#simulationCells input[type=number]').forEach(x=>x.oninput=()=>{document.getElementById('sim'+x.id.substring(4)).value=x.value})}let state=!s.operationsStarted?'waiting for clock':(s.outOfMemory?'log full':'logging available');if(s.simulationEnabled)state+=' (simulation)';if(s.activeIrrigationZone>=0)state+=' (zone '+(s.activeIrrigationZone+1)+' open)';if(s.startupTimeWaitTimedOut)state+=' (time sync timed out)';sub.textContent='http://'+s.ip+' - '+state;if(Date.now()>sensorBusyUntil&&!sensors.matches(':focus-within'))sensors.innerHTML=vc.map(card).join('')}finally{statusBusy=false}}"));
        client.println(F("async function setThreshold(i){sensorBusyUntil=Date.now()+3000;api('/api/set_threshold?cell='+i+'&start='+esc(document.getElementById('st'+i).value)+'&stop='+esc(document.getElementById('sp'+i).value))}"));
        client.println(F("function inputValueOrPlaceholder(id){let e=document.getElementById(id);return e.value||e.placeholder}async function setCalibration(i){sensorBusyUntil=Date.now()+3000;api('/api/set_calibration?cell='+i+'&dry='+esc(inputValueOrPlaceholder('dry'+i))+'&wet='+esc(inputValueOrPlaceholder('wet'+i)))}"));
        client.println(F("function useCurrentDry(i,v){document.getElementById('dry'+i).value=v;setCalibration(i)}function useCurrentWet(i,v){document.getElementById('wet'+i).value=v;setCalibration(i)}"));
        client.println(F("async function setSensorSource(i,v){sensorBusyUntil=Date.now()+3000;api('/api/set_sensor_source?cell='+i+'&source='+esc(v))}"));
        client.println(F("async function setZoneSensor(i,v){zoneBusyUntil=Date.now()+3000;api('/api/set_zone_sensor?zone='+i+'&sensor='+esc(v))}"));
        client.println(F("async function applySimulation(){simulationBusyUntil=Date.now()+3000;let p='/api/set_simulation?enabled='+(simulationEnabled.checked?1:0);for(let i=0;i<4;i++)p+='&c'+i+'='+esc(document.getElementById('simn'+i).value);api(p)}"));
        client.println(F("function saveText(t){let stamp=new Date().toISOString().replace(/[:.]/g,'-');let a=document.createElement('a');a.href=URL.createObjectURL(new Blob([t],{type:'text/plain'}));a.download='garden-pump-dump-'+stamp+'.txt';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000)}"));
        client.println(F("async function downloadDumpFile(){out.textContent='Preparing dump...';let r=await fetch('/api/dump_no_erase');let t=await r.text();out.textContent=t;saveText(t);status()}async function clearMemory(){if(!confirm('Clear EEPROM log memory? This cannot be undone.'))return;api('/api/clear_log?confirm=yes')}async function downloadAndClear(){if(!confirm('Download the current dump and then clear EEPROM log memory?'))return;await downloadDumpFile();await clearMemory()}"));
        client.println(F("document.querySelectorAll('a.btn').forEach(a=>a.onclick=e=>{e.preventDefault();api(a.getAttribute('href'))});document.querySelectorAll('input[name=forcedZone]').forEach(x=>x.onchange=()=>{forcedBusyUntil=Date.now()+3000;api('/api/set_forced_irrigation?zone='+esc(x.value))});sensors.onchange=e=>{let n=e.target.name||'';if(n.startsWith('source'))setSensorSource(Number(n.substring(6)),e.target.value)};irrigationZones.onchange=e=>{let n=e.target.name||'';if(n.startsWith('zoneSensor'))setZoneSensor(Number(n.substring(10)),e.target.value)};saveSimulation.onclick=applySimulation;saveZoneSwitchInterval.onclick=()=>api('/api/set_zone_switch_interval?seconds='+esc(zoneSwitchInterval.value));downloadDump.onclick=downloadDumpFile;clearLog.onclick=clearMemory;downloadThenClear.onclick=downloadAndClear;refreshPipeline.onclick=pipelineHistory;status();pipelineLog();setInterval(status,500);setInterval(pipelineLog,1000)"));
        client.println(F("</script></main></body></html>"));
    }

    void sendJsonStatus(WiFiClient& client)
    {
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
        pinMode(analogPin, INPUT);
        int reading = 0;
        for (int idx = 0; idx < AnalogSettlingReadCount; ++idx)
        {
            reading = analogRead(analogPin);
            delayMicroseconds(AnalogSettlingDelayUs);
        }
        return analogRead(analogPin);
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

        enterIrrigationMode();
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
            out.print(F(" source="));
            printSensorInputMode(out, Cells[cellIdx].GetSensorInputMode());
            if (Cells[cellIdx].IsSimulated())
            {
                out.print(F("/sim"));
            }
            out.print(F(" addr=0x"));
            out.print(Cells[cellIdx].GetSensorAddress(), HEX);
            out.print(F(" analog="));
            printAnalogPinName(out, Cells[cellIdx].GetAnalogPin());
            out.print(F("("));
            out.print(Cells[cellIdx].GetAnalogPin());
            out.print(F(")"));
            out.print(F(" connected="));
            out.print((connectedMask(snapshot) & cellMask(cellIdx)) ? F("yes") : F("no"));
            out.print(F(" error="));
            out.print((errorMask(snapshot) & cellMask(cellIdx)) ? F("yes") : F("no"));
            out.print(F(" raw="));
            out.print(snapshot.raw[cellIdx]);
            out.print(F(" filtered="));
            out.print(Cells[cellIdx].GetFilteredCapacitanceReading());
            out.print(F(" mv="));
            out.print(Cells[cellIdx].GetLastVoltageMv());
            out.print(F(" vwc="));
            out.print(Cells[cellIdx].GetLastVwcPercent(), 1);
            out.print(F(" score="));
            out.print(Cells[cellIdx].GetVh400ConnectionScore());
            out.print(F(" spread="));
            out.print(Cells[cellIdx].GetLastReadSpread());
            out.print(F(" accepted="));
            out.print(Cells[cellIdx].GetAcceptedReadCount());
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
    analogReadResolution(10);
    loadConfig();
    applyI2cClock();
    RTC.begin();
    LedMatrix.begin();

    Cells[0].Initialize(0, 0, 0, 0x36, AnalogPins[0]);
    Cells[1].Initialize(0, 5, 1, 0x37, AnalogPins[1]);
    Cells[2].Initialize(7, 0, 2, 0x38, AnalogPins[2]);
    Cells[3].Initialize(7, 5, 3, 0x39, AnalogPins[3]);
    applyConfig();

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
    updatePipelineHistory();

    if (!OperationsStarted)
    {
        startOperationsIfReady();
        delay(UpdateDelayMs);
        return;
    }

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

    if (!DataGatheringActive)
    {
        const int updatedCell = CurrentCell;
        LedMatrix.beginDraw();
        Cells[updatedCell].RefreshSensor();
        Cells[updatedCell].UpdateWateringState();
        if (updatedCell < DiagnosticCells)
        {
            DataState.latest[updatedCell] = Cells[updatedCell].GetMoistureByte();
        }
        updateIrrigationScheduler();
        const int sensorIdx = Config.zoneSensor[updatedCell] < NrCells ? Config.zoneSensor[updatedCell] : updatedCell;
        Cells[updatedCell].RenderFrom(LedMatrix, Cells[sensorIdx]);
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
    if (CurrentCell < DiagnosticCells)
    {
        DataState.latest[CurrentCell] = Cells[CurrentCell].GetMoistureByte();
    }
    CurrentCell = (CurrentCell + 1) % NrCells;

    if ((millis() - DataState.lastWriteMs) >= logIntervalMs())
    {
        SensorSnapshot snapshot;
        snapshotCachedCells(snapshot);
        appendSample(snapshot);
    }

    LedMatrix.beginDraw();
    renderGatherModeDisplay();
    LedMatrix.endDraw();

    delay(UpdateDelayMs);
}
