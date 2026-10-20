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

        void appendEscaped(String &out, const String &input) {
          for(size_t i = 0; i < input.length(); i++) {
            char c = input.charAt(i);
            if(c == '&')
              out += F("&amp;");
            else if(c == '<')
              out += F("&lt;");
            else if(c == '>')
              out += F("&gt;");
            else if(c == '"')
              out += F("&quot;");
            else
              out += c;
          }
        }

        String wifiSetupOptions(const String &variable) {
          if(variable != F("WIFI_OPTIONS"))
            return String();

          // Four cached SSIDs are the only dynamic part of the portal page.
          // Keep the full page in flash; a RAM-backed response may truncate.
          String options;
          options.reserve(1536);
          const String configuredSsid = RFLink::Wifi::getConfiguredClientSsid();
          const bool hasConfiguredSsid = configuredSsid.length() > 0 && configuredSsid != F("My Home Wifi");
          bool configuredSsidInCache = false;
          const int count = RFLink::Wifi::getCachedWifiNetworkCount();
          for(int i = 0; i < count; i++) {
            String ssid;
            int32_t rssi;
            bool secure;
            if(RFLink::Wifi::getCachedWifiNetwork(i, ssid, rssi, secure) && ssid == configuredSsid) {
              configuredSsidInCache = true;
              break;
            }
          }

          if(hasConfiguredSsid && !configuredSsidInCache) {
            options += F("<option selected value=\"");
            appendEscaped(options, configuredSsid);
            options += F("\">");
            appendEscaped(options, configuredSsid);
            options += F(" (saved network)</option>");
          }

          for(int i = 0; i < count; i++) {
            String ssid;
            int32_t rssi;
            bool secure;
            if(!RFLink::Wifi::getCachedWifiNetwork(i, ssid, rssi, secure))
              continue;
            options += F("<option");
            if(ssid == configuredSsid)
              options += F(" selected");
            options += F(" value=\"");
            appendEscaped(options, ssid);
            options += F("\">");
            appendEscaped(options, ssid);
            options += F(" (");
            options += rssi;
            options += F(" dBm)</option>");
          }

          if(options.length() == 0)
            options = F("<option value=\"\">No WiFi networks found</option>");
          return options;
        }

        static const char wifiSetupPage[] PROGMEM = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>RF Bridge WiFi</title><style>*{box-sizing:border-box}body{margin:0;font-family:Avenir,Helvetica,Arial,sans-serif;color:#2c3e50;background:#f6f7f8}.app{min-height:100vh;display:flex;justify-content:center;padding:18px}.page{width:100%;max-width:460px}.bar{background:#333;color:#fff;border-radius:5px 5px 0 0;padding:14px 16px;text-align:center}.card{background:#fff;border:1px solid #0006;border-top:0;border-radius:0 0 5px 5px;padding:16px}h1{margin:0 0 14px;text-align:center;font-size:1.35rem}label{display:block;margin:0 0 5px}select,input{width:100%;font-size:16px;padding:10px;border:1px solid gray;border-radius:5px;background:#fff}.password{display:flex;gap:6px}.password input{min-width:0}.password button{width:42px;border:1px solid gray;border-radius:5px;background:#fff;display:grid;place-items:center;padding:0}.eye{position:relative;display:block;width:20px;height:12px;border:2px solid #333;border-radius:50%}.eye:after{content:'';position:absolute;width:4px;height:4px;left:6px;top:2px;border-radius:50%;background:#333}.revealed .eye:before{content:'';position:absolute;z-index:1;width:24px;left:-4px;top:3px;border-top:2px solid #333;transform:rotate(-35deg)}.field{margin:0 0 14px}.btn{background:yellowgreen;border:1px solid #7fa734;cursor:pointer}.toast{display:none;margin:0 0 14px;padding:10px;border-radius:5px;color:#fff;background:#04b13d}.toast.err{background:crimson}@media(max-width:520px){.app{padding:0}.page{max-width:none}.bar,.card{border-radius:0;border-left:0;border-right:0}}</style></head><body><div class="app"><div class="page"><div class="bar">RF Link 32 - Sonoff RF Bridge</div><div class="card"><h1>WiFi</h1><div id="toast" class="toast"></div><form id="wifi-form" method="post" action="/wifi"><div class="field"><label for="client_ssid">WiFi name</label><select id="client_ssid" name="client_ssid">%WIFI_OPTIONS%</select></div><div class="field"><label for="client_password">WiFi password</label><div class="password"><input id="client_password" name="client_password" type="password" autocomplete="new-password" value=""><button id="toggle-password" type="button" aria-label="Show password" title="Show password"><span class="eye" aria-hidden="true"></span></button></div></div><input type="submit" class="btn" value="Save"></form><script>(function(){var f=document.getElementById('wifi-form'),t=document.getElementById('toast'),p=document.getElementById('client_password'),v=document.getElementById('toggle-password');function show(m,e){t.textContent=m;t.className=e?'toast err':'toast';t.style.display='block'}v.addEventListener('click',function(){var visible=p.type==='password';p.type=visible?'text':'password';v.classList.toggle('revealed',visible);v.setAttribute('aria-label',visible?'Hide password':'Show password');v.setAttribute('title',visible?'Hide password':'Show password')});f.addEventListener('submit',function(e){e.preventDefault();var b=f.querySelector('[type=submit]');if(b)b.disabled=true;var d=new FormData(f);d.append('async','1');fetch('/wifi',{method:'POST',body:d,credentials:'same-origin'}).then(function(r){return r.text().then(function(x){return{ok:r.ok,text:x}})}).then(function(r){show(r.text||(r.ok?'Saved. Connecting to WiFi...':'Save failed'),!r.ok)}).catch(function(){show('Network error while saving',true)}).finally(function(){if(b)b.disabled=false})})})();</script></div></div></div></body></html>)HTML";

        // Keep the proven portal form in flash.  This is the same DOM and
        // interaction model as the prior streamed page, without the heap use
        // that made complete responses unreliable on the ESP8266.
        static const char wifiSetupPageStable[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>RF Bridge WiFi</title><style>
*{box-sizing:border-box}body{margin:0;font-family:Avenir,Helvetica,Arial,sans-serif;color:#2c3e50;background:#f6f7f8}.app{min-height:100vh;display:flex;justify-content:center;padding:18px}.page{width:100%;max-width:460px}.bar{background:#333;color:#fff;border-radius:5px 5px 0 0;padding:14px 16px;text-align:center}.card{background:#fff;border:1px solid #0006;border-top:0;border-radius:0 0 5px 5px;padding:16px}h1{margin:0 0 14px;text-align:center;font-size:1.35rem}label{display:block;margin:0 0 5px}select,input{width:100%;font-size:16px;padding:10px;border:1px solid gray;border-radius:5px;background:#fff}.password{display:flex;gap:6px}.password input{min-width:0}.password button{width:42px;border:1px solid gray;border-radius:5px;background:#fff;display:grid;place-items:center;padding:0}.password button svg{width:20px;height:20px;fill:#333}.password button .eye-open{display:none}.password button.revealed .eye-hidden{display:none}.password button.revealed .eye-open{display:block}.field{margin:0 0 14px}.btn{background:yellowgreen;border:1px solid #7fa734;cursor:pointer}.toast{display:none;margin:0 0 14px;padding:10px;border-radius:5px;color:#fff;background:#04b13d}.toast.err{background:crimson}@media(max-width:520px){.app{padding:0}.page{max-width:none}.bar,.card{border-radius:0;border-left:0;border-right:0}}
</style></head><body><div class="app"><div class="page"><div class="bar">RF Link 32 - Sonoff RF Bridge</div><div class="card"><h1>WiFi</h1><div id="toast" class="toast"></div><form id="wifi-form" method="post" action="/wifi"><div class="field"><label for="client_ssid">WiFi name</label><select id="client_ssid" name="client_ssid">%WIFI_OPTIONS%</select></div><div class="field"><label for="client_password">WiFi password</label><div class="password"><input id="client_password" name="client_password" type="password" autocomplete="new-password" value=""><button id="toggle-password" type="button" aria-label="Show password" title="Show password"><svg class="eye-hidden" aria-hidden="true" viewBox="0 0 16 16"><path d="M13.359 11.238C15.06 9.72 16 8 16 8s-3-5.5-8-5.5a7 7 0 0 0-2.79.588l.77.771A6 6 0 0 1 8 3.5c2.12 0 3.879 1.168 5.168 2.457A13 13 0 0 1 14.828 8q-.086.13-.195.288c-.335.48-.83 1.12-1.465 1.755q-.247.248-.517.486z"/><path d="M11.297 9.176a3.5 3.5 0 0 0-4.474-4.474l.823.823a2.5 2.5 0 0 1 2.829 2.829zm-2.943 1.299.822.822a3.5 3.5 0 0 1-4.474-4.474l.823.823a2.5 2.5 0 0 0 2.829 2.829"/><path d="M3.35 5.47q-.27.24-.518.487A13 13 0 0 0 1.172 8l.195.288c.335.48.83 1.12 1.465 1.755C4.121 11.332 5.881 12.5 8 12.5c.716 0 1.39-.133 2.02-.36l.77.772A7 7 0 0 1 8 13.5C3 13.5 0 8 0 8s.939-1.721 2.641-3.238l.708.709zm10.296 8.884-12-12 .708-.708 12 12z"/></svg><svg class="eye-open" aria-hidden="true" viewBox="0 0 16 16"><path d="M16 8s-3-5.5-8-5.5S0 8 0 8s3 5.5 8 5.5S16 8 16 8M1.173 8a13 13 0 0 1 1.66-2.043C4.12 4.668 5.88 3.5 8 3.5s3.879 1.168 5.168 2.457A13 13 0 0 1 14.828 8q-.086.13-.195.288c-.335.48-.83 1.12-1.465 1.755C11.879 11.332 10.119 12.5 8 12.5s-3.879-1.168-5.168-2.457A13 13 0 0 1 1.172 8z"/><path d="M8 5.5a2.5 2.5 0 1 0 0 5M4.5 8a3.5 3.5 0 1 1 7 0 3.5 3.5 0 0 1-7 0"/></svg></button></div></div><input type="submit" class="btn" value="Save"></form><script>(function(){var f=document.getElementById('wifi-form'),t=document.getElementById('toast'),p=document.getElementById('client_password'),v=document.getElementById('toggle-password');function show(m,e){t.textContent=m;t.className=e?'toast err':'toast';t.style.display='block'}v.addEventListener('click',function(){var showPassword=p.type==='password';p.type=showPassword?'text':'password';v.classList.toggle('revealed',showPassword);v.setAttribute('aria-label',showPassword?'Hide password':'Show password');v.setAttribute('title',showPassword?'Hide password':'Show password')});f.addEventListener('submit',function(ev){ev.preventDefault();var b=f.querySelector('[type=submit]');if(b)b.disabled=true;var d=new FormData(f);d.append('async','1');fetch('/wifi',{method:'POST',body:d,credentials:'same-origin'}).then(function(r){return r.text().then(function(x){return{ok:r.ok,text:x}})}).then(function(r){show(r.text||(r.ok?'Saved. Connecting to WiFi...':'Save failed'),!r.ok)}).catch(function(){show('Network error while saving',true)}).finally(function(){if(b)b.disabled=false})})})();</script></div></div></div></body></html>)HTML";

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

          AsyncWebServerResponse *staticResponse = request->beginResponse(
                  200,
                  F("text/html; charset=utf-8"),
                  reinterpret_cast<const uint8_t *>(wifiSetupPageStable),
                  strlen_P(wifiSetupPageStable),
                  wifiSetupOptions
          );
          staticResponse->addHeader(F("Cache-Control"), F("no-store"));
          staticResponse->addHeader(F("Connection"), F("close"));
          request->send(staticResponse);
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
          response->print(F("select,input{width:100%;font-size:16px;padding:10px;border:1px solid gray;border-radius:5px;background:#fff}.password{display:flex;gap:6px}.password input{min-width:0}.password button{width:42px;border:1px solid gray;border-radius:5px;background:#fff;display:grid;place-items:center;padding:0}.password button svg{width:20px;height:20px;fill:#333}.password button .eye-open{display:none}.password button.revealed .eye-hidden{display:none}.password button.revealed .eye-open{display:block}"));
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
          response->print(F("<div class='password'><input id='client_password' name='client_password' type='password' autocomplete='new-password' value=''><button id='toggle-password' type='button' aria-label='Show password' title='Show password'><svg class='eye-hidden' aria-hidden='true' viewBox='0 0 16 16'><path d='M13.359 11.238C15.06 9.72 16 8 16 8s-3-5.5-8-5.5a7 7 0 0 0-2.79.588l.77.771A6 6 0 0 1 8 3.5c2.12 0 3.879 1.168 5.168 2.457A13 13 0 0 1 14.828 8q-.086.13-.195.288c-.335.48-.83 1.12-1.465 1.755q-.247.248-.517.486z'/><path d='M11.297 9.176a3.5 3.5 0 0 0-4.474-4.474l.823.823a2.5 2.5 0 0 1 2.829 2.829zm-2.943 1.299.822.822a3.5 3.5 0 0 1-4.474-4.474l.823.823a2.5 2.5 0 0 0 2.829 2.829'/><path d='M3.35 5.47q-.27.24-.518.487A13 13 0 0 0 1.172 8l.195.288c.335.48.83 1.12 1.465 1.755C4.121 11.332 5.881 12.5 8 12.5c.716 0 1.39-.133 2.02-.36l.77.772A7 7 0 0 1 8 13.5C3 13.5 0 8 0 8s.939-1.721 2.641-3.238l.708.709zm10.296 8.884-12-12 .708-.708 12 12z'/></svg><svg class='eye-open' aria-hidden='true' viewBox='0 0 16 16'><path d='M16 8s-3-5.5-8-5.5S0 8 0 8s3 5.5 8 5.5S16 8 16 8M1.173 8a13 13 0 0 1 1.66-2.043C4.12 4.668 5.88 3.5 8 3.5s3.879 1.168 5.168 2.457A13 13 0 0 1 14.828 8q-.086.13-.195.288c-.335.48-.83 1.12-1.465 1.755C11.879 11.332 10.119 12.5 8 12.5s-3.879-1.168-5.168-2.457A13 13 0 0 1 1.172 8z'/><path d='M8 5.5a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5M4.5 8a3.5 3.5 0 1 1 7 0 3.5 3.5 0 0 1-7 0'/></svg></button></div></div>"));
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
