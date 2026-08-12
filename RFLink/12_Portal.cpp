#include "RFLink.h"

#ifndef RFLINK_PORTAL_DISABLED

#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <LittleFS.h>

#include "2_Signal.h"
#include "6_MQTT.h"
#include "11_Config.h"
#include "12_Portal.h"
#include "10_Wifi.h"

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
        const char json_name_auth_enabled[] = "auth_enabled";
        const char json_name_auth_user[] = "auth_user";
        const char json_name_auth_password[] = "auth_password";

        Config::ConfigItem configItems[] =  {
                Config::ConfigItem(json_name_enabled,      Config::SectionId::Portal_id, true, paramsUpdatedCallback),
                Config::ConfigItem(json_name_auth_enabled, Config::SectionId::Portal_id, true, paramsUpdatedCallback),
                Config::ConfigItem(json_name_auth_user,    Config::SectionId::Portal_id, RFLINK_WEBUI_DEFAULT_USER, paramsUpdatedCallback),
                Config::ConfigItem(json_name_auth_password,Config::SectionId::Portal_id, RFLINK_WEBUI_DEFAULT_PASSWORD, paramsUpdatedCallback),
                Config::ConfigItem(), // dont remove it!
        };

        namespace params {
          bool enabled = true;
          bool auth_enabled = false;
          String auth_user;
          String auth_password;
        }

        AsyncWebServer server(80);
        void serveWifiSetupPage(AsyncWebServerRequest *request);

        static const char PAGE_CSS[] PROGMEM = R"CSS(html,body{margin:0;height:100%;font-family:Avenir,Helvetica,Arial,sans-serif;-webkit-font-smoothing:antialiased;-moz-osx-font-smoothing:grayscale;color:#2c3e50}.app{display:flex;flex-direction:row;height:100vh}.menu{position:absolute;top:0;left:0;z-index:20;background:white;height:100vh;width:290px;transition:all .25s}.menu-backdrop{display:none}.page{position:absolute;top:0;left:290px;width:calc(100% - 290px);height:100%}.navbar>header{background:#333;padding:.5em;display:grid;grid-template-columns:auto auto auto;justify-items:stretch}.white{color:white}.menu-title{background:#585858;height:62px;display:flex;align-items:center;justify-content:center;color:#fff}.menu-title h1{margin:0;text-align:center}.menu-cat{background:#a8a7a7;padding:.5em 0;margin:0;list-style:none}.link{display:block;text-decoration:none;font-size:1rem;color:black;margin-top:0}.link>li{padding:10px;margin-top:0;list-style:none}.menu-cat li:hover{background:#0002}.link-icon{font-size:1.2rem}.page-slot-container{height:calc(100% - 78px);overflow-y:auto;padding:8px}.page-title{margin:0;text-align:center}.container{border:1px solid #0006;border-radius:5px;width:calc(100% - 14px);padding:5px;margin:15px 3px;background:white}.w-auto{width:min-content!important;max-width:calc(100% - 14px)}table{font-family:arial,sans-serif;border-collapse:collapse;width:100%;border:1px solid #ddd}tr,td,th{border:1px solid #ddd}td,th{text-align:left;padding:8px}.category{font-weight:bold;font-size:1.15rem}.setting{font-size:1.05rem}input,select,textarea,button{font-size:16px;box-sizing:border-box;max-width:100%}input[type=text]:disabled,input[type=password]:disabled,input[type=number]:disabled,select:disabled{opacity:.25}input[type=text],input[type=password],input[type=number],select,textarea{padding:8px;font-size:.9rem;box-sizing:border-box;width:100%;border:1px solid gray;border-radius:5px;-webkit-appearance:none}select{-webkit-appearance:menulist}button,input[type=submit],input[type=file]::-webkit-file-upload-button{margin:3px 0;padding:8px;font-size:.9rem;width:100%;cursor:pointer;border-radius:5px;-webkit-appearance:none}.btn-green{background-color:yellowgreen;border:1px solid #7fa734}.btn-red{background-color:rgb(223,71,89);border:rgb(198,69,85)}.btn-big{font-size:1.2rem;padding:12px}.switch{position:relative;display:inline-block;width:60px;height:30px}.switch input{opacity:0;width:0;height:0}.slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background-color:#ccc;-webkit-transition:.4s;transition:.4s}.slider:before{position:absolute;content:'';height:20px;width:20px;left:7px;bottom:5px;background-color:white;-webkit-transition:.15s;transition:.15s}input:checked+.slider{background-color:#2196F3}input:disabled+.slider{background-color:#f6f6f6}input:focus+.slider{box-shadow:0 0 1px #2196F3}input:checked+.slider:before{-webkit-transform:translateX(26px);-ms-transform:translateX(26px);transform:translateX(26px)}.slider.round{border-radius:20px}.slider.round:before{border-radius:50%}.input-container{padding:3px;width:100%}.status-connected,.status-enabled,.status-running{color:#3db43d}.status-disconnected{color:#dc872e}.status-disabled{color:#929292}pre{white-space:pre-wrap;overflow:auto;margin:0}.info-page{display:flex;height:calc(100% - 30px);justify-content:center;align-items:center;flex-direction:column;text-align:center}.info-page span{margin:2px 0}#toast-root{position:fixed;right:16px;top:16px;z-index:50}.toast-container{position:relative;overflow:hidden;padding:16px 24px;background-color:#aaa;display:flex;align-items:center;border-radius:5px;cursor:pointer;flex-direction:column;margin-bottom:16px;min-width:220px}.toast-container>p{margin:0}.toast-title{font-size:1.5rem;font-weight:bold}.toast-message,.toast-title{font-family:Avenir,Helvetica,Arial,sans-serif;color:white}.toast-success{background-color:#04b13d!important}.toast-warning{background-color:orange!important}.toast-error{background-color:crimson!important}.toast-close{position:absolute;top:0;right:0;width:32px;height:32px;padding:0;border:2px solid white;background-color:#fff}.toast-bar{position:absolute;background-color:#fff6;bottom:0;left:0;height:8px;width:100%}#menu-checkbox{display:none}.menu-link{display:none;color:#fff;text-transform:uppercase;border:1px solid white;border-radius:5px;padding:5px 10px;font-size:30px}@media(max-width:992px){.menu{left:-100%}.page{left:0;width:100%}.menu-link{display:inline-block}#menu-checkbox:checked~.menu{left:0}.menu-backdrop{display:none}#menu-checkbox:checked~.menu-backdrop{display:block;position:absolute;top:0;left:0;z-index:10;background:#0008;height:100vh;width:100%}})CSS";

        static const char APP_JS[] PROGMEM = R"JS((function(){function esc(s){return String(s||'').replace(/[&<>]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;'}[c]})}function toast(type,msg){var r=document.getElementById('toast-root');if(!r)return;var el=document.createElement('div');el.className='toast-container toast-'+type;el.innerHTML='<button class="toast-close" type="button">x</button><p class="toast-title">'+type.charAt(0).toUpperCase()+type.slice(1)+'</p><p class="toast-message">'+esc(msg)+'</p><div class="toast-bar"></div>';r.appendChild(el);var close=function(){if(el.parentNode)el.parentNode.removeChild(el)};el.onclick=close;setTimeout(close,type==='success'?5000:10000)}function loadWifi(){var b=document.getElementById('scan-wifi'),l=document.getElementById('wifi-networks');if(!b||!l)return;b.disabled=true;b.textContent='Scanning...';fetch('/api/wifi/scan',{credentials:'same-origin'}).then(function(r){return r.json()}).then(function(items){l.innerHTML='';items.forEach(function(n){var o=document.createElement('option');o.value=n.ssid;o.label=n.ssid+' ('+n.rssi+' dBm)';l.appendChild(o)});toast('success',items.length?'WiFi scan complete':'No networks found')}).catch(function(e){toast('error','WiFi scan failed: '+e)}).finally(function(){b.disabled=false;b.textContent='Scan'})}document.addEventListener('click',function(e){if(e.target&&e.target.id==='scan-wifi')loadWifi()},false);document.addEventListener('DOMContentLoaded',loadWifi);document.addEventListener('submit',function(e){var f=e.target;if(!f.matches('form[data-async-save],form[data-async-reboot]'))return;e.preventDefault();var b=f.querySelector('[type=submit]');if(b)b.disabled=true;var opts={method:f.method||'POST',credentials:'same-origin'};if(f.matches('form[data-async-save]')){var data=new FormData(f);data.append('async','1');opts.body=data}fetch(f.action,opts).then(function(resp){return resp.text().then(function(t){return {ok:resp.ok,text:t}})}).then(function(r){if(r.ok)toast('success',r.text||'Saved');else toast('error',r.text||'Request failed')}).catch(function(err){toast('error','A network error occured: '+err)}).finally(function(){if(b)b.disabled=false})},false)})();)JS";

        Config::ConfigItem *cfg(const char *name, Config::SectionId section) {
          return Config::findConfigItem(name, section);
        }

        String cfgString(const char *name, Config::SectionId section) {
          Config::ConfigItem *item = cfg(name, section);
          if(item == nullptr)
            return String();
          return String(item->getCharValue());
        }

        long cfgLong(const char *name, Config::SectionId section) {
          Config::ConfigItem *item = cfg(name, section);
          if(item == nullptr)
            return 0;
          return item->getLongIntValue();
        }

        bool cfgBool(const char *name, Config::SectionId section) {
          Config::ConfigItem *item = cfg(name, section);
          if(item == nullptr)
            return false;
          return item->getBoolValue();
        }

        void serveStaticJs(AsyncWebServerRequest *request) {
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

        void writeJsonEscaped(Print *out, const String &input) {
          for(size_t i = 0; i < input.length(); i++) {
            char c = input.charAt(i);
            if(c == '"' || c == '\\') {
              out->print('\\');
              out->print(c);
            } else if(c == '\n') {
              out->print(F("\\n"));
            } else if(c == '\r') {
              out->print(F("\\r"));
            } else {
              out->print(c);
            }
          }
        }

        void writePageHead(AsyncResponseStream *out, const __FlashStringHelper *title) {
          out->print(F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>"));
          out->print(title);
          out->print(F("</title><style>*{box-sizing:border-box}html,body{margin:0;min-height:100%;font-family:Avenir,Helvetica,Arial,sans-serif;color:#2c3e50;background:#f6f7f8}.app{min-height:100vh;display:flex;align-items:flex-start;justify-content:center;padding:18px}.page{width:100%;max-width:460px}.navbar{background:#333;color:#fff;border-radius:5px 5px 0 0;padding:14px 16px;text-align:center}.navbar h2{font-size:1.1rem;line-height:1.25;margin:0;overflow-wrap:anywhere}.page-slot-container{background:white;border:1px solid #0006;border-top:0;border-radius:0 0 5px 5px;padding:16px}.page-title{margin:0 0 14px;text-align:center;font-size:1.5rem}.field{margin:0 0 14px}.field label{display:block;font-size:.95rem;margin-bottom:5px}.ssid-row{display:grid;grid-template-columns:minmax(0,1fr) 82px;gap:8px}input,button{font-size:16px;width:100%;padding:10px;border:1px solid gray;border-radius:5px}button,input[type=submit]{cursor:pointer}.btn-green{background-color:yellowgreen;border:1px solid #7fa734}.btn-secondary{background:#e9ecef;border:1px solid #adb5bd}#toast-root{position:fixed;right:16px;top:16px;z-index:50}.toast-container{position:relative;overflow:hidden;padding:16px 24px;background-color:#aaa;display:flex;align-items:center;border-radius:5px;cursor:pointer;flex-direction:column;margin-bottom:16px;min-width:220px}.toast-container>p{margin:0}.toast-title{font-size:1.5rem;font-weight:bold}.toast-message,.toast-title{font-family:Avenir,Helvetica,Arial,sans-serif;color:white}.toast-success{background-color:#04b13d!important}.toast-error{background-color:crimson!important}.toast-close{position:absolute;top:0;right:0;width:32px;height:32px;padding:0;border:2px solid white;background-color:#fff}.toast-bar{position:absolute;background-color:#fff6;bottom:0;left:0;height:8px;width:100%}@media(max-width:520px){.app{padding:0}.page{max-width:none}.navbar,.page-slot-container{border-radius:0;border-left:0;border-right:0}.ssid-row{grid-template-columns:1fr}.page-title{font-size:1.35rem}}</style></head><body><div class='app'><div id='toast-root'></div><div class='page'><div class='navbar'><h2>"));
          out->print(F(RFLINK_HARDWARE_NAME));
          out->print(F("</h2></div><div class='page-slot-container'><h1 class='page-title'>"));
          out->print(title);
          out->print(F("</h1>"));
        }

        void writePageTail(AsyncResponseStream *out) {
          out->print(F("<script src='/assets/app.js'></script></div></div></div></body></html>"));
        }

        void writeFieldRowStart(AsyncResponseStream *out, const __FlashStringHelper *label) {
          out->print(F("<tr><td class='setting'>"));
          out->print(label);
          out->print(F("</td><td>"));
        }

        void writeFieldRowEnd(AsyncResponseStream *out) {
          out->print(F("</td></tr>"));
        }

        void writeTextInput(AsyncResponseStream *out, const __FlashStringHelper *label, const __FlashStringHelper *name, const String &value, const __FlashStringHelper *type) {
          if(strlen_P((PGM_P)label) > 0) {
            out->print(F("<label>"));
            out->print(label);
            out->print(F("</label>"));
          }
          out->print(F("<input name='"));
          out->print(name);
          out->print(F("' type='"));
          out->print(type);
          out->print(F("' value=\""));
          writeEscaped(out, value);
          out->print(F("\">"));
        }

        void writeTextInput(AsyncResponseStream *out, const __FlashStringHelper *label, const __FlashStringHelper *name, const String &value) {
          writeTextInput(out, label, name, value, F("text"));
        }

        void writeNumberInput(AsyncResponseStream *out, const __FlashStringHelper *label, const __FlashStringHelper *name, long value) {
          writeTextInput(out, label, name, String(value), F("number"));
        }

        void writeCheckbox(AsyncResponseStream *out, const __FlashStringHelper *label, const __FlashStringHelper *name, bool checked) {
          out->print(F("<div class='input-container'><label class='switch' style='margin:0 5px'><input name='"));
          out->print(name);
          out->print(F("' type='checkbox' value='1'"));
          if(checked)
            out->print(F(" checked"));
          out->print(F("><span class='slider round'></span></label>"));
          if(strlen_P((PGM_P)label) > 0) {
            out->print(F(" "));
            out->print(label);
          }
          out->print(F("</div>"));
        }

        bool formBool(AsyncWebServerRequest *request, const __FlashStringHelper *name) {
          return request->hasParam(name, true);
        }

        String formString(AsyncWebServerRequest *request, const __FlashStringHelper *name) {
          if(!request->hasParam(name, true))
            return String();
          return request->getParam(name, true)->value();
        }

        long formLong(AsyncWebServerRequest *request, const __FlashStringHelper *name, long defaultValue = 0) {
          if(!request->hasParam(name, true))
            return defaultValue;
          return request->getParam(name, true)->value().toInt();
        }

        bool pushConfigAndReply(AsyncWebServerRequest *request, JsonObject root, const __FlashStringHelper *page) {
          String message;
          message.reserve(256);
          bool ok = Config::pushNewConfiguration(root, message, true);

          if(request->hasParam(F("async"), true)) {
            if(ok)
              request->send(200, F("text/plain"), message);
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
          if(!params::auth_enabled)
            return true;

          if(requestIsFromAccessPoint(request))
            return true;

          if(!request->authenticate(params::auth_user.c_str(), params::auth_password.c_str())) {
            request->requestAuthentication();
            return false;
          }
          return true;
        }

        bool checkPageAuthentication(AsyncWebServerRequest *request) {
          if(requestIsFromAccessPoint(request))
            return true;

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
          response->print(F("<form method='post' action='/wifi' data-async-save='1'><div class='field'><label for='client_ssid'>WiFi name</label><div class='ssid-row'><input id='client_ssid' name='client_ssid' list='wifi-networks' type='text' autocomplete='off' value=\""));
          writeEscaped(response, cfgString("client_ssid", Config::SectionId::Wifi_id));
          response->print(F("\"><button id='scan-wifi' class='btn-secondary' type='button'>Scan</button></div><datalist id='wifi-networks'></datalist></div><div class='field'><label for='client_password'>WiFi password</label><input id='client_password' name='client_password' type='password' autocomplete='current-password' value=\""));
          writeEscaped(response, cfgString("client_password", Config::SectionId::Wifi_id));
          response->print(F("\"></div><input type='submit' class='btn-green' value='Save'></form>"));
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
          wifi[F("ap_enabled")] = true;
          wifi[F("ap_ssid")] = cfgString("ap_ssid", Config::SectionId::Wifi_id);
          wifi[F("ap_password")] = cfgString("ap_password", Config::SectionId::Wifi_id);
          wifi[F("ap_ip")] = cfgString("ap_ip", Config::SectionId::Wifi_id);
          wifi[F("ap_network")] = cfgString("ap_network", Config::SectionId::Wifi_id);
          wifi[F("ap_mask")] = cfgString("ap_mask", Config::SectionId::Wifi_id);

          pushConfigAndReply(request, root, F("/wifi"));
        }

        void serveWifiScanGet(AsyncWebServerRequest *request) {
          if(!checkPageAuthentication(request))
            return;

          int count = WiFi.scanNetworks();
          AsyncResponseStream *response = request->beginResponseStream(F("application/json"));
          response->print('[');
          for(int i = 0; i < count; i++) {
            if(i > 0)
              response->print(',');
            response->print(F("{\"ssid\":\""));
            writeJsonEscaped(response, WiFi.SSID(i));
            response->print(F("\",\"rssi\":"));
            response->print(WiFi.RSSI(i));
            response->print(F(",\"secure\":"));
            response->print(WiFi.encryptionType(i) == ENC_TYPE_NONE ? F("false") : F("true"));
            response->print('}');
          }
          response->print(']');
          WiFi.scanDelete();
          sendStream(request, response, F("WiFi scan"));
        }

        void serverApiConfigGet(AsyncWebServerRequest *request) {
          if(!checkHttpAuthentication(request))
            return;

          String dump;
          if(! dump.reserve(2048))
          {
            Serial.println(F("Not enough memory"));
            request->send(500, F("text/plain"), F("Not enough memory"));
            return;
          }
          Config::dumpConfigToString(dump);
          request->send(200, F("application/json"), dump);
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

        void serveApiReboot(AsyncWebServerRequest *request) {
          if(!checkHttpAuthentication(request))
            return;

          request->send(200, F("text/plain"), F("Rebooting in 5 seconds"));
          RFLink::scheduleReboot(5);
        }

        void serverApiConfigPush(AsyncWebServerRequest *request, JsonVariant &json) {
          if(!checkHttpAuthentication(request))
            return;

          if (not json.is<JsonObject>()) {
            Serial.println(F("API Config push requested but invalid JSON was received!"));
            request->send(400, F("text/plain"), F("Not an object"));
            return;
          }

          JsonObject && data = json.as<JsonObject>();

          String message;
          message.reserve(256); // reserve 256 to avoid fragmentation

          String response;
          response.reserve(256);

          if( !Config::pushNewConfiguration(data, message, true) ) {
            response = F("{ \"success\": false, \"message\": ");
          }
          else {
            response = F("{ \"success\": true, \"message\": ");
          }

          if( message.length() > 0 ) {
            response += '"';
            response += message + "\"}";
          } else {
            response += F(" null }");
          }

          request->send(200, F("application/json"), response);
        }

        void init() {

          refreshParametersFromConfig(false);

          server.onNotFound(notFound);
          server.on(PSTR("/assets/app.js"), HTTP_GET, serveStaticJs);
          server.on(PSTR("/"), HTTP_GET, serveRootPage);
          server.on(PSTR("/index.html"), HTTP_GET, serveRootPage);
          server.on(PSTR("/home"), HTTP_GET, serveWifiRedirect);
          server.on(PSTR("/wifi"), HTTP_GET, serveWifiSetupPage);
          server.on(PSTR("/wifi"), HTTP_POST, serveWifiSetupPost);
          server.on(PSTR("/services"), HTTP_GET, serveWifiRedirect);
          server.on(PSTR("/portal"), HTTP_GET, serveServicesRedirect);
          server.on(PSTR("/mqtt"), HTTP_GET, serveServicesRedirect);
          server.on(PSTR("/signal"), HTTP_GET, serveWifiRedirect);
          server.on(PSTR("/advanced"), HTTP_GET, serveWifiRedirect);
          server.on(PSTR("/infos"), HTTP_GET, serveWifiRedirect);
          server.on(PSTR("/generate_204"), HTTP_GET, serveCaptiveGenerate204);
          server.on(PSTR("/gen_204"), HTTP_GET, serveCaptiveGenerate204);
          server.on(PSTR("/capport"), HTTP_GET, serveCaptivePortalApi);
          server.on(PSTR("/hotspot-detect.html"), HTTP_GET, serveCaptiveIosSuccess);
          server.on(PSTR("/library/test/success.html"), HTTP_GET, serveCaptiveIosSuccess);
          server.on(PSTR("/connecttest.txt"), HTTP_GET, serveCaptiveConnectTest);
          server.on(PSTR("/ncsi.txt"), HTTP_GET, serveCaptiveNcsi);
          server.on(PSTR("/canonical.html"), HTTP_GET, serveCaptivePortal);
          server.on(PSTR("/fwlink"), HTTP_GET, serveCaptivePortal);
          server.on(PSTR("/redirect"), HTTP_GET, serveCaptivePortal);
          server.on(PSTR("/wifi-setup"), HTTP_GET, serveWifiRedirect);
          server.on(PSTR("/wifi-setup"), HTTP_POST, serveWifiSetupPost);

          server.on(PSTR("/api/config"), HTTP_GET, serverApiConfigGet);
          server.on(PSTR("/api/status"), HTTP_GET, serveApiStatusGet);
          server.on(PSTR("/api/wifi/scan"), HTTP_GET, serveWifiScanGet);

          server.on(PSTR("/api/reboot"), HTTP_GET, serveApiReboot);
          server.on(PSTR("/reboot"), HTTP_POST, serveApiReboot);

          AsyncCallbackJsonWebHandler* handler = new AsyncCallbackJsonWebHandler(PSTR("/api/config"), serverApiConfigPush, 4000);
          server.addHandler(handler);

        }

        void start() {
          if(params::enabled) {
            Serial.print(F("Starting WebServer... "));
            server.begin();
            Serial.println(F("OK"));
          }
        }

        void stop() {
          Serial.print(F("Stopping WebServer... "));
          server.end();
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

        item = Config::findConfigItem(json_name_auth_enabled, Config::SectionId::Portal_id);
        if (item->getBoolValue() != params::auth_enabled) {
          //changesDetected = true; // no need to trigger an update cycle
          params::auth_enabled = item->getBoolValue();
        }

        item = Config::findConfigItem(json_name_auth_user, Config::SectionId::Portal_id);
        if( params::auth_user != item->getCharValue() ) {
          //changesDetected = true; // no need to trigger an update cycle
          params::auth_user = item->getCharValue();
        }

        item = Config::findConfigItem(json_name_auth_password, Config::SectionId::Portal_id);
        if( params::auth_password != item->getCharValue() ) {
          //changesDetected = true; // no need to trigger an update cycle
          params::auth_password = item->getCharValue();
        }

        if (triggerChanges && changesDetected) {
          if(params::enabled)
            server.begin();
          else
            server.end();
        }
      }

    } // end of Portal namespace
} // end of RFLink namespace

#endif // RFLINK_PORTAL_DISABLED
