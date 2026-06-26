#include "BaseSensor.h"

namespace BaseSensor
{
    bool IsValidCapacitanceReading(int reading)
    {
        return reading > 0 && reading <= MaxCapacitance;
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
        if (count < MinValidSensorReads)
        {
            return false;
        }

        int bestStart = 0;
        int bestSpread = readings[MinValidSensorReads - 1] - readings[0];
        for (int start = 1; (start + MinValidSensorReads) <= count; ++start)
        {
            const int candidateSpread = readings[start + MinValidSensorReads - 1] - readings[start];
            if (candidateSpread < bestSpread)
            {
                bestSpread = candidateSpread;
                bestStart = start;
            }
        }

        spread = bestSpread;
        median = readings[bestStart + (MinValidSensorReads / 2)];
        return bestSpread <= MaxStableReadSpread;
    }
}
