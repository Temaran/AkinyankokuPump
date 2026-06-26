#include "FirmwareServices.h"

#include "GardenLogic.h"

namespace GardenPump
{
    float estimatedWaterFlowMlPerSecond()
    {
        return WaterEstimateFlowMlPerSecond;
    }

    float estimatedZoneWaterMl(int zoneIdx)
    {
        if (!GardenLogic::IsValidCellIndex(zoneIdx))
        {
            return 0.0f;
        }

        return (static_cast<float>(ZoneWaterOpenMs[zoneIdx]) / 1000.0f) * estimatedWaterFlowMlPerSecond();
    }

    void accountZoneWaterRuntime()
    {
        const unsigned long now = millis();
        if (LastWaterAccountingMs != 0)
        {
            const unsigned long elapsedMs = now - LastWaterAccountingMs;
            for (int zoneIdx = 0; zoneIdx < NrCells; ++zoneIdx)
            {
                if (Cells[zoneIdx].IsRelayEnabled())
                {
                    ZoneWaterOpenMs[zoneIdx] += elapsedMs;
                }
            }
        }
        LastWaterAccountingMs = now;
    }

    bool setForcedIrrigationZone(int zone)
    {
        if (zone < -1 || zone >= NrCells)
        {
            return false;
        }

        ForcedIrrigationZone = zone;
        ActiveIrrigationZone = zone;
        LastZoneSwitchMs = zone >= 0 ? millis() : 0;
        if (DataGatheringActive)
        {
            ForcedIrrigationZone = -1;
            ActiveIrrigationZone = -1;
            applyRelayOutputs(-1);
            return true;
        }

        applyRelayOutputs(ActiveIrrigationZone);
        return true;
    }

    bool isForcedIrrigationActive()
    {
        return !DataGatheringActive && ForcedIrrigationZone >= 0 && ForcedIrrigationZone < NrCells;
    }

    bool zoneNeedsWater(int zoneIdx)
    {
        if (!GardenLogic::IsValidCellIndex(zoneIdx))
        {
            return false;
        }

        const int sensorIdx = Config.zoneSensor[zoneIdx];
        if (!GardenLogic::IsValidCellIndex(sensorIdx))
        {
            return false;
        }

        return Cells[sensorIdx].ShouldWater();
    }

    int countWateringZonesNeeded()
    {
        bool needsWater[NrCells] = {};
        for (int zoneIdx = 0; zoneIdx < NrCells; ++zoneIdx)
        {
            needsWater[zoneIdx] = zoneNeedsWater(zoneIdx);
        }

        return GardenLogic::CountWateringZonesNeeded(needsWater, NrCells);
    }

    int nextWateringZoneAfter(int startZone)
    {
        bool needsWater[NrCells] = {};
        for (int zoneIdx = 0; zoneIdx < NrCells; ++zoneIdx)
        {
            needsWater[zoneIdx] = zoneNeedsWater(zoneIdx);
        }

        return GardenLogic::NextWateringZoneAfter(startZone, needsWater, NrCells);
    }

    void applyRelayOutputs(int activeZone)
    {
        accountZoneWaterRuntime();
        for (int zoneIdx = 0; zoneIdx < NrCells; ++zoneIdx)
        {
            Cells[zoneIdx].SetSolenoidOutput(zoneIdx == activeZone);
        }
    }

    void updateIrrigationScheduler()
    {
        if (DataGatheringActive)
        {
            ForcedIrrigationZone = -1;
            ActiveIrrigationZone = -1;
            applyRelayOutputs(-1);
            return;
        }

        const unsigned long now = millis();
        const unsigned long intervalMs = Config.zoneSwitchIntervalSeconds * 1000UL;
        if (LastZoneSwitchMs == 0)
        {
            LastZoneSwitchMs = now;
        }

        if (isForcedIrrigationActive())
        {
            ActiveIrrigationZone = ForcedIrrigationZone;
            if ((now - LastZoneSwitchMs) < intervalMs)
            {
                applyRelayOutputs(ActiveIrrigationZone);
                return;
            }

            ForcedIrrigationZone = -1;
        }

        const int neededCount = countWateringZonesNeeded();
        if (neededCount == 0)
        {
            ActiveIrrigationZone = -1;
            applyRelayOutputs(-1);
            return;
        }

        const bool activeStillNeedsWater = zoneNeedsWater(ActiveIrrigationZone);
        const bool switchDue = (now - LastZoneSwitchMs) >= intervalMs;
        if (!activeStillNeedsWater || (neededCount > 1 && switchDue))
        {
            ActiveIrrigationZone = nextWateringZoneAfter(ActiveIrrigationZone);
            LastZoneSwitchMs = now;
        }

        applyRelayOutputs(ActiveIrrigationZone);
    }
}
