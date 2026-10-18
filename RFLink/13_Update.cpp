#include "13_Update.h"

#if defined(ESP8266)
#include <BearSSLHelpers.h>
#include <ESP8266WiFi.h>
#include <Updater.h>

#include "12_Portal.h"
#include "2_Signal.h"
#include "Updater_Signing.h"

namespace {
  constexpr size_t MAX_UPDATE_URL_BYTES = 384;
  constexpr size_t MAX_UPDATE_HOST_BYTES = 96;
  constexpr size_t HTTP_LINE_BYTES = 192;
  constexpr size_t UPDATE_BUFFER_BYTES = 512;
  constexpr unsigned long UPDATE_START_DELAY_MS = 500;
  constexpr unsigned long HTTP_READ_TIMEOUT_MS = 8000;

  BearSSL::PublicKey updateSigningPublicKey(signing_pubkey);
  BearSSL::HashSHA256 updateSigningHash;
  BearSSL::SigningVerifier updateSigningVerifier(&updateSigningPublicKey);
  char pendingUpdateUrl[MAX_UPDATE_URL_BYTES] = {0};
  char updateLastError[96] = {0};
  unsigned long updateStartMillis = 0;
  bool updatePending = false;

  bool readHttpLine(WiFiClient &client, char *line, size_t lineSize)
  {
    size_t length = 0;
    const unsigned long deadline = millis() + HTTP_READ_TIMEOUT_MS;
    while((long)(millis() - deadline) < 0) {
      while(client.available()) {
        const char value = static_cast<char>(client.read());
        if(value == '\n') {
          if(length > 0 && line[length - 1] == '\r')
            length--;
          line[length] = 0;
          return true;
        }
        if(length + 1 >= lineSize)
          return false;
        line[length++] = value;
      }
      if(!client.connected())
        return false;
      delay(1);
      yield();
    }
    return false;
  }

  bool parseHttpUrl(const char *url, char *host, size_t hostSize, uint16_t &port, const char *&path)
  {
    const char *authority = url + 7;
    const char *pathStart = strchr(authority, '/');
    const char *authorityEnd = pathStart == nullptr ? authority + strlen(authority) : pathStart;
    const char *colon = nullptr;
    for(const char *cursor = authority; cursor < authorityEnd; cursor++) {
      if(*cursor == ':')
        colon = cursor;
    }
    const char *hostEnd = colon == nullptr ? authorityEnd : colon;
    const size_t hostLength = hostEnd - authority;
    if(hostLength == 0 || hostLength >= hostSize)
      return false;
    memcpy(host, authority, hostLength);
    host[hostLength] = 0;

    port = 80;
    if(colon != nullptr) {
      char *end = nullptr;
      const unsigned long parsedPort = strtoul(colon + 1, &end, 10);
      if(end != authorityEnd || parsedPort == 0 || parsedPort > 65535)
        return false;
      port = static_cast<uint16_t>(parsedPort);
    }
    path = pathStart == nullptr ? "/" : pathStart;
    return true;
  }

  bool fetchAndApplyUpdate(const char *url, char *error, size_t errorSize)
  {
    char host[MAX_UPDATE_HOST_BYTES];
    uint16_t port = 80;
    const char *path = nullptr;
    if(!parseHttpUrl(url, host, sizeof(host), port, path)) {
      snprintf(error, errorSize, "Invalid update URL");
      return false;
    }

    WiFiClient client;
    if(!client.connect(host, port)) {
      snprintf(error, errorSize, "Update server connection failed");
      return false;
    }
    client.printf_P(PSTR("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n"), path, host);

    char line[HTTP_LINE_BYTES];
    if(!readHttpLine(client, line, sizeof(line)) || strncmp(line, "HTTP/1.1 200", 12) != 0) {
      snprintf(error, errorSize, "Update server did not return 200");
      client.stop();
      return false;
    }

    size_t contentLength = 0;
    bool hasLength = false;
    while(readHttpLine(client, line, sizeof(line))) {
      if(line[0] == 0)
        break;
      if(strncasecmp(line, "Content-Length:", 15) == 0) {
        char *end = nullptr;
        const unsigned long parsedLength = strtoul(line + 15, &end, 10);
        if(end == line + 15 || *end != 0 || parsedLength == 0) {
          snprintf(error, errorSize, "Invalid update length");
          client.stop();
          return false;
        }
        contentLength = parsedLength;
        hasLength = true;
      }
    }
    if(!hasLength) {
      snprintf(error, errorSize, "Update server omitted length");
      client.stop();
      return false;
    }
    if(!Update.begin(contentLength)) {
      snprintf(error, errorSize, "Update image does not fit");
      client.stop();
      return false;
    }

    uint8_t buffer[UPDATE_BUFFER_BYTES];
    size_t remaining = contentLength;
    unsigned long deadline = millis() + HTTP_READ_TIMEOUT_MS;
    while(remaining > 0) {
      const size_t available = client.available();
      if(available == 0) {
        if((long)(millis() - deadline) >= 0) {
          Update.end();
          snprintf(error, errorSize, "Timed out receiving update");
          client.stop();
          return false;
        }
        delay(1);
        yield();
        continue;
      }
      const size_t wanted = min(remaining, min(available, sizeof(buffer)));
      const size_t received = client.read(buffer, wanted);
      if(received == 0 || Update.write(buffer, received) != received) {
        Update.end();
        snprintf(error, errorSize, "Failed writing update");
        client.stop();
        return false;
      }
      remaining -= received;
      deadline = millis() + HTTP_READ_TIMEOUT_MS;
      yield();
    }
    client.stop();
    if(!Update.end()) {
      snprintf(error, errorSize, "Update signature verification failed");
      return false;
    }
    return true;
  }
}

namespace RFLink::UpdateSecurity {
  void setup()
  {
    Update.installSignature(&updateSigningHash, &updateSigningVerifier);
  }

  bool schedule(const char *url, const char *&error)
  {
    if(url == nullptr || strncmp(url, "http://", 7) != 0) {
      error = "Only an http update URL is supported";
      return false;
    }

    const size_t length = strlen(url);
    if(length >= sizeof(pendingUpdateUrl)) {
      error = "Update URL is too long";
      return false;
    }

    memcpy(pendingUpdateUrl, url, length + 1);
    updateLastError[0] = 0;
    updateStartMillis = millis() + UPDATE_START_DELAY_MS;
    updatePending = true;
    error = nullptr;
    return true;
  }

  void maintain()
  {
    if(!updatePending || (long)(millis() - updateStartMillis) < 0)
      return;

    updatePending = false;
    const bool resumeScanning = !RFLink::Signal::AsyncSignalScanner::isStopped();
    RFLink::Signal::AsyncSignalScanner::stopScanning();
    RFLink::Signal::AsyncSignalScanner::nextPulseTimeoutTime_us = 0;
    RFLink::Signal::RawSignal.readyForDecoder = false;
    RFLink::Signal::RawSignal.Number = 0;
    RFLink::Signal::RawSignal.Time = 0;
    if(!fetchAndApplyUpdate(pendingUpdateUrl, updateLastError, sizeof(updateLastError))) {
      Serial.print(F("Signed update failed: "));
      Serial.println(updateLastError);
      if(resumeScanning)
        RFLink::Signal::AsyncSignalScanner::startScanning();
      return;
    }
    RFLink::markRebootSource(RFLink::RebootSource::SignedUpdate);
    ESP.restart();
  }

  bool isPending()
  {
    return updatePending;
  }

  const char *lastError()
  {
    return updateLastError;
  }
}
#else
namespace RFLink::UpdateSecurity {
  void setup() {}
  bool schedule(const char *, const char *&error) { error = "Updates require ESP8266"; return false; }
  void maintain() {}
  bool isPending() { return false; }
  const char *lastError() { return ""; }
}
#endif
