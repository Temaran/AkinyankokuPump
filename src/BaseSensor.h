#pragma once

namespace BaseSensor
{
    constexpr int MaxCapacitance = 1023;
    constexpr int MinValidSensorReads = 3;
    constexpr int MaxStableReadSpread = 80;

    bool IsValidCapacitanceReading(int reading);
    void SortReadings(int* readings, int count);
    bool FindStableClusterMedian(const int* readings, int count, int& median, int& spread);
}
