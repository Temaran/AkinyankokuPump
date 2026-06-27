#include "FirmwareServices.h"

#include <math.h>
#include <stdlib.h>

namespace GardenPump
{
    namespace
    {
        constexpr size_t HttpHeaderLineMaxLength = 768;

        const char GoogleTrustServicesRootR1[] =
            "-----BEGIN CERTIFICATE-----\n"
            "MIIFVzCCAz+gAwIBAgINAgPlk28xsBNJiGuiFzANBgkqhkiG9w0BAQwFADBHMQsw\n"
            "CQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEU\n"
            "MBIGA1UEAxMLR1RTIFJvb3QgUjEwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAw\n"
            "MDAwWjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZp\n"
            "Y2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjEwggIiMA0GCSqGSIb3DQEBAQUA\n"
            "A4ICDwAwggIKAoICAQC2EQKLHuOhd5s73L+UPreVp0A8of2C+X0yBoJx9vaMf/vo\n"
            "27xqLpeXo4xL+Sv2sfnOhB2x+cWX3u+58qPpvBKJXqeqUqv4IyfLpLGcY9vXmX7w\n"
            "Cl7raKb0xlpHDU0QM+NOsROjyBhsS+z8CZDfnWQpJSMHobTSPS5g4M/SCYe7zUjw\n"
            "TcLCeoiKu7rPWRnWr4+wB7CeMfGCwcDfLqZtbBkOtdh+JhpFAz2weaSUKK0Pfybl\n"
            "qAj+lug8aJRT7oM6iCsVlgmy4HqMLnXWnOunVmSPlk9orj2XwoSPwLxAwAtcvfaH\n"
            "szVsrBhQf4TgTM2S0yDpM7xSma8ytSmzJSq0SPly4cpk9+aCEI3oncKKiPo4Zor8\n"
            "Y/kB+Xj9e1x3+naH+uzfsQ55lVe0vSbv1gHR6xYKu44LtcXFilWr06zqkUspzBmk\n"
            "MiVOKvFlRNACzqrOSbTqn3yDsEB750Orp2yjj32JgfpMpf/VjsPOS+C12LOORc92\n"
            "wO1AK/1TD7Cn1TsNsYqiA94xrcx36m97PtbfkSIS5r762DL8EGMUUXLeXdYWk70p\n"
            "aDPvOmbsB4om3xPXV2V4J95eSRQAogB/mqghtqmxlbCluQ0WEdrHbEg8QOB+DVrN\n"
            "VjzRlwW5y0vtOUucxD/SVRNuJLDWcfr0wbrM7Rv1/oFB2ACYPTrIrnqYNxgFlQID\n"
            "AQABo0IwQDAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4E\n"
            "FgQU5K8rJnEaK0gnhS9SZizv8IkTcT4wDQYJKoZIhvcNAQEMBQADggIBAJ+qQibb\n"
            "C5u+/x6Wki4+omVKapi6Ist9wTrYggoGxval3sBOh2Z5ofmmWJyq+bXmYOfg6LEe\n"
            "QkEzCzc9zolwFcq1JKjPa7XSQCGYzyI0zzvFIoTgxQ6KfF2I5DUkzps+GlQebtuy\n"
            "h6f88/qBVRRiClmpIgUxPoLW7ttXNLwzldMXG+gnoot7TiYaelpkttGsN/H9oPM4\n"
            "7HLwEXWdyzRSjeZ2axfG34arJ45JK3VmgRAhpuo+9K4l/3wV3s6MJT/KYnAK9y8J\n"
            "ZgfIPxz88NtFMN9iiMG1D53Dn0reWVlHxYciNuaCp+0KueIHoI17eko8cdLiA6Ef\n"
            "MgfdG+RCzgwARWGAtQsgWSl4vflVy2PFPEz0tv/bal8xa5meLMFrUKTX5hgUvYU/\n"
            "Z6tGn6D/Qqc6f1zLXbBwHSs09dR2CQzreExZBfMzQsNhFRAbd03OIozUhfJFfbdT\n"
            "6u9AWpQKXCBfTkBdYiJ23//OYb2MI3jSNwLgjt7RETeJ9r/tSQdirpLsQBqvFAnZ\n"
            "0E6yove+7u7Y/9waLd64NnHi/Hm3lCXRSHNboTXns5lndcEZOitHTtNCjv0xyBZm\n"
            "2tIMPNuzjsmhDYAPexZ3FL//2wmUspO8IFgV6dtxQ/PeEMMA3KgqlbbC1j+Qa3bb\n"
            "bP6MvPJwNQzcmRk13NfIRmPVNnGuV/u3gm3c\n"
            "-----END CERTIFICATE-----\n";

        struct ParsedHttpsUrl
        {
            String host;
            String path;
        };

        void setCloudLogMessage(const __FlashStringHelper* message)
        {
            String text(message);
            text.toCharArray(LastCloudLogMessage, sizeof(LastCloudLogMessage));
        }

        void setCloudLogMessage(const String& message)
        {
            message.toCharArray(LastCloudLogMessage, sizeof(LastCloudLogMessage));
        }

        bool parseHttpsUrl(const String& url, ParsedHttpsUrl& parsed)
        {
            if (!url.startsWith(F("https://")))
            {
                return false;
            }

            const int hostStart = 8;
            int pathStart = url.indexOf('/', hostStart);
            if (pathStart < 0)
            {
                parsed.host = url.substring(hostStart);
                parsed.path = "/";
            }
            else
            {
                parsed.host = url.substring(hostStart, pathStart);
                parsed.path = url.substring(pathStart);
            }

            return parsed.host.length() > 0;
        }

        String readHttpLine(WiFiClient& client, unsigned long timeoutMs)
        {
            String line;
            const unsigned long start = millis();
            while ((millis() - start) < timeoutMs)
            {
                while (client.available())
                {
                    const char ch = static_cast<char>(client.read());
                    if (ch == '\r')
                    {
                        continue;
                    }
                    if (ch == '\n')
                    {
                        return line;
                    }
                    if (line.length() < HttpHeaderLineMaxLength)
                    {
                        line += ch;
                    }
                }
                if (!client.connected() && !client.available())
                {
                    break;
                }
                delay(1);
            }
            return line;
        }

        int httpStatusCode(const String& statusLine)
        {
            if (!statusLine.startsWith(F("HTTP/")))
            {
                return 0;
            }
            const int firstSpace = statusLine.indexOf(' ');
            if (firstSpace < 0)
            {
                return 0;
            }
            return statusLine.substring(firstSpace + 1, firstSpace + 4).toInt();
        }

        String headerValue(const String& line, const __FlashStringHelper* headerName)
        {
            String name(headerName);
            if (!line.startsWith(name))
            {
                return "";
            }
            String value = line.substring(name.length());
            value.trim();
            return value;
        }

        bool readHttpByte(WiFiClient& remote, uint8_t& value, unsigned long timeoutMs)
        {
            const unsigned long start = millis();
            while ((millis() - start) < timeoutMs)
            {
                if (remote.available())
                {
                    value = static_cast<uint8_t>(remote.read());
                    return true;
                }
                if (!remote.connected())
                {
                    return false;
                }
                delay(1);
            }
            return false;
        }

        bool streamExactHttpBytes(
            WiFiClient& remote,
            WiFiClient& client,
            unsigned long byteCount)
        {
            uint8_t buffer[128];
            unsigned long remaining = byteCount;
            unsigned long lastProgressMs = millis();
            while (remaining > 0)
            {
                const int available = remote.available();
                if (available <= 0)
                {
                    if (!remote.connected() ||
                        (millis() - lastProgressMs) >= CloudLogHttpTimeoutMs)
                    {
                        return false;
                    }
                    delay(1);
                    continue;
                }

                const unsigned long wanted =
                    min(remaining, static_cast<unsigned long>(sizeof(buffer)));
                const int readCount = remote.read(
                    buffer,
                    min(available, static_cast<int>(wanted)));
                if (readCount <= 0)
                {
                    continue;
                }
                if (!client.connected() ||
                    client.write(buffer, static_cast<size_t>(readCount)) !=
                        static_cast<size_t>(readCount))
                {
                    return false;
                }
                remaining -= static_cast<unsigned long>(readCount);
                lastProgressMs = millis();
            }
            return true;
        }

        bool streamChunkedHttpBody(WiFiClient& remote, WiFiClient& client)
        {
            while (true)
            {
                String sizeLine = readHttpLine(remote, CloudLogHttpTimeoutMs);
                const int extensionStart = sizeLine.indexOf(';');
                if (extensionStart >= 0)
                {
                    sizeLine.remove(extensionStart);
                }
                sizeLine.trim();
                if (sizeLine.length() == 0)
                {
                    return false;
                }

                const unsigned long chunkBytes =
                    strtoul(sizeLine.c_str(), nullptr, 16);
                if (chunkBytes == 0)
                {
                    while (remote.connected() || remote.available())
                    {
                        if (readHttpLine(remote, CloudLogHttpTimeoutMs).length() == 0)
                        {
                            break;
                        }
                    }
                    return true;
                }

                if (!streamExactHttpBytes(remote, client, chunkBytes))
                {
                    return false;
                }

                uint8_t carriageReturn = 0;
                uint8_t lineFeed = 0;
                if (!readHttpByte(remote, carriageReturn, CloudLogHttpTimeoutMs) ||
                    !readHttpByte(remote, lineFeed, CloudLogHttpTimeoutMs) ||
                    carriageReturn != '\r' ||
                    lineFeed != '\n')
                {
                    return false;
                }
            }
        }

        bool streamHttpBodyUntilClose(WiFiClient& remote, WiFiClient& client)
        {
            uint8_t buffer[128];
            unsigned long lastProgressMs = millis();
            while (remote.connected() || remote.available())
            {
                const int available = remote.available();
                if (available <= 0)
                {
                    if ((millis() - lastProgressMs) >= CloudLogHttpTimeoutMs)
                    {
                        return false;
                    }
                    delay(1);
                    continue;
                }

                const int readCount =
                    remote.read(buffer, min(available, static_cast<int>(sizeof(buffer))));
                if (readCount <= 0)
                {
                    continue;
                }
                if (!client.connected() ||
                    client.write(buffer, static_cast<size_t>(readCount)) !=
                        static_cast<size_t>(readCount))
                {
                    return false;
                }
                lastProgressMs = millis();
            }
            return true;
        }

        bool sendHttpsPostJson(const String& url, const String& body)
        {
            ParsedHttpsUrl parsed;
            if (!parseHttpsUrl(url, parsed))
            {
                LastCloudLogHttpStatus = 0;
                setCloudLogMessage(F("invalid endpoint"));
                return false;
            }

            WiFiSSLClient remote;
            remote.setCACert(GoogleTrustServicesRootR1);
            setRuntimeStage(RuntimeStage::CloudConnect);
            if (!remote.connect(parsed.host.c_str(), 443))
            {
                LastCloudLogHttpStatus = 0;
                setCloudLogMessage(F("connect failed"));
                return false;
            }

            remote.print(F("POST "));
            remote.print(parsed.path);
            remote.println(F(" HTTP/1.1"));
            remote.print(F("Host: "));
            remote.println(parsed.host);
            remote.println(F("User-Agent: GardenPump/0.1"));
            remote.println(F("Connection: close"));
            remote.println(F("Content-Type: application/json"));
            remote.print(F("Content-Length: "));
            remote.println(body.length());
            remote.println();
            setRuntimeStage(RuntimeStage::CloudBody);
            remote.print(body);

            setRuntimeStage(RuntimeStage::CloudHeaders);
            const String statusLine = readHttpLine(remote, CloudLogHttpTimeoutMs);
            const int status = httpStatusCode(statusLine);
            LastCloudLogHttpStatus = status;

            while (remote.connected() || remote.available())
            {
                const String line = readHttpLine(remote, CloudLogHttpTimeoutMs);
                if (line.length() == 0)
                {
                    break;
                }
            }
            remote.stop();

            if (status >= 200 && status < 400)
            {
                setCloudLogMessage(F("sent"));
                return true;
            }

            setCloudLogMessage(statusLine.length() > 0 ? statusLine : String(F("no response")));
            return false;
        }

        bool streamHttpsGetBody(const String& url, WiFiClient& client, int redirectsLeft)
        {
            ParsedHttpsUrl parsed;
            if (!parseHttpsUrl(url, parsed))
            {
                return false;
            }

            WiFiSSLClient remote;
            remote.setCACert(GoogleTrustServicesRootR1);
            setRuntimeStage(RuntimeStage::HistoryConnect);
            if (!remote.connect(parsed.host.c_str(), 443))
            {
                return false;
            }

            remote.print(F("GET "));
            remote.print(parsed.path);
            remote.println(F(" HTTP/1.1"));
            remote.print(F("Host: "));
            remote.println(parsed.host);
            remote.println(F("User-Agent: GardenPump/0.1"));
            remote.println(F("Connection: close"));
            remote.println(F("Accept-Encoding: identity"));
            remote.println();

            setRuntimeStage(RuntimeStage::HistoryHeaders);
            const String statusLine = readHttpLine(remote, CloudLogHttpTimeoutMs);
            const int status = httpStatusCode(statusLine);
            String location;
            String transferEncoding;

            while (remote.connected() || remote.available())
            {
                const String line = readHttpLine(remote, CloudLogHttpTimeoutMs);
                if (line.length() == 0)
                {
                    break;
                }
                if (location.length() == 0)
                {
                    location = headerValue(line, F("Location:"));
                }
                if (transferEncoding.length() == 0)
                {
                    transferEncoding = headerValue(line, F("Transfer-Encoding:"));
                }
            }

            if (status >= 300 && status < 400 && location.length() > 0 && redirectsLeft > 0)
            {
                remote.stop();
                return streamHttpsGetBody(location, client, redirectsLeft - 1);
            }

            if (status < 200 || status >= 300)
            {
                remote.stop();
                return false;
            }

            sendHttpHeaders(client, "application/json");
            setRuntimeStage(RuntimeStage::HistoryBody);
            transferEncoding.toLowerCase();
            if (transferEncoding.indexOf(F("chunked")) >= 0)
            {
                streamChunkedHttpBody(remote, client);
            }
            else
            {
                streamHttpBodyUntilClose(remote, client);
            }
            remote.stop();
            // Headers have already been sent to the browser, so this request is
            // handled even if the browser disconnected while the body streamed.
            return true;
        }

        String cloudHistoryUrl(int limit)
        {
            String url(Config.cloudLogEndpoint);
            url += url.indexOf('?') >= 0 ? '&' : '?';
            url += F("token=");
            url += Config.cloudLogToken;
            url += F("&limit=");
            url += limit;
            return url;
        }

        bool cloudLogReadingsReady()
        {
            bool hasInstalledSensor = false;
            for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
            {
                if (Cells[cellIdx].GetSensorInputMode() == SensorInputMode::NotUsed)
                {
                    continue;
                }
                hasInstalledSensor = true;
                if (Cells[cellIdx].GetLastRefreshMs() == 0)
                {
                    return false;
                }
            }
            return hasInstalledSensor;
        }
    }

    uint8_t currentRelayMask()
    {
        uint8_t mask = 0;
        for (int zoneIdx = 0; zoneIdx < NrCells; ++zoneIdx)
        {
            if (Cells[zoneIdx].IsRelayEnabled())
            {
                mask |= static_cast<uint8_t>(1 << zoneIdx);
            }
        }
        return mask;
    }

    void captureCloudLogSnapshot(CloudLogSnapshot& snapshot)
    {
        accountZoneWaterRuntime();
        snapshot.valid = true;
        snapshot.relayMask = currentRelayMask();
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            snapshot.moisturePercent[cellIdx] = Cells[cellIdx].GetMoistnessNorm() * 100.0f;
            snapshot.waterMl[cellIdx] = estimatedZoneWaterMl(cellIdx);
        }
    }

    bool shouldSendCloudLog(const CloudLogSnapshot& snapshot)
    {
        if (!snapshot.valid)
        {
            return false;
        }
        if (!LastCloudLogSnapshot.valid)
        {
            return true;
        }
        if (snapshot.relayMask != LastCloudLogSnapshot.relayMask)
        {
            return true;
        }
        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            if (fabs(snapshot.moisturePercent[cellIdx] - LastCloudLogSnapshot.moisturePercent[cellIdx]) >= CloudLogMoistureDeltaPercent)
            {
                return true;
            }
            if (fabs(snapshot.waterMl[cellIdx] - LastCloudLogSnapshot.waterMl[cellIdx]) >= CloudLogWaterDeltaMl)
            {
                return true;
            }
        }
        return LastCloudLogSuccessMs == 0 || (millis() - LastCloudLogSuccessMs) >= CloudLogHeartbeatMs;
    }

    bool sendCloudLogNow(bool force)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            LastCloudLogOk = false;
            LastCloudLogHttpStatus = 0;
            setCloudLogMessage(F("wifi disconnected"));
            return false;
        }
        if (!hasCloudLogConfig())
        {
            LastCloudLogOk = false;
            LastCloudLogHttpStatus = 0;
            setCloudLogMessage(F("not configured"));
            return false;
        }
        if (!cloudLogReadingsReady())
        {
            LastCloudLogOk = false;
            LastCloudLogHttpStatus = 0;
            setCloudLogMessage(F("sensor readings not ready"));
            return false;
        }
        if (!TimeSynced && !syncTimeFromNtp())
        {
            LastCloudLogOk = false;
            LastCloudLogHttpStatus = 0;
            setCloudLogMessage(F("time not synced"));
            return false;
        }

        CloudLogSnapshot snapshot;
        captureCloudLogSnapshot(snapshot);
        if (!force && !shouldSendCloudLog(snapshot))
        {
            return false;
        }

        String body = F("{\"token\":\"");
        body += Config.cloudLogToken;
        body += F("\",\"sensor1\":");
        body += String(snapshot.moisturePercent[0], 1);
        body += F(",\"sensor2\":");
        body += String(snapshot.moisturePercent[1], 1);
        body += F(",\"sensor3\":");
        body += String(snapshot.moisturePercent[2], 1);
        body += F(",\"sensor4\":");
        body += String(snapshot.moisturePercent[3], 1);
        body += F(",\"relayMask\":");
        body += String(snapshot.relayMask);
        for (int zoneIdx = 0; zoneIdx < NrCells; ++zoneIdx)
        {
            body += F(",\"zone");
            body += zoneIdx + 1;
            body += F("Ml\":");
            body += String(snapshot.waterMl[zoneIdx], 1);
        }
        body += '}';

        noteCloudAttempt();
        LastCloudLogOk = sendHttpsPostJson(Config.cloudLogEndpoint, body);
        noteCloudResult(LastCloudLogOk);
        if (LastCloudLogOk)
        {
            LastCloudLogSnapshot = snapshot;
            LastCloudLogSuccessMs = millis();
        }
        return LastCloudLogOk;
    }

    void updateCloudLogger()
    {
        if (!OperationsStarted || DataGatheringActive)
        {
            return;
        }
        if (!cloudLogReadingsReady())
        {
            return;
        }
        if ((millis() - LastCloudLogEvaluateMs) < CloudLogEvaluateIntervalMs && LastCloudLogEvaluateMs != 0)
        {
            return;
        }

        LastCloudLogEvaluateMs = millis();
        sendCloudLogNow(false);
    }

    void printCloudLogStatus(Print& out)
    {
        out.print(F("Cloud logging configured: "));
        out.println(hasCloudLogConfig() ? F("yes") : F("no"));
        out.print(F("Endpoint configured: "));
        out.println(Config.cloudLogEndpoint[0] != '\0' ? F("yes") : F("no"));
        out.print(F("Token configured: "));
        out.println(Config.cloudLogToken[0] != '\0' ? F("yes") : F("no"));
        out.print(F("Time synced: "));
        out.println(TimeSynced ? F("yes") : F("no"));
        out.print(F("WiFi status: "));
        out.println(WiFi.status());
        out.print(F("Connectivity firmware: "));
        out.println(WiFi.firmwareVersion());
        out.print(F("Expected connectivity firmware: "));
        out.println(F(WIFI_FIRMWARE_LATEST_VERSION));
        if (Config.cloudLogEndpoint[0] != '\0')
        {
            out.print(F("Endpoint: "));
            out.println(Config.cloudLogEndpoint);
        }
        out.print(F("Last send ok: "));
        out.println(LastCloudLogOk ? F("yes") : F("no"));
        out.print(F("Last HTTP status: "));
        out.println(LastCloudLogHttpStatus);
        out.print(F("Last message: "));
        out.println(LastCloudLogMessage);
    }

    void sendCloudHistory(WiFiClient& client, const String& requestLine)
    {
        const int limit = constrain(queryIntValue(requestLine, "limit", 144), 1, 60000);
        if (WiFi.status() == WL_CONNECTED && hasCloudLogConfig())
        {
            if (streamHttpsGetBody(cloudHistoryUrl(limit), client, 2))
            {
                return;
            }
        }

        sendHttpHeaders(client, "application/json");
        client.println(F("[]"));
    }
}
