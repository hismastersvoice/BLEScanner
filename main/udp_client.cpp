#include "udp_client.h"
#include <WiFiUdp.h>
#include "settings.h"
#include "DebugPrint.h"
#include "WiFiManager.h"

#define UDP_PORT 7000
namespace UDPClient
{
    static WiFiUDP udpClient;

    void initializeUDP()
    {
        udpClient.begin(SettingsMngr.udpPort);
        DEBUG_PRINTLN("UDP client started");
    }

    void publishBLEStateUDP(const BLETrackedDevice &device)
    {

        char payload[150];
        int rssi = device.isDiscovered ? device.rssiValue : -100;
        unsigned long now = NTPTime::getTimeStamp();

#if PUBLISH_BATTERY_LEVEL
        snprintf(payload, sizeof(payload), "module=%s.%s %s=%d rssi=%d battery=%d timestamp=%lu",
        SettingsMngr.location, SettingsMngr.gateway.c_str(), device.address, device.isDiscovered, device.rssiValue, device.batteryLevel, now);
#else
        snprintf(payload, sizeof(payload), "module=%s.%s %s=%s rssi=%d timestamp=%lu",
        SettingsMngr.location, SettingsMngr.gateway.c_str(), device.address, device.isDiscovered, device.rssiValue, now);
#endif

        udpClient.beginPacket(SettingsMngr.udpServer.c_str(), SettingsMngr.udpPort);
        udpClient.write((uint8_t *)payload, strlen(payload));
        udpClient.endPacket();
        DEBUG_PRINTLN(payload);
    }

    void publishSySInfoUDP()
    {
        const size_t ssidlen = SettingsMngr.wifiSSID.length() + 1;
        unsigned long now = NTPTime::getTimeStamp();
        long rssi = WiFi.RSSI();
        static String IP;
        IP.reserve(16);
        IP = WiFi.localIP().toString();

        char strmilli[20];

        const size_t maxPayloadSize = ssidlen + 200;
        char payload[maxPayloadSize];

        uint8_t mac[6];
        WiFi.macAddress(mac);
        snprintf(payload, maxPayloadSize, "module=%s.%s alive uptime=%s SSID=%s rssi=%d IP=%s MAC=%02X:%02X:%02X:%02X:%02X:%02X timestamp=%lu",
            SettingsMngr.location, SettingsMngr.gateway.c_str(), formatMillis(millis(), strmilli), SettingsMngr.wifiSSID.c_str(), rssi, IP.c_str(),
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], now);

        udpClient.beginPacket(SettingsMngr.udpServer.c_str(), SettingsMngr.udpPort);
        udpClient.write((uint8_t *)payload, strlen(payload));
        udpClient.endPacket();
        DEBUG_PRINTLN(payload);
    }
}
