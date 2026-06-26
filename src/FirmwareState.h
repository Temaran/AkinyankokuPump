#pragma once

#include <Arduino.h>
#include <WiFiS3.h>
#include <WiFiUdp.h>
#include "Arduino_LED_Matrix.h"

#include "GardenCell.h"

namespace GardenPump
{
    constexpr int NrCells = 4;
    constexpr int DiagnosticCells = 3;
    constexpr uint32_t ConfigMarker = 0x47434647; // GCFG
    constexpr uint8_t ConfigVersion = 7;
    constexpr uint16_t ConfigStartAddress = 0;
    constexpr uint16_t LogStartAddress = 512;
    constexpr int WifiSsidMaxLength = 32;
    constexpr int WifiPasswordMaxLength = 64;
    constexpr int CloudLogEndpointMaxLength = 191;
    constexpr int CloudLogTokenMaxLength = 64;
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
    constexpr unsigned long WebClientFirstByteTimeoutMs = 1000;
    constexpr unsigned long WebClientLineReadTimeoutMs = 50;
    constexpr unsigned long NtpRetryMs = 300000;
    constexpr unsigned long NtpStartupRetryMs = 10000;
    constexpr unsigned long StartupTimeSyncTimeoutMs = 60000;
    constexpr uint16_t HttpPort = 80;
    constexpr uint16_t NtpLocalPort = 2390;
    constexpr int NtpPacketSize = 48;
    constexpr int DebugAdcMaxReading = 1023;
    constexpr int DebugAnalogReferenceMv = 5000;
    constexpr unsigned long PipelineLogIntervalMs = 1000;
    constexpr int PipelineLogEntries = 12;
    constexpr int PipelineLogReadSlots = 5;
    constexpr float PipeInsideDiameterMm = 4.2f;
    constexpr float WaterEstimateVelocityMps = 1.0f;
    constexpr unsigned long CloudLogEvaluateIntervalMs = 60UL * 1000UL;
    constexpr unsigned long CloudLogHeartbeatMs = 10UL * 60UL * 1000UL;
    constexpr float CloudLogMoistureDeltaPercent = 1.0f;
    constexpr float CloudLogWaterDeltaMl = 100.0f;
    constexpr unsigned long CloudLogHttpTimeoutMs = 10000UL;

    extern const char WifiHostname[];
    extern const int AnalogPins[NrCells];

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
        char cloudLogEndpoint[CloudLogEndpointMaxLength + 1] = {};
        char cloudLogToken[CloudLogTokenMaxLength + 1] = {};
        uint8_t checksum = 0;
    };

    struct PumpConfigV6
    {
        uint32_t marker = ConfigMarker;
        uint8_t version = 6;
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

    struct CloudLogSnapshot
    {
        bool valid = false;
        uint8_t relayMask = 0;
        float moisturePercent[NrCells] = {0, 0, 0, 0};
        float waterMl[NrCells] = {0, 0, 0, 0};
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

    extern GardenCell Cells[NrCells];
    extern ArduinoLEDMatrix LedMatrix;
    extern WiFiServer WebServer;
    extern WiFiUDP Udp;
    extern IPAddress NtpServer;
    extern byte NtpPacket[NtpPacketSize];
    extern GatherState DataState;
    extern PumpConfig Config;
    extern PipelineLogEntry PipelineHistory[PipelineLogEntries];
    extern int PipelineHistoryNext;
    extern int PipelineHistoryCount;
    extern unsigned long LastPipelineLogMs;
    extern int CurrentCell;
    extern int ForcedIrrigationZone;
    extern int ActiveIrrigationZone;
    extern unsigned long LastZoneSwitchMs;
    extern unsigned long ZoneWaterOpenMs[NrCells];
    extern unsigned long LastWaterAccountingMs;
    extern CloudLogSnapshot LastCloudLogSnapshot;
    extern unsigned long LastCloudLogEvaluateMs;
    extern unsigned long LastCloudLogSuccessMs;
    extern bool LastCloudLogOk;
    extern int LastCloudLogHttpStatus;
    extern char LastCloudLogMessage[64];
    extern bool SimulationEnabled;
    extern uint8_t SimulatedMoisturePercent[NrCells];
    extern bool DataGatheringActive;
    extern unsigned long IntroAnimEndsAtMs;
    extern bool IntroFinished;
    extern String SerialCommandBuffer;
    extern int WifiStatus;
    extern bool WebServerStarted;
    extern bool UdpStarted;
    extern bool TimeSynced;
    extern bool OperationsStarted;
    extern bool StartupTimeWaitTimedOut;
    extern unsigned long LastWifiAttemptMs;
    extern unsigned long LastNtpAttemptMs;
    extern unsigned long StartupStartedAtMs;
}
