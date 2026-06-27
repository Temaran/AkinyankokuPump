#include "FirmwareServices.h"

#include <EEPROM.h>

namespace GardenPump
{
    int logEndAddress()
    {
        return EEPROM.length();
    }

    int logStartAddress()
    {
        return LogStartAddress;
    }

    uint32_t readU32(int address)
    {
        uint32_t value = 0;
        EEPROM.get(address, value);
        return value;
    }

    void writeU32(int address, uint32_t value)
    {
        EEPROM.put(address, value);
    }

    void writeBytes(int address, const uint8_t* data, int count)
    {
        for (int idx = 0; idx < count; ++idx)
        {
            EEPROM.update(address + idx, data[idx]);
        }
    }

    uint16_t readU16(int address)
    {
        uint16_t value = 0;
        EEPROM.get(address, value);
        return value;
    }

    void writeU16(int address, uint16_t value)
    {
        EEPROM.put(address, value);
    }

    bool hasRoomForBytes(int byteCount)
    {
        return (DataState.nextLogAddress + byteCount) <= logEndAddress();
    }

    void updateStatsFromSample(const uint8_t sample[NrCells])
    {
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            DataState.latest[cellIdx] = sample[cellIdx];
            DataState.minValues[cellIdx] = min(DataState.minValues[cellIdx], sample[cellIdx]);
            DataState.maxValues[cellIdx] = max(DataState.maxValues[cellIdx], sample[cellIdx]);
        }
    }

    uint8_t cellMask(int cellIdx)
    {
        return static_cast<uint8_t>(1U << cellIdx);
    }

    uint8_t connectedMask(const SensorSnapshot& snapshot)
    {
        return snapshot.statusMask & 0x0F;
    }

    uint8_t errorMask(const SensorSnapshot& snapshot)
    {
        return (snapshot.statusMask >> 4) & 0x0F;
    }

    void sampleAllCells(SensorSnapshot& outSnapshot)
    {
        outSnapshot = SensorSnapshot{};
        for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
        {
            Cells[cellIdx].RefreshSensor();
            Cells[cellIdx].ForceDefaultSolenoidState();
            outSnapshot.moisture[cellIdx] = Cells[cellIdx].GetMoistureByte();

            const int raw = Cells[cellIdx].GetLastCapacitanceReading();
            outSnapshot.raw[cellIdx] = static_cast<uint16_t>(constrain(raw, 0, 65535));
            if (Cells[cellIdx].HasConnected())
            {
                outSnapshot.statusMask |= cellMask(cellIdx);
            }
            if (Cells[cellIdx].HasError())
            {
                outSnapshot.statusMask |= static_cast<uint8_t>(cellMask(cellIdx) << 4);
            }
        }
    }

    void snapshotCachedCells(SensorSnapshot& outSnapshot)
    {
        outSnapshot = SensorSnapshot{};
        for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
        {
            outSnapshot.moisture[cellIdx] = Cells[cellIdx].GetMoistureByte();
            const int raw = Cells[cellIdx].GetLastCapacitanceReading();
            outSnapshot.raw[cellIdx] = static_cast<uint16_t>(constrain(raw, 0, 65535));
            if (Cells[cellIdx].HasConnected())
            {
                outSnapshot.statusMask |= cellMask(cellIdx);
            }
            if (Cells[cellIdx].HasError())
            {
                outSnapshot.statusMask |= static_cast<uint8_t>(cellMask(cellIdx) << 4);
            }
        }
    }

    void sampleAllCells(uint8_t outSample[NrCells])
    {
        SensorSnapshot snapshot;
        sampleAllCells(snapshot);
        for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
        {
            outSample[cellIdx] = snapshot.moisture[cellIdx];
        }
        for (int cellIdx = DiagnosticCells; cellIdx < NrCells; ++cellIdx)
        {
            outSample[cellIdx] = 0;
        }
    }

    void writeDiagnosticSample(int address, const SensorSnapshot& snapshot)
    {
        writeBytes(address, snapshot.moisture, DiagnosticCells);
        for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
        {
            writeU16(address + DiagnosticCells + (cellIdx * 2), snapshot.raw[cellIdx]);
        }
        EEPROM.update(address + DiagnosticCells + (DiagnosticCells * 2), snapshot.statusMask);
    }

    void readDiagnosticSample(int address, SensorSnapshot& snapshot)
    {
        snapshot = SensorSnapshot{};
        EEPROM.get(address, snapshot.moisture);
        for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
        {
            snapshot.raw[cellIdx] = readU16(address + DiagnosticCells + (cellIdx * 2));
        }
        snapshot.statusMask = EEPROM.read(address + DiagnosticCells + (DiagnosticCells * 2));
    }

    void appendSample(const SensorSnapshot& snapshot)
    {
        if (!hasRoomForBytes(DiagnosticSampleBytes))
        {
            DataState.outOfMemory = true;
            return;
        }

        writeDiagnosticSample(DataState.nextLogAddress, snapshot);
        DataState.nextLogAddress += DiagnosticSampleBytes;
        DataState.totalSamples++;
        uint8_t statSample[NrCells] = {};
        for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
        {
            statSample[cellIdx] = snapshot.moisture[cellIdx];
        }
        updateStatsFromSample(statSample);
        DataState.lastWriteMs = millis();

        Serial.print(F("Logged sample #"));
        Serial.print(DataState.totalSamples);
        Serial.print(F(" : "));
        for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
        {
            if (cellIdx > 0)
            {
                Serial.print(F(", "));
            }
            Serial.print(snapshot.moisture[cellIdx]);
        }
        Serial.println();
        printSensorDiagnostics(snapshot);
    }

    void startNewGatheringSession()
    {
        if (!hasRoomForBytes(12))
        {
            DataState.outOfMemory = true;
            return;
        }

        writeU32(DataState.nextLogAddress, SessionMarkerV2);
        writeU32(DataState.nextLogAddress + 4, getCurrentTimestamp());
        writeU32(DataState.nextLogAddress + 8, Config.logIntervalSeconds);
        DataState.nextLogAddress += 12;
        DataState.lastWriteMs = millis();
    }

    void scanExistingLog()
    {
        DataState = GatherState{};
        DataState.nextLogAddress = logStartAddress();
        int sampleBytes = LegacySampleBytes;

        while ((DataState.nextLogAddress + 4) <= logEndAddress())
        {
            const uint32_t word = readU32(DataState.nextLogAddress);
            if (word == EmptyWord)
            {
                break;
            }

            if (word == SessionMarkerV1 || word == SessionMarkerV2)
            {
                if ((DataState.nextLogAddress + 12) > logEndAddress())
                {
                    break;
                }
                sampleBytes = (word == SessionMarkerV2) ? DiagnosticSampleBytes : LegacySampleBytes;
                DataState.nextLogAddress += 12;
                continue;
            }

            if (!hasRoomForBytes(sampleBytes))
            {
                break;
            }

            if (sampleBytes == DiagnosticSampleBytes)
            {
                SensorSnapshot snapshot;
                readDiagnosticSample(DataState.nextLogAddress, snapshot);
                uint8_t sample[NrCells] = {};
                for (int cellIdx = 0; cellIdx < DiagnosticCells; ++cellIdx)
                {
                    sample[cellIdx] = snapshot.moisture[cellIdx];
                }
                updateStatsFromSample(sample);
            }
            else
            {
                uint8_t sample[NrCells] = {};
                EEPROM.get(DataState.nextLogAddress, sample);
                updateStatsFromSample(sample);
            }
            DataState.totalSamples++;
            DataState.nextLogAddress += sampleBytes;
        }

        DataState.initialized = true;
        DataState.outOfMemory = !hasRoomForBytes(12 + DiagnosticSampleBytes);
        DataState.lastWriteMs = millis();
        DataState.lastStatsScreenSwapMs = millis();
    }

    void beginGatherMode()
    {
        ForcedIrrigationZone = -1;
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            Cells[cellIdx].ForceDefaultSolenoidState();
        }

        scanExistingLog();
        if (!DataState.outOfMemory)
        {
            startNewGatheringSession();
        }

        Serial.println(F("Entered data gathering mode."));
        Serial.print(F("Next log address: "));
        Serial.println(DataState.nextLogAddress);
        Serial.print(F("Existing total sample count: "));
        Serial.println(DataState.totalSamples);
    }

    void enterIrrigationMode()
    {
        DataGatheringActive = false;
        invalidateIrrigationDisplayAnimation();
        Serial.println(F("Entered irrigation mode."));
    }

    void enterGatherMode()
    {
        DataGatheringActive = true;
        beginGatherMode();
    }

    bool setDataGatheringActive(bool active)
    {
        if (DataGatheringActive == active)
        {
            return true;
        }

        if (active)
        {
            enterGatherMode();
        }
        else
        {
            enterIrrigationMode();
        }

        return true;
    }

    void performDump(bool eraseAfterDump)
    {
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            Cells[cellIdx].ForceDefaultSolenoidState();
        }

        LedMatrix.beginDraw();
        renderDumpDisplay();
        LedMatrix.endDraw();

        Serial.println(eraseAfterDump
            ? F("Serial command DUMP received. Dumping EEPROM, erasing it, then resetting." )
            : F("Serial command DUMP_NO_ERASE received. Dumping EEPROM without erasing."));
        dumpLogToSerial();

        if (eraseAfterDump)
        {
            eraseEntireLog();
            Serial.println(F("EEPROM erased."));
            Serial.flush();
            NVIC_SystemReset();
            while (true) {}
        }
    }

    void eraseLogAndReset()
    {
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            Cells[cellIdx].ForceDefaultSolenoidState();
        }

        eraseEntireLog();
        Serial.println(F("EEPROM erased."));
        Serial.flush();
        NVIC_SystemReset();
        while (true) {}
    }

    void eraseEntireLog()
    {
        for (int address = logStartAddress(); address < EEPROM.length(); ++address)
        {
            EEPROM.update(address, 0xFF);
        }
    }

    void dumpLogToSerial()
    {
        dumpLog(Serial);
    }

    void dumpLog(Print& out)
    {
        out.println(F("AKINYANKOKUPUMP_DUMP_BEGIN"));
        out.println(F("FORMAT 2"));

        int address = logStartAddress();
        int sampleBytes = LegacySampleBytes;
        while ((address + 4) <= logEndAddress())
        {
            const uint32_t word = readU32(address);
            if (word == EmptyWord)
            {
                break;
            }

            if (word == SessionMarkerV1 || word == SessionMarkerV2)
            {
                if ((address + 12) > logEndAddress())
                {
                    out.println(F("WARN TRUNCATED_SESSION_HEADER"));
                    break;
                }

                const uint32_t timestamp = readU32(address + 4);
                const uint32_t intervalSeconds = readU32(address + 8);
                sampleBytes = (word == SessionMarkerV2) ? DiagnosticSampleBytes : LegacySampleBytes;
                out.print(F("SESSION "));
                out.print(timestamp);
                out.print(' ');
                out.println(intervalSeconds);
                address += 12;
                continue;
            }

            if ((address + sampleBytes) > logEndAddress())
            {
                out.println(F("WARN TRUNCATED_SAMPLE"));
                break;
            }

            if (sampleBytes == DiagnosticSampleBytes)
            {
                SensorSnapshot snapshot;
                readDiagnosticSample(address, snapshot);
                out.print(F("SAMPLE2 "));
                out.print(snapshot.moisture[0]);
                out.print(' ');
                out.print(snapshot.moisture[1]);
                out.print(' ');
                out.print(snapshot.moisture[2]);
                out.print(' ');
                out.print(snapshot.raw[0]);
                out.print(' ');
                out.print(snapshot.raw[1]);
                out.print(' ');
                out.print(snapshot.raw[2]);
                out.print(' ');
                out.println(snapshot.statusMask);
            }
            else
            {
                uint8_t sample[NrCells] = {};
                EEPROM.get(address, sample);
                out.print(F("SAMPLE "));
                out.print(sample[0]);
                out.print(' ');
                out.print(sample[1]);
                out.print(' ');
                out.print(sample[2]);
                out.print(' ');
                out.println(sample[3]);
            }
            address += sampleBytes;
        }

        out.println(F("AKINYANKOKUPUMP_DUMP_END"));
        out.flush();
    }
}
