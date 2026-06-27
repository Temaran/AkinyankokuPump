#include "FirmwareServices.h"

#include <WDT.h>

namespace GardenPump
{
    namespace
    {
        constexpr uint32_t RuntimeBackupMagic = 0x47505744UL; // GPWD
        constexpr uint8_t RuntimeBackupVersion = 2;
        constexpr uint16_t BackupRegisterUnlock = 0xA502U;
        constexpr uint16_t BackupRegisterLock = 0xA500U;
        constexpr int BackupMagicOffset = RuntimeBackupRegisterOffset;
        constexpr int BackupVersionOffset = RuntimeBackupRegisterOffset + 4;
        constexpr int BackupStageOffset = RuntimeBackupRegisterOffset + 5;
        constexpr int BackupStageStartedOffset = RuntimeBackupRegisterOffset + 6;
        constexpr int BackupWatchdogCountOffset = RuntimeBackupRegisterOffset + 10;
        constexpr int BackupMaxLoopOffset = RuntimeBackupRegisterOffset + 14;
        constexpr int BackupChecksumOffset = RuntimeBackupRegisterOffset + 18;

        uint32_t readBackupU32(int offset)
        {
            uint32_t value = 0;
            for (int idx = 0; idx < 4; ++idx)
            {
                value |= static_cast<uint32_t>(R_SYSTEM->VBTBKR[offset + idx]) << (idx * 8);
            }
            return value;
        }

        void writeBackupU8(int offset, uint8_t value)
        {
            R_SYSTEM->PRCR = BackupRegisterUnlock;
            R_SYSTEM->VBTBKR[offset] = value;
            R_SYSTEM->PRCR = BackupRegisterLock;
        }

        void writeBackupU32(int offset, uint32_t value)
        {
            R_SYSTEM->PRCR = BackupRegisterUnlock;
            for (int idx = 0; idx < 4; ++idx)
            {
                R_SYSTEM->VBTBKR[offset + idx] =
                    static_cast<uint8_t>((value >> (idx * 8)) & 0xFFU);
            }
            R_SYSTEM->PRCR = BackupRegisterLock;
        }

        uint32_t backupChecksum()
        {
            uint32_t checksum = 2166136261UL;
            for (int offset = BackupVersionOffset; offset < BackupChecksumOffset; ++offset)
            {
                checksum ^= R_SYSTEM->VBTBKR[offset];
                checksum *= 16777619UL;
            }
            return checksum;
        }

        void updateBackupChecksum()
        {
            writeBackupU32(BackupChecksumOffset, backupChecksum());
        }

        bool hasValidBackupRecord()
        {
            return readBackupU32(BackupMagicOffset) == RuntimeBackupMagic &&
                   R_SYSTEM->VBTBKR[BackupVersionOffset] == RuntimeBackupVersion &&
                   readBackupU32(BackupChecksumOffset) == backupChecksum();
        }

        void initializeBackupRecord()
        {
            writeBackupU32(BackupMagicOffset, RuntimeBackupMagic);
            writeBackupU8(BackupVersionOffset, RuntimeBackupVersion);
            writeBackupU8(BackupStageOffset, static_cast<uint8_t>(RuntimeStage::Boot));
            writeBackupU32(BackupStageStartedOffset, 0);
            writeBackupU32(BackupWatchdogCountOffset, 0);
            writeBackupU32(BackupMaxLoopOffset, 0);
            updateBackupChecksum();
        }
    }

    const __FlashStringHelper* runtimeStageName(RuntimeStage stage)
    {
        switch (stage)
        {
            case RuntimeStage::Boot: return F("boot");
            case RuntimeStage::Idle: return F("idle");
            case RuntimeStage::Serial: return F("serial");
            case RuntimeStage::SensorRead: return F("sensor-read");
            case RuntimeStage::Irrigation: return F("irrigation");
            case RuntimeStage::WebRequest: return F("web-request");
            case RuntimeStage::WifiConnect: return F("wifi-connect");
            case RuntimeStage::CloudConnect: return F("cloud-connect");
            case RuntimeStage::CloudHeaders: return F("cloud-headers");
            case RuntimeStage::CloudBody: return F("cloud-body");
            case RuntimeStage::HistoryConnect: return F("history-connect");
            case RuntimeStage::HistoryHeaders: return F("history-headers");
            case RuntimeStage::HistoryBody: return F("history-body");
            case RuntimeStage::Ntp: return F("ntp");
            case RuntimeStage::Eeprom: return F("eeprom");
            default: return F("unknown");
        }
    }

    void initializeRuntimeDiagnostics()
    {
        Runtime.currentStage = RuntimeStage::Boot;
        Runtime.currentStageStartedMs = millis();
        if (!RetainedRuntimeDiagnosticsEnabled)
        {
            return;
        }

        Runtime.previousResetWasWatchdog = R_SYSTEM->RSTSR1_b.WDTRF != 0;
        if (!hasValidBackupRecord())
        {
            initializeBackupRecord();
        }

        Runtime.previousResetStage =
            static_cast<RuntimeStage>(R_SYSTEM->VBTBKR[BackupStageOffset]);
        Runtime.previousStageStartedMs = readBackupU32(BackupStageStartedOffset);
        Runtime.watchdogResetCount = readBackupU32(BackupWatchdogCountOffset);
        Runtime.maxLoopMs = readBackupU32(BackupMaxLoopOffset);
        if (Runtime.previousResetWasWatchdog)
        {
            Runtime.watchdogResetCount++;
            writeBackupU32(BackupWatchdogCountOffset, Runtime.watchdogResetCount);
            updateBackupChecksum();
            R_SYSTEM->RSTSR1_b.WDTRF = 0;
        }

        setRuntimeStage(RuntimeStage::Boot);
        printRuntimeDiagnostics(Serial);
    }

    void initializeWatchdog()
    {
        if (!HardwareWatchdogEnabled)
        {
            Runtime.watchdogEnabled = false;
            Serial.println(F("Hardware watchdog disabled."));
            return;
        }

        Runtime.watchdogEnabled = WDT.begin(WatchdogTimeoutMs) != 0;
        if (Runtime.watchdogEnabled)
        {
            WDT.refresh();
            Serial.print(F("Watchdog enabled: "));
            Serial.print(WDT.getTimeout());
            Serial.println(F(" ms"));
        }
        else
        {
            Serial.println(F("Watchdog initialization failed."));
        }
    }

    void setRuntimeStage(RuntimeStage stage)
    {
        Runtime.currentStage = stage;
        Runtime.currentStageStartedMs = millis();
        if (!RetainedRuntimeDiagnosticsEnabled)
        {
            return;
        }

        writeBackupU8(BackupStageOffset, static_cast<uint8_t>(stage));
        writeBackupU32(BackupStageStartedOffset, Runtime.currentStageStartedMs);
        updateBackupChecksum();
    }

    void beginControlLoopIteration()
    {
        Runtime.loopStartedMs = millis();
    }

    void completeControlLoopIteration()
    {
        Runtime.lastLoopMs = millis() - Runtime.loopStartedMs;
        Runtime.loopCount++;
        if (Runtime.lastLoopMs > Runtime.maxLoopMs)
        {
            Runtime.maxLoopMs = Runtime.lastLoopMs;
            if (RetainedRuntimeDiagnosticsEnabled)
            {
                writeBackupU32(BackupMaxLoopOffset, Runtime.maxLoopMs);
                updateBackupChecksum();
            }
        }
        setRuntimeStage(RuntimeStage::Idle);
        watchdogProgress();
    }

    void watchdogProgress()
    {
        if (Runtime.watchdogEnabled)
        {
            WDT.refresh();
        }
    }

    void noteWebRequest()
    {
        Runtime.webRequestCount++;
    }

    void noteCloudAttempt()
    {
        Runtime.cloudAttemptCount++;
    }

    void noteCloudResult(bool success)
    {
        if (success)
        {
            Runtime.cloudSuccessCount++;
            Runtime.consecutiveCloudFailures = 0;
        }
        else
        {
            Runtime.cloudFailureCount++;
            Runtime.consecutiveCloudFailures++;
        }
    }

    void noteWifiReconnect()
    {
        Runtime.wifiReconnectCount++;
    }

    void printRuntimeDiagnostics(Print& out)
    {
        out.print(F("Runtime stage: "));
        out.println(runtimeStageName(Runtime.currentStage));
        out.print(F("Previous reset watchdog: "));
        out.println(Runtime.previousResetWasWatchdog ? F("yes") : F("no"));
        out.print(F("Previous reset stage: "));
        out.println(runtimeStageName(Runtime.previousResetStage));
        out.print(F("Previous stage uptime ms: "));
        out.println(Runtime.previousStageStartedMs);
        out.print(F("Watchdog resets: "));
        out.println(Runtime.watchdogResetCount);
        out.print(F("Loop count: "));
        out.println(Runtime.loopCount);
        out.print(F("Last/max loop ms: "));
        out.print(Runtime.lastLoopMs);
        out.print('/');
        out.println(Runtime.maxLoopMs);
        out.print(F("Web requests: "));
        out.println(Runtime.webRequestCount);
        out.print(F("Cloud attempts/success/failure/consecutive: "));
        out.print(Runtime.cloudAttemptCount);
        out.print('/');
        out.print(Runtime.cloudSuccessCount);
        out.print('/');
        out.print(Runtime.cloudFailureCount);
        out.print('/');
        out.println(Runtime.consecutiveCloudFailures);
        out.print(F("WiFi reconnects: "));
        out.println(Runtime.wifiReconnectCount);
    }
}
