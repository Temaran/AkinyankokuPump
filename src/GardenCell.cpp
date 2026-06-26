#include "GardenCell.h"
#include "GardenLogic.h"
#include "Utils.h"

namespace
{
    constexpr int kSensorReadCount = 5;
    constexpr int kMinValidSensorReads = 3;
    constexpr int kMaxConsecutiveReadErrors = 10;
    constexpr int kFilterPreviousWeight = 3;
    constexpr int kReadDelayMs = 1;
    constexpr int kMaxStableReadSpread = 80;
    constexpr int kVh400SensorReadCount = 1;
    constexpr int kVh400ReadDelayMs = 0;
    constexpr int kVh400SettlingReadCount = 6;
    constexpr int kVh400SettlingDelayUs = 250;
    constexpr int kVh400MaxOutputMv = 3000;
    constexpr int kVh400MaxConnectedMv = 3050;
    constexpr int kVh400ConnectionScoreMax = 12;
    constexpr int kVh400ConnectionThreshold = 8;
    constexpr int kVh400DisconnectionThreshold = 3;
    constexpr int kVh400GoodReadingScore = 2;
    constexpr int kVh400BadReadingScore = 1;

    bool IsValidCapacitanceReading(int reading)
    {
        return reading > 0 && reading <= GardenLogic::MaxCalibrationRaw;
    }

    void SortReadings(int* readings, int count)
    {
        for (int idx = 1; idx < count; ++idx)
        {
            const int value = readings[idx];
            int insertIdx = idx - 1;
            while (insertIdx >= 0 && readings[insertIdx] > value)
            {
                readings[insertIdx + 1] = readings[insertIdx];
                insertIdx--;
            }
            readings[insertIdx + 1] = value;
        }
    }

    bool FindStableClusterMedian(const int* readings, int count, int& median, int& spread)
    {
        if (count < kMinValidSensorReads)
        {
            return false;
        }

        int bestStart = 0;
        int bestSpread = readings[kMinValidSensorReads - 1] - readings[0];
        for (int start = 1; (start + kMinValidSensorReads) <= count; ++start)
        {
            const int candidateSpread = readings[start + kMinValidSensorReads - 1] - readings[start];
            if (candidateSpread < bestSpread)
            {
                bestSpread = candidateSpread;
                bestStart = start;
            }
        }

        spread = bestSpread;
        median = readings[bestStart + (kMinValidSensorReads / 2)];
        return bestSpread <= kMaxStableReadSpread;
    }

    int ReadAnalogSettled(int analogPin)
    {
        pinMode(analogPin, INPUT);
        int reading = 0;
        for (int idx = 0; idx < kVh400SettlingReadCount; ++idx)
        {
            reading = analogRead(analogPin);
            delayMicroseconds(kVh400SettlingDelayUs);
        }
        return analogRead(analogPin);
    }

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
    pinMode(_analogPin, INPUT);
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

    int validReadings[kSensorReadCount] = {};
    int validCount = 0;
    int lastReading = 0;
    _debugReadCount = 0;
    _debugStableCluster = false;
    _debugSelectedReading = 0;
    for (int idx = 0; idx < kSensorReadCount; ++idx)
    {
        lastReading = _soilSensor.touchRead(0);
        if (_debugReadCount < 8)
        {
            _debugReadings[_debugReadCount] = lastReading;
            _debugReadCount++;
        }
        if (IsValidCapacitanceReading(lastReading))
        {
            validReadings[validCount] = lastReading;
            validCount++;
        }
        delay(kReadDelayMs);
    }

    SortReadings(validReadings, validCount);
    int capacitanceReading = 0;
    int stableSpread = 0;
    const bool hasStableCluster = FindStableClusterMedian(validReadings, validCount, capacitanceReading, stableSpread);
    _debugStableCluster = hasStableCluster;
    _debugSelectedReading = hasStableCluster ? capacitanceReading : lastReading;
    _lastReadSpread = stableSpread;
    _acceptedReadCount = hasStableCluster ? kMinValidSensorReads : 0;

    if (validCount < kMinValidSensorReads)
    {
        _lastCapacitanceReading = lastReading;
        _consecutiveReadErrors++;
        _hasError = _consecutiveReadErrors >= kMaxConsecutiveReadErrors;
        if (!_hasFilteredReading)
        {
            _moistnessNorm = 0.0f;
        }
        return;
    }

    if (!hasStableCluster)
    {
        _lastCapacitanceReading = validReadings[validCount / 2];
        _debugSelectedReading = _lastCapacitanceReading;
        _consecutiveReadErrors++;
        _hasError = !_hasFilteredReading && _consecutiveReadErrors >= kMaxConsecutiveReadErrors;
        return;
    }

    _lastCapacitanceReading = capacitanceReading;
    _debugSelectedReading = capacitanceReading;
    _consecutiveReadErrors = 0;
    _hasError = false;

    if (!_hasFilteredReading)
    {
        _filteredCapacitanceReading = capacitanceReading;
        _hasFilteredReading = true;
    }
    else
    {
        _filteredCapacitanceReading =
            ((_filteredCapacitanceReading * kFilterPreviousWeight) + capacitanceReading) / (kFilterPreviousWeight + 1);
    }

    const float capacitanceRange = static_cast<float>(_wetCalibrationRaw - _dryCalibrationRaw);
    const int capValue = _filteredCapacitanceReading - _dryCalibrationRaw;
    _moistnessNorm = Utils::Clamp(static_cast<float>(capValue) / capacitanceRange, 0.0f, 1.0f);
    _lastVoltageMv = 0;
    _lastVwcPercent = _moistnessNorm * 100.0f;
}

void GardenCell::RefreshVh400Sensor()
{
    int validReadings[kVh400SensorReadCount] = {};
    int validCount = 0;
    int lastReading = 0;
    _debugReadCount = 0;
    _debugStableCluster = false;
    _debugSelectedReading = 0;
    for (int idx = 0; idx < kVh400SensorReadCount; ++idx)
    {
        lastReading = ReadAnalogSettled(_analogPin);
        if (_debugReadCount < 8)
        {
            _debugReadings[_debugReadCount] = lastReading;
            _debugReadCount++;
        }
        if (lastReading >= 0 && lastReading <= GardenLogic::AdcMaxReading)
        {
            validReadings[validCount] = lastReading;
            validCount++;
        }
        delay(kVh400ReadDelayMs);
    }

    SortReadings(validReadings, validCount);
    int analogReading = lastReading;
    int stableSpread = 0;
    const bool hasStableCluster = FindStableClusterMedian(validReadings, validCount, analogReading, stableSpread);
    _debugStableCluster = hasStableCluster;
    _debugSelectedReading = analogReading;
    _lastReadSpread = stableSpread;
    _acceptedReadCount = validCount;

    if (validCount == 0)
    {
        _lastCapacitanceReading = lastReading;
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

    _lastCapacitanceReading = analogReading;
    _debugSelectedReading = analogReading;
    _filteredCapacitanceReading = analogReading;
    _hasFilteredReading = true;
    _vh400ConnectionScore = hasStableCluster ? kVh400ConnectionScoreMax : kVh400ConnectionThreshold;
    _consecutiveReadErrors = 0;

    _lastVoltageMv = constrain(GardenLogic::AnalogReadingToMillivolts(analogReading), 0, kVh400MaxOutputMv);
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
    _acceptedReadCount = kMinValidSensorReads;
    _moistnessNorm = Utils::Clamp(static_cast<float>(_simulatedMoisturePercent) / 100.0f, 0.0f, 1.0f);
    _lastCapacitanceReading = static_cast<int>(_moistnessNorm * GardenLogic::MaxCalibrationRaw + 0.5f);
    _filteredCapacitanceReading = _lastCapacitanceReading;
    _lastVoltageMv = static_cast<int>(_moistnessNorm * kVh400MaxOutputMv + 0.5f);
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
        return ReadAnalogSettled(_analogPin);
    }

    if (!_hasConnected)
    {
        return 0;
    }

    return _soilSensor.touchRead(0);
}

uint32_t GardenCell::GetSensorVersion()
{
    if (_sensorInputMode == SensorInputMode::NotUsed || _sensorInputMode == SensorInputMode::Vh400 || _simulationEnabled || !_hasConnected)
    {
        return 0;
    }

    return _soilSensor.getVersion();
}

float GardenCell::GetSensorTemperatureC()
{
    if (_sensorInputMode == SensorInputMode::NotUsed || _sensorInputMode == SensorInputMode::Vh400 || _simulationEnabled || !_hasConnected)
    {
        return 0.0f;
    }

    return _soilSensor.getTemp();
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
        pinMode(_analogPin, INPUT);
        SetSolenoidOutput(false);
        return;
    }

    if (_sensorInputMode == SensorInputMode::Vh400)
    {
        _hasConnected = false;
        _hasError = false;
        pinMode(_analogPin, INPUT);
        return;
    }

    _hasConnected = _soilSensor.begin(_sensorAddress);
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

void GardenCell::Update(ArduinoLEDMatrix& ledMatrix)
{
    RefreshSensor();
    UpdateWateringState();
    Render(ledMatrix);
}

void GardenCell::UpdateWateringState()
{
    if (_hasConnected && !_hasError)
    {
        if (_shouldWater)
        {
            if (_moistnessNorm > _stopWateringThresholdNorm)
            {
                _shouldWater = false;
            }
        }
        else if (_moistnessNorm < _startWateringThresholdNorm)
        {
            _shouldWater = true;
        }
    }

    if (!_shouldWater || _hasError || !_hasConnected)
    {
        SetSolenoidOutput(false);
    }
}

void GardenCell::Render(ArduinoLEDMatrix& ledMatrix)
{
    RenderFrom(ledMatrix, *this);
}

void GardenCell::RenderFrom(ArduinoLEDMatrix& ledMatrix, const GardenCell& sourceCell)
{
    ClearGraphics(ledMatrix);
    GardenFrame* sourceFrame = &GardenErrorIcon;
    if (!sourceCell._hasConnected)
    {
        sourceFrame = &GardenNotConnectedIcon;
        _currentAnimFrame = 0;
    }
    else if (sourceCell._hasError)
    {
        sourceFrame = &GardenErrorIcon;
        _currentAnimFrame = 0;
    }
    else if (sourceCell._shouldWater)
    {
        sourceFrame = &GardenWateringAnim[_currentAnimFrame];
        _currentAnimFrame = (_currentAnimFrame + 1) % GardenWateringAnimLength;
    }
    else
    {
        sourceFrame = &GardenNotWateringIcon;
        _currentAnimFrame = 0;
    }
    WriteGardenFrame(ledMatrix, *sourceFrame);

    if (sourceCell._hasConnected && !sourceCell._hasError)
    {
        const int moistureDots = GardenLogic::MoistureDotCount(
            sourceCell._moistnessNorm,
            sourceCell._startWateringThresholdNorm,
            sourceCell._stopWateringThresholdNorm);
        if (moistureDots >= 1)
        {
            WritePixel(ledMatrix, 4, 2, true);
        }
        if (moistureDots >= 2)
        {
            WritePixel(ledMatrix, 4, 1, true);
        }
        if (moistureDots >= 3)
        {
            WritePixel(ledMatrix, 4, 0, true);
        }
    }
}

void GardenCell::ClearGraphics(ArduinoLEDMatrix& ledMatrix, bool clearState)
{
    for (int x = 0; x < _ledMatrixWidth; x++)
    {
        for (int y = 0; y < _ledMatrixHeight; y++)
        {
            WritePixel(ledMatrix, x, y, clearState);
        }
    }
}

void GardenCell::WriteGardenFrame(ArduinoLEDMatrix& ledMatrix, GardenFrame& frame)
{
    for (int x = 0; x < 3; x++)
    {
        for (int y = 0; y < 3; y++)
        {
            const int value = frame[x][y];
            WritePixel(ledMatrix, x, y, value > 0);
        }
    }
}

void GardenCell::WritePixel(ArduinoLEDMatrix& ledMatrix, int x, int y, bool newValue)
{
    const int value = newValue ? 255 : 0;
    ledMatrix.set(_ledMatrixStartX + x, _ledMatrixStartY + y, value, value, value);
}
