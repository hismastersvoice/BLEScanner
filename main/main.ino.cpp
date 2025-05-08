# 1 "C:\\Users\\dieter\\AppData\\Local\\Temp\\tmp9dfwpked"
#include <Arduino.h>
# 1 "E:/BLEScanner/main/main.ino"
#include "main.h"

#include <BLEDevice.h>
#include <sstream>
#include <iomanip>
#include "WiFiManager.h"

#include "config.h"

#define CONFIG_ESP32_DEBUG_OCDAWARE 1

#include "esp_system.h"
#include "DebugPrint.h"

#if ENABLE_OTA_WEBSERVER
#include "OTAWebServer.h"
#endif

#include "settings.h"
#include "watchdog.h"

#include "SPIFFSLogger.h"
#include "utility.h"

#include "mqtt_client.h"
#include "udp_client.h"

#if USE_FHEM_LEPRESENCE_SERVER
#include "fhem_lepresence_server.h"
#endif

#include "NTPTime.h"

MyRWMutex trackedDevicesMutex;
std::vector<BLETrackedDevice> BLETrackedDevices;
std::map<std::string, bool> FastDiscovery;

BLEScan *pBLEScan;

#define SYS_INFORMATION_DELAY 120
unsigned long lastSySInfoTime = 0;

#if ENABLE_OTA_WEBSERVER
OTAWebServer webserver;
#endif

extern "C"
{
  void vApplicationMallocFailedHook(void);
  void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName);
}
void vApplicationMallocFailedHook(void);
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName);
void ForceBatteryRead(const char *normalizedAddr);
void batteryTask();
bool batteryLevel(const char address[ADDRESS_STRING_SIZE], esp_ble_addr_type_t addressType, int8_t &battLevel, bool &hasBatteryService);
void LogResetReason();
void setup();
void loop();
#line 53 "E:/BLEScanner/main/main.ino"
void vApplicationMallocFailedHook(void)
{
  DEBUG_PRINTLN("---MallocFailed----");
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  DEBUG_PRINTF("StackOverflow:%x (%s)\n", xTask, pcTaskName);
}




class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{

  void onResult(BLEAdvertisedDevice advertisedDevice) override
  {
    Watchdog::Feed();
    const uint8_t shortNameSize = 31;
    char address[ADDRESS_STRING_SIZE];
    NormalizeAddress(*(advertisedDevice.getAddress().getNative()), address);

    if (!SettingsMngr.IsTraceable(address))
      return;

    char shortName[shortNameSize];
    memset(shortName, 0, shortNameSize);
    if (advertisedDevice.haveName())
      strncpy(shortName, advertisedDevice.getName().c_str(), shortNameSize - 1);

    int RSSI = advertisedDevice.getRSSI();

    CRITICALSECTION_WRITESTART(trackedDevicesMutex)
    for (auto &trackedDevice : BLETrackedDevices)
    {
      if (strcmp(address, trackedDevice.address) == 0)
      {
#if NUM_OF_ADVERTISEMENT_IN_SCAN > 1
        trackedDevice.advertisementCounter++;


        if (trackedDevice.advertisementCounter != NUM_OF_ADVERTISEMENT_IN_SCAN)
          return;
#endif

        if (!trackedDevice.advertised)
        {
          trackedDevice.addressType = advertisedDevice.getAddressType();
          trackedDevice.advertised = true;
          trackedDevice.lastDiscoveryTime = NTPTime::seconds();
          trackedDevice.rssiValue = RSSI;
          if (!trackedDevice.isDiscovered)
          {
            trackedDevice.isDiscovered = true;
            trackedDevice.connectionRetry = 0;
            FastDiscovery[trackedDevice.address] = true;
            DEBUG_PRINTF("INFO: Tracked device discovered again, Address: %s , RSSI: %d\n", address, RSSI);
            if (advertisedDevice.haveName())
            {
              LOG_TO_FILE_D("Device %s ( %s ) within range, RSSI: %d ", address, shortName, RSSI);
            }
            else
              LOG_TO_FILE_D("Device %s within range, RSSI: %d ", address, RSSI);
          }
          else
          {
            DEBUG_PRINTF("INFO: Tracked device discovered, Address: %s , RSSI: %d\n", address, RSSI);
          }
        }
        return;
      }
    }


    BLETrackedDevice trackedDevice;
    trackedDevice.advertised = NUM_OF_ADVERTISEMENT_IN_SCAN <= 1;
    memcpy(trackedDevice.address, address, ADDRESS_STRING_SIZE);
    trackedDevice.addressType = advertisedDevice.getAddressType();
    trackedDevice.isDiscovered = NUM_OF_ADVERTISEMENT_IN_SCAN <= 1;
    trackedDevice.lastDiscoveryTime = NTPTime::seconds();
    trackedDevice.lastBattMeasureTime = 0;
    trackedDevice.batteryLevel = -1;
    trackedDevice.hasBatteryService = true;
    trackedDevice.connectionRetry = 0;
    trackedDevice.rssiValue = RSSI;
    trackedDevice.advertisementCounter = 1;
    BLETrackedDevices.push_back(std::move(trackedDevice));
    FastDiscovery[trackedDevice.address] = true;
#if NUM_OF_ADVERTISEMENT_IN_SCAN > 1


    return;
#endif
    CRITICALSECTION_WRITEEND;

    DEBUG_PRINTF("INFO: Device discovered, Address: %s , RSSI: %d\n", address, RSSI);
    if (advertisedDevice.haveName())
      LOG_TO_FILE_D("Discovered new device %s ( %s ) within range, RSSI: %d ", address, shortName, RSSI);
    else
      LOG_TO_FILE_D("Discovered new device %s within range, RSSI: %d ", address, RSSI);
  }
};

static BLEUUID service_BATT_UUID(BLEUUID((uint16_t)0x180F));
static BLEUUID char_BATT_UUID(BLEUUID((uint16_t)0x2A19));

#if PUBLISH_BATTERY_LEVEL
class MyBLEClientCallBack : public BLEClientCallbacks
{
  void onConnect(BLEClient *pClient)
  {
  }

  virtual void onDisconnect(BLEClient *pClient)
  {
    log_i(" >> onDisconnect callback");
    pClient->disconnect();
  }
};

void ForceBatteryRead(const char *normalizedAddr)
{
  for (auto &trackedDevice : BLETrackedDevices)
  {
    if (strcmp(trackedDevice.address, normalizedAddr) == 0)
    {
      trackedDevice.forceBatteryRead = true;
      return;
    }
  }
}

void batteryTask()
{


  for (auto &trackedDevice : BLETrackedDevices)
  {
    if (!(SettingsMngr.InBatteryList(trackedDevice.address) || trackedDevice.forceBatteryRead))
      continue;



    unsigned long BatteryReadTimeout = trackedDevice.lastBattMeasureTime + BATTERY_READ_PERIOD;
    unsigned long BatteryRetryTimeout = trackedDevice.lastBattMeasureTime + BATTERY_RETRY_PERIOD;
    unsigned long now = NTPTime::getTimeStamp();
    bool batterySet = trackedDevice.batteryLevel > 0;
    if (trackedDevice.advertised && trackedDevice.hasBatteryService && trackedDevice.rssiValue > -90 &&
        ((batterySet && (BatteryReadTimeout < now)) ||
         (!batterySet && (BatteryRetryTimeout < now)) ||
         trackedDevice.forceBatteryRead))
    {
      DEBUG_PRINTF("\nReading Battery level for %s: Retries: %d\n", trackedDevice.address, trackedDevice.connectionRetry);
      bool connectionEstablished = batteryLevel(trackedDevice.address, trackedDevice.addressType, trackedDevice.batteryLevel, trackedDevice.hasBatteryService);
      if (connectionEstablished || !trackedDevice.hasBatteryService)
      {
        log_i("Device %s has battery service: %s", trackedDevice.address, trackedDevice.hasBatteryService ? "YES" : "NO");
        trackedDevice.connectionRetry = 0;
        trackedDevice.lastBattMeasureTime = now;
      }
      else
      {
        trackedDevice.connectionRetry++;
        if (trackedDevice.connectionRetry >= MAX_BLE_CONNECTION_RETRIES)
        {
          trackedDevice.connectionRetry = 0;
          trackedDevice.lastBattMeasureTime = now;

          LOG_TO_FILE_E("Error: Connection to device %s failed", trackedDevice.address);
          DEBUG_PRINTF("Error: Connection to device %s failed", trackedDevice.address);
        }
        else
        {

          LOG_TO_FILE_D("Error: Connection to device %s failed", trackedDevice.address);
          DEBUG_PRINTF("Error: Connection to device %s failed", trackedDevice.address);
        }
      }
    }
    else if (BatteryReadTimeout < now)
    {


      trackedDevice.batteryLevel = -1;
    }
    trackedDevice.forceBatteryRead = false;
    Watchdog::Feed();
  }

}

bool batteryLevel(const char address[ADDRESS_STRING_SIZE], esp_ble_addr_type_t addressType, int8_t &battLevel, bool &hasBatteryService)
{
  log_i(">> ------------------batteryLevel----------------- ");
  bool bleconnected;
  BLEClient client;
  battLevel = -1;
  static char canonicalAddress[ADDRESS_STRING_SIZE + 5];
  CanonicalAddress(address, canonicalAddress);
  BLEAddress bleAddress = BLEAddress(canonicalAddress);
  log_i("connecting to : %s", bleAddress.toString().c_str());
  LOG_TO_FILE_D("Reading battery level for device %s", address);
  MyBLEClientCallBack callback;
  client.setClientCallbacks(&callback);


  bleconnected = client.connect(bleAddress, addressType);
  if (bleconnected)
  {
    log_i("Connected to server");
    BLERemoteService *pRemote_BATT_Service = client.getService(service_BATT_UUID);
    if (pRemote_BATT_Service == nullptr)
    {
      log_i("Cannot find the BATTERY service.");
      LOG_TO_FILE_E("Error: Cannot find the BATTERY service for the device %s", address);
      hasBatteryService = false;
    }
    else
    {
      BLERemoteCharacteristic *pRemote_BATT_Characteristic = pRemote_BATT_Service->getCharacteristic(char_BATT_UUID);
      if (pRemote_BATT_Characteristic == nullptr)
      {
        log_i("Cannot find the BATTERY characteristic.");
        LOG_TO_FILE_E("Error: Cannot find the BATTERY characteristic for device %s", address);
        hasBatteryService = false;
      }
      else
      {
        std::string value = pRemote_BATT_Characteristic->readValue();
        if (value.length() > 0)
          battLevel = (int8_t)value[0];
        log_i("Reading BATTERY level : %d", battLevel);
        LOG_TO_FILE_I("Battery level for device %s is %d", address, battLevel);
        hasBatteryService = true;
      }
    }


    if (client.isConnected())
    {
      log_i("disconnecting...");
      client.disconnect();
    }
    log_i("waiting for disconnection...");
    while (client.isConnected())
      delay(100);
    log_i("Client disconnected.");
  }
  else
  {

    BLEDevice::removePeerDevice(client.m_appId, true);
    log_i("-------------------Not connected!!!--------------------");
  }

  log_i("<< ------------------batteryLevel----------------- ");
  return bleconnected;
}
#endif

void LogResetReason()
{
  esp_reset_reason_t r = esp_reset_reason();
  char *msg;
  switch (r)
  {
  case ESP_RST_POWERON:
    msg = "Reset due to power-on event";
    break;
  case ESP_RST_EXT:
    msg = "Reset by external pin";
    break;
  case ESP_RST_SW:
    msg = " Software reset via esp_restart";
    break;
  case ESP_RST_PANIC:
    msg = "Software reset due to exception/panic";
    break;
  case ESP_RST_INT_WDT:
    msg = "Reset (software or hardware) due to interrupt watchdog";
    break;
  case ESP_RST_TASK_WDT:
    msg = "Reset due to task watchdog";
    break;
  case ESP_RST_WDT:
    msg = "Reset due to other watchdogs";
    break;
  case ESP_RST_DEEPSLEEP:
    msg = "Reset after exiting deep sleep mode";
    break;
  case ESP_RST_BROWNOUT:
    msg = "Brownout reset (software or hardware)";
    break;
  case ESP_RST_SDIO:
    msg = "Reset over SDIO";
    break;
  case ESP_RST_UNKNOWN:
  default:
    msg = "Reset reason can not be determined";
    break;
  }
  DEBUG_PRINTLN(msg);
  LOG_TO_FILE_E(msg);
}




void setup()
{
#if defined(DEBUG_SERIAL)
  Serial.begin(115200);
  while (!Serial)
    ;
#endif

  Serial.println("INFO: Running setup");

#if defined(_SPIFFS_H_)
  if (!SPIFFS.begin())
  {
    if (!SPIFFS.format())
      DEBUG_PRINTLN("Failed to initialize SPIFFS");
  }
#endif

#if ERASE_DATA_AFTER_FLASH
  int dataErased = 0;
  String ver = Firmware::FullVersion();
  if (ver != Firmware::readVersion())
  {
    dataErased++;
    if (SPIFFS.format())
      dataErased++;
    Firmware::writeVersion();
  }
#endif

  const char *settingsFile = "/settings.bin";
  SettingsMngr.SettingsFile(settingsFile);
  if (SPIFFS.exists(settingsFile))
    SettingsMngr.Load();

#if ENABLE_FILE_LOG
  SPIFFSLogger.Initialize("/logs.bin", MAX_NUM_OF_SAVED_LOGS);
  SPIFFSLogger.setLogLevel(SPIFFSLoggerClass::LogLevel(SettingsMngr.logLevel));
#endif

#if ENABLE_OTA_WEBSERVER
  webserver.setup(SettingsMngr.gateway);
#endif

  if (SettingsMngr.wifiSSID.isEmpty())
  {
    WiFiManager::StartAccessPointMode();
  }
  else
  {
    WiFiManager::WiFiConnect(SettingsMngr.wifiSSID, SettingsMngr.wifiPwd);
  }

  LogResetReason();

#if ERASE_DATA_AFTER_FLASH
  if (dataErased == 1)
    LOG_TO_FILE_E("Error Erasing all persitent data!");
  else if (dataErased == 2)
    LOG_TO_FILE_I("Erased all persitent data!");
#endif

#if ENABLE_OTA_WEBSERVER
  webserver.begin();
#endif

  BLETrackedDevices.reserve(SettingsMngr.GetMaxNumOfTraceableDevices());
  for (const auto &dev : SettingsMngr.GetKnownDevicesList())
  {
    BLETrackedDevice trackedDevice;
    memcpy(trackedDevice.address, dev.address, ADDRESS_STRING_SIZE);
    trackedDevice.forceBatteryRead = dev.readBattery;
    BLETrackedDevices.push_back(std::move(trackedDevice));
  }

  Watchdog::Initialize();

  if (!WiFiManager::IsAccessPointModeOn())
  {
    BLEDevice::init(SettingsMngr.gateway.c_str());
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), NUM_OF_ADVERTISEMENT_IN_SCAN > 1);
    pBLEScan->setActiveScan(ACTIVE_SCAN);
    pBLEScan->setInterval(50);
    pBLEScan->setWindow(50);

    if (SettingsMngr.mqttEnabled)
    {
      MQTTClient::initializeMQTT();
      MQTTClient::connectToMQTT();
      if (SettingsMngr.haEnabled)
      {
        if (MQTTClient::connectToMQTT())
        {
          MQTTClient::publishTrackerDeviceDiscovery();
          MQTTClient::publishDevicesListSensorDiscovery();

          MQTTClient::publishTrackerStatus();
          MQTTClient::publishDevicesList();
        }
      }
    }

    if (SettingsMngr.udpEnabled)
    {
      UDPClient::initializeUDP();
    }

#if USE_FHEM_LEPRESENCE_SERVER
    FHEMLePresenceServer::initializeServer();
#endif
  }

  LOG_TO_FILE("BLETracker initialized");
}

char *formatMillis(unsigned long milliseconds, char outstr[20])
{
  unsigned long seconds = milliseconds / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  snprintf(outstr, 20, "%d.%02d:%02d:%02d", days, hours % 24, minutes % 60, seconds % 60);
  return outstr;
}

std::function<void(const BLETrackedDevice &)> publishBLEState = [](const BLETrackedDevice &device)
{ MQTTClient::publishBLEState(device); };
std::function<void(void)> publishSySInfo = MQTTClient::publishSySInfo;

std::function<void(const BLETrackedDevice &)> publishBLEStateUDP = UDPClient::publishBLEStateUDP;
std::function<void(void)> publishSySInfoUDP = UDPClient::publishSySInfoUDP;

void loop()
{
  try
  {
    if (WiFiManager::IsAccessPointModeOn())
    {
      WiFiManager::CheckAPModeTimeout();
    }
    else
    {
      if (SettingsMngr.mqttEnabled)
      {
        MQTTClient::mqttLoop();
      }
    }

#if USE_FHEM_LEPRESENCE_SERVER

    if (!SettingsMngr.wifiSSID.isEmpty())
    {
      WiFiManager::WiFiConnect(SettingsMngr.wifiSSID, SettingsMngr.wifiPwd);
    }
#endif

    Watchdog::Feed();

    Serial.println("INFO: Running mainloop");
    DEBUG_PRINTF("Main loop Free heap: %u\n", xPortGetFreeHeapSize());
    DEBUG_PRINTF("Number device discovered: %d\n", BLETrackedDevices.size());

    if (BLETrackedDevices.size() == SettingsMngr.GetMaxNumOfTraceableDevices())
    {
      const char *errMsg = "Restarting: reached the max number of traceable devices";
      DEBUG_PRINTLN(errMsg);
      LOG_TO_FILE(errMsg);
      esp_restart();
    }

    if (!WiFiManager::IsAccessPointModeOn())
    {
#if PROGRESSIVE_SCAN
      bool scanCompleted = false;
#endif
      bool scanEnabled = SettingsMngr.IsManualScanOn() || !SettingsMngr.IsManualScanEnabled();

      if (scanEnabled)
      {
#if PROGRESSIVE_SCAN
        static uint32_t elapsedScanTime = 0;
        static uint32_t lastScanTime = 0;

        bool continuePrevScan = elapsedScanTime > 0;
        if (!continuePrevScan)
        {

          for (auto &trackedDevice : BLETrackedDevices)
          {
            trackedDevice.advertised = false;
            trackedDevice.rssiValue = -100;
            trackedDevice.advertisementCounter = 0;
          }
        }

        lastScanTime = NTPTime::seconds();
        pBLEScan->start(1, continuePrevScan);
        pBLEScan->stop();
        elapsedScanTime += NTPTime::seconds() - lastScanTime;
        scanCompleted = elapsedScanTime > SettingsMngr.scanPeriod;
        if (scanCompleted)
        {
          elapsedScanTime = 0;
          pBLEScan->clearResults();
        }
#else

        for (auto &trackedDevice : BLETrackedDevices)
        {
          trackedDevice.advertised = false;
          trackedDevice.rssiValue = -100;
          trackedDevice.advertisementCounter = 0;
        }


        pBLEScan->start(SettingsMngr.scanPeriod);
        pBLEScan->stop();
        pBLEScan->clearResults();

#endif
      }
      else
      {
        for (auto &trackedDevice : BLETrackedDevices)
        {
          trackedDevice.advertised = false;
          trackedDevice.rssiValue = -100;
          trackedDevice.advertisementCounter = 0;
        }
        pBLEScan->clearResults();
      }

      for (auto &trackedDevice : BLETrackedDevices)
      {
        if (trackedDevice.isDiscovered && (trackedDevice.lastDiscoveryTime + SettingsMngr.maxNotAdvPeriod) < NTPTime::seconds())
        {
          trackedDevice.isDiscovered = false;
          FastDiscovery[trackedDevice.address] = false;
          LOG_TO_FILE_D("Devices %s is gone out of range", trackedDevice.address);
        }
      }

#if PUBLISH_BATTERY_LEVEL
#if PROGRESSIVE_SCAN
      if (scanCompleted)
#endif
        batteryTask();
#endif

      if (SettingsMngr.mqttEnabled || SettingsMngr.udpEnabled)
      {
        bool publishSystemInfo = ((lastSySInfoTime + SYS_INFORMATION_DELAY) < NTPTime::seconds()) || (lastSySInfoTime == 0);

        if (scanEnabled || publishSystemInfo)
        {
          for (auto &trackedDevice : BLETrackedDevices)
          {

            if (SettingsMngr.mqttEnabled)
            {
              publishBLEState(trackedDevice);
            }
            if (SettingsMngr.udpEnabled)
            {
              publishBLEStateUDP(trackedDevice);
            }
          }
        }


        if (publishSystemInfo)
        {
          if (SettingsMngr.mqttEnabled)
          {
            publishSySInfo();
          }

          if (SettingsMngr.udpEnabled)
          {
            publishSySInfoUDP();
          }
          lastSySInfoTime = NTPTime::seconds();
        }
      }

#if USE_FHEM_LEPRESENCE_SERVER
      FHEMLePresenceServer::loop();
#endif
    }
  }
  catch (std::exception &e)
  {
    const char *errMsg = "Error Caught Exception %s";
    DEBUG_PRINTF(errMsg, e.what());
    LOG_TO_FILE_E(errMsg, e.what());
  }
  catch (...)
  {
    const char *errMsg = "Error Unhandled exception trapped in main loop";
    DEBUG_PRINTLN(errMsg);
    LOG_TO_FILE_E(errMsg);
  }

  delay(100);
}