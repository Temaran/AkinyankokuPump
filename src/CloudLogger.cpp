#include "FirmwareServices.h"

#include <math.h>

namespace GardenPump
{
    namespace
    {
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
                    if (line.length() < 220)
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
            remote.print(body);

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
            remote.println();

            const String statusLine = readHttpLine(remote, CloudLogHttpTimeoutMs);
            const int status = httpStatusCode(statusLine);
            String location;

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
            const unsigned long start = millis();
            while ((remote.connected() || remote.available()) && (millis() - start) < CloudLogHttpTimeoutMs)
            {
                while (remote.available())
                {
                    client.write(static_cast<uint8_t>(remote.read()));
                }
                delay(1);
            }
            remote.stop();
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

        LastCloudLogOk = sendHttpsPostJson(Config.cloudLogEndpoint, body);
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
