#pragma once

namespace VH400Sensor
{
    constexpr int ReadCount = 1;
    constexpr int ReadDelayMs = 0;
    constexpr int SettlingReadCount = 6;
    constexpr int SettlingDelayUs = 250;
    constexpr int MaxOutputMv = 3000;
    constexpr int ConnectionScoreMax = 12;
    constexpr int ConnectionThreshold = 8;
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

    void ConfigureInput(int analogPin);
    int ReadAnalogSettled(int analogPin);
    Reading Read(int analogPin);
}
