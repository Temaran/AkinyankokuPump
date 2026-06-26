#include "SeesawSensor.h"

#include <Arduino.h>

#include "BaseSensor.h"

namespace SeesawSensor
{
    bool Begin(Adafruit_seesaw& sensor, int address)
    {
        return sensor.begin(address);
    }

    Reading Read(Adafruit_seesaw& sensor)
    {
        Reading reading;
        int validReadings[ReadCount] = {};
        for (int idx = 0; idx < ReadCount; ++idx)
        {
            reading.lastReading = sensor.touchRead(0);
            if (reading.debugReadCount < DebugReadSlots)
            {
                reading.debugReadings[reading.debugReadCount] = reading.lastReading;
                reading.debugReadCount++;
            }
            if (BaseSensor::IsValidCapacitanceReading(reading.lastReading))
            {
                validReadings[reading.validReadCount] = reading.lastReading;
                reading.validReadCount++;
            }
            delay(ReadDelayMs);
        }

        BaseSensor::SortReadings(validReadings, reading.validReadCount);
        reading.stableCluster = BaseSensor::FindStableClusterMedian(
            validReadings,
            reading.validReadCount,
            reading.selectedReading,
            reading.spread);
        reading.acceptedReadCount = reading.stableCluster ? BaseSensor::MinValidSensorReads : 0;
        if (!reading.stableCluster)
        {
            reading.selectedReading = reading.validReadCount > 0
                ? validReadings[reading.validReadCount / 2]
                : reading.lastReading;
        }

        return reading;
    }

    int ReadRaw(Adafruit_seesaw& sensor)
    {
        return sensor.touchRead(0);
    }

    uint32_t GetVersion(Adafruit_seesaw& sensor)
    {
        return sensor.getVersion();
    }

    float GetTemperatureC(Adafruit_seesaw& sensor)
    {
        return sensor.getTemp();
    }
}
