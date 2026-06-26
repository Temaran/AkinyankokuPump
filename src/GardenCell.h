#pragma once

#include <Arduino.h>
#include "Adafruit_seesaw.h"
#include "Arduino_LED_Matrix.h"

#include "GardenGraphics.h"

enum class SensorInputMode : uint8_t
{
    Seesaw = 0,
    Vh400 = 1,
    NotUsed = 2,
};

class GardenCell
{
public:
    void Initialize(int ledMatrixStartX, int ledMatrixStartY, int solanoidAddress, int sensorAddress, int analogPin);
    void Update(ArduinoLEDMatrix& ledMatrix);
    void RefreshSensor();
    void UpdateWateringState();
    void Render(ArduinoLEDMatrix& ledMatrix);
    void RenderFrom(ArduinoLEDMatrix& ledMatrix, const GardenCell& sourceCell);
    void ForceDefaultSolenoidState();
    void ForceSolenoidState(bool enabled);
    void SetSolenoidOutput(bool enabled);
    void SetSensorInputMode(SensorInputMode mode);
    SensorInputMode GetSensorInputMode() const { return _sensorInputMode; }
    void SetSimulatedMoisture(bool enabled, uint8_t moisturePercent);
    bool IsSimulated() const { return _simulationEnabled; }
    uint8_t GetSimulatedMoisturePercent() const { return _simulatedMoisturePercent; }

    bool HasError() const { return _hasError; }
    bool HasConnected() const { return _hasConnected; }
    float GetMoistnessNorm() const { return _moistnessNorm; }
    int GetSensorAddress() const { return _sensorAddress; }
    int GetAnalogPin() const { return _analogPin; }
    int GetLastCapacitanceReading() const { return _lastCapacitanceReading; }
    int GetFilteredCapacitanceReading() const { return _filteredCapacitanceReading; }
    int GetLastReadSpread() const { return _lastReadSpread; }
    int GetAcceptedReadCount() const { return _acceptedReadCount; }
    int GetLastVoltageMv() const { return _lastVoltageMv; }
    float GetLastVwcPercent() const { return _lastVwcPercent; }
    int GetVh400ConnectionScore() const { return _vh400ConnectionScore; }
    int GetDebugReadCount() const { return _debugReadCount; }
    int GetDebugReading(int index) const { return (index >= 0 && index < _debugReadCount) ? _debugReadings[index] : 0; }
    bool GetDebugStableCluster() const { return _debugStableCluster; }
    int GetDebugSelectedReading() const { return _debugSelectedReading; }
    unsigned long GetLastRefreshMs() const { return _lastRefreshMs; }
    int ReadRawCapacitance();
    uint32_t GetSensorVersion();
    float GetSensorTemperatureC();
    int GetSolenoidAddress() const { return _solanoidAddress; }
    bool ShouldWater() const { return _shouldWater; }
    bool IsRelayEnabled() const { return _relayEnabled; }
    float GetStartWateringThresholdNorm() const { return _startWateringThresholdNorm; }
    float GetStopWateringThresholdNorm() const { return _stopWateringThresholdNorm; }
    int GetDryCalibrationRaw() const { return _dryCalibrationRaw; }
    int GetWetCalibrationRaw() const { return _wetCalibrationRaw; }
    void SetWateringThresholds(float startNorm, float stopNorm);
    void SetMoistureCalibration(int dryRaw, int wetRaw);
    uint8_t GetMoistureByte() const;

private:
    // Basic info
    int _ledMatrixStartX = 0;
    int _ledMatrixStartY = 0;
    int _ledMatrixWidth = 6;
    int _ledMatrixHeight = 4;
    int _solanoidAddress = 0;
    int _sensorAddress = 0;
    int _analogPin = A0;

    // Sensor
    SensorInputMode _sensorInputMode = SensorInputMode::Seesaw;
    float _moistnessNorm = 0.0f;
    float _startWateringThresholdNorm = 0.4f;
    float _stopWateringThresholdNorm = 0.6f;
    int _dryCalibrationRaw = 324;
    int _wetCalibrationRaw = 1023;
    bool _hasConnected = false;
    bool _hasError = false;
    bool _shouldWater = false;
    bool _relayEnabled = false;
    int _lastCapacitanceReading = 0;
    int _filteredCapacitanceReading = 0;
    int _lastReadSpread = 0;
    int _acceptedReadCount = 0;
    int _lastVoltageMv = 0;
    float _lastVwcPercent = 0.0f;
    int _consecutiveReadErrors = 0;
    int _vh400ConnectionScore = 0;
    bool _hasFilteredReading = false;
    bool _simulationEnabled = false;
    uint8_t _simulatedMoisturePercent = 0;
    int _debugReadings[8] = {};
    int _debugReadCount = 0;
    bool _debugStableCluster = false;
    int _debugSelectedReading = 0;
    unsigned long _lastRefreshMs = 0;

    // Drawing
    int _currentAnimFrame = 0;

    Adafruit_seesaw _soilSensor;

    void ClearGraphics(ArduinoLEDMatrix& ledMatrix, bool clearState = false);
    void WriteGardenFrame(ArduinoLEDMatrix& ledMatrix, GardenFrame& frame);
    void WritePixel(ArduinoLEDMatrix& ledMatrix, int x, int y, bool newValue);
    void RefreshSeesawSensor();
    void RefreshVh400Sensor();
    void RefreshSimulatedSensor();
};
