#include <Arduino.h>
#include <RTC.h>
#include <Wire.h>
#include <WiFiS3.h>
#include <WiFiUdp.h>
#include "Arduino_LED_Matrix.h"

#include "GardenCell.h"
#include "GardenLogic.h"
#include "FirmwareState.h"
#include "FirmwareServices.h"

namespace GardenPump
{
    const char WifiHostname[] = "garden-pump";
    const int AnalogPins[NrCells] = {A0, A1, A2, A3};

    GardenCell Cells[NrCells];
    ArduinoLEDMatrix LedMatrix;
    WiFiServer WebServer(HttpPort);
    WiFiUDP Udp;
    IPAddress NtpServer(162, 159, 200, 123);
    byte NtpPacket[NtpPacketSize];

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
    unsigned long ZoneWaterOpenMs[NrCells] = {0, 0, 0, 0};
    unsigned long LastWaterAccountingMs = 0;
    CloudLogSnapshot LastCloudLogSnapshot;
    unsigned long LastCloudLogEvaluateMs = 0;
    unsigned long LastCloudLogSuccessMs = 0;
    bool LastCloudLogOk = false;
    int LastCloudLogHttpStatus = 0;
    char LastCloudLogMessage[64] = "not sent";
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
}

using namespace GardenPump;

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
        Cells[updatedCell].RefreshSensor();
        Cells[updatedCell].UpdateWateringState();
        if (updatedCell < DiagnosticCells)
        {
            DataState.latest[updatedCell] = Cells[updatedCell].GetMoistureByte();
        }
        updateIrrigationScheduler();
        updateIrrigationDisplayAnimation();
        CurrentCell = (CurrentCell + 1) % NrCells;
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
