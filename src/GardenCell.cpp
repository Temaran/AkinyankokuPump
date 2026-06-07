#include "GardenCell.h"
#include "Utils.h"

namespace
{
    constexpr int kMaxCapacitance = 1023;
    constexpr int kBaseCapacitance = 324;
}

void GardenCell::Initialize(int ledMatrixStartX, int ledMatrixStartY, int solanoidAddress, int sensorAddress)
{
    _ledMatrixStartX = ledMatrixStartX;
    _ledMatrixStartY = ledMatrixStartY;
    _solanoidAddress = solanoidAddress;
    _sensorAddress = sensorAddress;

    pinMode(_solanoidAddress, OUTPUT);
    digitalWrite(_solanoidAddress, LOW);
    _hasConnected = _soilSensor.begin(_sensorAddress);
}

void GardenCell::RefreshSensor()
{
    if (!_hasConnected)
    {
        _hasError = false;
        _moistnessNorm = 0.0f;
        _lastCapacitanceReading = 0;
        return;
    }

    const float capacitanceRange = static_cast<float>(kMaxCapacitance - kBaseCapacitance);
    const int capacitanceReading = _soilSensor.touchRead(0);
    _lastCapacitanceReading = capacitanceReading;
    const int capValue = capacitanceReading - kBaseCapacitance;
    _moistnessNorm = Utils::Clamp(static_cast<float>(capValue) / capacitanceRange, 0.0f, 1.0f);
    _hasError = capacitanceReading <= 0 || capacitanceReading > kMaxCapacitance;
}

uint8_t GardenCell::GetMoistureByte() const
{
    if (!_hasConnected || _hasError)
    {
        return 0;
    }

    // Reserve 255 so 0xFFFFFFFF can mean "unwritten" in EEPROM.
    const int scaled = static_cast<int>(_moistnessNorm * 254.0f + 0.5f);
    return static_cast<uint8_t>(Utils::Clamp(static_cast<float>(scaled), 0.0f, 254.0f));
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

void GardenCell::ForceDefaultSolenoidState()
{
    _shouldWater = false;
    digitalWrite(_solanoidAddress, LOW);
}

void GardenCell::Update(ArduinoLEDMatrix& ledMatrix)
{
    ClearGraphics(ledMatrix);
    RefreshSensor();

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

    const PinStatus pinOutput = (_shouldWater && !_hasError && _hasConnected) ? HIGH : LOW;
    digitalWrite(_solanoidAddress, pinOutput);

    GardenFrame* sourceFrame = &GardenErrorIcon;
    if (!_hasConnected)
    {
        sourceFrame = &GardenNotConnectedIcon;
        _currentAnimFrame = 0;
    }
    else if (_hasError)
    {
        sourceFrame = &GardenErrorIcon;
        _currentAnimFrame = 0;
    }
    else if (_shouldWater)
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

    if (!_hasError)
    {
        if (_moistnessNorm > 0.3f)
        {
            WritePixel(ledMatrix, 4, 2, true);
        }
        if (_moistnessNorm > 0.6f)
        {
            WritePixel(ledMatrix, 4, 1, true);
        }
        if (_moistnessNorm > 0.9f)
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
