#include "13_Update.h"

#if defined(ESP8266)
#include <BearSSLHelpers.h>
#include <ESP8266httpUpdate.h>

#include "12_Portal.h"
#include "2_Signal.h"
#include "Updater_Signing.h"

namespace {
  constexpr size_t MAX_UPDATE_URL_BYTES = 384;
  constexpr unsigned long UPDATE_START_DELAY_MS = 500;
  constexpr unsigned int HTTP_UPDATE_TIMEOUT_MS = 15000;
  constexpr uint32_t UPDATE_SIGNATURE_BYTES = 256;

  BearSSL::PublicKey updateSigningPublicKey(signing_pubkey);
  BearSSL::HashSHA256 updateSigningHash;
  BearSSL::SigningVerifier updateSigningVerifier(&updateSigningPublicKey);
  char pendingUpdateUrl[MAX_UPDATE_URL_BYTES] = {0};
  char updateLastError[96] = {0};
  unsigned long updateStartMillis = 0;
  bool updatePending = false;
  // SHA-256 of the PEM public key compiled into Updater_Signing.h. This is
  // public metadata used by Home Assistant to reject a mismatched OTA before
  // the bridge starts downloading it.
  constexpr char UPDATE_SIGNING_KEY_ID[] =
    "2cd04321efe940bfcafa11b426412d93db6a8703aba88c4dc141b8e6c4270457";

  bool signingVerifierReady()
  {
    return updateSigningPublicKey.isRSA()
      && updateSigningVerifier.length() == UPDATE_SIGNATURE_BYTES;
  }

  bool fetchAndApplyUpdate(const char *url, char *error, size_t errorSize)
  {
    ESP8266HTTPUpdate updater(HTTP_UPDATE_TIMEOUT_MS);
    updater.rebootOnUpdate(false);
    updater.closeConnectionsOnUpdate(false);
    WiFiClient client;
    const t_httpUpdate_return result = updater.update(
      client, String(url), String(RFLINK_FIRMWARE_VERSION)
    );
    if(result == HTTP_UPDATE_OK)
      return true;

    const String detail = updater.getLastErrorString();
    snprintf(
      error, errorSize, "HTTP OTA failed (%d): %s",
      updater.getLastError(), detail.c_str()
    );
    return false;
  }
}

namespace RFLink::UpdateSecurity {
  void setup()
  {
    if(!signingVerifierReady()) {
      snprintf(
        updateLastError, sizeof(updateLastError),
        "Embedded OTA signing key is invalid"
      );
      return;
    }
    Update.installSignature(&updateSigningHash, &updateSigningVerifier);
  }

  bool schedule(const char *url, const char *&error)
  {
    if(!signingVerifierReady()) {
      error = "Embedded OTA signing key is invalid";
      return false;
    }

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

  const char *signingKeyId()
  {
    return UPDATE_SIGNING_KEY_ID;
  }
}
#else
namespace RFLink::UpdateSecurity {
  void setup() {}
  bool schedule(const char *, const char *&error) { error = "Updates require ESP8266"; return false; }
  void maintain() {}
  bool isPending() { return false; }
  const char *lastError() { return ""; }
  const char *signingKeyId() { return ""; }
}
#endif
