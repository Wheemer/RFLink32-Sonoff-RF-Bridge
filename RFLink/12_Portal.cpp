#include "RFLink.h"

#ifndef RFLINK_PORTAL_DISABLED

#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <LittleFS.h>

#include "2_Signal.h"
#include "3_Serial.h"
#include "6_MQTT.h"
#include "10_Wifi.h"
#include "11_Config.h"
#include "12_Portal.h"

#if defined(ESP8266)
#include "ESP8266WiFi.h"
#include "ESPAsyncTCP.h"
#include "flash_hal.h"
#include "FS.h"
#elif defined(ESP32)
#include "AsyncTCP.h"
#endif
#include <LittleFS.h>

namespace RFLink { namespace Portal {

        const char json_name_enabled[] = "enabled";

        Config::ConfigItem configItems[] =  {
                Config::ConfigItem(json_name_enabled,      Config::SectionId::Portal_id, true, paramsUpdatedCallback),
                Config::ConfigItem(), // dont remove it!
        };

        namespace params {
          bool enabled = true;
        }

        AsyncWebServer setupServer(80);
        AsyncWebServer apiServer(RFLINK_API_PORT);
        bool setupServerStarted = false;
        bool apiServerStarted = false;
        void serveWifiSetupPage(AsyncWebServerRequest *request);
        bool requestIsFromAccessPoint(AsyncWebServerRequest *request);
        bool checkPageAuthentication(AsyncWebServerRequest *request);

        static const char APP_JS[] PROGMEM = R"JS((function(){function esc(s){return String(s||'').replace(/[&<>]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;'}[c]})}function toast(type,msg){var r=document.getElementById('toast-root');if(!r)return;var el=document.createElement('div');el.className='toast-container toast-'+type;el.innerHTML='<button class="toast-close" type="button">x</button><p class="toast-title">'+type.charAt(0).toUpperCase()+type.slice(1)+'</p><p class="toast-message">'+esc(msg)+'</p><div class="toast-bar"></div>';r.appendChild(el);var close=function(){if(el.parentNode)el.parentNode.removeChild(el)};el.onclick=close;setTimeout(close,type==='success'?5000:10000)}document.addEventListener('submit',function(e){var f=e.target;if(!f.matches('form[data-async-save],form[data-async-reboot]'))return;e.preventDefault();var b=f.querySelector('[type=submit]');if(b)b.disabled=true;var opts={method:f.method||'POST',credentials:'same-origin'};if(f.matches('form[data-async-save]')){var data=new FormData(f);data.append('async','1');opts.body=data}fetch(f.action,opts).then(function(resp){return resp.text().then(function(t){return {ok:resp.ok,text:t}})}).then(function(r){if(r.ok)toast('success',r.text||'Saved');else toast('error',r.text||'Request failed')}).catch(function(err){toast('error','A network error occured: '+err)}).finally(function(){if(b)b.disabled=false})},false)})();)JS";

        void serveStaticJs(AsyncWebServerRequest *request) {
          if(!checkPageAuthentication(request))
            return;
          AsyncWebServerResponse *response = request->beginResponse_P(200, F("application/javascript"), APP_JS);
          response->addHeader(F("Cache-Control"), F("public, max-age=86400"));
          request->send(response);
        }

        void sendStream(AsyncWebServerRequest *request, AsyncResponseStream *response, const __FlashStringHelper *pageName) {
          response->addHeader(F("Cache-Control"), F("no-store"));
          response->addHeader(F("Connection"), F("close"));
#if defined(ESP8266)
          Serial.print(F("Portal page "));
          Serial.print(pageName);
          Serial.print(F(" heap="));
          Serial.println(ESP.getFreeHeap());
#endif
          request->send(response);
        }

        void writeEscaped(Print *out, const String &input) {
          for(size_t i = 0; i < input.length(); i++) {
            char c = input.charAt(i);
            if(c == '&')
              out->print(F("&amp;"));
            else if(c == '<')
              out->print(F("&lt;"));
            else if(c == '>')
              out->print(F("&gt;"));
            else if(c == '"')
              out->print(F("&quot;"));
            else
              out->print(c);
          }
        }

        void writePageHead(AsyncResponseStream *out, const __FlashStringHelper *title) {
          out->print(F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>"));
          out->print(title);
          out->print(F("</title><style>*{box-sizing:border-box}html,body{margin:0;min-height:100%;font-family:Avenir,Helvetica,Arial,sans-serif;color:#2c3e50;background:#f6f7f8}.app{min-height:100vh;display:flex;align-items:flex-start;justify-content:center;padding:18px}.page{width:100%;max-width:460px}.navbar{background:#333;color:#fff;border-radius:5px 5px 0 0;padding:14px 16px;text-align:center}.navbar h2{font-size:1.1rem;line-height:1.25;margin:0;overflow-wrap:anywhere}.page-slot-container{background:white;border:1px solid #0006;border-top:0;border-radius:0 0 5px 5px;padding:16px}.page-title{margin:0 0 14px;text-align:center;font-size:1.5rem}.field{margin:0 0 14px}.field label{display:block;font-size:.95rem;margin-bottom:5px}input,select{font-size:16px;width:100%;padding:10px;border:1px solid gray;border-radius:5px}select{margin-bottom:8px;background:white}input[type=submit]{font-size:16px;width:100%;padding:10px;cursor:pointer}.btn-green{background-color:yellowgreen;border:1px solid #7fa734}#toast-root{position:fixed;right:16px;top:16px;z-index:50}.toast-container{position:relative;overflow:hidden;padding:16px 24px;background-color:#aaa;display:flex;align-items:center;border-radius:5px;cursor:pointer;flex-direction:column;margin-bottom:16px;min-width:220px}.toast-container>p{margin:0}.toast-title{font-size:1.5rem;font-weight:bold}.toast-message,.toast-title{font-family:Avenir,Helvetica,Arial,sans-serif;color:white}.toast-success{background-color:#04b13d!important}.toast-error{background-color:crimson!important}.toast-close{position:absolute;top:0;right:0;width:32px;height:32px;padding:0;border:2px solid white;background-color:#fff}.toast-bar{position:absolute;background-color:#fff6;bottom:0;left:0;height:8px;width:100%}@media(max-width:520px){.app{padding:0}.page{max-width:none}.navbar,.page-slot-container{border-radius:0;border-left:0;border-right:0}.page-title{font-size:1.35rem}}</style></head><body><div class='app'><div id='toast-root'></div><div class='page'><div class='navbar'><h2>"));
          out->print(F(RFLINK_HARDWARE_NAME));
          out->print(F("</h2></div><div class='page-slot-container'><h1 class='page-title'>"));
          out->print(title);
          out->print(F("</h1>"));
        }

        void writePageTail(AsyncResponseStream *out) {
          out->print(F("<script src='/assets/app.js'></script></div></div></div></body></html>"));
        }

        bool pushConfigAndReply(AsyncWebServerRequest *request, JsonObject root, const __FlashStringHelper *page) {
          String message;
          message.reserve(256);
          bool ok = Config::pushNewConfiguration(root, message, true);

          if(request->hasParam(F("async"), true)) {
            if(ok)
              request->send(200, F("text/plain"), message.length() > 0 ? message : F("Saved. Connecting to WiFi..."));
            else
              request->send(500, F("text/plain"), message.length() > 0 ? message : F("Configuration was not saved."));
            return ok;
          }

          AsyncWebServerResponse *response = request->beginResponse(302);
          response->addHeader(F("Location"), page);
          response->addHeader(F("Cache-Control"), F("no-store"));
          response->addHeader(F("Connection"), F("close"));
          request->send(response);
          return ok;
        }

        bool requestIsFromAccessPoint(AsyncWebServerRequest *request) {
#if defined(ESP8266) || defined(ESP32)
          return request->client()->localIP() == WiFi.softAPIP();
#else
          return false;
#endif
        }

        bool hostLooksLikeIpAddress(const String &host) {
          if(host.length() == 0)
            return false;

          for(size_t i = 0; i < host.length(); i++) {
            char c = host.charAt(i);
            if(!(c == '.' || (c >= '0' && c <= '9')))
              return false;
          }
          return true;
        }

        bool shouldRedirectToCaptiveSetup(AsyncWebServerRequest *request) {
          if(!requestIsFromAccessPoint(request))
            return false;

          if(!request->hasHeader(F("Host")))
            return false;

          String host = request->getHeader(F("Host"))->value();
          int portSeparator = host.indexOf(':');
          if(portSeparator >= 0)
            host = host.substring(0, portSeparator);

          if(host == WiFi.softAPIP().toString())
            return false;

          return !hostLooksLikeIpAddress(host);
        }

        String portalBaseUrl();

        void serveCaptiveSetupRedirect(AsyncWebServerRequest *request) {
          if(!requestIsFromAccessPoint(request)) {
            request->send(404, F("application/json"), F("{\"success\":false,\"message\":\"RF Bridge local API\"}"));
            return;
          }
          AsyncWebServerResponse *response = request->beginResponse(302);
          response->addHeader(F("Location"), portalBaseUrl() + F("/"));
          response->addHeader(F("Cache-Control"), F("no-store"));
          response->addHeader(F("Connection"), F("close"));
          request->send(response);
        }

        bool redirectToWifiSetupIfCaptive(AsyncWebServerRequest *request) {
          if(!shouldRedirectToCaptiveSetup(request))
            return false;

          serveCaptiveSetupRedirect(request);
          return true;
        }

        bool checkHttpAuthentication(AsyncWebServerRequest *request) {
          (void) request;
          return true;
        }

        bool checkPageAuthentication(AsyncWebServerRequest *request) {
          if(!requestIsFromAccessPoint(request)) {
            request->send(404, F("application/json"), F("{\"success\":false,\"message\":\"RF Bridge local API\"}"));
            return false;
          }
          return checkHttpAuthentication(request);
        }

        String portalBaseUrl() {
          return String(F("http://")) + WiFi.softAPIP().toString();
        }

        void serveCaptiveGenerate204(AsyncWebServerRequest *request) {
          serveCaptiveSetupRedirect(request);
        }

        void serveCaptiveNcsi(AsyncWebServerRequest *request) {
          serveCaptiveSetupRedirect(request);
        }

        void serveCaptiveConnectTest(AsyncWebServerRequest *request) {
          serveCaptiveSetupRedirect(request);
        }

        void serveCaptiveIosSuccess(AsyncWebServerRequest *request) {
          serveCaptiveSetupRedirect(request);
        }

        void serveCaptivePortal(AsyncWebServerRequest *request) {
          serveCaptiveSetupRedirect(request);
        }

        void serveCaptivePortalApi(AsyncWebServerRequest *request) {
          if(!requestIsFromAccessPoint(request)) {
            request->send(404, F("application/json"), F("{\"success\":false,\"message\":\"RF Bridge local API\"}"));
            return;
          }
          String json = String(F("{\"captive\":true,\"user-portal-url\":\"")) + portalBaseUrl() + F("/\"}");
          AsyncWebServerResponse *response = request->beginResponse(
                  200,
                  F("application/captive+json"),
                  json
          );
          response->addHeader(F("Cache-Control"), F("no-store"));
          response->addHeader(F("Connection"), F("close"));
          request->send(response);
        }

        void serveRootPage(AsyncWebServerRequest *request) {
          serveWifiSetupPage(request);
        }

        void serveRedirect(AsyncWebServerRequest *request, const __FlashStringHelper *location) {
          if(!checkPageAuthentication(request))
            return;
          AsyncWebServerResponse *response = request->beginResponse(302);
          response->addHeader(F("Location"), location);
          response->addHeader(F("Cache-Control"), F("no-store"));
          request->send(response);
        }

        void serveServicesRedirect(AsyncWebServerRequest *request) {
          serveRedirect(request, F("/wifi"));
        }

        void serveWifiRedirect(AsyncWebServerRequest *request) {
          serveRedirect(request, F("/wifi"));
        }

        void notFound(AsyncWebServerRequest *request) {
          String url = request->url();
          if(url.startsWith(F("/generate_204")) || url.startsWith(F("/gen_204"))) {
            serveCaptiveGenerate204(request);
            return;
          }
          if(url == F("/ncsi.txt")) {
            serveCaptiveNcsi(request);
            return;
          }
          if(url == F("/connecttest.txt")) {
            serveCaptiveConnectTest(request);
            return;
          }
          if(url == F("/hotspot-detect.html") || url == F("/library/test/success.html")) {
            serveCaptiveIosSuccess(request);
            return;
          }
          if(url == F("/canonical.html") || url == F("/fwlink") || url == F("/redirect"))
            return serveCaptivePortal(request);

          if(redirectToWifiSetupIfCaptive(request))
            return;

          request->send(404, F("text/plain"), F("Not found"));
        }

        String formatUptime(unsigned long seconds) {
          unsigned long days = seconds / 86400;
          seconds %= 86400;
          unsigned long hours = seconds / 3600;
          seconds %= 3600;
          unsigned long minutes = seconds / 60;
          seconds %= 60;

          String out;
          if(days > 0) {
            out += String(days);
            out += F("d ");
          }
          if(hours > 0 || days > 0) {
            out += String(hours);
            out += F("h ");
          }
          if(minutes > 0 || hours > 0 || days > 0) {
            out += String(minutes);
            out += F("m ");
          }
          out += String(seconds);
          out += F("s");
          return out;
        }

        void writeStatusValueRow(AsyncResponseStream *out, const __FlashStringHelper *label, const String &value, const String &statusClass = String()) {
          out->print(F("<tr><td class='setting'>"));
          out->print(label);
          out->print(F("</td><td"));
          if(statusClass.length() > 0) {
            out->print(F(" class='status-"));
            writeEscaped(out, statusClass);
            out->print(F("'"));
          }
          out->print(F(">"));
          writeEscaped(out, value);
          out->print(F("</td></tr>"));
        }

        String jsonStringOr(JsonVariant value, const __FlashStringHelper *fallback) {
          if(value.isNull())
            return String(fallback);
          if(value.is<const char *>())
            return String(value.as<const char *>());
          if(value.is<unsigned long>())
            return String(value.as<unsigned long>());
          if(value.is<long>())
            return String(value.as<long>());
          if(value.is<int>())
            return String(value.as<int>());
          return String(fallback);
        }

        void serveWifiSetupPage(AsyncWebServerRequest *request) {
          if(!checkPageAuthentication(request))
            return;

          AsyncResponseStream *response = request->beginResponseStream(F("text/html; charset=utf-8"));
          writePageHead(response, F("WiFi"));
          response->print(F("<form method='post' action='/wifi' data-async-save='1'><div class='field'><label for='client_ssid'>WiFi name</label><select id='client_ssid' name='client_ssid'>"));
          int count = RFLink::Wifi::getCachedWifiNetworkCount();
          if(count == 0) {
            response->print(F("<option value=''>No WiFi networks found</option>"));
          } else {
            for(int i = 0; i < count; i++) {
              String ssid;
              int32_t rssi;
              bool secure;
              if(!RFLink::Wifi::getCachedWifiNetwork(i, ssid, rssi, secure))
                continue;
              response->print(F("<option value=\""));
              writeEscaped(response, ssid);
              response->print(F("\">"));
              writeEscaped(response, ssid);
              response->print(F(" ("));
              response->print(rssi);
              response->print(F(" dBm)</option>"));
            }
          }
          response->print(F("</select></div><div class='field'><label for='client_password'>WiFi password</label><input id='client_password' name='client_password' type='password' autocomplete='new-password' value=\"\"></div><input type='submit' class='btn-green' value='Save'></form>"));
          writePageTail(response);
          sendStream(request, response, F("WiFi"));
        }

        void serveWifiSetupPost(AsyncWebServerRequest *request) {
          if(!checkPageAuthentication(request))
            return;

          if(!request->hasParam(F("client_ssid"), true)) {
            request->send(400, F("text/plain"), F("Missing WiFi name"));
            return;
          }

          String ssid = request->getParam(F("client_ssid"), true)->value();
          String password;
          if(request->hasParam(F("client_password"), true))
            password = request->getParam(F("client_password"), true)->value();

          StaticJsonDocument<512> input;
          JsonObject root = input.to<JsonObject>();
          JsonObject wifi = root.createNestedObject(F("wifi"));
          wifi[F("client_enabled")] = true;
          wifi[F("client_dhcp_enabled")] = true;
          wifi[F("client_ssid")] = ssid;
          wifi[F("client_password")] = password;
          if(input.overflowed()) {
            request->send(500, F("text/plain"), F("WiFi setup payload overflow"));
            return;
          }

          if(pushConfigAndReply(request, root, F("/wifi")))
            RFLink::Wifi::requestSetupAccessPointShutdown();
        }

        void serveApiStatusGet(AsyncWebServerRequest *request) {
          if(!checkHttpAuthentication(request))
            return;

          DynamicJsonDocument output(1000);

          auto && obj = output.to<JsonObject>();

          RFLink::getStatusJsonString(obj);

          RFLink::Wifi::getStatusJsonString(obj);
          #ifndef RFLINK_MQTT_DISABLED
          RFLink::Mqtt::getStatusJsonString(obj);
          #endif // RFLINK_MQTT_DISABLED
          RFLink::Signal::getStatusJsonString(obj);

          String buffer;
          if(!buffer.reserve(512) ) {
            request->send(500, F("text/plain"), F("Not enough memory"));
            return;
          }
          serializeJson(output, buffer);

          request->send(200, F("application/json"), buffer);
        }

        void serveApiEventsGet(AsyncWebServerRequest *request) {
          if(!checkHttpAuthentication(request))
            return;

          uint32_t since = 0;
          uint8_t limit = 8;
          if(request->hasParam(F("since")))
            since = strtoul(request->getParam(F("since"))->value().c_str(), nullptr, 10);
          if(request->hasParam(F("limit"))) {
            int requestedLimit = request->getParam(F("limit"))->value().toInt();
            if(requestedLimit > 0 && requestedLimit < 255)
              limit = static_cast<uint8_t>(requestedLimit);
          }
          if(limit > 2)
            limit = 2;

          DynamicJsonDocument output(4600);
          JsonObject root = output.to<JsonObject>();
          root[F("cursor")] = RFLink::getRfEventCursor();
          JsonArray events = root.createNestedArray(F("events"));
          RFLink::appendRfEventsJson(events, since, limit);

          String buffer;
          if(!buffer.reserve(4200)) {
            request->send(500, F("text/plain"), F("Not enough memory"));
            return;
          }
          serializeJson(output, buffer);

          request->send(200, F("application/json"), buffer);
        }

        void serveApiLearn(AsyncWebServerRequest *request) {
          if(!checkHttpAuthentication(request))
            return;

          RFLink::armRawRfLearning(30);
          DynamicJsonDocument output(160);
          JsonObject root = output.to<JsonObject>();
          root[F("success")] = true;
          root[F("cursor")] = RFLink::getRfEventCursor();
          root[F("raw_learning_armed")] = true;
          String buffer;
          serializeJson(output, buffer);
          request->send(200, F("application/json"), buffer);
        }

        void serveApiReboot(AsyncWebServerRequest *request) {
          if(!checkHttpAuthentication(request))
            return;

          request->send(200, F("text/plain"), F("Rebooting in 5 seconds"));
          RFLink::scheduleReboot(5);
        }

        void serveApiCommandPost(AsyncWebServerRequest *request, JsonVariant &json) {
          if(!checkHttpAuthentication(request))
            return;

          if(!json.is<JsonObject>()) {
            request->send(400, F("application/json"), F("{\"success\":false,\"message\":\"Not an object\"}"));
            return;
          }

          JsonObject data = json.as<JsonObject>();
          const char *command = data["command"].as<const char *>();
          if(command == nullptr)
            command = data["cmd"].as<const char *>();
          if(command == nullptr || strlen(command) == 0) {
            request->send(400, F("application/json"), F("{\"success\":false,\"message\":\"Missing command\"}"));
            return;
          }

          size_t commandLength = strlen(command);
          if(commandLength >= INPUT_COMMAND_SIZE - 1) {
            request->send(413, F("application/json"), F("{\"success\":false,\"message\":\"Command too long\"}"));
            return;
          }

          for(size_t i = 0; i < commandLength; i++) {
            if(command[i] == '\r' || command[i] == '\n') {
              request->send(400, F("application/json"), F("{\"success\":false,\"message\":\"Command must be a single line\"}"));
              return;
            }
          }

          char commandBuffer[INPUT_COMMAND_SIZE];
          strncpy(commandBuffer, command, INPUT_COMMAND_SIZE - 1);
          commandBuffer[INPUT_COMMAND_SIZE - 1] = 0;

          bool ok = RFLink::executeCliCommand(commandBuffer);
          request->send(ok ? 200 : 422, F("application/json"), ok ? F("{\"success\":true}") : F("{\"success\":false,\"message\":\"Command rejected\"}"));
        }

        void init() {

          refreshParametersFromConfig(false);

          setupServer.onNotFound(notFound);
          setupServer.on(PSTR("/assets/app.js"), HTTP_GET, serveStaticJs);
          setupServer.on(PSTR("/"), HTTP_GET, serveRootPage);
          setupServer.on(PSTR("/index.html"), HTTP_GET, serveRootPage);
          setupServer.on(PSTR("/home"), HTTP_GET, serveWifiRedirect);
          setupServer.on(PSTR("/wifi"), HTTP_GET, serveWifiSetupPage);
          setupServer.on(PSTR("/wifi"), HTTP_POST, serveWifiSetupPost);
          setupServer.on(PSTR("/services"), HTTP_GET, serveWifiRedirect);
          setupServer.on(PSTR("/portal"), HTTP_GET, serveServicesRedirect);
          setupServer.on(PSTR("/signal"), HTTP_GET, serveWifiRedirect);
          setupServer.on(PSTR("/advanced"), HTTP_GET, serveWifiRedirect);
          setupServer.on(PSTR("/infos"), HTTP_GET, serveWifiRedirect);
          setupServer.on(PSTR("/generate_204"), HTTP_GET, serveCaptiveGenerate204);
          setupServer.on(PSTR("/gen_204"), HTTP_GET, serveCaptiveGenerate204);
          setupServer.on(PSTR("/capport"), HTTP_GET, serveCaptivePortalApi);
          setupServer.on(PSTR("/hotspot-detect.html"), HTTP_GET, serveCaptiveIosSuccess);
          setupServer.on(PSTR("/library/test/success.html"), HTTP_GET, serveCaptiveIosSuccess);
          setupServer.on(PSTR("/connecttest.txt"), HTTP_GET, serveCaptiveConnectTest);
          setupServer.on(PSTR("/ncsi.txt"), HTTP_GET, serveCaptiveNcsi);
          setupServer.on(PSTR("/canonical.html"), HTTP_GET, serveCaptivePortal);
          setupServer.on(PSTR("/fwlink"), HTTP_GET, serveCaptivePortal);
          setupServer.on(PSTR("/redirect"), HTTP_GET, serveCaptivePortal);
          setupServer.on(PSTR("/wifi-setup"), HTTP_GET, serveWifiRedirect);
          setupServer.on(PSTR("/wifi-setup"), HTTP_POST, serveWifiSetupPost);

          apiServer.on(PSTR("/api/status"), HTTP_GET, serveApiStatusGet);
          apiServer.on(PSTR("/api/events"), HTTP_GET, serveApiEventsGet);
          apiServer.on(PSTR("/api/learn"), HTTP_GET, serveApiLearn);
          apiServer.on(PSTR("/api/learn"), HTTP_POST, serveApiLearn);
          apiServer.on(PSTR("/api/reboot"), HTTP_GET, serveApiReboot);
          apiServer.on(PSTR("/reboot"), HTTP_POST, serveApiReboot);
          apiServer.onNotFound([](AsyncWebServerRequest *request) {
            request->send(404, F("application/json"), F("{\"success\":false,\"message\":\"RF Bridge local API\"}"));
          });

          AsyncCallbackJsonWebHandler* commandHandler = new AsyncCallbackJsonWebHandler(PSTR("/api/command"), serveApiCommandPost, INPUT_COMMAND_SIZE + 256);
          apiServer.addHandler(commandHandler);

        }

        void start() {
          if(!apiServerStarted) {
            Serial.print(F("Starting API server... "));
            apiServer.begin();
            apiServerStarted = true;
            Serial.println(F("OK"));
          }
          if(params::enabled && (WiFi.getMode() & WIFI_AP) != 0)
            startSetupServer();
        }

        void stop() {
          stopSetupServer();
          if(apiServerStarted) {
            Serial.print(F("Stopping API server... "));
            apiServer.end();
            apiServerStarted = false;
            Serial.println(F("OK"));
          }
        }

        void startSetupServer() {
          if(setupServerStarted || !params::enabled || (WiFi.getMode() & WIFI_AP) == 0)
            return;

          Serial.print(F("Starting setup server... "));
          setupServer.begin();
          setupServerStarted = true;
          Serial.println(F("OK"));
        }

        void stopSetupServer() {
          if(!setupServerStarted)
            return;

          Serial.print(F("Stopping setup server... "));
          setupServer.end();
          setupServerStarted = false;
          Serial.println(F("OK"));
        }

      void paramsUpdatedCallback() {
        refreshParametersFromConfig();
      }

      void refreshParametersFromConfig(bool triggerChanges) {
        Config::ConfigItem *item;
        bool changesDetected = false;

        item = Config::findConfigItem(json_name_enabled, Config::SectionId::Portal_id);
        if (item->getBoolValue() != params::enabled) {
          changesDetected = true;
          params::enabled = item->getBoolValue();
        }

          if (triggerChanges && changesDetected) {
            if(params::enabled)
              startSetupServer();
            else
              stopSetupServer();
          }
      }

    } // end of Portal namespace
} // end of RFLink namespace

#endif // RFLINK_PORTAL_DISABLED
