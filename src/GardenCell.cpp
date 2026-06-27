#include "GardenCell.h"

#include <string.h>

#include "BaseSensor.h"
#include "GardenLogic.h"
#include "SeesawSensor.h"
#include "Utils.h"
#include "VH400Sensor.h"

namespace
{
    constexpr int kMaxConsecutiveReadErrors = 10;
    constexpr int kFilterPreviousWeight = 3;
}

void GardenCell::Initialize(int ledMatrixStartX, int ledMatrixStartY, int solanoidAddress, int sensorAddress, int analogPin)
{
    _ledMatrixStartX = ledMatrixStartX;
    _ledMatrixStartY = ledMatrixStartY;
    _solanoidAddress = solanoidAddress;
    _sensorAddress = sensorAddress;
    _analogPin = analogPin;

    pinMode(_solanoidAddress, OUTPUT);
    digitalWrite(_solanoidAddress, LOW);
    VH400Sensor::ConfigureInput(_analogPin);
    SetSensorInputMode(_sensorInputMode);
}

void GardenCell::RefreshSensor()
{
    if (_sensorInputMode == SensorInputMode::NotUsed)
    {
        _hasConnected = false;
        _hasError = false;
        _shouldWater = false;
        _moistnessNorm = 0.0f;
        _lastCapacitanceReading = 0;
        _filteredCapacitanceReading = 0;
        _lastReadSpread = 0;
        _acceptedReadCount = 0;
        _lastVoltageMv = 0;
        _lastVwcPercent = 0.0f;
        _vh400ConnectionScore = 0;
        _debugReadCount = 0;
        _debugStableCluster = false;
        _debugSelectedReading = 0;
    }
    else if (_simulationEnabled)
    {
        RefreshSimulatedSensor();
    }
    else if (_sensorInputMode == SensorInputMode::Vh400)
    {
        RefreshVh400Sensor();
    }
    else
    {
        RefreshSeesawSensor();
    }

    _lastRefreshMs = millis();
}

void GardenCell::RefreshSeesawSensor()
{
    if (!_hasConnected)
    {
        _hasError = false;
        _moistnessNorm = 0.0f;
        _lastCapacitanceReading = 0;
        _filteredCapacitanceReading = 0;
        _lastReadSpread = 0;
        _acceptedReadCount = 0;
        _lastVoltageMv = 0;
        _lastVwcPercent = 0.0f;
        _consecutiveReadErrors = 0;
        _hasFilteredReading = false;
        return;
    }

    const SeesawSensor::Reading reading = SeesawSensor::Read(_soilSensor);
    memcpy(_debugReadings, reading.debugReadings, sizeof(_debugReadings));
    _debugReadCount = reading.debugReadCount;
    _debugStableCluster = reading.stableCluster;
    _debugSelectedReading = reading.selectedReading;
    _lastReadSpread = reading.spread;
    _acceptedReadCount = reading.acceptedReadCount;

    if (reading.validReadCount < BaseSensor::MinValidSensorReads)
    {
        _lastCapacitanceReading = reading.lastReading;
        _consecutiveReadErrors++;
        _hasError = _consecutiveReadErrors >= kMaxConsecutiveReadErrors;
        if (!_hasFilteredReading)
        {
            _moistnessNorm = 0.0f;
        }
        return;
    }

    if (!reading.stableCluster)
    {
        _lastCapacitanceReading = reading.selectedReading;
        _debugSelectedReading = _lastCapacitanceReading;
        _consecutiveReadErrors++;
        _hasError = !_hasFilteredReading && _consecutiveReadErrors >= kMaxConsecutiveReadErrors;
        return;
    }

    _lastCapacitanceReading = reading.selectedReading;
    _debugSelectedReading = reading.selectedReading;
    _consecutiveReadErrors = 0;
    _hasError = false;

    if (!_hasFilteredReading)
    {
        _filteredCapacitanceReading = reading.selectedReading;
        _hasFilteredReading = true;
    }
    else
    {
        _filteredCapacitanceReading =
            ((_filteredCapacitanceReading * kFilterPreviousWeight) + reading.selectedReading) / (kFilterPreviousWeight + 1);
    }

    const float capacitanceRange = static_cast<float>(_wetCalibrationRaw - _dryCalibrationRaw);
    const int capValue = _filteredCapacitanceReading - _dryCalibrationRaw;
    _moistnessNorm = Utils::Clamp(static_cast<float>(capValue) / capacitanceRange, 0.0f, 1.0f);
    _lastVoltageMv = 0;
    _lastVwcPercent = _moistnessNorm * 100.0f;
}

void GardenCell::RefreshVh400Sensor()
{
    const VH400Sensor::Reading reading = VH400Sensor::Read(_analogPin);
    memcpy(_debugReadings, reading.debugReadings, sizeof(_debugReadings));
    _debugReadCount = reading.debugReadCount;
    _debugStableCluster = reading.stableCluster;
    _debugSelectedReading = reading.selectedReading;
    _lastReadSpread = reading.spread;
    _acceptedReadCount = reading.acceptedReadCount;

    if (reading.validReadCount == 0)
    {
        _lastCapacitanceReading = reading.lastReading;
        _consecutiveReadErrors++;
        _hasConnected = false;
        _hasError = true;
        _moistnessNorm = 0.0f;
        _lastVwcPercent = 0.0f;
        _hasFilteredReading = false;
        _filteredCapacitanceReading = 0;
        _vh400ConnectionScore = 0;
        return;
    }

    _lastCapacitanceReading = reading.selectedReading;
    _debugSelectedReading = reading.selectedReading;
    _filteredCapacitanceReading = reading.selectedReading;
    _hasFilteredReading = true;
    _vh400ConnectionScore = reading.stableCluster ? VH400Sensor::ConnectionScoreMax : VH400Sensor::ConnectionThreshold;
    _consecutiveReadErrors = 0;

    _lastVoltageMv = constrain(GardenLogic::AnalogReadingToMillivolts(reading.selectedReading), 0, VH400Sensor::MaxOutputMv);
    _hasConnected = true;
    _hasError = false;
    const float voltage = static_cast<float>(_lastVoltageMv) / 1000.0f;
    _lastVwcPercent = Utils::Clamp(GardenLogic::Vh400VwcPercentFromVoltage(voltage), 0.0f, 100.0f);
    _moistnessNorm = Utils::Clamp(_lastVwcPercent / 100.0f, 0.0f, 1.0f);
}

void GardenCell::RefreshSimulatedSensor()
{
    _hasConnected = true;
    _hasError = false;
    _consecutiveReadErrors = 0;
    _hasFilteredReading = true;
    _lastReadSpread = 0;
    _acceptedReadCount = BaseSensor::MinValidSensorReads;
    _moistnessNorm = Utils::Clamp(static_cast<float>(_simulatedMoisturePercent) / 100.0f, 0.0f, 1.0f);
    _lastCapacitanceReading = static_cast<int>(_moistnessNorm * GardenLogic::MaxCalibrationRaw + 0.5f);
    _filteredCapacitanceReading = _lastCapacitanceReading;
    _lastVoltageMv = static_cast<int>(_moistnessNorm * VH400Sensor::MaxOutputMv + 0.5f);
    _lastVwcPercent = _moistnessNorm * 100.0f;
    _debugReadCount = 1;
    _debugReadings[0] = _lastCapacitanceReading;
    _debugStableCluster = true;
    _debugSelectedReading = _lastCapacitanceReading;
}

int GardenCell::ReadRawCapacitance()
{
    if (_simulationEnabled)
    {
        return _lastCapacitanceReading;
    }

    if (_sensorInputMode == SensorInputMode::NotUsed)
    {
        return 0;
    }

    if (_sensorInputMode == SensorInputMode::Vh400)
    {
        return VH400Sensor::ReadAnalogSettled(_analogPin);
    }

    if (!_hasConnected)
    {
        return 0;
    }

    return SeesawSensor::ReadRaw(_soilSensor);
}

uint32_t GardenCell::GetSensorVersion()
{
    if (_sensorInputMode == SensorInputMode::NotUsed || _sensorInputMode == SensorInputMode::Vh400 || _simulationEnabled || !_hasConnected)
    {
        return 0;
    }

    return SeesawSensor::GetVersion(_soilSensor);
}

float GardenCell::GetSensorTemperatureC()
{
    if (_sensorInputMode == SensorInputMode::NotUsed || _sensorInputMode == SensorInputMode::Vh400 || _simulationEnabled || !_hasConnected)
    {
        return 0.0f;
    }

    return SeesawSensor::GetTemperatureC(_soilSensor);
}

uint8_t GardenCell::GetMoistureByte() const
{
    if (!_hasConnected || _hasError)
    {
        return 0;
    }

    // Reserve 255 so 0xFFFFFFFF can mean "unwritten" in EEPROM.
    return static_cast<uint8_t>(GardenLogic::MoistureByteFromNorm(_moistnessNorm));
}

void GardenCell::SetWateringThresholds(float startNorm, float stopNorm)
{
    startNorm = Utils::Clamp(startNorm, 0.0f, 1.0f);
    stopNorm = Utils::Clamp(stopNorm, 0.0f, 1.0f);
    if (stopNorm < startNorm)
    {
        stopNorm = startNorm;
    }

    _startWateringThresholdNorm = startNorm;
    _stopWateringThresholdNorm = stopNorm;
}

void GardenCell::SetMoistureCalibration(int dryRaw, int wetRaw)
{
    dryRaw = constrain(dryRaw, 0, GardenLogic::MaxCalibrationRaw - 1);
    wetRaw = constrain(wetRaw, dryRaw + 1, GardenLogic::MaxCalibrationRaw);
    _dryCalibrationRaw = dryRaw;
    _wetCalibrationRaw = wetRaw;
}

void GardenCell::SetSensorInputMode(SensorInputMode mode)
{
    _sensorInputMode = mode;
    _hasFilteredReading = false;
    _consecutiveReadErrors = 0;
    _lastCapacitanceReading = 0;
    _filteredCapacitanceReading = 0;
    _lastReadSpread = 0;
    _acceptedReadCount = 0;
    _lastVoltageMv = 0;
    _lastVwcPercent = 0.0f;
    _vh400ConnectionScore = 0;
    _debugReadCount = 0;
    _debugStableCluster = false;
    _debugSelectedReading = 0;

    if (_sensorInputMode == SensorInputMode::NotUsed)
    {
        _hasConnected = false;
        _hasError = false;
        _shouldWater = false;
        VH400Sensor::ConfigureInput(_analogPin);
        SetSolenoidOutput(false);
        return;
    }

    if (_sensorInputMode == SensorInputMode::Vh400)
    {
        _hasConnected = false;
        _hasError = false;
        VH400Sensor::ConfigureInput(_analogPin);
        return;
    }

    _hasConnected = SeesawSensor::Begin(_soilSensor, _sensorAddress);
    _hasError = false;
}

void GardenCell::SetSimulatedMoisture(bool enabled, uint8_t moisturePercent)
{
    _simulationEnabled = enabled;
    _simulatedMoisturePercent = static_cast<uint8_t>(constrain(moisturePercent, 0, 100));
    if (_simulationEnabled && _sensorInputMode != SensorInputMode::NotUsed)
    {
        RefreshSimulatedSensor();
    }
}

void GardenCell::ForceDefaultSolenoidState()
{
    _shouldWater = false;
    SetSolenoidOutput(false);
}

void GardenCell::ForceSolenoidState(bool enabled)
{
    _shouldWater = enabled;
    SetSolenoidOutput(enabled);
}

void GardenCell::SetSolenoidOutput(bool enabled)
{
    _relayEnabled = enabled;
    digitalWrite(_solanoidAddress, enabled ? HIGH : LOW);
}

void GardenCell::UpdateWateringState()
{
    if (_hasConnected && !_hasError)
    {
        _shouldWater = GardenLogic::ShouldRequestWater(
            _moistnessNorm,
            _startWateringThresholdNorm,
            _stopWateringThresholdNorm,
            _shouldWater);
    }
    else
    {
        _shouldWater = false;
    }

    if (!_shouldWater)
    {
        SetSolenoidOutput(false);
    }
}

void GardenCell::RenderFrameToBuffer(
    uint8_t* pixels,
    int matrixWidth,
    int matrixHeight,
    const GardenCell& sourceCell,
    int animationFrame) const
{
    const auto writePixel = [&](int localX, int localY, bool on)
    {
        const int x = _ledMatrixStartX + localX;
        const int y = _ledMatrixStartY + localY;
        if (x >= 0 && x < matrixWidth && y >= 0 && y < matrixHeight)
        {
            pixels[y * matrixWidth + x] = on ? 1 : 0;
        }
    };

    for (int x = 0; x < _ledMatrixWidth; ++x)
    {
        for (int y = 0; y < _ledMatrixHeight; ++y)
        {
            writePixel(x, y, false);
        }
    }

    GardenFrame* sourceFrame = &GardenErrorIcon;
    if (!sourceCell._hasConnected)
    {
        sourceFrame = &GardenNotConnectedIcon;
    }
    else if (sourceCell._hasError)
    {
        sourceFrame = &GardenErrorIcon;
    }
    else if (sourceCell._shouldWater)
    {
        const int frame = constrain(animationFrame, 0, GardenWateringAnimLength - 1);
        sourceFrame = &GardenWateringAnim[frame];
    }
    else
    {
        sourceFrame = &GardenNotWateringIcon;
    }

    for (int x = 0; x < 3; ++x)
    {
        for (int y = 0; y < 3; ++y)
        {
            writePixel(x, y, (*sourceFrame)[x][y] > 0);
        }
    }

    if (sourceCell._hasConnected && !sourceCell._hasError)
    {
        const int moistureDots = GardenLogic::MoistureDotCount(
            sourceCell._moistnessNorm,
            sourceCell._startWateringThresholdNorm,
            sourceCell._stopWateringThresholdNorm);
        if (moistureDots >= 1)
        {
            writePixel(4, 2, true);
        }
        if (moistureDots >= 2)
        {
            writePixel(4, 1, true);
        }
        if (moistureDots >= 3)
        {
            writePixel(4, 0, true);
        }
    }
}
