#include "VH400Sensor.h"

#include <Arduino.h>

#include "BaseSensor.h"
#include "GardenLogic.h"

namespace VH400Sensor
{
    void ConfigureInput(int analogPin)
    {
        pinMode(analogPin, INPUT);
    }

    int ReadAnalogSettled(int analogPin)
    {
        ConfigureInput(analogPin);
        int reading = 0;
        for (int idx = 0; idx < SettlingReadCount; ++idx)
        {
            reading = analogRead(analogPin);
            delayMicroseconds(SettlingDelayUs);
        }
        return analogRead(analogPin);
    }

    Reading Read(int analogPin)
    {
        Reading reading;
        int validReadings[ReadCount] = {};
        for (int idx = 0; idx < ReadCount; ++idx)
        {
            reading.lastReading = ReadAnalogSettled(analogPin);
            if (reading.debugReadCount < DebugReadSlots)
            {
                reading.debugReadings[reading.debugReadCount] = reading.lastReading;
                reading.debugReadCount++;
            }
            if (reading.lastReading >= 0 && reading.lastReading <= GardenLogic::AdcMaxReading)
            {
                validReadings[reading.validReadCount] = reading.lastReading;
                reading.validReadCount++;
            }
            delay(ReadDelayMs);
        }

        BaseSensor::SortReadings(validReadings, reading.validReadCount);
        reading.selectedReading = reading.lastReading;
        reading.stableCluster = BaseSensor::FindStableClusterMedian(
            validReadings,
            reading.validReadCount,
            reading.selectedReading,
            reading.spread);
        reading.acceptedReadCount = reading.validReadCount;
        return reading;
    }
}
