#include "FirmwareServices.h"

#include <Modem.h>
#include <RTC.h>

#include "GardenLogic.h"

namespace GardenPump
{
    namespace
    {
        void startNetworkServices()
        {
            if (!WebServerStarted)
            {
                WebServer.begin();
                WebServerStarted = true;
            }
            if (!UdpStarted)
            {
                Udp.begin(NtpLocalPort);
                UdpStarted = true;
            }
        }
    }

    bool isLeapYear(int year)
    {
        return GardenLogic::IsLeapYear(year);
    }

    int daysInMonth(int year, int month)
    {
        return GardenLogic::DaysInMonth(year, month);
    }

    int dayOfWeek(int year, int month, int day)
    {
        return GardenLogic::DayOfWeek(year, month, day);
    }

    int lastSundayOfMonth(int year, int month)
    {
        return GardenLogic::LastSundayOfMonth(year, month);
    }

    uint32_t unixTimeUtc(int year, int month, int day, int hour, int minute, int second)
    {
        return GardenLogic::UnixTimeUtc(year, month, day, hour, minute, second);
    }

    bool isEuropeStockholmDst(uint32_t utcUnixTime)
    {
        RTCTime utcTime(static_cast<time_t>(utcUnixTime));
        return GardenLogic::IsEuropeStockholmDst(utcTime.getYear(), utcUnixTime);
    }

    uint32_t europeStockholmOffsetSeconds(uint32_t utcUnixTime)
    {
        return isEuropeStockholmDst(utcUnixTime) ? 2UL * 60UL * 60UL : 1UL * 60UL * 60UL;
    }

    const __FlashStringHelper* europeStockholmTimeZoneName(uint32_t utcUnixTime)
    {
        return isEuropeStockholmDst(utcUnixTime) ? F("CEST") : F("CET");
    }

    uint32_t getCurrentTimestamp()
    {
        RTCTime currentTime;
        RTC.getTime(currentTime);
        return currentTime.getUnixTime();
    }

    String currentTimeString()
    {
        RTCTime currentTime;
        if (!RTC.getTime(currentTime))
        {
            return String("unavailable");
        }
        const uint32_t utcTimestamp = currentTime.getUnixTime();
        RTCTime localTime(static_cast<time_t>(utcTimestamp + europeStockholmOffsetSeconds(utcTimestamp)));
        String formatted = localTime.toString();
        formatted += ' ';
        formatted += europeStockholmTimeZoneName(utcTimestamp);
        return formatted;
    }

    void initializeWifiModem()
    {
        // WiFiS3's public connection timeout does not cover its internal
        // modem commands, whose library default is ten seconds each.
        modem.begin(115200, 1);
        modem.timeout(WifiModemCommandTimeoutMs);
        WiFi.setTimeout(WifiConnectTimeoutMs);
    }

    void connectWifiIfNeeded()
    {
        if (!hasWifiConfig())
        {
            return;
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            startNetworkServices();
            return;
        }

        if ((millis() - LastWifiAttemptMs) < WifiRetryMs && LastWifiAttemptMs != 0)
        {
            return;
        }

        LastWifiAttemptMs = millis();
        if (WiFi.status() == WL_NO_MODULE)
        {
            Serial.println(F("WiFi module not available."));
            return;
        }

        Serial.print(F("Connecting to WiFi SSID: "));
        Serial.println(Config.wifiSsid);
        WiFi.setHostname(WifiHostname);
        setRuntimeStage(RuntimeStage::WifiConnect);
        WifiStatus = WiFi.begin(Config.wifiSsid, Config.wifiPassword);
        if (WifiStatus == WL_CONNECTED)
        {
            noteWifiReconnect();
            startNetworkServices();
            printWifiStatus();
            syncTimeFromNtp();
        }
        else
        {
            Serial.println(F("WiFi not connected yet."));
        }
    }

    void sendNtpPacket()
    {
        memset(NtpPacket, 0, NtpPacketSize);
        NtpPacket[0] = 0b11100011;
        NtpPacket[1] = 0;
        NtpPacket[2] = 6;
        NtpPacket[3] = 0xEC;
        NtpPacket[12] = 49;
        NtpPacket[13] = 0x4E;
        NtpPacket[14] = 49;
        NtpPacket[15] = 52;

        Udp.beginPacket(NtpServer, 123);
        Udp.write(NtpPacket, NtpPacketSize);
        Udp.endPacket();
    }

    bool syncTimeFromNtp()
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            return false;
        }

        if (!UdpStarted)
        {
            Udp.begin(NtpLocalPort);
            UdpStarted = true;
        }

        LastNtpAttemptMs = millis();
        setRuntimeStage(RuntimeStage::Ntp);
        sendNtpPacket();
        const unsigned long responseWaitStartedMs = millis();
        while ((millis() - responseWaitStartedMs) < 1000)
        {
            delay(1);
        }

        if (!Udp.parsePacket())
        {
            Serial.println(F("NTP sync failed: no packet."));
            return false;
        }

        Udp.read(NtpPacket, NtpPacketSize);
        const unsigned long highWord = word(NtpPacket[40], NtpPacket[41]);
        const unsigned long lowWord = word(NtpPacket[42], NtpPacket[43]);
        const unsigned long secsSince1900 = (highWord << 16) | lowWord;
        const unsigned long unixTime = secsSince1900 - 2208988800UL;
        RTCTime rtcTime(static_cast<time_t>(unixTime));
        RTC.setTime(rtcTime);
        TimeSynced = true;
        Serial.print(F("RTC synced from NTP: "));
        Serial.println(currentTimeString());
        return true;
    }

    void printWifiStatus()
    {
        Serial.print(F("Connectivity firmware: "));
        Serial.println(WiFi.firmwareVersion());
        Serial.print(F("Expected connectivity firmware: "));
        Serial.println(F(WIFI_FIRMWARE_LATEST_VERSION));
        Serial.print(F("SSID: "));
        Serial.println(WiFi.SSID());
        Serial.print(F("IP Address: "));
        Serial.println(WiFi.localIP());
        Serial.print(F("RSSI: "));
        Serial.print(WiFi.RSSI());
        Serial.println(F(" dBm"));
        Serial.print(F("Garden pump dashboard: http://"));
        Serial.println(WiFi.localIP());
        Serial.print(F("Hostname, if supported by router: http://"));
        Serial.println(WifiHostname);
    }

    void handleWifi()
    {
        connectWifiIfNeeded();
        if (WiFi.status() == WL_CONNECTED)
        {
            handleWebClient();
            updateCloudLogger();
            const unsigned long retryMs = OperationsStarted ? NtpRetryMs : NtpStartupRetryMs;
            if (!TimeSynced && (millis() - LastNtpAttemptMs) > retryMs)
            {
                syncTimeFromNtp();
            }
        }
    }

    bool startupTimeWaitFinished()
    {
        if (TimeSynced)
        {
            return true;
        }

        if ((millis() - StartupStartedAtMs) >= StartupTimeSyncTimeoutMs)
        {
            if (!StartupTimeWaitTimedOut)
            {
                StartupTimeWaitTimedOut = true;
                Serial.println(F("Time sync startup timeout reached; starting normal operation without valid wall-clock time."));
            }
            return true;
        }

        return false;
    }

    void startOperationsIfReady()
    {
        if (OperationsStarted || !startupTimeWaitFinished())
        {
            return;
        }

        enterIrrigationMode();
        OperationsStarted = true;
    }
}
