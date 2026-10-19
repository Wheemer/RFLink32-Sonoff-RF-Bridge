#include <Arduino.h>
#include <ArduinoJson.h>
#include <BearSSLHelpers.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

#include "../../RFLink/Updater_Signing.h"

namespace {
constexpr uint16_t API_PORT = 8080;
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 30000;
constexpr unsigned long UPDATE_START_DELAY_MS = 250;
constexpr uint32_t UPDATE_SIGNATURE_BYTES = 256;

ESP8266WebServer server(API_PORT);
BearSSL::PublicKey signingKey(signing_pubkey);
BearSSL::HashSHA256 signingHash;
BearSSL::SigningVerifier signingVerifier(&signingKey);
String stagedUrl;
unsigned long updateAt = 0;
String updateError;

struct WifiSettings {
  bool dhcp;
  String ssid;
  String password;
  String ip;
  String mask;
  String gateway;
  String dns;
};

bool loadWifiCredentials(WifiSettings& settings) {
  LittleFSConfig noFormat(false);
  if (!LittleFS.setConfig(noFormat) || !LittleFS.begin()) {
    updateError = F("Could not mount existing configuration");
    return false;
  }
  File config = LittleFS.open(F("/config.json"), "r");
  if (!config) {
    updateError = F("Existing Wi-Fi configuration is missing");
    return false;
  }
  StaticJsonDocument<256> filter;
  JsonObject filteredWifi = filter.createNestedObject(F("wifi"));
  filteredWifi[F("client_enabled")] = true;
  filteredWifi[F("client_dhcp_enabled")] = true;
  filteredWifi[F("client_ssid")] = true;
  filteredWifi[F("client_password")] = true;
  filteredWifi[F("client_ip")] = true;
  filteredWifi[F("client_mask")] = true;
  filteredWifi[F("client_gateway")] = true;
  filteredWifi[F("client_dns")] = true;
  StaticJsonDocument<512> document;
  const DeserializationError result = deserializeJson(
      document, config, DeserializationOption::Filter(filter));
  config.close();
  if (result) {
    updateError = F("Existing Wi-Fi configuration is invalid");
    return false;
  }
  JsonObject wifi = document[F("wifi")].as<JsonObject>();
  if (wifi.isNull() || !wifi[F("client_enabled")].as<bool>()) {
    updateError = F("Existing client Wi-Fi is disabled");
    return false;
  }
  settings.dhcp = wifi[F("client_dhcp_enabled")] | true;
  settings.ssid = wifi[F("client_ssid")].as<String>();
  settings.password = wifi[F("client_password")].as<String>();
  settings.ip = wifi[F("client_ip")].as<String>();
  settings.mask = wifi[F("client_mask")].as<String>();
  settings.gateway = wifi[F("client_gateway")].as<String>();
  settings.dns = wifi[F("client_dns")].as<String>();
  if (settings.ssid.isEmpty()) {
    updateError = F("Existing Wi-Fi name is missing");
    return false;
  }
  return true;
}

void sendStatus() {
  StaticJsonDocument<256> document;
  document[F("version")] = F(RF_BRIDGE_BOOTSTRAP_VERSION);
  document[F("mode")] = F("signed_ota_bootstrap");
  document[F("connected")] = WiFi.isConnected();
  document[F("ip")] = WiFi.localIP().toString();
  document[F("update_pending")] = updateAt != 0;
  document[F("update_signing_key_bytes")] = signingVerifier.length();
  if (!updateError.isEmpty()) {
    document[F("update_error")] = updateError;
  }
  String response;
  serializeJson(document, response);
  server.send(200, F("application/json"), response);
}

void scheduleUpdate() {
  if (!server.hasArg(F("url"))) {
    server.send(400, F("text/plain"), F("Missing update URL"));
    return;
  }
  const String url = server.arg(F("url"));
  if (!url.startsWith(F("http://"))) {
    server.send(400, F("text/plain"), F("Only http update URLs are supported"));
    return;
  }
  if (signingVerifier.length() != UPDATE_SIGNATURE_BYTES) {
    server.send(500, F("text/plain"), F("Embedded signing key is invalid"));
    return;
  }
  stagedUrl = url;
  updateError = String();
  updateAt = millis() + UPDATE_START_DELAY_MS;
  server.send(202, F("text/plain"), F("Signed firmware update scheduled"));
}

void applyStagedUpdate() {
  if (updateAt == 0 || static_cast<long>(millis() - updateAt) < 0) {
    return;
  }
  updateAt = 0;
  Update.installSignature(&signingHash, &signingVerifier);
  ESP8266HTTPUpdate updater(15000);
  updater.rebootOnUpdate(false);
  WiFiClient client;
  const t_httpUpdate_return result = updater.update(client, stagedUrl, F("bootstrap"));
  if (result == HTTP_UPDATE_OK) {
    ESP.restart();
    return;
  }
  updateError = F("HTTP OTA failed (");
  updateError += updater.getLastError();
  updateError += F("): ");
  updateError += updater.getLastErrorString();
}
}  // namespace

void setup() {
  Serial.begin(115200);
  WifiSettings wifi;
  if (!loadWifiCredentials(wifi)) {
    return;
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  if (!wifi.dhcp) {
    IPAddress ip;
    IPAddress mask;
    IPAddress gateway;
    IPAddress dns;
    if (!ip.fromString(wifi.ip) || !mask.fromString(wifi.mask) ||
        !gateway.fromString(wifi.gateway) || !dns.fromString(wifi.dns)) {
      updateError = F("Existing static Wi-Fi configuration is invalid");
      return;
    }
    WiFi.config(ip, gateway, mask, dns);
  }
  WiFi.begin(wifi.ssid, wifi.password);
  const unsigned long deadline = millis() + WIFI_CONNECT_TIMEOUT_MS;
  while (!WiFi.isConnected() && static_cast<long>(millis() - deadline) < 0) {
    delay(100);
  }
  if (!WiFi.isConnected()) {
    updateError = F("Could not reconnect using existing Wi-Fi configuration");
    return;
  }
  server.on(F("/api/bootstrap/status"), HTTP_GET, sendStatus);
  server.on(F("/api/bootstrap/update"), HTTP_POST, scheduleUpdate);
  server.begin();
}

void loop() {
  server.handleClient();
  applyStagedUpdate();
}
