#include "FirmwareServices.h"

#include <EEPROM.h>
#include <Wire.h>

#include "GardenLogic.h"

namespace GardenPump
{
    uint8_t configChecksum(const PumpConfig& config)
    {
        uint8_t sum = 0;
        sum ^= static_cast<uint8_t>(config.marker);
        sum ^= static_cast<uint8_t>(config.marker >> 8);
        sum ^= static_cast<uint8_t>(config.marker >> 16);
        sum ^= static_cast<uint8_t>(config.marker >> 24);
        sum ^= config.version;
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            sum ^= config.startThreshold[cellIdx];
            sum ^= static_cast<uint8_t>(config.stopThreshold[cellIdx] << 1);
        }
        for (int idx = 0; idx <= WifiSsidMaxLength; ++idx)
        {
            sum ^= static_cast<uint8_t>(config.wifiSsid[idx] + idx);
        }
        for (int idx = 0; idx <= WifiPasswordMaxLength; ++idx)
        {
            sum ^= static_cast<uint8_t>(config.wifiPassword[idx] + (idx * 3));
        }
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 8);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 16);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 24);
        sum ^= static_cast<uint8_t>(config.i2cClockHz);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 8);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 16);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 24);
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            sum ^= static_cast<uint8_t>(config.dryCalibrationRaw[cellIdx]);
            sum ^= static_cast<uint8_t>(config.dryCalibrationRaw[cellIdx] >> 8);
            sum ^= static_cast<uint8_t>(config.wetCalibrationRaw[cellIdx]);
            sum ^= static_cast<uint8_t>(config.wetCalibrationRaw[cellIdx] >> 8);
            sum ^= static_cast<uint8_t>(config.sensorInputMode[cellIdx] + (cellIdx * 7));
            sum ^= static_cast<uint8_t>(config.zoneSensor[cellIdx] + (cellIdx * 11));
        }
        sum ^= static_cast<uint8_t>(config.zoneSwitchIntervalSeconds);
        sum ^= static_cast<uint8_t>(config.zoneSwitchIntervalSeconds >> 8);
        sum ^= static_cast<uint8_t>(config.zoneSwitchIntervalSeconds >> 16);
        sum ^= static_cast<uint8_t>(config.zoneSwitchIntervalSeconds >> 24);
        for (int idx = 0; idx <= CloudLogEndpointMaxLength; ++idx)
        {
            sum ^= static_cast<uint8_t>(config.cloudLogEndpoint[idx] + (idx * 5));
        }
        for (int idx = 0; idx <= CloudLogTokenMaxLength; ++idx)
        {
            sum ^= static_cast<uint8_t>(config.cloudLogToken[idx] + (idx * 9));
        }
        return sum;
    }

    uint8_t configChecksumV6(const PumpConfigV6& config)
    {
        uint8_t sum = 0;
        sum ^= static_cast<uint8_t>(config.marker);
        sum ^= static_cast<uint8_t>(config.marker >> 8);
        sum ^= static_cast<uint8_t>(config.marker >> 16);
        sum ^= static_cast<uint8_t>(config.marker >> 24);
        sum ^= config.version;
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            sum ^= config.startThreshold[cellIdx];
            sum ^= static_cast<uint8_t>(config.stopThreshold[cellIdx] << 1);
        }
        for (int idx = 0; idx <= WifiSsidMaxLength; ++idx)
        {
            sum ^= static_cast<uint8_t>(config.wifiSsid[idx] + idx);
        }
        for (int idx = 0; idx <= WifiPasswordMaxLength; ++idx)
        {
            sum ^= static_cast<uint8_t>(config.wifiPassword[idx] + (idx * 3));
        }
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 8);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 16);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 24);
        sum ^= static_cast<uint8_t>(config.i2cClockHz);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 8);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 16);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 24);
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            sum ^= static_cast<uint8_t>(config.dryCalibrationRaw[cellIdx]);
            sum ^= static_cast<uint8_t>(config.dryCalibrationRaw[cellIdx] >> 8);
            sum ^= static_cast<uint8_t>(config.wetCalibrationRaw[cellIdx]);
            sum ^= static_cast<uint8_t>(config.wetCalibrationRaw[cellIdx] >> 8);
            sum ^= static_cast<uint8_t>(config.sensorInputMode[cellIdx] + (cellIdx * 7));
            sum ^= static_cast<uint8_t>(config.zoneSensor[cellIdx] + (cellIdx * 11));
        }
        sum ^= static_cast<uint8_t>(config.zoneSwitchIntervalSeconds);
        sum ^= static_cast<uint8_t>(config.zoneSwitchIntervalSeconds >> 8);
        sum ^= static_cast<uint8_t>(config.zoneSwitchIntervalSeconds >> 16);
        sum ^= static_cast<uint8_t>(config.zoneSwitchIntervalSeconds >> 24);
        return sum;
    }

    uint8_t configChecksumV5(const PumpConfigV5& config)
    {
        uint8_t sum = 0;
        sum ^= static_cast<uint8_t>(config.marker);
        sum ^= static_cast<uint8_t>(config.marker >> 8);
        sum ^= static_cast<uint8_t>(config.marker >> 16);
        sum ^= static_cast<uint8_t>(config.marker >> 24);
        sum ^= config.version;
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            sum ^= config.startThreshold[cellIdx];
            sum ^= static_cast<uint8_t>(config.stopThreshold[cellIdx] << 1);
        }
        for (int idx = 0; idx <= WifiSsidMaxLength; ++idx)
        {
            sum ^= static_cast<uint8_t>(config.wifiSsid[idx] + idx);
        }
        for (int idx = 0; idx <= WifiPasswordMaxLength; ++idx)
        {
            sum ^= static_cast<uint8_t>(config.wifiPassword[idx] + (idx * 3));
        }
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 8);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 16);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 24);
        sum ^= static_cast<uint8_t>(config.i2cClockHz);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 8);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 16);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 24);
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            sum ^= static_cast<uint8_t>(config.dryCalibrationRaw[cellIdx]);
            sum ^= static_cast<uint8_t>(config.dryCalibrationRaw[cellIdx] >> 8);
            sum ^= static_cast<uint8_t>(config.wetCalibrationRaw[cellIdx]);
            sum ^= static_cast<uint8_t>(config.wetCalibrationRaw[cellIdx] >> 8);
            sum ^= static_cast<uint8_t>(config.sensorInputMode[cellIdx] + (cellIdx * 7));
        }
        return sum;
    }

    uint8_t configChecksumV4(const PumpConfigV4& config)
    {
        uint8_t sum = 0;
        sum ^= static_cast<uint8_t>(config.marker);
        sum ^= static_cast<uint8_t>(config.marker >> 8);
        sum ^= static_cast<uint8_t>(config.marker >> 16);
        sum ^= static_cast<uint8_t>(config.marker >> 24);
        sum ^= config.version;
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            sum ^= config.startThreshold[cellIdx];
            sum ^= static_cast<uint8_t>(config.stopThreshold[cellIdx] << 1);
        }
        for (int idx = 0; idx <= WifiSsidMaxLength; ++idx)
        {
            sum ^= static_cast<uint8_t>(config.wifiSsid[idx] + idx);
        }
        for (int idx = 0; idx <= WifiPasswordMaxLength; ++idx)
        {
            sum ^= static_cast<uint8_t>(config.wifiPassword[idx] + (idx * 3));
        }
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 8);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 16);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 24);
        sum ^= static_cast<uint8_t>(config.i2cClockHz);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 8);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 16);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 24);
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            sum ^= static_cast<uint8_t>(config.dryCalibrationRaw[cellIdx]);
            sum ^= static_cast<uint8_t>(config.dryCalibrationRaw[cellIdx] >> 8);
            sum ^= static_cast<uint8_t>(config.wetCalibrationRaw[cellIdx]);
            sum ^= static_cast<uint8_t>(config.wetCalibrationRaw[cellIdx] >> 8);
        }
        return sum;
    }

    uint8_t configChecksumV3(const PumpConfigV3& config)
    {
        uint8_t sum = 0;
        sum ^= static_cast<uint8_t>(config.marker);
        sum ^= static_cast<uint8_t>(config.marker >> 8);
        sum ^= static_cast<uint8_t>(config.marker >> 16);
        sum ^= static_cast<uint8_t>(config.marker >> 24);
        sum ^= config.version;
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            sum ^= config.startThreshold[cellIdx];
            sum ^= static_cast<uint8_t>(config.stopThreshold[cellIdx] << 1);
        }
        for (int idx = 0; idx <= WifiSsidMaxLength; ++idx)
        {
            sum ^= static_cast<uint8_t>(config.wifiSsid[idx] + idx);
        }
        for (int idx = 0; idx <= WifiPasswordMaxLength; ++idx)
        {
            sum ^= static_cast<uint8_t>(config.wifiPassword[idx] + (idx * 3));
        }
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 8);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 16);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 24);
        sum ^= static_cast<uint8_t>(config.i2cClockHz);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 8);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 16);
        sum ^= static_cast<uint8_t>(config.i2cClockHz >> 24);
        return sum;
    }

    uint8_t configChecksumV2(const PumpConfigV2& config)
    {
        uint8_t sum = 0;
        sum ^= static_cast<uint8_t>(config.marker);
        sum ^= static_cast<uint8_t>(config.marker >> 8);
        sum ^= static_cast<uint8_t>(config.marker >> 16);
        sum ^= static_cast<uint8_t>(config.marker >> 24);
        sum ^= config.version;
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            sum ^= config.startThreshold[cellIdx];
            sum ^= static_cast<uint8_t>(config.stopThreshold[cellIdx] << 1);
        }
        for (int idx = 0; idx <= WifiSsidMaxLength; ++idx)
        {
            sum ^= static_cast<uint8_t>(config.wifiSsid[idx] + idx);
        }
        for (int idx = 0; idx <= WifiPasswordMaxLength; ++idx)
        {
            sum ^= static_cast<uint8_t>(config.wifiPassword[idx] + (idx * 3));
        }
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 8);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 16);
        sum ^= static_cast<uint8_t>(config.logIntervalSeconds >> 24);
        return sum;
    }

    uint8_t configChecksumV1(const PumpConfigV1& config)
    {
        uint8_t sum = 0;
        sum ^= static_cast<uint8_t>(config.marker);
        sum ^= static_cast<uint8_t>(config.marker >> 8);
        sum ^= static_cast<uint8_t>(config.marker >> 16);
        sum ^= static_cast<uint8_t>(config.marker >> 24);
        sum ^= config.version;
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            sum ^= config.startThreshold[cellIdx];
            sum ^= static_cast<uint8_t>(config.stopThreshold[cellIdx] << 1);
        }
        for (int idx = 0; idx <= WifiSsidMaxLength; ++idx)
        {
            sum ^= static_cast<uint8_t>(config.wifiSsid[idx] + idx);
        }
        for (int idx = 0; idx <= WifiPasswordMaxLength; ++idx)
        {
            sum ^= static_cast<uint8_t>(config.wifiPassword[idx] + (idx * 3));
        }
        return sum;
    }

    void applyConfig()
    {
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            Cells[cellIdx].SetWateringThresholds(
                static_cast<float>(Config.startThreshold[cellIdx]) / 100.0f,
                static_cast<float>(Config.stopThreshold[cellIdx]) / 100.0f);
            Cells[cellIdx].SetMoistureCalibration(
                Config.dryCalibrationRaw[cellIdx],
                Config.wetCalibrationRaw[cellIdx]);
            Cells[cellIdx].SetSensorInputMode(static_cast<SensorInputMode>(Config.sensorInputMode[cellIdx]));
            Cells[cellIdx].SetSimulatedMoisture(SimulationEnabled, SimulatedMoisturePercent[cellIdx]);
        }
    }

    void applyI2cClock()
    {
        Wire.setClock(Config.i2cClockHz);
    }

    void saveConfig()
    {
        Config.marker = ConfigMarker;
        Config.version = ConfigVersion;
        Config.checksum = configChecksum(Config);
        EEPROM.put(ConfigStartAddress, Config);
    }

    void loadConfig()
    {
        EEPROM.get(ConfigStartAddress, Config);
        const bool valid = Config.marker == ConfigMarker
            && Config.version == ConfigVersion
            && Config.checksum == configChecksum(Config);
        if (!valid)
        {
            PumpConfigV6 oldConfigV6;
            EEPROM.get(ConfigStartAddress, oldConfigV6);
            const bool validV6 = oldConfigV6.marker == ConfigMarker
                && oldConfigV6.version == 6
                && oldConfigV6.checksum == configChecksumV6(oldConfigV6);

            PumpConfigV5 oldConfigV5;
            EEPROM.get(ConfigStartAddress, oldConfigV5);
            const bool validV5 = oldConfigV5.marker == ConfigMarker
                && oldConfigV5.version == 5
                && oldConfigV5.checksum == configChecksumV5(oldConfigV5);

            PumpConfigV4 oldConfigV4;
            EEPROM.get(ConfigStartAddress, oldConfigV4);
            const bool validV4 = oldConfigV4.marker == ConfigMarker
                && oldConfigV4.version == 4
                && oldConfigV4.checksum == configChecksumV4(oldConfigV4);

            PumpConfigV3 oldConfigV3;
            EEPROM.get(ConfigStartAddress, oldConfigV3);
            const bool validV3 = oldConfigV3.marker == ConfigMarker
                && oldConfigV3.version == 3
                && oldConfigV3.checksum == configChecksumV3(oldConfigV3);

            PumpConfigV2 oldConfigV2;
            EEPROM.get(ConfigStartAddress, oldConfigV2);
            const bool validV2 = oldConfigV2.marker == ConfigMarker
                && oldConfigV2.version == 2
                && oldConfigV2.checksum == configChecksumV2(oldConfigV2);

            PumpConfigV1 oldConfig;
            EEPROM.get(ConfigStartAddress, oldConfig);
            const bool validV1 = oldConfig.marker == ConfigMarker
                && oldConfig.version == 1
                && oldConfig.checksum == configChecksumV1(oldConfig);

            Config = PumpConfig{};
            if (validV6)
            {
                memcpy(Config.startThreshold, oldConfigV6.startThreshold, sizeof(Config.startThreshold));
                memcpy(Config.stopThreshold, oldConfigV6.stopThreshold, sizeof(Config.stopThreshold));
                memcpy(Config.wifiSsid, oldConfigV6.wifiSsid, sizeof(Config.wifiSsid));
                memcpy(Config.wifiPassword, oldConfigV6.wifiPassword, sizeof(Config.wifiPassword));
                Config.logIntervalSeconds = oldConfigV6.logIntervalSeconds;
                Config.i2cClockHz = oldConfigV6.i2cClockHz;
                memcpy(Config.dryCalibrationRaw, oldConfigV6.dryCalibrationRaw, sizeof(Config.dryCalibrationRaw));
                memcpy(Config.wetCalibrationRaw, oldConfigV6.wetCalibrationRaw, sizeof(Config.wetCalibrationRaw));
                memcpy(Config.sensorInputMode, oldConfigV6.sensorInputMode, sizeof(Config.sensorInputMode));
                memcpy(Config.zoneSensor, oldConfigV6.zoneSensor, sizeof(Config.zoneSensor));
                Config.zoneSwitchIntervalSeconds = oldConfigV6.zoneSwitchIntervalSeconds;
            }
            else if (validV5)
            {
                memcpy(Config.startThreshold, oldConfigV5.startThreshold, sizeof(Config.startThreshold));
                memcpy(Config.stopThreshold, oldConfigV5.stopThreshold, sizeof(Config.stopThreshold));
                memcpy(Config.wifiSsid, oldConfigV5.wifiSsid, sizeof(Config.wifiSsid));
                memcpy(Config.wifiPassword, oldConfigV5.wifiPassword, sizeof(Config.wifiPassword));
                Config.logIntervalSeconds = oldConfigV5.logIntervalSeconds;
                Config.i2cClockHz = oldConfigV5.i2cClockHz;
                memcpy(Config.dryCalibrationRaw, oldConfigV5.dryCalibrationRaw, sizeof(Config.dryCalibrationRaw));
                memcpy(Config.wetCalibrationRaw, oldConfigV5.wetCalibrationRaw, sizeof(Config.wetCalibrationRaw));
                memcpy(Config.sensorInputMode, oldConfigV5.sensorInputMode, sizeof(Config.sensorInputMode));
            }
            else if (validV4)
            {
                memcpy(Config.startThreshold, oldConfigV4.startThreshold, sizeof(Config.startThreshold));
                memcpy(Config.stopThreshold, oldConfigV4.stopThreshold, sizeof(Config.stopThreshold));
                memcpy(Config.wifiSsid, oldConfigV4.wifiSsid, sizeof(Config.wifiSsid));
                memcpy(Config.wifiPassword, oldConfigV4.wifiPassword, sizeof(Config.wifiPassword));
                Config.logIntervalSeconds = oldConfigV4.logIntervalSeconds;
                Config.i2cClockHz = oldConfigV4.i2cClockHz;
                memcpy(Config.dryCalibrationRaw, oldConfigV4.dryCalibrationRaw, sizeof(Config.dryCalibrationRaw));
                memcpy(Config.wetCalibrationRaw, oldConfigV4.wetCalibrationRaw, sizeof(Config.wetCalibrationRaw));
            }
            else if (validV3)
            {
                memcpy(Config.startThreshold, oldConfigV3.startThreshold, sizeof(Config.startThreshold));
                memcpy(Config.stopThreshold, oldConfigV3.stopThreshold, sizeof(Config.stopThreshold));
                memcpy(Config.wifiSsid, oldConfigV3.wifiSsid, sizeof(Config.wifiSsid));
                memcpy(Config.wifiPassword, oldConfigV3.wifiPassword, sizeof(Config.wifiPassword));
                Config.logIntervalSeconds = oldConfigV3.logIntervalSeconds;
                Config.i2cClockHz = oldConfigV3.i2cClockHz;
            }
            else if (validV2)
            {
                memcpy(Config.startThreshold, oldConfigV2.startThreshold, sizeof(Config.startThreshold));
                memcpy(Config.stopThreshold, oldConfigV2.stopThreshold, sizeof(Config.stopThreshold));
                memcpy(Config.wifiSsid, oldConfigV2.wifiSsid, sizeof(Config.wifiSsid));
                memcpy(Config.wifiPassword, oldConfigV2.wifiPassword, sizeof(Config.wifiPassword));
                Config.logIntervalSeconds = oldConfigV2.logIntervalSeconds;
                Config.i2cClockHz = DefaultI2cClockHz;
            }
            else if (validV1)
            {
                memcpy(Config.startThreshold, oldConfig.startThreshold, sizeof(Config.startThreshold));
                memcpy(Config.stopThreshold, oldConfig.stopThreshold, sizeof(Config.stopThreshold));
                memcpy(Config.wifiSsid, oldConfig.wifiSsid, sizeof(Config.wifiSsid));
                memcpy(Config.wifiPassword, oldConfig.wifiPassword, sizeof(Config.wifiPassword));
                Config.logIntervalSeconds = DefaultLogIntervalSeconds;
                Config.i2cClockHz = DefaultI2cClockHz;
            }
        }

        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            Config.startThreshold[cellIdx] = constrain(Config.startThreshold[cellIdx], 0, 100);
            Config.stopThreshold[cellIdx] = constrain(Config.stopThreshold[cellIdx], 0, 100);
            if (Config.stopThreshold[cellIdx] < Config.startThreshold[cellIdx])
            {
                Config.stopThreshold[cellIdx] = Config.startThreshold[cellIdx];
            }
            Config.dryCalibrationRaw[cellIdx] = constrain(
                Config.dryCalibrationRaw[cellIdx],
                MinCalibrationRaw,
                static_cast<uint16_t>(MaxCalibrationRaw - 1));
            Config.wetCalibrationRaw[cellIdx] = constrain(
                Config.wetCalibrationRaw[cellIdx],
                static_cast<uint16_t>(Config.dryCalibrationRaw[cellIdx] + 1),
                MaxCalibrationRaw);
            if (Config.sensorInputMode[cellIdx] != static_cast<uint8_t>(SensorInputMode::Seesaw)
                && Config.sensorInputMode[cellIdx] != static_cast<uint8_t>(SensorInputMode::Vh400)
                && Config.sensorInputMode[cellIdx] != static_cast<uint8_t>(SensorInputMode::NotUsed))
            {
                Config.sensorInputMode[cellIdx] = static_cast<uint8_t>(SensorInputMode::Seesaw);
            }
            if (Config.zoneSensor[cellIdx] >= NrCells)
            {
                Config.zoneSensor[cellIdx] = cellIdx;
            }
        }
        Config.wifiSsid[WifiSsidMaxLength] = '\0';
        Config.wifiPassword[WifiPasswordMaxLength] = '\0';
        Config.cloudLogEndpoint[CloudLogEndpointMaxLength] = '\0';
        Config.cloudLogToken[CloudLogTokenMaxLength] = '\0';
        if (Config.logIntervalSeconds < MinLogIntervalSeconds || Config.logIntervalSeconds > MaxLogIntervalSeconds)
        {
            Config.logIntervalSeconds = DefaultLogIntervalSeconds;
        }
        if (Config.i2cClockHz < MinI2cClockHz || Config.i2cClockHz > MaxI2cClockHz)
        {
            Config.i2cClockHz = DefaultI2cClockHz;
        }
        if (Config.zoneSwitchIntervalSeconds < MinZoneSwitchIntervalSeconds
            || Config.zoneSwitchIntervalSeconds > MaxZoneSwitchIntervalSeconds)
        {
            Config.zoneSwitchIntervalSeconds = DefaultZoneSwitchIntervalSeconds;
        }
        saveConfig();
    }

    bool setCellThresholds(int cellIdx, uint8_t startPercent, uint8_t stopPercent)
    {
        if (!GardenLogic::IsValidCellIndex(cellIdx)
            || !GardenLogic::IsValidThresholdConfig(startPercent, stopPercent))
        {
            return false;
        }

        Config.startThreshold[cellIdx] = startPercent;
        Config.stopThreshold[cellIdx] = stopPercent;
        applyConfig();
        saveConfig();
        return true;
    }

    bool setCellCalibration(int cellIdx, uint16_t dryRaw, uint16_t wetRaw)
    {
        if (!GardenLogic::IsValidCellIndex(cellIdx)
            || !GardenLogic::IsValidCalibrationConfig(dryRaw, wetRaw))
        {
            return false;
        }

        Config.dryCalibrationRaw[cellIdx] = dryRaw;
        Config.wetCalibrationRaw[cellIdx] = wetRaw;
        applyConfig();
        saveConfig();
        return true;
    }

    bool setCellSensorInputMode(int cellIdx, SensorInputMode mode)
    {
        if (cellIdx < 0 || cellIdx >= NrCells
            || (mode != SensorInputMode::Seesaw && mode != SensorInputMode::Vh400 && mode != SensorInputMode::NotUsed))
        {
            return false;
        }

        Config.sensorInputMode[cellIdx] = static_cast<uint8_t>(mode);
        Cells[cellIdx].SetSensorInputMode(mode);
        Cells[cellIdx].SetSimulatedMoisture(SimulationEnabled, SimulatedMoisturePercent[cellIdx]);
        saveConfig();
        return true;
    }

    bool setZoneSensor(int zoneIdx, int sensorIdx)
    {
        if (!GardenLogic::IsValidZoneSensor(zoneIdx, sensorIdx))
        {
            return false;
        }

        Config.zoneSensor[zoneIdx] = static_cast<uint8_t>(sensorIdx);
        saveConfig();
        LastZoneSwitchMs = 0;
        return true;
    }

    bool setZoneSwitchIntervalSeconds(uint32_t intervalSeconds)
    {
        if (intervalSeconds < MinZoneSwitchIntervalSeconds || intervalSeconds > MaxZoneSwitchIntervalSeconds)
        {
            return false;
        }

        Config.zoneSwitchIntervalSeconds = intervalSeconds;
        saveConfig();
        return true;
    }

    bool setI2cClockHz(uint32_t clockHz)
    {
        if (clockHz < MinI2cClockHz || clockHz > MaxI2cClockHz)
        {
            return false;
        }

        Config.i2cClockHz = clockHz;
        applyI2cClock();
        saveConfig();
        return true;
    }

    bool setLogIntervalSeconds(uint32_t intervalSeconds)
    {
        if (intervalSeconds < MinLogIntervalSeconds || intervalSeconds > MaxLogIntervalSeconds)
        {
            return false;
        }

        Config.logIntervalSeconds = intervalSeconds;
        saveConfig();
        return true;
    }

    bool hasWifiConfig()
    {
        return Config.wifiSsid[0] != '\0' && Config.wifiPassword[0] != '\0';
    }

    bool setWifiConfig(const String& ssid, const String& password)
    {
        if (ssid.length() == 0 || ssid.length() > WifiSsidMaxLength
            || password.length() == 0 || password.length() > WifiPasswordMaxLength)
        {
            return false;
        }

        memset(Config.wifiSsid, 0, sizeof(Config.wifiSsid));
        memset(Config.wifiPassword, 0, sizeof(Config.wifiPassword));
        ssid.toCharArray(Config.wifiSsid, sizeof(Config.wifiSsid));
        password.toCharArray(Config.wifiPassword, sizeof(Config.wifiPassword));
        saveConfig();
        return true;
    }

    void clearWifiConfig()
    {
        memset(Config.wifiSsid, 0, sizeof(Config.wifiSsid));
        memset(Config.wifiPassword, 0, sizeof(Config.wifiPassword));
        saveConfig();
    }

    void printWifiConfigHelp(Print& out)
    {
        out.println(F("WiFi config commands:"));
        out.println(F("  SET_WIFI <ssid> <password>"));
        out.println(F("  CLEAR_WIFI"));
        out.println(F("  WIFI_STATUS"));
    }

    bool hasCloudLogConfig()
    {
        return Config.cloudLogEndpoint[0] != '\0' && Config.cloudLogToken[0] != '\0';
    }

    bool setCloudLogEndpoint(const String& endpoint)
    {
        if (!endpoint.startsWith(F("https://")) || endpoint.length() == 0 || endpoint.length() > CloudLogEndpointMaxLength)
        {
            return false;
        }

        memset(Config.cloudLogEndpoint, 0, sizeof(Config.cloudLogEndpoint));
        endpoint.toCharArray(Config.cloudLogEndpoint, sizeof(Config.cloudLogEndpoint));
        saveConfig();
        return true;
    }

    bool setCloudLogToken(const String& token)
    {
        if (token.length() == 0 || token.length() > CloudLogTokenMaxLength)
        {
            return false;
        }

        memset(Config.cloudLogToken, 0, sizeof(Config.cloudLogToken));
        token.toCharArray(Config.cloudLogToken, sizeof(Config.cloudLogToken));
        saveConfig();
        return true;
    }

    void clearCloudLogConfig()
    {
        memset(Config.cloudLogEndpoint, 0, sizeof(Config.cloudLogEndpoint));
        memset(Config.cloudLogToken, 0, sizeof(Config.cloudLogToken));
        saveConfig();
    }

    unsigned long logIntervalMs()
    {
        return static_cast<unsigned long>(Config.logIntervalSeconds) * 1000UL;
    }
}
