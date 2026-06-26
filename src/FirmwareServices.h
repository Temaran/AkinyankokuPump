#pragma once

#include "FirmwareState.h"

namespace GardenPump
{
    int logEndAddress();
    int logStartAddress();

    void loadConfig();
    void saveConfig();
    void applyConfig();
    void applyI2cClock();
    uint8_t configChecksum(const PumpConfig& config);
    uint8_t configChecksumV6(const PumpConfigV6& config);
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
    bool hasWifiConfig();
    bool setWifiConfig(const String& ssid, const String& password);
    void clearWifiConfig();
    void printWifiConfigHelp(Print& out);
    bool hasCloudLogConfig();
    bool setCloudLogEndpoint(const String& endpoint);
    bool setCloudLogToken(const String& token);
    void clearCloudLogConfig();
    unsigned long logIntervalMs();

    bool setForcedIrrigationZone(int zone);
    bool isForcedIrrigationActive();
    bool zoneNeedsWater(int zoneIdx);
    int countWateringZonesNeeded();
    int nextWateringZoneAfter(int startZone);
    float estimatedWaterFlowMlPerSecond();
    float estimatedZoneWaterMl(int zoneIdx);
    void accountZoneWaterRuntime();
    void applyRelayOutputs(int activeZone);
    void updateIrrigationScheduler();

    void writePixel(int x, int y, bool on);
    void clearMatrix();
    void drawGlyph3x5(int startX, int startY, const uint8_t glyph[5]);
    void drawDigit3x5(int startX, int startY, int digit);
    void drawCountScreen(uint32_t count);
    void drawBarScreen(const uint8_t label[5], const uint8_t values[NrCells]);
    void drawText3x5(const uint8_t left[5], const uint8_t middle[5], const uint8_t right[5]);
    void renderGatherModeDisplay();
    void renderDumpDisplay();

    uint32_t readU32(int address);
    void writeU32(int address, uint32_t value);
    void writeBytes(int address, const uint8_t* data, int count);
    uint16_t readU16(int address);
    void writeU16(int address, uint16_t value);
    bool hasRoomForBytes(int byteCount);
    void updateStatsFromSample(const uint8_t sample[NrCells]);
    uint8_t cellMask(int cellIdx);
    uint8_t connectedMask(const SensorSnapshot& snapshot);
    uint8_t errorMask(const SensorSnapshot& snapshot);
    void sampleAllCells(SensorSnapshot& outSnapshot);
    void snapshotCachedCells(SensorSnapshot& outSnapshot);
    void sampleAllCells(uint8_t outSample[NrCells]);
    void writeDiagnosticSample(int address, const SensorSnapshot& snapshot);
    void readDiagnosticSample(int address, SensorSnapshot& snapshot);
    void appendSample(const SensorSnapshot& snapshot);
    void startNewGatheringSession();
    void scanExistingLog();
    void beginGatherMode();
    void enterIrrigationMode();
    void enterGatherMode();
    bool setDataGatheringActive(bool active);
    void performDump(bool eraseAfterDump);
    void eraseLogAndReset();
    void eraseEntireLog();
    void dumpLogToSerial();
    void dumpLog(Print& out);

    bool isLeapYear(int year);
    int daysInMonth(int year, int month);
    int dayOfWeek(int year, int month, int day);
    int lastSundayOfMonth(int year, int month);
    uint32_t unixTimeUtc(int year, int month, int day, int hour, int minute, int second);
    bool isEuropeStockholmDst(uint32_t utcUnixTime);
    uint32_t europeStockholmOffsetSeconds(uint32_t utcUnixTime);
    const __FlashStringHelper* europeStockholmTimeZoneName(uint32_t utcUnixTime);
    uint32_t getCurrentTimestamp();
    String currentTimeString();
    void connectWifiIfNeeded();
    void sendNtpPacket();
    bool syncTimeFromNtp();
    void printWifiStatus();
    void handleWifi();
    bool startupTimeWaitFinished();
    void startOperationsIfReady();

    uint8_t currentRelayMask();
    void captureCloudLogSnapshot(CloudLogSnapshot& snapshot);
    bool shouldSendCloudLog(const CloudLogSnapshot& snapshot);
    bool sendCloudLogNow(bool force);
    void updateCloudLogger();
    void printCloudLogStatus(Print& out);
    void sendCloudHistory(WiFiClient& client, const String& requestLine);

    void printModeHelp();
    void processSerialCommand(String command);
    void scanI2CBus();
    void scanI2CBus(Print& out);
    void handleSerialCommands();

    void sendHttpHeaders(WiFiClient& client, const char* contentType);
    void sendJsonStatus(WiFiClient& client);
    void sendTextDiagnostics(WiFiClient& client, bool force);
    void sendTextI2CScan(WiFiClient& client);
    void printRawSensorDiagnostics(Print& out);
    void sendRawSensorDiagnostics(WiFiClient& client);
    void sendAnalogDiagnostics(WiFiClient& client);
    void capturePipelineEntry(PipelineLogEntry& entry);
    void updatePipelineHistory();
    void printPipelineCell(Print& out, int cellIdx, const PipelineCellLog& cell);
    void printPipelineEntry(Print& out, const PipelineLogEntry& entry);
    void sendPipelineLog(WiFiClient& client);
    void sendPipelineLatest(WiFiClient& client);
    int queryIntValue(const String& requestLine, const char* key, int fallback);
    char hexNibble(char ch);
    String urlDecode(String value);
    String queryStringValue(const String& requestLine, const char* key);
    void printAnalogPinName(Print& out, int analogPin);
    int readAnalogSettled(int analogPin);
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
    void sendWifiUpdate(WiFiClient& client, const String& requestLine);
    void sendCloudLogEndpointUpdate(WiFiClient& client, const String& requestLine);
    void sendCloudLogTokenUpdate(WiFiClient& client, const String& requestLine);
    void sendTextDump(WiFiClient& client, bool eraseAfterDump);
    void sendNotFound(WiFiClient& client);
    void handleWebClient();

    void printSensorInputMode(Print& out, SensorInputMode mode);
    void printSensorDiagnostics(Print& out, const SensorSnapshot& snapshot);
    void printSensorDiagnostics();
    void printSensorDiagnostics(const SensorSnapshot& snapshot);
}
