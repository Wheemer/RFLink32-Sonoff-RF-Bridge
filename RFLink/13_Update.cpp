#include "13_Update.h"

#if defined(ESP8266)
#include <BearSSLHelpers.h>
#include <ESP8266httpUpdate.h>
#include <LittleFS.h>

#include "12_Portal.h"
#include "2_Signal.h"
#include "Updater_Signing.h"

namespace {
  constexpr size_t MAX_UPDATE_URL_BYTES = 384;
  constexpr unsigned long UPDATE_START_DELAY_MS = 500;
  constexpr unsigned int HTTP_UPDATE_TIMEOUT_MS = 15000;
  constexpr uint32_t UPDATE_SIGNATURE_BYTES = 256;
  constexpr uint32_t OTA_STATE_MAGIC = 0x4F544131UL;
  constexpr uint16_t OTA_STATE_SCHEMA = 1;
  constexpr char OTA_STATE_FILE_A[] = "/ota-state-a.bin";
  constexpr char OTA_STATE_FILE_B[] = "/ota-state-b.bin";

  enum class UpdateResult : uint8_t {
    None,
    Scheduled,
    Fetching,
    Failed,
    Staged,
  };

  struct OtaState {
    uint32_t magic;
    uint16_t schema;
    uint8_t result;
    uint8_t reserved;
    uint32_t sequence;
    uint32_t attempt;
    int32_t errorCode;
    char detail[96];
    uint32_t checksum;
  };

  char pendingUpdateUrl[MAX_UPDATE_URL_BYTES] = {0};
  char updateLastError[96] = {0};
  OtaState otaState = {};
  bool otaStateLoaded = false;
  unsigned long updateStartMillis = 0;
  bool updatePending = false;
  // SHA-256 of the PEM public key compiled into Updater_Signing.h. This is
  // public metadata used by Home Assistant to reject a mismatched OTA before
  // the bridge starts downloading it.
  constexpr char UPDATE_SIGNING_KEY_ID[] =
    "2cd04321efe940bfcafa11b426412d93db6a8703aba88c4dc141b8e6c4270457";

  uint32_t stateChecksum(const OtaState &state)
  {
    uint32_t value = 2166136261UL;
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&state);
    for(size_t index = 0; index < sizeof(OtaState) - sizeof(state.checksum); ++index) {
      value ^= bytes[index];
      value *= 16777619UL;
    }
    return value;
  }

  bool validState(const OtaState &state)
  {
    return state.magic == OTA_STATE_MAGIC && state.schema == OTA_STATE_SCHEMA &&
           state.checksum == stateChecksum(state);
  }

  bool readState(const char *path, OtaState &state)
  {
    File file = LittleFS.open(path, "r");
    if(!file)
      return false;
    const bool complete = file.size() == sizeof(state) &&
      file.read(reinterpret_cast<uint8_t *>(&state), sizeof(state)) == sizeof(state);
    file.close();
    return complete && validState(state);
  }

  void loadPersistentState()
  {
    if(otaStateLoaded)
      return;
    otaStateLoaded = true;
    OtaState first = {};
    OtaState second = {};
    const bool firstValid = readState(OTA_STATE_FILE_A, first);
    const bool secondValid = readState(OTA_STATE_FILE_B, second);
    if(firstValid && (!secondValid || first.sequence >= second.sequence))
      otaState = first;
    else if(secondValid)
      otaState = second;
    if(otaState.magic == OTA_STATE_MAGIC)
      snprintf(updateLastError, sizeof(updateLastError), "%s", otaState.detail);
  }

  bool persistState(UpdateResult result, int32_t errorCode, const char *detail)
  {
    loadPersistentState();
    OtaState next = {};
    next.magic = OTA_STATE_MAGIC;
    next.schema = OTA_STATE_SCHEMA;
    next.result = static_cast<uint8_t>(result);
    next.sequence = otaState.sequence + 1;
    next.attempt = otaState.attempt + (result == UpdateResult::Scheduled ? 1 : 0);
    next.errorCode = errorCode;
    snprintf(next.detail, sizeof(next.detail), "%s", detail == nullptr ? "" : detail);
    next.checksum = stateChecksum(next);

    const char *path = (next.sequence & 1U) ? OTA_STATE_FILE_A : OTA_STATE_FILE_B;
    File file = LittleFS.open(path, "w");
    if(!file)
      return false;
    const bool complete = file.write(reinterpret_cast<const uint8_t *>(&next), sizeof(next)) == sizeof(next);
    file.close();
    if(!complete)
      return false;
    otaState = next;
    snprintf(updateLastError, sizeof(updateLastError), "%s", otaState.detail);
    return true;
  }

  const char *resultText(UpdateResult result)
  {
    switch(result) {
      case UpdateResult::Scheduled: return "scheduled";
      case UpdateResult::Fetching: return "fetching";
      case UpdateResult::Failed: return "failed";
      case UpdateResult::Staged: return "staged";
      case UpdateResult::None:
      default: return "none";
    }
  }

  bool fetchAndApplyUpdate(const char *url, char *error, size_t errorSize, int32_t &errorCode)
  {
    errorCode = -1;
    // Construct the verifier only when OTA is first used, then retain it.
    // SigningVerifier reserves the ESP8266's secondary crypto stack, so a
    // per-attempt object would leak that reservation. Reinstall it directly
    // before every update, as required by the updater's singleton API.
    static BearSSL::PublicKey signingKey(signing_pubkey);
    if(!signingKey.isRSA()) {
      snprintf(error, errorSize, "Embedded OTA signing key is invalid");
      return false;
    }
    static BearSSL::HashSHA256 signingHash;
    static BearSSL::SigningVerifier signingVerifier(&signingKey);
    if(signingVerifier.length() != UPDATE_SIGNATURE_BYTES) {
      snprintf(error, errorSize, "Embedded OTA signing verifier is invalid");
      return false;
    }
    Update.installSignature(&signingHash, &signingVerifier);

    ESP8266HTTPUpdate updater(HTTP_UPDATE_TIMEOUT_MS);
    updater.rebootOnUpdate(false);
    // Home Assistant closes its long-lived socket before scheduling OTA. Do
    // not also globally tear down TCP/UDP from inside the updater: that can
    // disrupt the async API while the bridge is writing the staged image.
    updater.closeConnectionsOnUpdate(false);
    WiFiClient client;
    const t_httpUpdate_return result = updater.update(
      client, String(url), String(RFLINK_FIRMWARE_VERSION)
    );
    if(result == HTTP_UPDATE_OK)
      return true;

    const String detail = updater.getLastErrorString();
    errorCode = updater.getLastError();
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
    if(!persistState(UpdateResult::Scheduled, 0, "")) {
      error = "Could not persist OTA diagnostic state";
      return false;
    }
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
    if(!persistState(UpdateResult::Fetching, 0, "")) {
      snprintf(updateLastError, sizeof(updateLastError), "Could not persist OTA fetch state");
      Serial.println(updateLastError);
      return;
    }
    const bool resumeScanning = !RFLink::Signal::AsyncSignalScanner::isStopped();
    RFLink::Signal::AsyncSignalScanner::stopScanning();
    RFLink::Signal::AsyncSignalScanner::nextPulseTimeoutTime_us = 0;
    RFLink::Signal::RawSignal.readyForDecoder = false;
    RFLink::Signal::RawSignal.Number = 0;
    RFLink::Signal::RawSignal.Time = 0;
    int32_t errorCode = -1;
    if(!fetchAndApplyUpdate(pendingUpdateUrl, updateLastError, sizeof(updateLastError), errorCode)) {
      persistState(UpdateResult::Failed, errorCode, updateLastError);
      Serial.print(F("Signed update failed: "));
      Serial.println(updateLastError);
      if(resumeScanning)
        RFLink::Signal::AsyncSignalScanner::startScanning();
      return;
    }
    if(!persistState(UpdateResult::Staged, 0, "Firmware staged; restarting")) {
      snprintf(updateLastError, sizeof(updateLastError), "Could not persist OTA staged state");
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
    loadPersistentState();
    return updateLastError;
  }

  const char *lastResult()
  {
    loadPersistentState();
    return resultText(static_cast<UpdateResult>(otaState.result));
  }

  int32_t lastErrorCode()
  {
    loadPersistentState();
    return otaState.errorCode;
  }

  uint32_t lastAttempt()
  {
    loadPersistentState();
    return otaState.attempt;
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
  const char *lastResult() { return "none"; }
  int32_t lastErrorCode() { return 0; }
  uint32_t lastAttempt() { return 0; }
  const char *signingKeyId() { return ""; }
}
#endif
