#pragma once

#include "Adafruit_seesaw.h"

namespace SeesawSensor
{
    constexpr int ReadCount = 5;
    constexpr int ReadDelayMs = 1;
    constexpr int DebugReadSlots = 8;

    struct Reading
    {
        int debugReadings[DebugReadSlots] = {};
        int debugReadCount = 0;
        bool stableCluster = false;
        int selectedReading = 0;
        int lastReading = 0;
        int spread = 0;
        int acceptedReadCount = 0;
        int validReadCount = 0;
    };

    bool Begin(Adafruit_seesaw& sensor, int address);
    Reading Read(Adafruit_seesaw& sensor);
    int ReadRaw(Adafruit_seesaw& sensor);
    uint32_t GetVersion(Adafruit_seesaw& sensor);
    float GetTemperatureC(Adafruit_seesaw& sensor);
}
