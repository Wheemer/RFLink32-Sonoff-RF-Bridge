#include "RFLink.h"

#ifndef RFLINK_PORTAL_DISABLED

#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <LittleFS.h>

#include "2_Signal.h"
#include "6_MQTT.h"
#include "9_Serial2Net.h"
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
        void serveStatusPage(AsyncWebServerRequest *request);
        void serveWifiSetupPage(AsyncWebServerRequest *request);
        void serveServicesPage(AsyncWebServerRequest *request);

        static const char PAGE_CSS[] PROGMEM = R"CSS(html,body{margin:0;height:100%;font-family:Avenir,Helvetica,Arial,sans-serif;-webkit-font-smoothing:antialiased;-moz-osx-font-smoothing:grayscale;color:#2c3e50}.app{display:flex;flex-direction:row;height:100vh}.menu{position:absolute;top:0;left:0;z-index:20;background:white;height:100vh;width:290px;transition:all .25s}.menu-backdrop{display:none}.page{position:absolute;top:0;left:290px;width:calc(100% - 290px);height:100%}.navbar>header{background:#333;padding:.5em;display:grid;grid-template-columns:auto auto auto;justify-items:stretch}.white{color:white}.menu-title{background:#585858;height:62px;display:flex;align-items:center;justify-content:center;color:#fff}.menu-title h1{margin:0;text-align:center}.menu-cat{background:#a8a7a7;padding:.5em 0;margin:0;list-style:none}.link{display:block;text-decoration:none;font-size:1rem;color:black;margin-top:0}.link>li{padding:10px;margin-top:0;list-style:none}.menu-cat li:hover{background:#0002}.link-icon{font-size:1.2rem}.page-slot-container{height:calc(100% - 78px);overflow-y:auto;padding:8px}.page-title{margin:0;text-align:center}.container{border:1px solid #0006;border-radius:5px;width:calc(100% - 14px);padding:5px;margin:15px 3px;background:white}.w-auto{width:min-content!important;max-width:calc(100% - 14px)}table{font-family:arial,sans-serif;border-collapse:collapse;width:100%;border:1px solid #ddd}tr,td,th{border:1px solid #ddd}td,th{text-align:left;padding:8px}.category{font-weight:bold;font-size:1.15rem}.setting{font-size:1.05rem}input,select,textarea,button{font-size:16px;box-sizing:border-box;max-width:100%}input[type=text]:disabled,input[type=password]:disabled,input[type=number]:disabled,select:disabled{opacity:.25}input[type=text],input[type=password],input[type=number],select,textarea{padding:8px;font-size:.9rem;box-sizing:border-box;width:100%;border:1px solid gray;border-radius:5px;-webkit-appearance:none}select{-webkit-appearance:menulist}button,input[type=submit],input[type=file]::-webkit-file-upload-button{margin:3px 0;padding:8px;font-size:.9rem;width:100%;cursor:pointer;border-radius:5px;-webkit-appearance:none}.btn-green{background-color:yellowgreen;border:1px solid #7fa734}.btn-red{background-color:rgb(223,71,89);border:rgb(198,69,85)}.btn-big{font-size:1.2rem;padding:12px}.switch{position:relative;display:inline-block;width:60px;height:30px}.switch input{opacity:0;width:0;height:0}.slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background-color:#ccc;-webkit-transition:.4s;transition:.4s}.slider:before{position:absolute;content:'';height:20px;width:20px;left:7px;bottom:5px;background-color:white;-webkit-transition:.15s;transition:.15s}input:checked+.slider{background-color:#2196F3}input:disabled+.slider{background-color:#f6f6f6}input:focus+.slider{box-shadow:0 0 1px #2196F3}input:checked+.slider:before{-webkit-transform:translateX(26px);-ms-transform:translateX(26px);transform:translateX(26px)}.slider.round{border-radius:20px}.slider.round:before{border-radius:50%}.input-container{padding:3px;width:100%}.status-connected,.status-enabled,.status-running{color:#3db43d}.status-disconnected{color:#dc872e}.status-disabled{color:#929292}pre{white-space:pre-wrap;overflow:auto;margin:0}.info-page{display:flex;height:calc(100% - 30px);justify-content:center;align-items:center;flex-direction:column;text-align:center}.info-page span{margin:2px 0}#toast-root{position:fixed;right:16px;top:16px;z-index:50}.toast-container{position:relative;overflow:hidden;padding:16px 24px;background-color:#aaa;display:flex;align-items:center;border-radius:5px;cursor:pointer;flex-direction:column;margin-bottom:16px;min-width:220px}.toast-container>p{margin:0}.toast-title{font-size:1.5rem;font-weight:bold}.toast-message,.toast-title{font-family:Avenir,Helvetica,Arial,sans-serif;color:white}.toast-success{background-color:#04b13d!important}.toast-warning{background-color:orange!important}.toast-error{background-color:crimson!important}.toast-close{position:absolute;top:0;right:0;width:32px;height:32px;padding:0;border:2px solid white;background-color:#fff}.toast-bar{position:absolute;background-color:#fff6;bottom:0;left:0;height:8px;width:100%}#menu-checkbox{display:none}.menu-link{display:none;color:#fff;text-transform:uppercase;border:1px solid white;border-radius:5px;padding:5px 10px;font-size:30px}@media(max-width:992px){.menu{left:-100%}.page{left:0;width:100%}.menu-link{display:inline-block}#menu-checkbox:checked~.menu{left:0}.menu-backdrop{display:none}#menu-checkbox:checked~.menu-backdrop{display:block;position:absolute;top:0;left:0;z-index:10;background:#0008;height:100vh;width:100%}})CSS";

        static const char APP_JS[] PROGMEM = R"JS((function(){function esc(s){return String(s||'').replace(/[&<>]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;'}[c]})}function toast(type,msg){var r=document.getElementById('toast-root');if(!r)return;var el=document.createElement('div');el.className='toast-container toast-'+type;el.innerHTML='<button class="toast-close" type="button">x</button><p class="toast-title">'+type.charAt(0).toUpperCase()+type.slice(1)+'</p><p class="toast-message">'+esc(msg)+'</p><div class="toast-bar"></div>';r.appendChild(el);var close=function(){if(el.parentNode)el.parentNode.removeChild(el)};el.onclick=close;setTimeout(close,type==='success'?5000:10000)}document.addEventListener('submit',function(e){var f=e.target;if(!f.matches('form[data-async-save],form[data-async-reboot]'))return;e.preventDefault();var b=f.querySelector('[type=submit]');if(b)b.disabled=true;var opts={method:f.method||'POST',credentials:'same-origin'};if(f.matches('form[data-async-save]')){var data=new FormData(f);data.append('async','1');opts.body=data}fetch(f.action,opts).then(function(resp){return resp.text().then(function(t){return {ok:resp.ok,text:t}})}).then(function(r){if(r.ok)toast('success',r.text||'Saved');else toast('error',r.text||'Request failed')}).catch(function(err){toast('error','A network error occured: '+err)}).finally(function(){if(b)b.disabled=false})},false)})();)JS";

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

        void serveStaticCss(AsyncWebServerRequest *request) {
          AsyncWebServerResponse *response = request->beginResponse_P(200, F("text/css"), PAGE_CSS);
          response->addHeader(F("Cache-Control"), F("public, max-age=86400"));
          request->send(response);
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

        void writePageHead(AsyncResponseStream *out, const __FlashStringHelper *title) {
          out->print(F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>"));
          out->print(title);
          out->print(F("</title><link rel='stylesheet' href='/assets/style.css'><style>*{box-sizing:border-box}html,body{color:#2c3e50;overflow-x:hidden}.navbar>header{background-color:#333}.navbar h2{overflow-wrap:anywhere}.menu{background-color:white}.menu-title{background-color:#585858;color:#fff}.menu-cat{background-color:#a8a7a7;color:#fff;padding:.5em 0}.link{color:black;text-decoration:none;font-size:1rem}.link>li{padding:10px;margin-top:0;list-style:none}.link>li:hover{background-color:#0002}.link-icon{font-size:1.2rem}.page-slot-container{overflow-x:hidden}.container{border:1px solid #0006;border-radius:5px;padding:5px;margin:15px 3px;width:calc(100% - 6px);max-width:760px;overflow-x:hidden}.page-title{margin:0}table{width:100%;max-width:100%;table-layout:fixed}td,th{overflow-wrap:anywhere;word-break:break-word}.category{width:28%}.setting{width:34%}.info-page{display:flex;height:calc(100% - 30px);justify-content:flex-start;align-items:center;flex-direction:column;text-align:left}.info-page .container{max-width:760px}.info-page h2{text-align:center;margin:6px 0 10px}.credit-line{line-height:1.35;margin:8px 0}.switch{position:relative;display:inline-block;width:60px;height:30px}.switch input{opacity:0;width:0;height:0}.slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background-color:#ccc;transition:.4s}.slider:before{position:absolute;content:'';height:20px;width:20px;left:7px;bottom:5px;background-color:white;transition:.15s}input:checked+.slider{background-color:#2196F3}input:checked+.slider:before{transform:translateX(26px)}.slider.round{border-radius:20px}.slider.round:before{border-radius:50%}@media(max-width:600px){.page-slot-container{padding:6px}.container{width:100%;max-width:100%;margin:10px 0;padding:5px}.page-title{font-size:1.45rem}.navbar>header{grid-template-columns:42px minmax(0,1fr) 42px}.navbar h2{font-size:1rem;line-height:1.2}td,th{padding:6px 5px;font-size:.92rem}.category{font-size:.98rem;width:30%}.setting{font-size:.92rem;width:35%}input,select,textarea,button{font-size:16px}.btn-big{font-size:1rem;padding:10px}.link>li{padding:12px 10px}}</style></head><body><div class='app'><input type='checkbox' id='menu-checkbox'><div class='menu'><nav role='navigation' class='mobile-menu' style='display:flex;flex-direction:column;height:100%'><div class='menu-title'><h1>Menu</h1></div><ul class='menu-cat' style='flex-grow:1'>"));
          out->print(F("<a class='link' href='/home'><li><span class='link-icon'>&#127968;</span> Home</li></a><a class='link' href='/wifi'><li><span class='link-icon'>&#128246;</span> Wifi</li></a><a class='link' href='/signal'><li><span class='link-icon'>&#128225;</span> Signals</li></a><a class='link' href='/services'><li><span class='link-icon'>&#127760;</span> Services</li></a>"));
          out->print(F("</ul><ul class='menu-cat'><a class='link' href='/infos'><li>Infos / Credits</li></a></ul></nav></div><label class='menu-backdrop' for='menu-checkbox'></label><div id='toast-root'></div><div class='page'><div class='navbar'><header><div><label for='menu-checkbox'><span class='menu-link'>&#9776;</span></label></div><div style='display:flex;justify-content:center;flex-direction:column'><h2 class='white' style='margin:0;text-align:center'>"));
          out->print(F(RFLINK_HARDWARE_NAME));
          out->print(F("</h2></div><div></div></header></div><div class='page-slot-container'><h1 class='page-title'>"));
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
          if(requestIsFromAccessPoint(request)) {
            serveWifiSetupPage(request);
            return;
          }

          serveStatusPage(request);
        }

        void serveRedirect(AsyncWebServerRequest *request, const __FlashStringHelper *location) {
          AsyncWebServerResponse *response = request->beginResponse(302);
          response->addHeader(F("Location"), location);
          response->addHeader(F("Cache-Control"), F("no-store"));
          request->send(response);
        }

        void serveServicesRedirect(AsyncWebServerRequest *request) {
          serveRedirect(request, F("/services"));
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

        void serveStatusPage(AsyncWebServerRequest *request) {
          if(!checkHttpAuthentication(request))
            return;

          DynamicJsonDocument output(1000);
          JsonObject obj = output.to<JsonObject>();
          RFLink::getStatusJsonString(obj);
          RFLink::Wifi::getStatusJsonString(obj);
          #ifndef RFLINK_MQTT_DISABLED
          RFLink::Mqtt::getStatusJsonString(obj);
          #endif
          RFLink::Signal::getStatusJsonString(obj);
          RFLink::Serial2Net::getStatusJsonString(obj);

          String uptime = formatUptime(obj[F("uptime")].as<unsigned long>());
          String swVersion = jsonStringOr(obj[F("sw_version")], F("unknown"));
          String hardware = jsonStringOr(obj[F("hardware")], F("unknown"));
          String receivedCount = jsonStringOr(obj[F("signal")][F("received_signal_count")], F("0"));
          String decodedCount = jsonStringOr(obj[F("signal")][F("successfully_decoded_count")], F("0"));
          String mqttStatus = jsonStringOr(obj[F("mqtt")][F("status")], F("disabled"));
          String wifiClientStatus = jsonStringOr(obj[F("network")][F("wifi_client")][F("status")], F("disconnected"));
          String wifiClientIp = jsonStringOr(obj[F("network")][F("wifi_client")][F("ip")], F(""));
          String wifiClientNetmask = jsonStringOr(obj[F("network")][F("wifi_client")][F("netmask")], F(""));
          String wifiClientDns = jsonStringOr(obj[F("network")][F("wifi_client")][F("dns")], F(""));
          String wifiApStatus = jsonStringOr(obj[F("network")][F("wifi_ap")][F("status")], F("disabled"));
          String serialStatus = jsonStringOr(obj[F("serial2net")][F("status")], F("disabled"));
          String serialClients = jsonStringOr(obj[F("serial2net")][F("clients_count")], F("0"));
          output.clear();

          AsyncResponseStream *response = request->beginResponseStream(F("text/html; charset=utf-8"));
          writePageHead(response, F("Home"));
          response->print(F("<div style='display:flex;flex-direction:column;align-items:center'><div class='container' style='min-width:33%'><h2 style='margin:5px 0'>Status</h2><table>"));
          writeStatusValueRow(response, F("Uptime"), uptime);
          writeStatusValueRow(response, F("Software Version"), swVersion);
          writeStatusValueRow(response, F("Hardware"), hardware);
          response->print(F("<tr><td rowspan='3' class='category'>Signals</td></tr>"));
          writeStatusValueRow(response, F("Received Count"), receivedCount);
          writeStatusValueRow(response, F("Successfully Decoded"), decodedCount);
          response->print(F("<tr><td rowspan='2' class='category'>MQTT</td></tr>"));
          writeStatusValueRow(response, F("Status"), mqttStatus, mqttStatus);
          response->print(F("<tr><td rowspan='5' class='category'>Wifi Client Mode</td></tr>"));
          writeStatusValueRow(response, F("Status"), wifiClientStatus, wifiClientStatus);
          writeStatusValueRow(response, F("IP Address"), wifiClientIp);
          writeStatusValueRow(response, F("Netmask"), wifiClientNetmask);
          writeStatusValueRow(response, F("DNS"), wifiClientDns);
          response->print(F("<tr><td rowspan='2' class='category'>Wifi Access Point</td></tr>"));
          writeStatusValueRow(response, F("Status"), wifiApStatus, wifiApStatus);
          response->print(F("<tr><td rowspan='3' class='category'>Serial2Net</td></tr>"));
          writeStatusValueRow(response, F("Status"), serialStatus, serialStatus);
          writeStatusValueRow(response, F("Client count"), serialClients);
          response->print(F("</table></div><div class='container' style='min-width:33%'><h2 style='margin:5px 0'>Controls</h2><form method='post' action='/reboot' data-async-reboot='1'><button class='btn-red btn-big' type='submit'>REBOOT</button></form></div></div>"));
          writePageTail(response);
          sendStream(request, response, F("Home"));
        }

        void serveWifiSetupPage(AsyncWebServerRequest *request) {
          if(!checkPageAuthentication(request))
            return;

          AsyncResponseStream *response = request->beginResponseStream(F("text/html; charset=utf-8"));
          writePageHead(response, F("WiFi"));
          response->print(F("<form method='post' action='/wifi' data-async-save='1'><div class='container'><h2 style='margin:5px 0'>Client</h2><table>"));
          writeFieldRowStart(response, F("Enable client WiFi"));
          writeCheckbox(response, F(""), F("client_enabled"), cfgBool("client_enabled", Config::SectionId::Wifi_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("WiFi name"));
          writeTextInput(response, F(""), F("client_ssid"), cfgString("client_ssid", Config::SectionId::Wifi_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("WiFi password"));
          writeTextInput(response, F(""), F("client_password"), cfgString("client_password", Config::SectionId::Wifi_id), F("password"));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Use DHCP"));
          writeCheckbox(response, F(""), F("client_dhcp_enabled"), cfgBool("client_dhcp_enabled", Config::SectionId::Wifi_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Static IP"));
          writeTextInput(response, F(""), F("client_ip"), cfgString("client_ip", Config::SectionId::Wifi_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Mask"));
          writeTextInput(response, F(""), F("client_mask"), cfgString("client_mask", Config::SectionId::Wifi_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Gateway"));
          writeTextInput(response, F(""), F("client_gateway"), cfgString("client_gateway", Config::SectionId::Wifi_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("DNS"));
          writeTextInput(response, F(""), F("client_dns"), cfgString("client_dns", Config::SectionId::Wifi_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Hostname"));
          writeTextInput(response, F(""), F("client_hostname"), cfgString("client_hostname", Config::SectionId::Wifi_id));
          writeFieldRowEnd(response);
          response->print(F("</table></div><div class='container'><h2 style='margin:5px 0'>Access Point</h2><table>"));
          writeFieldRowStart(response, F("Enable AP"));
          writeCheckbox(response, F(""), F("ap_enabled"), cfgBool("ap_enabled", Config::SectionId::Wifi_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("AP SSID"));
          writeTextInput(response, F(""), F("ap_ssid"), cfgString("ap_ssid", Config::SectionId::Wifi_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("AP password"));
          writeTextInput(response, F(""), F("ap_password"), cfgString("ap_password", Config::SectionId::Wifi_id), F("password"));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("AP IP"));
          writeTextInput(response, F(""), F("ap_ip"), cfgString("ap_ip", Config::SectionId::Wifi_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("AP network"));
          writeTextInput(response, F(""), F("ap_network"), cfgString("ap_network", Config::SectionId::Wifi_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("AP mask"));
          writeTextInput(response, F(""), F("ap_mask"), cfgString("ap_mask", Config::SectionId::Wifi_id));
          writeFieldRowEnd(response);
          response->print(F("</table><input type='submit' class='btn-green' value='Save'></div></form>"));
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

          StaticJsonDocument<640> input;
          JsonObject root = input.to<JsonObject>();
          JsonObject wifi = root.createNestedObject(F("wifi"));
          wifi[F("client_enabled")] = formBool(request, F("client_enabled"));
          wifi[F("client_dhcp_enabled")] = request->hasParam(F("client_dhcp_enabled"), true) ? formBool(request, F("client_dhcp_enabled")) : cfgBool("client_dhcp_enabled", Config::SectionId::Wifi_id);
          wifi[F("client_ssid")] = ssid;
          wifi[F("client_password")] = password;
          wifi[F("client_ip")] = request->hasParam(F("client_ip"), true) ? formString(request, F("client_ip")) : cfgString("client_ip", Config::SectionId::Wifi_id);
          wifi[F("client_mask")] = request->hasParam(F("client_mask"), true) ? formString(request, F("client_mask")) : cfgString("client_mask", Config::SectionId::Wifi_id);
          wifi[F("client_gateway")] = request->hasParam(F("client_gateway"), true) ? formString(request, F("client_gateway")) : cfgString("client_gateway", Config::SectionId::Wifi_id);
          wifi[F("client_dns")] = request->hasParam(F("client_dns"), true) ? formString(request, F("client_dns")) : cfgString("client_dns", Config::SectionId::Wifi_id);
          wifi[F("client_hostname")] = request->hasParam(F("client_hostname"), true) ? formString(request, F("client_hostname")) : cfgString("client_hostname", Config::SectionId::Wifi_id);
          wifi[F("ap_enabled")] = request->hasParam(F("ap_enabled"), true) ? formBool(request, F("ap_enabled")) : cfgBool("ap_enabled", Config::SectionId::Wifi_id);
          wifi[F("ap_ssid")] = request->hasParam(F("ap_ssid"), true) ? formString(request, F("ap_ssid")) : cfgString("ap_ssid", Config::SectionId::Wifi_id);
          wifi[F("ap_password")] = request->hasParam(F("ap_password"), true) ? formString(request, F("ap_password")) : cfgString("ap_password", Config::SectionId::Wifi_id);
          wifi[F("ap_ip")] = request->hasParam(F("ap_ip"), true) ? formString(request, F("ap_ip")) : cfgString("ap_ip", Config::SectionId::Wifi_id);
          wifi[F("ap_network")] = request->hasParam(F("ap_network"), true) ? formString(request, F("ap_network")) : cfgString("ap_network", Config::SectionId::Wifi_id);
          wifi[F("ap_mask")] = request->hasParam(F("ap_mask"), true) ? formString(request, F("ap_mask")) : cfgString("ap_mask", Config::SectionId::Wifi_id);

          pushConfigAndReply(request, root, F("/wifi"));
        }

        void serveMqttPost(AsyncWebServerRequest *request) {
          if(!checkHttpAuthentication(request))
            return;

          StaticJsonDocument<512> input;
          JsonObject root = input.to<JsonObject>();
          JsonObject mqtt = root.createNestedObject(F("mqtt"));
          mqtt[F("enabled")] = formBool(request, F("enabled"));
          mqtt[F("server")] = formString(request, F("server"));
          mqtt[F("port")] = formLong(request, F("port"), 1883);
          mqtt[F("id")] = formString(request, F("id"));
          mqtt[F("user")] = formString(request, F("user"));
          mqtt[F("password")] = formString(request, F("password"));
          mqtt[F("topic_in")] = formString(request, F("topic_in"));
          mqtt[F("topic_out")] = formString(request, F("topic_out"));
          mqtt[F("lwt_enabled")] = formBool(request, F("lwt_enabled"));
          mqtt[F("topic_lwt")] = formString(request, F("topic_lwt"));
          pushConfigAndReply(request, root, F("/services"));
        }

        void serveSerialPost(AsyncWebServerRequest *request) {
          if(!checkHttpAuthentication(request))
            return;

          StaticJsonDocument<192> input;
          JsonObject root = input.to<JsonObject>();
          JsonObject serial = root.createNestedObject(F("serial2net"));
          serial[F("enabled")] = formBool(request, F("enabled"));
          serial[F("port")] = formLong(request, F("port"), 1900);
          pushConfigAndReply(request, root, F("/services"));
        }

        void servePortalPost(AsyncWebServerRequest *request) {
          if(!checkHttpAuthentication(request))
            return;

          StaticJsonDocument<256> input;
          JsonObject root = input.to<JsonObject>();
          JsonObject portal = root.createNestedObject(F("portal"));
          portal[F("enabled")] = formBool(request, F("enabled"));
          portal[F("auth_enabled")] = formBool(request, F("auth_enabled"));
          portal[F("auth_user")] = formString(request, F("auth_user"));
          portal[F("auth_password")] = formString(request, F("auth_password"));
          pushConfigAndReply(request, root, F("/portal"));
        }

        void serveSignalPage(AsyncWebServerRequest *request) {
          if(!checkHttpAuthentication(request))
            return;

          AsyncResponseStream *response = request->beginResponseStream(F("text/html; charset=utf-8"));
          writePageHead(response, F("Signals"));
          response->print(F("<div class='container'><h2 style='margin:5px 0'>Signal</h2><form method='post' action='/signal' data-async-save='1'><table>"));
          writeFieldRowStart(response, F("Sample rate"));
          writeNumberInput(response, F(""), F("sample_rate"), cfgLong("sample_rate", Config::SectionId::Signal_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Min raw pulses"));
          writeNumberInput(response, F(""), F("min_raw_pulses"), cfgLong("min_raw_pulses", Config::SectionId::Signal_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Seek timeout"));
          writeNumberInput(response, F(""), F("seek_timeout"), cfgLong("seek_timeout", Config::SectionId::Signal_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Min preamble"));
          writeNumberInput(response, F(""), F("min_preamble"), cfgLong("min_preamble", Config::SectionId::Signal_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Min pulse length"));
          writeNumberInput(response, F(""), F("min_pulse_len"), cfgLong("min_pulse_len", Config::SectionId::Signal_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Signal end timeout"));
          writeNumberInput(response, F(""), F("signal_end_timeout"), cfgLong("signal_end_timeout", Config::SectionId::Signal_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Signal repeat time"));
          writeNumberInput(response, F(""), F("signal_repeat_time"), cfgLong("signal_repeat_time", Config::SectionId::Signal_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Scan high time"));
          writeNumberInput(response, F(""), F("scan_high_time"), cfgLong("scan_high_time", Config::SectionId::Signal_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Async mode"));
          writeCheckbox(response, F("enabled"), F("async_mode_enabled"), cfgBool("async_mode_enabled", Config::SectionId::Signal_id));
          writeFieldRowEnd(response);
          response->print(F("</table><input type='submit' class='btn-green' value='Save'></form></div>"));
          writePageTail(response);
          sendStream(request, response, F("Signals"));
        }

        void serveSignalPost(AsyncWebServerRequest *request) {
          if(!checkHttpAuthentication(request))
            return;

          StaticJsonDocument<384> input;
          JsonObject root = input.to<JsonObject>();
          JsonObject signal = root.createNestedObject(F("signal"));
          signal[F("async_mode_enabled")] = formBool(request, F("async_mode_enabled"));
          signal[F("sample_rate")] = request->hasParam(F("sample_rate"), true) ? formLong(request, F("sample_rate")) : cfgLong("sample_rate", Config::SectionId::Signal_id);
          signal[F("min_raw_pulses")] = request->hasParam(F("min_raw_pulses"), true) ? formLong(request, F("min_raw_pulses")) : cfgLong("min_raw_pulses", Config::SectionId::Signal_id);
          signal[F("seek_timeout")] = request->hasParam(F("seek_timeout"), true) ? formLong(request, F("seek_timeout")) : cfgLong("seek_timeout", Config::SectionId::Signal_id);
          signal[F("min_preamble")] = request->hasParam(F("min_preamble"), true) ? formLong(request, F("min_preamble")) : cfgLong("min_preamble", Config::SectionId::Signal_id);
          signal[F("min_pulse_len")] = request->hasParam(F("min_pulse_len"), true) ? formLong(request, F("min_pulse_len")) : cfgLong("min_pulse_len", Config::SectionId::Signal_id);
          signal[F("signal_end_timeout")] = request->hasParam(F("signal_end_timeout"), true) ? formLong(request, F("signal_end_timeout")) : cfgLong("signal_end_timeout", Config::SectionId::Signal_id);
          signal[F("signal_repeat_time")] = request->hasParam(F("signal_repeat_time"), true) ? formLong(request, F("signal_repeat_time")) : cfgLong("signal_repeat_time", Config::SectionId::Signal_id);
          signal[F("scan_high_time")] = request->hasParam(F("scan_high_time"), true) ? formLong(request, F("scan_high_time")) : cfgLong("scan_high_time", Config::SectionId::Signal_id);
          pushConfigAndReply(request, root, F("/signal"));
        }

        void serveAdvancedPage(AsyncWebServerRequest *request) {
          if(!checkHttpAuthentication(request))
            return;

          if(requestIsFromAccessPoint(request)) {
            AsyncResponseStream *response = request->beginResponseStream(F("text/html; charset=utf-8"));
            writePageHead(response, F("Advanced"));
            response->print(F("<div class='container'><p>Advanced configuration is not available in access point mode.</p></div>"));
            writePageTail(response);
            sendStream(request, response, F("Advanced"));
            return;
          }

          String dump;
          dump.reserve(2048);
          Config::dumpConfigToString(dump);

          AsyncResponseStream *response = request->beginResponseStream(F("text/html; charset=utf-8"));
          writePageHead(response, F("Advanced"));
          response->print(F("<div class='container'><form method='post' action='/advanced' data-async-save='1'><label>Configuration JSON</label><textarea name='config_json' rows='18'>"));
          writeEscaped(response, dump);
          response->print(F("</textarea><input type='submit' class='btn-green' value='Save JSON'></form></div>"));
          writePageTail(response);
          sendStream(request, response, F("Advanced"));
        }

        void serveAdvancedPost(AsyncWebServerRequest *request) {
          if(!checkHttpAuthentication(request))
            return;

          if(!request->hasParam(F("config_json"), true)) {
            request->send(400, F("text/plain"), F("Missing configuration JSON"));
            return;
          }

          String raw = request->getParam(F("config_json"), true)->value();
          DynamicJsonDocument input(3000);
          DeserializationError error = deserializeJson(input, raw);
          if(error) {
            AsyncResponseStream *response = request->beginResponseStream(F("text/html; charset=utf-8"));
            writePageHead(response, F("Invalid JSON"));
            response->print(F("<div class='container'><p>"));
            writeEscaped(response, String(error.c_str()));
            response->print(F("</p><p><a href='/advanced'>Back</a></p></div>"));
            writePageTail(response);
            sendStream(request, response, F("Invalid JSON"));
            return;
          }
          JsonObject root = input.as<JsonObject>();
          pushConfigAndReply(request, root, F("/advanced"));
        }

        void serveInfoPage(AsyncWebServerRequest *request) {
          if(!checkHttpAuthentication(request))
            return;

          AsyncResponseStream *response = request->beginResponseStream(F("text/html; charset=utf-8"));
          writePageHead(response, F("Infos / Credits"));
          response->print(F("<div class='info-page'><div class='container'><h2>RFLink32 - Sonoff RF Bridge</h2><table><tr><th>Item</th><th>Value</th></tr><tr><td>Firmware</td><td>RFLink_ESP_"));
          response->print(BUILDNR);
          response->print(F("."));
          response->print(REVNR);
          response->print(F("-"));
          response->print(F(RFLINK_BUILDNAME));
          response->print(F("</td></tr><tr><td>Custom build</td><td>Sonoff RF Bridge lightweight web UI with captive portal fixes and 1600-byte MQTT/Serial buffers</td></tr><tr><td>Hardware profile</td><td>"));
          response->print(F(RFLINK_HARDWARE_NAME));
          response->print(F("</td></tr><tr><td>Web UI</td><td>0.1.0-compatible, lightweight embedded renderer</td></tr></table></div><div class='container'><h2>Credits</h2><p class='credit-line'>This custom Sonoff RF Bridge firmware is based on <a href='https://github.com/cpainchaud/RFLink32'>cpainchaud/RFLink32</a> and its contributors.</p><p class='credit-line'>The original web interface is <a href='https://github.com/cpainchaud/rflink-webui'>cpainchaud/rflink-webui</a> and its contributors.</p><p class='credit-line'><a href='https://github.com/cpainchaud/RFLink32/graphs/contributors'>Firmware contributors</a> | <a href='https://github.com/cpainchaud/rflink-webui/graphs/contributors'>WebUI contributors</a></p></div></div>"));
          writePageTail(response);
          sendStream(request, response, F("Info"));
        }

        void serveServicesPage(AsyncWebServerRequest *request) {
          if(!checkHttpAuthentication(request))
            return;

          AsyncResponseStream *response = request->beginResponseStream(F("text/html; charset=utf-8"));
          writePageHead(response, F("Services"));
          response->print(F("<div class='container'><h2 style='margin:5px 0'>Portal GUI</h2><form method='post' action='/portal' data-async-save='1'><table>"));
          writeFieldRowStart(response, F("Enabled"));
          writeCheckbox(response, F(""), F("enabled"), cfgBool("enabled", Config::SectionId::Portal_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Authentication"));
          writeCheckbox(response, F(""), F("auth_enabled"), cfgBool("auth_enabled", Config::SectionId::Portal_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("User"));
          writeTextInput(response, F(""), F("auth_user"), cfgString("auth_user", Config::SectionId::Portal_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Password"));
          writeTextInput(response, F(""), F("auth_password"), cfgString("auth_password", Config::SectionId::Portal_id), F("password"));
          writeFieldRowEnd(response);
          response->print(F("</table><input type='submit' class='btn-green' value='Save'></form></div>"));
          response->print(F("<div class='container'><h2 style='margin:5px 0'>MQTT</h2><form method='post' action='/mqtt' data-async-save='1'><table>"));
          writeFieldRowStart(response, F("Enable MQTT"));
          writeCheckbox(response, F(""), F("enabled"), cfgBool("enabled", Config::SectionId::MQTT_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Server"));
          writeTextInput(response, F(""), F("server"), cfgString("server", Config::SectionId::MQTT_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Port"));
          writeNumberInput(response, F(""), F("port"), cfgLong("port", Config::SectionId::MQTT_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Client ID"));
          writeTextInput(response, F(""), F("id"), cfgString("id", Config::SectionId::MQTT_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("User"));
          writeTextInput(response, F(""), F("user"), cfgString("user", Config::SectionId::MQTT_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Password"));
          writeTextInput(response, F(""), F("password"), cfgString("password", Config::SectionId::MQTT_id), F("password"));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Topic in"));
          writeTextInput(response, F(""), F("topic_in"), cfgString("topic_in", Config::SectionId::MQTT_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Topic out"));
          writeTextInput(response, F(""), F("topic_out"), cfgString("topic_out", Config::SectionId::MQTT_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Enable LWT"));
          writeCheckbox(response, F(""), F("lwt_enabled"), cfgBool("lwt_enabled", Config::SectionId::MQTT_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("LWT topic"));
          writeTextInput(response, F(""), F("topic_lwt"), cfgString("topic_lwt", Config::SectionId::MQTT_id));
          writeFieldRowEnd(response);
          response->print(F("</table><input type='submit' class='btn-green' value='Save MQTT'></form></div><div class='container'><h2 style='margin:5px 0'>Serial2Net</h2><form method='post' action='/serial' data-async-save='1'><table>"));
          writeFieldRowStart(response, F("Enable serial TCP"));
          writeCheckbox(response, F(""), F("enabled"), cfgBool("enabled", Config::SectionId::Serial2Net_id));
          writeFieldRowEnd(response);
          writeFieldRowStart(response, F("Port"));
          writeNumberInput(response, F(""), F("port"), cfgLong("port", Config::SectionId::Serial2Net_id));
          writeFieldRowEnd(response);
          response->print(F("</table><input type='submit' class='btn-green' value='Save Serial2Net'></form></div>"));
          writePageTail(response);
          sendStream(request, response, F("Services"));
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
          RFLink::Serial2Net::getStatusJsonString(obj);

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


        void handleFileUploadFinalResponse(AsyncWebServerRequest *request) {
          if(!checkHttpAuthentication(request))
            return;

          //Serial.println(F("File upload via API request... done"));

          AsyncWebServerResponse *response = request->beginResponse(
                  200, "text/plain",
                  "OK"
          );
          response->addHeader(F("Connection"), F("close"));
          response->addHeader(F("Access-Control-Allow-Origin"), "*");
          request->send(response);
        }

        void handleFileUploadChunksReception(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        //Upload handler chunks in data

          if(!checkHttpAuthentication(request))
            return;

          if (!index) {
            Serial.print(F("File upload via API requested... "));
            if(!request->hasParam(F("filename"), false)) {
              Serial.println(F("missing 'filename' parameter"));
              return request->send(400, F("text/plain"), F("missing 'filename' parameter"));
            }
            String desiredFilename = request->getParam(F("filename"), false)->value();
            Serial.println(desiredFilename);
            #ifdef ESP32
            auto file = request->_tempFile = LittleFS.open(String('/' + desiredFilename), "w", true);
            #else
            auto file = request->_tempFile = LittleFS.open(String('/' + desiredFilename), "w");
            #endif

            if(!file) {
              //Serial.println(F("failed to open file for writing"));
              return request->send(500, F("text/plain"), F("failed to open file for writing"));
            }
          }

          if (len) {
            // stream the incoming chunk to the opened file
            auto written_bytes_count = request->_tempFile.write(data, len);
            if(written_bytes_count != len) {
              request->_tempFile.close();
              Serial.println(F("failed to write chunk to file"));
              return request->send(500, F("text/plain"), F("failed to write chunk to file"));
            }
          }
          if (final) {

            //Serial.println(F("File Uploaded !"));
            // close the file handle as the upload is now done

            #ifndef RFLINK_MQTT_DISABLED
            String desiredFilename('/' + request->getParam(F("filename"), false)->value());
            if(desiredFilename == Mqtt::mqtt_ca_cert_filename) {
              RFLink::Mqtt::triggerParamsHaveChanged();
              /*if(LittleFS.exists(Mqtt::mqtt_ca_cert_filename)) {
                Serial.println(F("MQTT CA cert file exists, MQTT will be reconfigured on next loop"));
              } else {
                Serial.println(F("MQTT CA cert file does not exist, MQTT will not be reconfigured on next loop"));
              }
              Serial.print(F("written filename:"));
              Serial.println(request->_tempFile.fullName());*/
            }
            #endif

            request->_tempFile.close();
            request->send(200, F("text/plain"), F("File Uploaded !"));
          }
        }

        void init() {

          refreshParametersFromConfig(false);

          server.onNotFound(notFound);
          server.on(PSTR("/assets/style.css"), HTTP_GET, serveStaticCss);
          server.on(PSTR("/assets/app.js"), HTTP_GET, serveStaticJs);
          server.on(PSTR("/"), HTTP_GET, serveRootPage);
          server.on(PSTR("/index.html"), HTTP_GET, serveRootPage);
          server.on(PSTR("/home"), HTTP_GET, serveStatusPage);
          server.on(PSTR("/wifi"), HTTP_GET, serveWifiSetupPage);
          server.on(PSTR("/wifi"), HTTP_POST, serveWifiSetupPost);
          server.on(PSTR("/services"), HTTP_GET, serveServicesPage);
          server.on(PSTR("/portal"), HTTP_GET, serveServicesRedirect);
          server.on(PSTR("/portal"), HTTP_POST, servePortalPost);
          // Portal, MQTT, and Serial2Net are edited from Services; keep old GET URLs as aliases.
          server.on(PSTR("/mqtt"), HTTP_GET, serveServicesRedirect);
          server.on(PSTR("/mqtt"), HTTP_POST, serveMqttPost);
          server.on(PSTR("/serial"), HTTP_GET, serveServicesRedirect);
          server.on(PSTR("/serial"), HTTP_POST, serveSerialPost);
          server.on(PSTR("/signal"), HTTP_GET, serveSignalPage);
          server.on(PSTR("/signal"), HTTP_POST, serveSignalPost);
          server.on(PSTR("/advanced"), HTTP_GET, serveAdvancedPage);
          server.on(PSTR("/advanced"), HTTP_POST, serveAdvancedPost);
          server.on(PSTR("/infos"), HTTP_GET, serveInfoPage);
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

          server.on(PSTR("/api/reboot"), HTTP_GET, serveApiReboot);
          server.on(PSTR("/reboot"), HTTP_POST, serveApiReboot);
          server.on(PSTR("/upload"), HTTP_POST, handleFileUploadFinalResponse, handleFileUploadChunksReception);

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
