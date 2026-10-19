#include "RFLink.h"

#ifndef RFLINK_PORTAL_DISABLED

#include <ESPAsyncWebServer.h>

#include "10_Wifi.h"
#include "11_Config.h"
#include "12_Portal.h"

#if defined(ESP8266)
#include "ESP8266WiFi.h"
#include "ESPAsyncTCP.h"
#elif defined(ESP32)
#include "AsyncTCP.h"
#endif

namespace RFLink { namespace Portal {

        const char json_name_enabled[] = "enabled";

        Config::ConfigItem configItems[] =  {
                Config::ConfigItem(json_name_enabled,      Config::SectionId::Portal_id, true, paramsUpdatedCallback),
                Config::ConfigItem(), // dont remove it!
        };

        namespace params {
          bool enabled = true;
        }

        class ApiWebServer : public AsyncWebServer {
          public:
            explicit ApiWebServer(uint16_t port) : AsyncWebServer(port) {}

            bool isListening() {
              return _server.status() != 0;
            }
        };

        AsyncWebServer setupServer(80);
        ApiWebServer apiServer(RFLINK_API_PORT);
        bool setupServerStarted = false;
        bool apiServerStarted = false;
        bool apiServerRestartPending = false;
        unsigned long apiServerRetryAt = 0;
        constexpr unsigned long API_SERVER_RETRY_MS = 2000;
        void serveWifiSetupPage(AsyncWebServerRequest *request);
        bool requestIsFromAccessPoint(AsyncWebServerRequest *request);
        bool checkPageAuthentication(AsyncWebServerRequest *request);

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
            request->send(404, F("application/json"), F("{\"success\":false,\"message\":\"RF Bridge setup portal is only available from the device AP\"}"));
            return;
          }
          AsyncWebServerResponse *response = request->beginResponse(302);
          response->addHeader(F("Location"), portalBaseUrl() + F("/"));
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
            request->send(404, F("application/json"), F("{\"success\":false,\"message\":\"RF Bridge setup portal is only available from the device AP\"}"));
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
            request->send(404, F("application/json"), F("{\"success\":false,\"message\":\"RF Bridge setup portal is only available from the device AP\"}"));
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

        void serveWifiSetupPage(AsyncWebServerRequest *request) {
          if(!checkPageAuthentication(request))
            return;

          AsyncResponseStream *response = request->beginResponseStream(F("text/html; charset=utf-8"));
          response->addHeader(F("Cache-Control"), F("no-store"));
          response->addHeader(F("Connection"), F("close"));

          response->print(F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"));
          response->print(F("<title>RF Bridge WiFi</title><style>"));
          response->print(F("*{box-sizing:border-box}body{margin:0;font-family:Avenir,Helvetica,Arial,sans-serif;color:#2c3e50;background:#f6f7f8}"));
          response->print(F(".app{min-height:100vh;display:flex;justify-content:center;padding:18px}.page{width:100%;max-width:460px}"));
          response->print(F(".bar{background:#333;color:#fff;border-radius:5px 5px 0 0;padding:14px 16px;text-align:center}"));
          response->print(F(".card{background:#fff;border:1px solid #0006;border-top:0;border-radius:0 0 5px 5px;padding:16px}"));
          response->print(F("h1{margin:0 0 14px;text-align:center;font-size:1.35rem}label{display:block;margin:0 0 5px}"));
          response->print(F("select,input{width:100%;font-size:16px;padding:10px;border:1px solid gray;border-radius:5px;background:#fff}.password{display:flex;gap:6px}.password input{min-width:0}.password button{position:relative;width:42px;border:1px solid gray;border-radius:5px;background:#fff;font-size:20px;line-height:1}.password button.revealed::after{content:'';position:absolute;left:7px;top:19px;width:26px;border-top:2px solid #333;transform:rotate(-45deg)}"));
          response->print(F(".field{margin:0 0 14px}.btn{background:yellowgreen;border:1px solid #7fa734;cursor:pointer}"));
          response->print(F(".toast{display:none;margin:0 0 14px;padding:10px;border-radius:5px;color:#fff;background:#04b13d}.toast.err{background:crimson}"));
          response->print(F("@media(max-width:520px){.app{padding:0}.page{max-width:none}.bar,.card{border-radius:0;border-left:0;border-right:0}}"));
          response->print(F("</style></head><body><div class='app'><div class='page'><div class='bar'>"));
          response->print(F(RFLINK_HARDWARE_NAME));
          response->print(F("</div><div class='card'><h1>WiFi</h1><div id='toast' class='toast'></div><form id='wifi-form' method='post' action='/wifi'>"));
          response->print(F("<div class='field'><label for='client_ssid'>WiFi name</label><select id='client_ssid' name='client_ssid'>"));
          String configuredSsid = RFLink::Wifi::getConfiguredClientSsid();
          bool configuredSsidInCache = false;
          int count = RFLink::Wifi::getCachedWifiNetworkCount();
          for(int i = 0; i < count; i++) {
            String ssid;
            int32_t rssi;
            bool secure;
            if(RFLink::Wifi::getCachedWifiNetwork(i, ssid, rssi, secure) && ssid == configuredSsid) {
              configuredSsidInCache = true;
              break;
            }
          }
          if(configuredSsid.length() > 0 && configuredSsid != "My Home Wifi" && !configuredSsidInCache) {
            response->print(F("<option selected value=\""));
            writeEscaped(response, configuredSsid);
            response->print(F("\">"));
            writeEscaped(response, configuredSsid);
            response->print(F(" (saved network)</option>"));
          }
          if(count == 0 && configuredSsid.length() == 0) {
            response->print(F("<option value=''>No WiFi networks found</option>"));
          } else {
            for(int i = 0; i < count; i++) {
              String ssid;
              int32_t rssi;
              bool secure;
              if(!RFLink::Wifi::getCachedWifiNetwork(i, ssid, rssi, secure))
                continue;
              response->print(F("<option"));
              if(ssid == configuredSsid)
                response->print(F(" selected"));
              response->print(F(" value=\""));
              writeEscaped(response, ssid);
              response->print(F("\">"));
              writeEscaped(response, ssid);
              response->print(F(" ("));
              response->print(rssi);
              response->print(F(" dBm)</option>"));
            }
          }
          response->print(F("</select></div><div class='field'><label for='client_password'>WiFi password</label>"));
          response->print(F("<div class='password'><input id='client_password' name='client_password' type='password' autocomplete='new-password' value=''><button id='toggle-password' type='button' aria-label='Show password' title='Show password'>&#128065;</button></div></div>"));
          response->print(F("<input type='submit' class='btn' value='Save'></form>"));
          response->print(F("<script>(function(){var f=document.getElementById('wifi-form'),t=document.getElementById('toast'),p=document.getElementById('client_password'),v=document.getElementById('toggle-password');function show(m,e){t.textContent=m;t.className=e?'toast err':'toast';t.style.display='block'}v.addEventListener('click',function(){var showPassword=p.type==='password';p.type=showPassword?'text':'password';v.classList.toggle('revealed',showPassword);v.setAttribute('aria-label',showPassword?'Hide password':'Show password');v.setAttribute('title',showPassword?'Hide password':'Show password')});f.addEventListener('submit',function(ev){ev.preventDefault();var b=f.querySelector('[type=submit]');if(b)b.disabled=true;var d=new FormData(f);d.append('async','1');fetch('/wifi',{method:'POST',body:d,credentials:'same-origin'}).then(function(r){return r.text().then(function(x){return{ok:r.ok,text:x}})}).then(function(r){show(r.text||(r.ok?'Saved. Connecting to WiFi...':'Save failed'),!r.ok)}).catch(function(){show('Network error while saving',true)}).finally(function(){if(b)b.disabled=false})})})();</script>"));
          response->print(F("</div></div></div></body></html>"));
          request->send(response);
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

        void init() {

          refreshParametersFromConfig(false);

          setupServer.onNotFound(notFound);
          setupServer.on(PSTR("/"), HTTP_GET, serveRootPage);
          setupServer.on(PSTR("/index.html"), HTTP_GET, serveRootPage);
          setupServer.on(PSTR("/wifi"), HTTP_GET, serveWifiSetupPage);
          setupServer.on(PSTR("/wifi"), HTTP_POST, serveWifiSetupPost);
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

          initWebSocket(apiServer);
          apiServer.onNotFound([](AsyncWebServerRequest *request) {
            request->send(404, F("application/json"), F("{\"success\":false,\"message\":\"Use WebSocket /api/ws\"}"));
          });

        }

        void start() {
          if(WiFi.isConnected()) {
            apiServerRestartPending = true;
            apiServerRetryAt = 0;
          }
          if(params::enabled && (WiFi.getMode() & WIFI_AP) != 0)
            startSetupServer();
        }

        void stop() {
          stopSetupServer();
          apiServerRestartPending = false;
          apiServerRetryAt = 0;
          if(apiServerStarted) {
            Serial.print(F("Stopping API server... "));
            apiServer.end();
            apiServerStarted = false;
            Serial.println(F("OK"));
          }
        }

        void notifyNetworkReady() {
          // ESPAsyncWebServer owns TCP state. Do not call end()/begin() from
          // the ESP8266 Wi-Fi event callback; defer a needed start to the
          // cooperative loop. The listener binds to IP_ANY_TYPE, so a normal
          // DHCP/Got-IP notification must not interrupt a healthy API server.
          if(apiServerStarted && apiServer.isListening())
            return;
          apiServerRestartPending = true;
          apiServerRetryAt = 0;
        }

        void maintainApiServer() {
          // A software restart can restore the station connection without
          // delivering a new Got-IP callback after Portal::start() has run.
          // In that state the bridge is on Wi-Fi but its API listener has
          // never been started. Treat the live station state as authoritative.
          if(!apiServerStarted && WiFi.isConnected() && !apiServerRestartPending) {
            apiServerRestartPending = true;
            apiServerRetryAt = 0;
          }

          // A TCP listener can disappear after an SDK-level network restart while
          // the station remains associated. Do not wait for another Got-IP event
          // that may never arrive before recreating the API listener.
          if(apiServerStarted && !apiServer.isListening()) {
            Serial.println(F("API listener stopped; scheduling restart"));
            closeWebSocketClients();
            apiServer.end();
            apiServerStarted = false;
            apiServerRestartPending = true;
            apiServerRetryAt = 0;
          }

          if(!apiServerRestartPending || !WiFi.isConnected())
            return;
          if(apiServerRetryAt != 0 &&
             (long)(millis() - apiServerRetryAt) < 0)
            return;

          if(apiServerStarted) {
            Serial.print(F("Restarting API server after WiFi IP change... "));
            closeWebSocketClients();
            apiServer.end();
            apiServerStarted = false;
          } else {
            Serial.print(F("Starting API server after WiFi IP assignment... "));
          }

          apiServer.begin();
          if(!apiServer.isListening()) {
            apiServerRetryAt = millis() + API_SERVER_RETRY_MS;
            Serial.println(F("FAILED; retrying"));
            return;
          }

          apiServerStarted = true;
          apiServerRestartPending = false;
          apiServerRetryAt = 0;
          Serial.println(F("OK"));
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
