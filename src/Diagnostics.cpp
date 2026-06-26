#include "FirmwareServices.h"

namespace GardenPump
{
    void printSensorInputMode(Print& out, SensorInputMode mode)
    {
        if (mode == SensorInputMode::Vh400)
        {
            out.print(F("vh400"));
        }
        else if (mode == SensorInputMode::NotUsed)
        {
            out.print(F("unused"));
        }
        else
        {
            out.print(F("seesaw"));
        }
    }

    void printSensorDiagnostics(const SensorSnapshot& snapshot)
    {
        printSensorDiagnostics(Serial, snapshot);
    }

    void printSensorDiagnostics(Print& out, const SensorSnapshot& snapshot)
    {
        out.println(F("Sensor diagnostics:"));
        for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
        {
            out.print(F("  cell "));
            out.print(cellIdx);
            out.print(F(" source="));
            printSensorInputMode(out, Cells[cellIdx].GetSensorInputMode());
            if (Cells[cellIdx].IsSimulated())
            {
                out.print(F("/sim"));
            }
            out.print(F(" addr=0x"));
            out.print(Cells[cellIdx].GetSensorAddress(), HEX);
            out.print(F(" analog="));
            printAnalogPinName(out, Cells[cellIdx].GetAnalogPin());
            out.print(F("("));
            out.print(Cells[cellIdx].GetAnalogPin());
            out.print(F(")"));
            out.print(F(" connected="));
            out.print((connectedMask(snapshot) & cellMask(cellIdx)) ? F("yes") : F("no"));
            out.print(F(" error="));
            out.print((errorMask(snapshot) & cellMask(cellIdx)) ? F("yes") : F("no"));
            out.print(F(" raw="));
            out.print(snapshot.raw[cellIdx]);
            out.print(F(" filtered="));
            out.print(Cells[cellIdx].GetFilteredCapacitanceReading());
            out.print(F(" mv="));
            out.print(Cells[cellIdx].GetLastVoltageMv());
            out.print(F(" vwc="));
            out.print(Cells[cellIdx].GetLastVwcPercent(), 1);
            out.print(F(" score="));
            out.print(Cells[cellIdx].GetVh400ConnectionScore());
            out.print(F(" spread="));
            out.print(Cells[cellIdx].GetLastReadSpread());
            out.print(F(" accepted="));
            out.print(Cells[cellIdx].GetAcceptedReadCount());
            out.print(F(" norm="));
            out.print(Cells[cellIdx].GetMoistnessNorm(), 3);
            out.print(F(" byte="));
            out.println(snapshot.moisture[cellIdx]);
        }
    }

    void printSensorDiagnostics()
    {
        SensorSnapshot snapshot;
        sampleAllCells(snapshot);
        printSensorDiagnostics(snapshot);
    }

}
