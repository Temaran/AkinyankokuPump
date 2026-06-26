#pragma once

#include <stdint.h>

namespace GardenLogic
{
    constexpr int CellCount = 4;
    constexpr int MaxCalibrationRaw = 1023;
    constexpr int AdcMaxReading = 1023;
    constexpr int AnalogReferenceMv = 5000;

    inline float ClampFloat(float value, float low, float high)
    {
        if (value < low)
        {
            return low;
        }
        if (value > high)
        {
            return high;
        }
        return value;
    }

    inline int ClampInt(int value, int low, int high)
    {
        if (value < low)
        {
            return low;
        }
        if (value > high)
        {
            return high;
        }
        return value;
    }

    inline bool IsValidCellIndex(int index)
    {
        return index >= 0 && index < CellCount;
    }

    inline bool IsValidThresholdConfig(int startPercent, int stopPercent)
    {
        return startPercent >= 0
            && startPercent <= 100
            && stopPercent >= 0
            && stopPercent <= 100
            && stopPercent >= startPercent;
    }

    inline bool IsValidCalibrationConfig(int dryRaw, int wetRaw)
    {
        return dryRaw >= 0 && dryRaw < wetRaw && wetRaw <= MaxCalibrationRaw;
    }

    inline bool IsValidZoneSensor(int zoneIdx, int sensorIdx)
    {
        return IsValidCellIndex(zoneIdx) && IsValidCellIndex(sensorIdx);
    }

    inline int CoerceZoneSensor(int zoneIdx, int sensorIdx)
    {
        return IsValidZoneSensor(zoneIdx, sensorIdx) ? sensorIdx : zoneIdx;
    }

    inline int MoistureByteFromNorm(float moistureNorm)
    {
        return ClampInt(static_cast<int>(moistureNorm * 254.0f + 0.5f), 0, 254);
    }

    inline int MoistureDotCount(float moistureNorm, float startThresholdNorm, float stopThresholdNorm)
    {
        if (moistureNorm < startThresholdNorm)
        {
            return 0;
        }
        if (moistureNorm >= stopThresholdNorm)
        {
            return 3;
        }
        if (stopThresholdNorm <= startThresholdNorm)
        {
            return 1;
        }

        const float midpointNorm = startThresholdNorm + ((stopThresholdNorm - startThresholdNorm) * 0.5f);
        return moistureNorm < midpointNorm ? 1 : 2;
    }

    inline bool ShouldRequestWater(float moistureNorm, float startThresholdNorm, float stopThresholdNorm, bool wasRequestingWater)
    {
        if (wasRequestingWater)
        {
            return moistureNorm < stopThresholdNorm;
        }
        return moistureNorm < startThresholdNorm;
    }

    inline float Vh400VwcPercentFromVoltage(float voltage)
    {
        if (voltage <= 1.1f)
        {
            return (10.0f * voltage) - 1.0f;
        }
        if (voltage <= 1.3f)
        {
            return (25.0f * voltage) - 17.5f;
        }
        if (voltage <= 1.82f)
        {
            return (48.08f * voltage) - 47.5f;
        }
        if (voltage <= 2.2f)
        {
            return (26.32f * voltage) - 7.89f;
        }
        return (62.5f * voltage) - 87.5f;
    }

    inline int AnalogReadingToMillivolts(int reading)
    {
        return static_cast<int>(static_cast<long>(reading) * AnalogReferenceMv / AdcMaxReading);
    }

    inline int CountWateringZonesNeeded(const bool* zoneNeedsWater, int zoneCount)
    {
        int count = 0;
        for (int zoneIdx = 0; zoneIdx < zoneCount; ++zoneIdx)
        {
            if (zoneNeedsWater[zoneIdx])
            {
                ++count;
            }
        }
        return count;
    }

    inline int NextWateringZoneAfter(int startZone, const bool* zoneNeedsWater, int zoneCount)
    {
        for (int offset = 1; offset <= zoneCount; ++offset)
        {
            const int zoneIdx = (startZone + offset + zoneCount) % zoneCount;
            if (zoneNeedsWater[zoneIdx])
            {
                return zoneIdx;
            }
        }
        return -1;
    }

    inline bool IsLeapYear(int year)
    {
        return ((year % 4) == 0 && (year % 100) != 0) || (year % 400) == 0;
    }

    inline int DaysInMonth(int year, int month)
    {
        static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month == 2 && IsLeapYear(year))
        {
            return 29;
        }
        return days[month - 1];
    }

    inline int DayOfWeek(int year, int month, int day)
    {
        static const int offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
        if (month < 3)
        {
            --year;
        }
        return (year + year / 4 - year / 100 + year / 400 + offsets[month - 1] + day) % 7;
    }

    inline int LastSundayOfMonth(int year, int month)
    {
        int day = DaysInMonth(year, month);
        while (DayOfWeek(year, month, day) != 0)
        {
            --day;
        }
        return day;
    }

    inline uint32_t UnixTimeUtc(int year, int month, int day, int hour, int minute, int second)
    {
        uint32_t days = 0;
        for (int y = 1970; y < year; ++y)
        {
            days += IsLeapYear(y) ? 366UL : 365UL;
        }
        for (int m = 1; m < month; ++m)
        {
            days += DaysInMonth(year, m);
        }
        days += static_cast<uint32_t>(day - 1);
        return (((days * 24UL) + static_cast<uint32_t>(hour)) * 60UL + static_cast<uint32_t>(minute)) * 60UL
            + static_cast<uint32_t>(second);
    }

    inline bool IsEuropeStockholmDst(int year, uint32_t utcUnixTime)
    {
        const uint32_t dstStart = UnixTimeUtc(year, 3, LastSundayOfMonth(year, 3), 1, 0, 0);
        const uint32_t dstEnd = UnixTimeUtc(year, 10, LastSundayOfMonth(year, 10), 1, 0, 0);
        return utcUnixTime >= dstStart && utcUnixTime < dstEnd;
    }
}
