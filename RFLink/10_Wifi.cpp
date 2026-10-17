#if defined(RFLINK_WIFI_ENABLED)

#include "RFLink.h"
#include <Arduino.h>

#include "6_Credentials.h"
#include "10_Wifi.h"
#include "2_Signal.h"
#include "6_MQTT.h"
#ifndef RFLINK_PORTAL_DISABLED
#include "12_Portal.h"
#endif
#include <time.h> // for NTP
#ifdef ESP32
extern "C" {
  #include "bootloader_random.h"
}
#include <ESPmDNS.h>
#include "esp_sntp.h"
#else // NOT ESP32
  #include <DNSServer.h>
  #include <ESP8266mDNS.h>
  #include <ESP8266TrueRandom.h>
  #include <LwipDhcpServer-NonOS.h>
#endif // ESP32


namespace RFLink { namespace Wifi {

        namespace params {

            bool client_enabled;
            String client_ssid;
            String client_password;

            bool client_dhcp_enabled;
            String client_ip;
            String client_mask;
            String client_gateway;
            String client_dns;
            String client_hostname;

            bool AP_enabled;
            String AP_ssid;
            String AP_password;
            String AP_ip;
            String AP_network;
            String AP_mask;
        }

        namespace ntp {
          bool synchronized = false;
          bool required = false;
        }

        bool clientParamsHaveChanged = false; // this will be set to True when Client Wifi mode configuration has changed
        bool accessPointParamsHaveChanged = false; // this will be set to True when Client Wifi mode configuration has changed
        unsigned short int cpWifiChannel = 1; // channel for CaptivePortal (will be randomized later)
#ifdef ESP8266
        DNSServer captiveDnsServer;
        bool captiveDnsStarted = false;
        unsigned long captiveDnsLastRetry = 0;
#endif
        bool mdnsStarted = false;
        bool mdnsStartAfterSetupAccessPointShutdown = false;
        bool setupAccessPointShutdownRequested = false;
        unsigned long setupAccessPointClientConnectedAt = 0;
        const unsigned long SETUP_AP_SHUTDOWN_GRACE_MS = 1000;
        const int MAX_WIFI_SCAN_CACHE = 4;
        String cachedWifiSsids[MAX_WIFI_SCAN_CACHE];
        int32_t cachedWifiRssis[MAX_WIFI_SCAN_CACHE];
        bool cachedWifiSecure[MAX_WIFI_SCAN_CACHE];
        int cachedWifiNetworkCount = 0;

        // All json variable names
        const char json_name_client_enabled[] = "client_enabled";
        const char json_name_client_dhcp_enabled[] = "client_dhcp_enabled";
        const char json_name_client_ssid[] = "client_ssid";
        const char json_name_client_password[] = "client_password";
        const char json_name_client_ip[] = "client_ip";
        const char json_name_client_mask[] = "client_mask";
        const char json_name_client_gateway[] = "client_gateway";
        const char json_name_client_dns[] = "client_dns";
        const char json_name_client_hostname[] = "client_hostname";

        const char json_name_ap_enabled[] = "ap_enabled";
        const char json_name_ap_ssid[] = "ap_ssid";
        const char json_name_ap_password[] = "ap_password";
        const char json_name_ap_ip[] = "ap_ip";
        const char json_name_ap_network[] = "ap_network";
        const char json_name_ap_mask[] = "ap_mask";
// end of json variable names


        Config::ConfigItem configItems[] =  {
                Config::ConfigItem(json_name_client_enabled,      Config::SectionId::Wifi_id, false, clientParamsUpdatedCallback),
                Config::ConfigItem(json_name_client_dhcp_enabled, Config::SectionId::Wifi_id, true, clientParamsUpdatedCallback),
                Config::ConfigItem(json_name_client_ssid,         Config::SectionId::Wifi_id, "My Home Wifi", clientParamsUpdatedCallback),
                Config::ConfigItem(json_name_client_password,     Config::SectionId::Wifi_id, "inputyourown", clientParamsUpdatedCallback),
                Config::ConfigItem(json_name_client_ip,           Config::SectionId::Wifi_id, "192.168.0.200", clientParamsUpdatedCallback),
                Config::ConfigItem(json_name_client_mask,         Config::SectionId::Wifi_id, "255.255.255.0", clientParamsUpdatedCallback),
                Config::ConfigItem(json_name_client_gateway,      Config::SectionId::Wifi_id, "192.168.0.1", clientParamsUpdatedCallback),
                Config::ConfigItem(json_name_client_dns,          Config::SectionId::Wifi_id, "192.168.0.1", clientParamsUpdatedCallback),
                Config::ConfigItem(json_name_client_hostname,     Config::SectionId::Wifi_id, DEFAULT_WIFI_CLIENT_HOSTNAME, clientParamsUpdatedCallback),

                Config::ConfigItem(json_name_ap_enabled,  Config::SectionId::Wifi_id, true, accessPointParamsUpdatedCallback),
                Config::ConfigItem(json_name_ap_ssid,     Config::SectionId::Wifi_id, "RFLink-AP", accessPointParamsUpdatedCallback),
                Config::ConfigItem(json_name_ap_password, Config::SectionId::Wifi_id, "", accessPointParamsUpdatedCallback),
                Config::ConfigItem(json_name_ap_ip,       Config::SectionId::Wifi_id, "4.3.2.1", accessPointParamsUpdatedCallback),
                Config::ConfigItem(json_name_ap_network,  Config::SectionId::Wifi_id, "4.3.2.0", accessPointParamsUpdatedCallback),
                Config::ConfigItem(json_name_ap_mask,     Config::SectionId::Wifi_id, "255.255.255.0", accessPointParamsUpdatedCallback),

                Config::ConfigItem()
        };

        void refreshClientParametersFromConfig(bool triggerChanges=true) {

          Config::ConfigItem *item;
          bool changesDetected = false;

          item = Config::findConfigItem(json_name_client_enabled, Config::SectionId::Wifi_id);
          if( item->getBoolValue() != params::client_enabled) {
            changesDetected = true;
            params::client_enabled = item->getBoolValue();
          }

          item = Config::findConfigItem(json_name_client_dhcp_enabled, Config::SectionId::Wifi_id);
          if( item->getBoolValue() != params::client_dhcp_enabled) {
            changesDetected = true;
            params::client_dhcp_enabled = item->getBoolValue();
          }

          item = Config::findConfigItem(json_name_client_ssid, Config::SectionId::Wifi_id);
          if( params::client_ssid != item->getCharValue() ) {
            changesDetected = true;
            params::client_ssid = item->getCharValue();
          }

          item = Config::findConfigItem(json_name_client_password, Config::SectionId::Wifi_id);
          if( params::client_password != item->getCharValue() ) {
            changesDetected = true;
            params::client_password = item->getCharValue();
          }

          item = Config::findConfigItem(json_name_client_ip, Config::SectionId::Wifi_id);
          if( params::client_ip != item->getCharValue() ) {
            changesDetected = true;
            params::client_ip = item->getCharValue();
          }

          item = Config::findConfigItem(json_name_client_mask, Config::SectionId::Wifi_id);
          if( params::client_mask != item->getCharValue() ) {
            changesDetected = true;
            params::client_mask = item->getCharValue();
          }

          item = Config::findConfigItem(json_name_client_gateway, Config::SectionId::Wifi_id);
          if( params::client_gateway != item->getCharValue() ) {
            changesDetected = true;
            params::client_gateway = item->getCharValue();
          }

          item = Config::findConfigItem(json_name_client_dns, Config::SectionId::Wifi_id);
          if( params::client_dns != item->getCharValue() ) {
            changesDetected = true;
            params::client_dns = item->getCharValue();
          }

          item = Config::findConfigItem(json_name_client_hostname, Config::SectionId::Wifi_id);
          if( params::client_hostname != item->getCharValue() ) {
            changesDetected = true;
            params::client_hostname = item->getCharValue();
          }

          // Applying changes will happen in mainLoop()
          if(triggerChanges && changesDetected) {
            clientParamsHaveChanged = true;
            Serial.println(F("Client Wifi settings haver changed and will be applied at next loop"));
          }
        }

        void refreshAccessPointParametersFromConfig(bool triggerChanges=true) {

          Config::ConfigItem *item;
          bool changesDetected = false;

          item = Config::findConfigItem(json_name_ap_enabled, Config::SectionId::Wifi_id);
          if( item->getBoolValue() != params::AP_enabled) {
            changesDetected = true;
            params::AP_enabled = item->getBoolValue();
          }

          item = Config::findConfigItem(json_name_ap_ssid, Config::SectionId::Wifi_id);
          if( params::AP_ssid != item->getCharValue() ) {
            changesDetected = true;
            params::AP_ssid = item->getCharValue();
          }

          item = Config::findConfigItem(json_name_ap_password, Config::SectionId::Wifi_id);
          if( params::AP_password != item->getCharValue() ) {
            changesDetected = true;
            params::AP_password = item->getCharValue();
          }

          item = Config::findConfigItem(json_name_ap_ip, Config::SectionId::Wifi_id);
          if( params::AP_ip != item->getCharValue() ) {
            changesDetected = true;
            params::AP_ip = item->getCharValue();
          }

          item = Config::findConfigItem(json_name_ap_network, Config::SectionId::Wifi_id);
          if( params::AP_network != item->getCharValue() ) {
            changesDetected = true;
            params::AP_network = item->getCharValue();
          }

          item = Config::findConfigItem(json_name_ap_mask, Config::SectionId::Wifi_id);
          if( params::AP_mask != item->getCharValue() ) {
            changesDetected = true;
            params::AP_mask = item->getCharValue();
          }

          // Applying changes will happen in mainLoop()
          if(triggerChanges && changesDetected) {
            accessPointParamsHaveChanged = true;
            Serial.println(F("AP Wifi settings haver changed and will be applied at next loop"));
          }

        }


        void clientParamsUpdatedCallback() {
          refreshClientParametersFromConfig();
        }

        void accessPointParamsUpdatedCallback() {
          refreshAccessPointParametersFromConfig();
        }


        static String WIFI_PWR = String(WIFI_PWR_0);

#ifdef ESP8266
        IPAddress getAccessPointIP() {
          return IPAddress(4, 3, 2, 1);
        }

        IPAddress getAccessPointMask() {
          IPAddress mask(255, 255, 255, 0);
          mask.fromString(params::AP_mask);
          return mask;
        }

        void stopCaptiveDns() {
          if(captiveDnsStarted) {
            captiveDnsServer.stop();
            captiveDnsStarted = false;
          }
        }

        void startCaptiveDns() {
          stopCaptiveDns();
          captiveDnsServer.setErrorReplyCode(DNSReplyCode::NoError);
          if(captiveDnsServer.start(53, "*", WiFi.softAPIP())) {
            captiveDnsStarted = true;
            captiveDnsLastRetry = 0;
            Serial.printf_P(PSTR("Captive portal DNS started on %s\r\n"), WiFi.softAPIP().toString().c_str());
          } else {
            captiveDnsLastRetry = millis();
            Serial.println(F("Captive portal DNS start FAILED"));
          }
        }

        void addCaptivePortalDhcpOption(const DhcpServer&, DhcpServer::OptionsBuffer &options) {
          char capport[48];
          snprintf_P(capport, sizeof(capport), PSTR("http://%s/capport"), WiFi.softAPIP().toString().c_str());
          options.add(114, capport);
        }

        void startCaptivePortalDhcpOptions() {
          getNonOSDhcpServer().onSendOptions(addCaptivePortalDhcpOption);
        }

        void configureAccessPointNetwork() {
          IPAddress ip = getAccessPointIP();
          IPAddress mask = getAccessPointMask();
          if(!WiFi.softAPConfig(ip, ip, mask)) {
            Serial.println(F("WIFI AP network config FAILED"));
          }
        }
#endif

        bool startAccessPoint() {
#ifdef ESP8266
          configureAccessPointNetwork();
          startCaptivePortalDhcpOptions();
#endif
          bool ok = WiFi.softAP(params::AP_ssid.c_str(), params::AP_password.c_str(), cpWifiChannel);
#ifdef ESP8266
          if(ok) {
            startCaptiveDns();
          } else {
            stopCaptiveDns();
          }
#endif
          return ok;
        }

        void refreshWifiScanCache() {
          cachedWifiNetworkCount = 0;
          Serial.println(F("Scanning WiFi networks before setup AP service"));
          WiFiMode_t previousMode = WiFi.getMode();
          if((previousMode & WIFI_STA) == 0) {
            WiFi.mode(WIFI_STA);
            delay(100);
          }
          int count = WiFi.scanNetworks(false, true);
          if(count < 0) {
            Serial.println(F("WiFi scan failed"));
            WiFi.scanDelete();
            WiFi.mode(previousMode);
            return;
          }
          for(int i = 0; i < count; i++) {
            String ssid = WiFi.SSID(i);
            if(ssid.length() == 0)
              continue;
            int32_t rssi = WiFi.RSSI(i);
            bool secure = WiFi.encryptionType(i) != ENC_TYPE_NONE;
            int insertAt = cachedWifiNetworkCount;
            for(int j = 0; j < cachedWifiNetworkCount; j++) {
              if(rssi > cachedWifiRssis[j]) {
                insertAt = j;
                break;
              }
            }
            if(insertAt >= MAX_WIFI_SCAN_CACHE)
              continue;
            int limit = cachedWifiNetworkCount;
            if(limit >= MAX_WIFI_SCAN_CACHE)
              limit = MAX_WIFI_SCAN_CACHE - 1;
            for(int j = limit; j > insertAt; j--) {
              cachedWifiSsids[j] = cachedWifiSsids[j - 1];
              cachedWifiRssis[j] = cachedWifiRssis[j - 1];
              cachedWifiSecure[j] = cachedWifiSecure[j - 1];
            }
            cachedWifiSsids[insertAt] = ssid;
            cachedWifiRssis[insertAt] = rssi;
            cachedWifiSecure[insertAt] = secure;
            if(cachedWifiNetworkCount < MAX_WIFI_SCAN_CACHE)
              cachedWifiNetworkCount++;
          }
          WiFi.scanDelete();
          WiFi.mode(previousMode);
          Serial.printf_P(PSTR("Cached %i WiFi networks\r\n"), cachedWifiNetworkCount);
        }

        int getCachedWifiNetworkCount() {
          return cachedWifiNetworkCount;
        }

        bool getCachedWifiNetwork(int index, String &ssid, int32_t &rssi, bool &secure) {
          if(index < 0 || index >= cachedWifiNetworkCount)
            return false;
          ssid = cachedWifiSsids[index];
          rssi = cachedWifiRssis[index];
          secure = cachedWifiSecure[index];
          return true;
        }

        void stopAccessPoint() {
#ifdef ESP8266
          stopCaptiveDns();
#endif
#ifndef RFLINK_PORTAL_DISABLED
          RFLink::Portal::stopSetupServer();
#endif
          WiFi.softAPdisconnect();
        }

        void stopMdnsDiscovery() {
#if defined(ESP8266) || defined(ESP32)
          if(!mdnsStarted)
            return;
          MDNS.end();
          mdnsStarted = false;
#endif
        }

        bool persistAccessPointDisabledAfterClientConnect() {
          if(!params::AP_enabled)
            return true;

          Config::ConfigItem *item = Config::findConfigItem(json_name_ap_enabled, Config::SectionId::Wifi_id);
          if(item != nullptr && item->getBoolValue()) {
            item->setBoolValue(false);
            if(!Config::saveConfigToFlash()) {
              Serial.println(F("Failed to persist setup AP disabled state"));
              return false;
            }
          }
          params::AP_enabled = false;
          accessPointParamsHaveChanged = false;
          return true;
        }

        void shutdownSetupAccessPointIfReady() {
          if(!setupAccessPointShutdownRequested)
            return;
          if(!WiFi.isConnected()) {
            setupAccessPointClientConnectedAt = 0;
            return;
          }
          if((WiFi.getMode() & WIFI_AP) == 0)
            return;

          if(setupAccessPointClientConnectedAt == 0) {
            setupAccessPointClientConnectedAt = millis();
            return;
          }
          if(millis() - setupAccessPointClientConnectedAt < SETUP_AP_SHUTDOWN_GRACE_MS)
            return;

          Serial.println(F("WiFi setup completed; shutting down setup AP"));
          if(!persistAccessPointDisabledAfterClientConnect()) {
            Serial.println(F("Setup AP shutdown postponed because AP disabled state did not persist"));
            return;
          }
          stopMdnsDiscovery();
          stopAccessPoint();
          WiFi.enableAP(false);
          if(params::client_enabled)
            WiFi.mode(WIFI_STA);
          setupAccessPointShutdownRequested = false;
          setupAccessPointClientConnectedAt = 0;
          mdnsStartAfterSetupAccessPointShutdown = true;
        }

        void requestSetupAccessPointShutdown() {
          setupAccessPointShutdownRequested = true;
          setupAccessPointClientConnectedAt = 0;
          clientParamsHaveChanged = true;
        }



        void resetClientWifi() {
          if(params::client_enabled) {

            // We start by connecting to a WiFi network
            Serial.print(F("Trying to connect WIFI SSID "));
            Serial.print(params::client_ssid.c_str());
            Serial.println(F(". A status will be given whenever it occurs."));

#ifdef ESP32
            WiFi.setHostname(params::client_hostname.c_str());
#else
            WiFi.hostname(params::client_hostname);
#endif

            if( !params::client_dhcp_enabled) {
              IPAddress ip, gateway, mask, dns;

              ip.fromString(params::client_ip);
              gateway.fromString(params::client_gateway);
              mask.fromString(params::client_mask);
              dns.fromString(params::client_dns);

              WiFi.config(ip, gateway, mask, dns );

            } else {
              WiFi.config(IPAddress((uint32_t) 0), IPAddress((uint32_t) 0), IPAddress((uint32_t) 0));
            }

            delay(500);
            if(!WiFi.isConnected() || WiFi.SSID() != params::client_ssid) {
              WiFi.begin(params::client_ssid.c_str(), params::client_password.c_str());
            }

            WiFi.setAutoConnect(true);
            WiFi.setAutoReconnect(true);

          }
          else {
            Serial.println(F("WiFi Client mode will be disconnected"));
            WiFi.setAutoConnect(false);
            WiFi.setAutoReconnect(false);
            WiFi.disconnect();
          }

        }

        void start_WIFI()
        {
          if( params::AP_enabled && params::client_enabled) {
            WiFi.mode(WIFI_AP_STA);
          } else if(params::AP_enabled) {
            WiFi.mode(WIFI_AP);
          } else if (params::client_enabled) {
            WiFi.mode(WIFI_STA);
          }

          if( params::AP_enabled ) {
            refreshWifiScanCache();
            #ifdef ESP32
            WiFi.setHostname(params::client_hostname.c_str());
            #else
            WiFi.hostname(params::client_hostname);
            #endif
            Serial.printf_P(PSTR("* WIFI AP starting with SSID '%s' and band=%i... "), params::AP_ssid.c_str(), cpWifiChannel);
            if( !startAccessPoint())
              Serial.println(F("FAILED"));
            else {
              Serial.println(F("OK"));
#ifndef RFLINK_PORTAL_DISABLED
              RFLink::Portal::startSetupServer();
#endif
            }
          }
          else {
            // just in case it
            stopAccessPoint();
          }

          if(params::client_enabled) {
            resetClientWifi();
          }

        }

        void stop_WIFI()
        {
#ifdef ESP8266
          stopCaptiveDns();
#endif
          WiFi.disconnect();
          WiFi.mode(WIFI_OFF);
          delay(500);
        }

        void setup_WIFI_OFF()
        {
          WiFi.persistent(false);
          WiFi.setAutoReconnect(false);
#ifdef ESP8266
          WiFi.setSleepMode(WIFI_MODEM_SLEEP);
#endif
          WiFi.mode(WIFI_OFF);
#ifdef ESP8266
          WiFi.forceSleepBegin();
#endif
        }

        #ifdef ESP8266
        /*bool getLocalTime(struct tm * info, uint32_t ms=5000)
        {
            uint32_t start = millis();
            time_t now;
            while((millis()-start) <= ms) {
                time(&now);
                localtime_r(&now, info);
                if(info->tm_year > (2016 - 1900)){
                    return true;
                }
                delay(10);
            }
            return false;
        }*/
        #endif

        void printLocalTime(struct timeval *newTime = nullptr)
        {
          struct tm tmp;
          struct tm *timeinfo;

          if(newTime == nullptr) {
            if(!getLocalTime(&tmp)){
              Serial.println(F("Failed to obtain time"));
              return;
            }
            timeinfo = &tmp;
          }
          else {
            timeinfo = localtime(&newTime->tv_sec);
          }

          Serial.printf_P(PSTR("%04i-%02i-%02i %02i:%02i:%02i"),
                          timeinfo->tm_year+1900, timeinfo->tm_mon+1, timeinfo->tm_mday,
                          timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
        }

        
        void ntpUpdateCallback(struct timeval *newTime){

          if(timeAtBoot.tv_sec < 10000 ) {
            timeAtBoot.tv_sec = newTime->tv_sec - 10;
            printLocalTime(newTime);
          }

        }

        void reconnectServices() {
          #ifdef ESP32
          // the following was commented out so ESP8266 and ESP32 behave the same
          // in case NTP forces a time drift we need to recalculate timeAtBoot
          //sntp_set_time_sync_notification_cb(ntpUpdateCallback);
          #endif

          configTzTime("UTC", RFLink::params::ntpServer);

          #ifndef RFLINK_MQTT_DISABLED
          if(RFLink::Mqtt::params::enabled)
            RFLink::Mqtt::reconnect(1, true);
          #endif // RFLINK_MQTT_DISABLED
        }

        void startMdnsDiscovery() {
          if(!WiFi.isConnected() || mdnsStarted)
            return;

          String instanceName = params::client_hostname;
          if(instanceName.length() == 0)
            instanceName = F("RFBridge");

          if(!MDNS.begin(instanceName.c_str())) {
            Serial.println(F("mDNS discovery start FAILED"));
            return;
          }

          String mac = WiFi.macAddress();
          mac.replace(":", "");
          mac.toLowerCase();
          String id = F("rflink32_");
          id += mac;
          char swVersion[48];
          snprintf_P(swVersion, sizeof(swVersion), PSTR("%s"), RFLINK_FIRMWARE_VERSION);

          MDNS.addService("rfbridge", "tcp", RFLINK_API_PORT);
          MDNS.addServiceTxt("rfbridge", "tcp", "id", id.c_str());
          MDNS.addServiceTxt("rfbridge", "tcp", "name", "RF Bridge");
          MDNS.addServiceTxt("rfbridge", "tcp", "model", "Sonoff RF Bridge");
          MDNS.addServiceTxt("rfbridge", "tcp", "sw_version", swVersion);
          MDNS.addServiceTxt("rfbridge", "tcp", "hw_version", RFLINK_HARDWARE_NAME);
          MDNS.addServiceTxt("rfbridge", "tcp", "path", "/api/ws");
          mdnsStarted = true;
          Serial.println(F("mDNS discovery started as _rfbridge._tcp"));
        }

#ifdef ESP32
        void eventHandler_WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {

          Serial.print(F("Connected to AP SSID:"));
          for(int i=0; i<info.wifi_sta_connected.ssid_len; i++){
            Serial.print((char) info.wifi_sta_connected.ssid[i]);
          }

          Serial.print("  BSSID: ");
          for(int i=0; i<6; i++){
            Serial.printf("%02X", info.wifi_sta_connected.bssid[i]);

            if(i<5){
              Serial.print(":");
            }
          }

          Serial.print(F("  Channel: "));
          Serial.print(info.wifi_sta_connected.channel);

          Serial.print(F("  Auth mode: "));
          Serial.println(info.wifi_sta_connected.authmode);

        }

        void eventHandler_WiFiStationGotIp(WiFiEvent_t event, WiFiEventInfo_t info) {
          Serial.printf_P(PSTR("WiFi Client has received a new IP: %s\r\n"), WiFi.localIP().toString().c_str());
          if(params::client_enabled && WiFi.isConnected())
            requestSetupAccessPointShutdown();
          if((WiFi.getMode() & WIFI_AP) == 0)
            startMdnsDiscovery();
          shutdownSetupAccessPointIfReady();
          reconnectServices();
        }

        void eventHandler_WiFiStationLostIp(WiFiEvent_t event, WiFiEventInfo_t info) {
          Serial.println("WiFi Client has lost its IP");
        }
#endif //ESP32

#ifdef ESP8266
WiFiEventHandler e1;
void eventHandler_WiFiStationConnected(const WiFiEventSoftAPModeStationConnected& evt) {
  Serial.println(F("Connected to AP!"));
 
  Serial.print(F("SSID: "));
  Serial.println(WiFi.SSID());
     
  Serial.print(F("\r\nChannel: "));
  Serial.println(WiFi.channel());
    
}
WiFiEventHandler e2;
void eventHandler_WiFiStationGotIp(const WiFiEventStationModeGotIP& evt) {
  Serial.printf_P(PSTR("WiFi Client has received a new IP: %s\r\n"), WiFi.localIP().toString().c_str());
  Serial.flush();
#ifndef RFLINK_PORTAL_DISABLED
  RFLink::Portal::notifyNetworkReady();
#endif
  if(params::client_enabled && WiFi.isConnected())
    requestSetupAccessPointShutdown();
  if((WiFi.getMode() & WIFI_AP) == 0)
    startMdnsDiscovery();
  shutdownSetupAccessPointIfReady();
  reconnectServices();
}
WiFiEventHandler e3;
void eventHandler_WiFiStationDisconnected(const WiFiEventStationModeDisconnected& evt) {
  Serial.println(F("WiFi Client has been disconnected"));
}
#endif


        bool clientNetworkIsUp() {
          return WiFi.isConnected();
        }

        bool ntpIsSynchronized() {
          return ntp::synchronized;
        }

        void setup()
        {
          #ifdef ESP32
          bootloader_random_enable();
          cpWifiChannel = esp_random()%13 + 1;
          bootloader_random_disable();
          #else
          cpWifiChannel = ESP8266TrueRandom.random(1,13);
          #endif // ESP32

          refreshClientParametersFromConfig(false);
          refreshAccessPointParametersFromConfig(false);

#ifdef ESP32
          WiFi.onEvent(eventHandler_WiFiStationConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED );
          WiFi.onEvent(eventHandler_WiFiStationGotIp, ARDUINO_EVENT_WIFI_STA_GOT_IP );
          WiFi.onEvent(eventHandler_WiFiStationLostIp, ARDUINO_EVENT_WIFI_STA_LOST_IP );
#else
          e1 = WiFi.onSoftAPModeStationConnected(&eventHandler_WiFiStationConnected);
          e2 = WiFi.onStationModeGotIP(&eventHandler_WiFiStationGotIp);
          e3 = WiFi.onStationModeDisconnected(&eventHandler_WiFiStationDisconnected);
#endif

#ifdef ESP32
          WiFi.setTxPower(WIFI_POWER_11dBm);
#elif ESP8266
          WiFi.setSleepMode(WIFI_MODEM_SLEEP);
  WiFi.setOutputPower(WIFI_PWR.toInt());
#endif // ESP

          start_WIFI();
        }

        void mainLoop() {
#if defined(ESP8266) || defined(ESP32)
          if(mdnsStartAfterSetupAccessPointShutdown && WiFi.isConnected() && (WiFi.getMode() & WIFI_AP) == 0) {
            startMdnsDiscovery();
            if(mdnsStarted)
              mdnsStartAfterSetupAccessPointShutdown = false;
          }
          if(mdnsStarted) {
            MDNS.update();
          }
#endif
#ifdef ESP8266
          if(captiveDnsStarted) {
            captiveDnsServer.processNextRequest();
          } else if(params::AP_enabled && !setupAccessPointShutdownRequested && WiFi.softAPIP() != IPAddress(0, 0, 0, 0) && (captiveDnsLastRetry == 0 || millis() - captiveDnsLastRetry > 3000)) {
            startCaptiveDns();
          }
#endif

          if(!ntp::synchronized && WiFi.isConnected()) {
            time_t now;
            struct tm timeinfo;

            time(&now);
            localtime_r(&now, &timeinfo);
            // Is time set? If not, tm_year will be (1970 - 1900).
            if (timeinfo.tm_year < (2022 - 1900)) {
              //Serial.println("NTP not synced");
            } else {
              timeAtBoot.tv_sec = now;
              ntp::synchronized = true;
              Serial.print(F("NTP synchronized: "));
              printLocalTime();
              Serial.println();
            }
          }

          if(accessPointParamsHaveChanged) {
            accessPointParamsHaveChanged = false;

            if(setupAccessPointShutdownRequested && WiFi.isConnected()) {
              Serial.println(F("Skipping AP setting apply while setup AP shutdown is pending"));
              shutdownSetupAccessPointIfReady();
              return;
            }

            Serial.println(F("Applying new Wifi AP settings"));

            #ifdef ESP32
            bool isEnabled = ((WiFi.getMode() & WIFI_MODE_AP) != 0);

            if(params::AP_enabled) {
              if(!isEnabled)
                WiFi.enableAP(true);

              if( !startAccessPoint() )
                Serial.println("WIFI AP start FAILED");
              else {
                Serial.println("WIFI AP started");
#ifndef RFLINK_PORTAL_DISABLED
                RFLink::Portal::startSetupServer();
#endif
              }
            } else {
              Serial.println("Shutting down AP");
              stopAccessPoint();
              WiFi.enableAP(false);
            }
            #endif
            #ifdef ESP8266
            if(params::AP_enabled) {
                Serial.printf_P(PSTR("* WIFI AP starting with SSID '%s'... "), params::AP_ssid.c_str());
                WiFi.enableAP(true);
                if( !startAccessPoint() )
                  Serial.println(F("WIFI AP start FAILED"));
                else {
                  Serial.println(F("WIFI AP started"));
#ifndef RFLINK_PORTAL_DISABLED
                  RFLink::Portal::startSetupServer();
#endif
                }
            } else {
              Serial.println(F("Shutting down AP"));
              stopAccessPoint();
              WiFi.enableAP(false);
            }
            #endif
          } // end of accessPointParamsHaveChanges

          if( clientParamsHaveChanged ) {
            clientParamsHaveChanged = 0;
            resetClientWifi();
          }

          shutdownSetupAccessPointIfReady();

#ifndef RFLINK_PORTAL_DISABLED
          if((WiFi.getMode() & WIFI_AP) == 0)
            RFLink::Portal::stopSetupServer();
#endif

        } // end of mainLoop()

        void getStatusJsonString(JsonObject &output) {

          auto && network = output.createNestedObject(F("network"));

          auto && wifi_ap = network.createNestedObject(F("wifi_ap"));

          if((WiFi.getMode() & WIFI_AP) != 0) {
            wifi_ap[F("status")] = F("enabled");
          } else {
            wifi_ap[F("status")] = F("disabled");
          }

          auto && wifi_client = network.createNestedObject(F("wifi_client"));

          if(params::client_enabled) {
            if(WiFi.isConnected()) {
              wifi_client[F("status")] = F("connected");
              wifi_client[F("ip")] = WiFi.localIP().toString();
              wifi_client[F("netmask")] = WiFi.subnetMask().toString();
              wifi_client[F("dns")] = WiFi.dnsIP().toString();
              wifi_client[F("hostname")] = params::client_hostname;
            } else {
              wifi_client[F("status")] = F("disconnected");
            }
          } else {
            wifi_client[F("status")] = F("disabled");
          }

        }

    } // end of Wifi namespace


} // end RFLink namespace

#endif // RFLINK_WIFI_ENABLED
