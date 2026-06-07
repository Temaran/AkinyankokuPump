#pragma once

#include <Arduino.h>
#include "Adafruit_seesaw.h"
#include "Arduino_LED_Matrix.h"

#include "GardenGraphics.h"

class GardenCell
{
public:
    void Initialize(int ledMatrixStartX, int ledMatrixStartY, int solanoidAddress, int sensorAddress);
    void Update(ArduinoLEDMatrix& ledMatrix);
    void RefreshSensor();
    void ForceDefaultSolenoidState();

    bool HasError() const { return _hasError; }
    bool HasConnected() const { return _hasConnected; }
    float GetMoistnessNorm() const { return _moistnessNorm; }
    int GetSensorAddress() const { return _sensorAddress; }
    int GetLastCapacitanceReading() const { return _lastCapacitanceReading; }
    int GetSolenoidAddress() const { return _solanoidAddress; }
    bool ShouldWater() const { return _shouldWater; }
    float GetStartWateringThresholdNorm() const { return _startWateringThresholdNorm; }
    float GetStopWateringThresholdNorm() const { return _stopWateringThresholdNorm; }
    void SetWateringThresholds(float startNorm, float stopNorm);
    uint8_t GetMoistureByte() const;

private:
    // Basic info
    int _ledMatrixStartX = 0;
    int _ledMatrixStartY = 0;
    int _ledMatrixWidth = 6;
    int _ledMatrixHeight = 4;
    int _solanoidAddress = 0;
    int _sensorAddress = 0;

    // Sensor
    float _moistnessNorm = 0.0f;
    float _startWateringThresholdNorm = 0.4f;
    float _stopWateringThresholdNorm = 0.6f;
    bool _hasConnected = false;
    bool _hasError = false;
    bool _shouldWater = false;
    int _lastCapacitanceReading = 0;

    // Drawing
    int _currentAnimFrame = 0;

    Adafruit_seesaw _soilSensor;

    void ClearGraphics(ArduinoLEDMatrix& ledMatrix, bool clearState = false);
    void WriteGardenFrame(ArduinoLEDMatrix& ledMatrix, GardenFrame& frame);
    void WritePixel(ArduinoLEDMatrix& ledMatrix, int x, int y, bool newValue);
};
