// *********************************************************************************************************************************
// * Portions GPLv3 2021 Christophe Painchaud https://github.com/cpainchaud/RFLink32
// * Arduino Project RFLink for ESP architecture https://github.com/couin3/RFLink (Branch esp)
// * Portions Free Software 2018..2020 StormTeam - Marc RIVES
// * Portions Free Software 2015..2016 StuntTeam - (RFLink R29~R33)
// * Portions © Copyright 2010..2015 Paul Tonkes (original Nodo 3.7 code)
// *
// *                                       RFLink32
// *
// ********************************************************************************************************************************
// * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License
// * as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
// * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
// * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
// * You received a copy of the GNU General Public License along with this program in file 'COPYING.TXT'.
// * For more information on GPL licensing: http://www.gnu.org/licenses
// ********************************************************************************************************************************

// ****************************************************************************
#include <Arduino.h>
#include "RFLink.h"
#include "1_Radio.h"
#include "2_Signal.h"
#include "3_Serial.h"
#include "4_Display.h"
#include "5_Plugin.h"
#include "8_OLED.h"
#include "10_Wifi.h"
#include "11_Config.h"
#include "12_Portal.h"
#include "13_Update.h"

#if (defined(__AVR_ATmega328P__) || defined(__AVR_ATmega2560__))
#include <avr/power.h>
#endif

//****************************************************************************************************************************************
void sendMsgFromBuffer(); // See at bottom

#if (defined(ESP8266) || defined(ESP32))

void CallReboot(void) {
  RFLink::sendMsgFromBuffer();
  delay(1);
  RFLink::markRebootSource(RFLink::RebootSource::LegacyCommand);
  ESP.restart();
}

#endif

namespace RFLink {

    namespace params {
      const char *ntpServer = "pool.ntp.org";
    }

    struct timeval timeAtBoot;
    unsigned long scheduledRebootAt = 0;
    RebootSource scheduledRebootSource = RebootSource::Unknown;
    char printBuf[300];

#ifndef RFLINK_SERIAL_RX_BUFFER_SIZE
#define RFLINK_SERIAL_RX_BUFFER_SIZE 512
#endif

    namespace {
#if defined(ESP8266)
      // RTC user memory survives a software restart without writing flash. The
      // ESP8266 OTA implementation reserves blocks 0-31, so use its final two
      // blocks (496-503) for this 8-byte diagnostic breadcrumb.
      constexpr uint32_t REBOOT_BREADCRUMB_MAGIC = 0x52464252UL;
      constexpr uint32_t REBOOT_BREADCRUMB_RTC_BLOCK = 124;

      struct alignas(4) RebootBreadcrumb {
        uint32_t magic;
        uint32_t source;
      };

      static_assert(sizeof(RebootBreadcrumb) == 8, "RTC reboot breadcrumb size");

      RebootSource rebootSourceAtBoot = RebootSource::Unknown;

      void loadRebootSourceAtBoot() {
        RebootBreadcrumb breadcrumb = {0, 0};
        if (ESP.rtcUserMemoryRead(
              REBOOT_BREADCRUMB_RTC_BLOCK,
              reinterpret_cast<uint32_t *>(&breadcrumb),
              sizeof(breadcrumb)
            ) && breadcrumb.magic == REBOOT_BREADCRUMB_MAGIC) {
          switch (static_cast<RebootSource>(breadcrumb.source)) {
            case RebootSource::LegacyCommand:
            case RebootSource::ApiRequest:
            case RebootSource::SignedUpdate:
              rebootSourceAtBoot = static_cast<RebootSource>(breadcrumb.source);
              break;
            case RebootSource::Unknown:
            default:
              break;
          }
        }

        RebootBreadcrumb cleared = {0, 0};
        ESP.rtcUserMemoryWrite(
          REBOOT_BREADCRUMB_RTC_BLOCK,
          reinterpret_cast<uint32_t *>(&cleared),
          sizeof(cleared)
        );
      }
#endif

      uint32_t rfEventSequence = 0;
      bool rawRfLearningArmed = false;
      unsigned long rawRfLearningExpiresAt = 0;

      bool isRfEventMessage(const char *message)
      {
        return message != nullptr && strncmp(message, "20;", 3) == 0;
      }

      void recordRfEvent(const char *message)
      {
        if(!isRfEventMessage(message))
          return;

#ifndef RFLINK_PORTAL_DISABLED
        RFLink::Portal::broadcastRfEvent(
          ++rfEventSequence,
          millis(),
          message,
          false,
          0,
          0,
          false,
          nullptr
        );
#endif
      }

    }

    void setup() {

      delay(250);         // Time needed to switch back from Upload to Console

      Serial.setRxBufferSize(RFLINK_SERIAL_RX_BUFFER_SIZE);
      Serial.setTimeout(1);

      Serial.begin(BAUD); // Initialise the serial port

#if defined(ESP8266)
      loadRebootSourceAtBoot();
#endif

      RFLink::UpdateSecurity::setup();

      if (gettimeofday(&timeAtBoot, NULL) != 0) {
        Serial.println(F("Failed to obtain time"));
      }

#if (defined(ESP32) || defined(ESP8266))
      Serial.println(); // ESP "Garbage" message
      Serial.print(F("Arduino IDE Version :\t"));
      Serial.println(ARDUINO);
#endif
#ifdef ESP32
      Serial.print(F("ESP SDK version:\t"));
      Serial.println(ESP.getSdkVersion());
#endif
#ifdef ESP8266
      Serial.print(F("ESP CoreVersion :\t"));
      Serial.println(ESP.getCoreVersion());
#endif // ESP8266
      Serial.print(F("Sketch File :\t\t"));
      Serial.println(F(__FILE__)); // "RFLink.ino" version is in 20;00 Message
      Serial.println(F("Compiled on :\t\t" __DATE__ " at " __TIME__));

      display_Header();
      display_Splash();
      display_Footer();

#ifdef SERIAL_ENABLED
      Serial.print(pbuffer);
#endif
#ifdef OLED_ENABLED
      splash_OLED();
#endif

#if defined(ESP32) || (ESP8266)
      RFLink::Config::setup();
#endif
      RFLink::Radio::setup();
      RFLink::Signal::setup();

#if defined(RFLINK_WIFI_ENABLED)
      RFLink::Wifi::setup();
      #ifndef RFLINK_PORTAL_DISABLED
      RFLink::Portal::init();
      #endif // RFLINK_PORTAL_DISABLED
#endif // RFLINK_WIFI_ENABLED


      PluginInit();
      PluginTXInit();

      Radio::set_Radio_mode(Radio::Radio_OFF);

#if defined(ESP8266) || defined(ESP32)
      Radio::show_Radio_Pin();
#endif // ESP8266 || ESP32

#ifdef OLED_ENABLED
      setup_OLED();
#endif


      pbuffer[0] = 0;
      Radio::set_Radio_mode(Radio::Radio_RX);


#ifdef RFLINK_WIFI_ENABLED
  #ifndef RFLINK_PORTAL_DISABLED
      RFLink::Portal::start();
  #endif // RFLINK_PORTAL_DISABLED
#endif
    }

    void mainLoop() {
#ifndef RFLINK_PORTAL_DISABLED
      maintainRawRfLearning();
      RFLink::Portal::processWebSocketJobs();
      RFLink::Portal::cleanupWebSocketClients();
#endif
      RFLink::UpdateSecurity::maintain();
      RFLink::sendMsgFromBuffer();

#if defined(RFLINK_WIFI_ENABLED)
      RFLink::Wifi::mainLoop();
#endif

#ifndef RFLINK_PORTAL_DISABLED
      RFLink::Portal::maintainApiServer();
#endif

#if defined(SERIAL_ENABLED)
      readSerialAndExecute();
#endif

      if (RFLink::Signal::ScanEvent()) {
        RFLink::sendMsgFromBuffer();
      }

      if (scheduledRebootAt != 0 &&
          (long)(millis() - scheduledRebootAt) >= 0) {
        Serial.println(F("***** Rebooting now for scheduled reboot !!! *****"));
        markRebootSource(scheduledRebootSource);
        ESP.restart();
      }

      Radio::mainLoop();
    }

    void sendMsgFromBuffer() {
      if (pbuffer[0] != 0) {
        recordRfEvent(pbuffer);

#ifdef SERIAL_ENABLED
        Serial.print(pbuffer);
#endif


#ifdef OLED_ENABLED
        print_OLED();
#endif

        pbuffer[0] = 0;
      }
    }

    uint32_t getRfEventCursor() {
      return rfEventSequence;
    }

    void armRawRfLearning(unsigned int seconds) {
      rawRfLearningArmed = true;
      rawRfLearningExpiresAt = millis() + ((unsigned long) seconds * 1000UL);
    }

    void maintainRawRfLearning() {
      if(!rawRfLearningArmed || rawRfLearningExpiresAt == 0)
        return;
      if((long)(millis() - rawRfLearningExpiresAt) >= 0) {
        rawRfLearningArmed = false;
        rawRfLearningExpiresAt = 0;
      }
    }

    void recordRawRfEvent(
      int number,
      byte repeat,
      byte delay,
      byte multiply,
      unsigned long time,
      float rssi,
      const uint16_t *pulses
    ) {
      if(!rawRfLearningArmed)
        return;
      if(rawRfLearningExpiresAt != 0 && (long)(millis() - rawRfLearningExpiresAt) >= 0) {
        rawRfLearningArmed = false;
        return;
      }
      if(number <= 0 || pulses == nullptr)
        return;

      bool pulseCountTrimmed = false;
      if((number % 2) != 0) {
        pulseCountTrimmed = true;
      }

      rawRfLearningArmed = false;
      rawRfLearningExpiresAt = 0;

#ifndef RFLINK_PORTAL_DISABLED
      RFLink::Portal::broadcastRawRfEvent(
        ++rfEventSequence,
        millis(),
        number,
        repeat,
        delay,
        multiply,
        (int) rssi,
        pulseCountTrimmed,
        pulses
      );
#endif
    }

    void sendRawPrint(const char *buf, bool end_of_line) {
      if (buf[0] != 0) {

#ifdef SERIAL_ENABLED
        Serial.print(buf);
        if(end_of_line)
          Serial.println();
#endif

      }
    }

    void sendRawPrint(long n)
    {
#ifdef SERIAL_ENABLED
      Serial.print(n);
#endif
      char numberBuf[16];
      ltoa(n, numberBuf, 10);
    }

    void sendRawPrint(unsigned long n)
    {
#ifdef SERIAL_ENABLED
      Serial.print(n);
#endif
      char numberBuf[16];
      ultoa(n, numberBuf, 10);
    }

    void sendRawPrint(int n)
    {
#ifdef SERIAL_ENABLED
      Serial.print(n);
#endif
      char numberBuf[16];
      itoa(n, numberBuf, 10);
    }

    void sendRawPrint(unsigned int n)
    {
#ifdef SERIAL_ENABLED
      Serial.print(n);
#endif
      char numberBuf[16];
      utoa(n, numberBuf, 10);
    }

  void sendRawPrint(float f)
  {
#ifdef SERIAL_ENABLED
    Serial.print(f);
#endif
    char numberBuf[24];
    dtostrf(f, 0, 2, numberBuf);
  }

    void sendRawPrint(char c)
    {
#ifdef SERIAL_ENABLED
      Serial.write(c);
#endif
    }

    void sendRawPrint(const __FlashStringHelper *buf, bool end_of_line){
      #ifdef SERIAL_ENABLED
      Serial.print(buf);
      if(end_of_line)
        Serial.println();
      #endif
    };

    /**
    */
    bool executeCliCommand(char *cmd) {
      static byte ValidCommand = 0;
      bool commandAccepted = false;

      // Copy input command to InputBuffer_Serial, because many plugins are based on it !
      if(cmd != InputBuffer_Serial) { // sometimes we already have the command in the right buffer
        memcpy(InputBuffer_Serial, cmd, INPUT_COMMAND_SIZE);;
      }

      if (strlen(cmd) > 7) { // need to see minimal 8 characters on the serial port
        // 10;....;..;ON;
        if (strncmp(cmd, "10;", 3) == 0) { // Command from Master to RFLink
          // -------------------------------------------------------
          // Handle Device Management Commands
          // -------------------------------------------------------
          if (strncasecmp(cmd + 3, "PING;", 5) == 0) {
            display_Header();
            display_Name(PSTR("PONG"));
            display_Footer();
            commandAccepted = true;
          } else if (strncasecmp(cmd + 3, "REBOOT;", 7) == 0) {
            display_Header();
            display_Name(PSTR("REBOOT"));
            display_Footer();
            CallReboot();
            commandAccepted = true;
          } else if (strncasecmp(cmd + 3, "RFDEBUG=O", 9) == 0) {
            if (cmd[12] == 'N' || cmd[12] == 'n') {
              RFDebug = true;    // full debug on
              QRFDebug = false;  // q full debug off
              RFUDebug = false;  // undecoded debug off
              QRFUDebug = false; // q undecoded debug off
              display_Header();
              display_Name(PSTR("RFDEBUG=ON"));
              display_Footer();
            } else {
              RFDebug = false; // full debug off
              display_Header();
              display_Name(PSTR("RFDEBUG=OFF"));
              display_Footer();
            }
            commandAccepted = true;
          } else if (strncasecmp(cmd + 3, "RFUDEBUG=O", 10) == 0) {
            if (cmd[13] == 'N' || cmd[13] == 'n') {
              RFDebug = false;   // full debug off
              QRFDebug = false;  // q debug off
              RFUDebug = true;   // undecoded debug on
              QRFUDebug = false; // q undecoded debug off
              display_Header();
              display_Name(PSTR("RFUDEBUG=ON"));
              display_Footer();
            } else {
              RFUDebug = false; // undecoded debug off
              display_Header();
              display_Name(PSTR("RFUDEBUG=OFF"));
              display_Footer();
            }
            commandAccepted = true;
          } else if (strncasecmp(cmd + 3, "QRFDEBUG=O", 10) == 0) {
            if (cmd[13] == 'N' || cmd[13] == 'n') {
              RFDebug = false;   // full debug off
              QRFDebug = true;   // q debug on
              RFUDebug = false;  // undecoded debug off
              QRFUDebug = false; // q undecoded debug off
              display_Header();
              display_Name(PSTR("QRFDEBUG=ON"));
              display_Footer();
            } else {
              QRFDebug = false; // q debug off
              display_Header();
              display_Name(PSTR("QRFDEBUG=OFF"));
              display_Footer();
            }
            commandAccepted = true;
          } else if (strncasecmp(cmd + 3, "QRFUDEBUG=O", 11) == 0) {
            if (cmd[14] == 'N' || cmd[14] == 'n') {
              RFDebug = false;  // full debug off
              QRFDebug = false; // q debug off
              RFUDebug = false; // undecoded debug off
              QRFUDebug = true; // q undecoded debug on
              display_Header();
              display_Name(PSTR("QRFUDEBUG=ON"));
              display_Footer();
            } else {
              QRFUDebug = false; // q undecode debug off
              display_Header();
              display_Name(PSTR("QRFUDEBUG=OFF"));
              display_Footer();
            }
            commandAccepted = true;
          } else if (strncasecmp(cmd + 3, "VERSION", 7) == 0) {
            display_Header();
            display_Splash();
            display_Footer();
            commandAccepted = true;
          } else if (strncasecmp(cmd + 3, "signal", 6) == 0) {
            commandAccepted = Signal::executeCliCommand(cmd + 3 + 6 + 1);
          } else if (strncasecmp(cmd + 3, "config", 6) == 0) {
            Config::executeCliCommand(cmd + 3 + 6 + 1);
            commandAccepted = true;
          } else {
            // -------------------------------------------------------
            // Handle Generic Commands / Translate protocol data into Nodo text commands
            // -------------------------------------------------------
            if (Radio::pins::TX_DATA == NOT_A_PIN) {
              display_Header();
              display_Name(PSTR("ERROR: TX_DATA PIN is not defined!"));
              display_Footer();
            }
            else {
              Radio::set_Radio_mode(Radio::Radio_TX);
              if (PluginTXCall(0, cmd))
                ValidCommand = 1;
              else // Answer that an invalid command was received?
                ValidCommand = 2;
              commandAccepted = ValidCommand == 1;

              Radio::set_Radio_mode(Radio::Radio_RX);
            }
          }
        }
      } // if > 7
      if (ValidCommand != 0) {
        display_Header();
        if (ValidCommand == 1)
          display_Name(PSTR("OK"));
        else
          display_Name(PSTR("CMD UNKNOWN"));
        display_Footer();
      }
      ValidCommand = 0;
      sendMsgFromBuffer(); // in case there is a response waiting to be sent
      resetSerialBuffer();
      return commandAccepted;
    }

    void markRebootSource(RebootSource source) {
#if defined(ESP8266)
      RebootBreadcrumb breadcrumb = {
        REBOOT_BREADCRUMB_MAGIC,
        static_cast<uint32_t>(source),
      };
      ESP.rtcUserMemoryWrite(
        REBOOT_BREADCRUMB_RTC_BLOCK,
        reinterpret_cast<uint32_t *>(&breadcrumb),
        sizeof(breadcrumb)
      );
#else
      (void)source;
#endif
    }

    const char *lastRequestedRebootSource() {
#if defined(ESP8266)
      switch (rebootSourceAtBoot) {
        case RebootSource::LegacyCommand:
          return "legacy_command";
        case RebootSource::ApiRequest:
          return "api_request";
        case RebootSource::SignedUpdate:
          return "signed_update";
        case RebootSource::Unknown:
        default:
          return "unknown";
      }
#else
      return "unknown";
#endif
    }

    void scheduleReboot(unsigned int seconds, RebootSource source) {
      scheduledRebootAt = millis() + ((unsigned long)seconds * 1000UL);
      scheduledRebootSource = source;
    }

    void getStatusJsonString(JsonObject &output) {

      struct timeval now;

      #ifdef ESP8266
      // Little Patch to get the right boot time when connected to wifi ... Bug ?
      if (timeAtBoot.tv_sec == 0) {
        gettimeofday(&timeAtBoot, NULL);
      }
      #endif

      if (gettimeofday(&now, NULL) != 0) {
        RFLink::sendRawPrint(F("Failed to obtain time"));
      }

      output["uptime"] = now.tv_sec - timeAtBoot.tv_sec;

      output["heap_free"] = ESP.getFreeHeap();
      #ifdef ESP32
      output["heap_min_free"] = ESP.getMinFreeHeap();
      output["heap_max_alloc"] = ESP.getMaxAllocHeap();
      #endif
#ifdef ESP8266
      output["heap_frag"] = ESP.getHeapFragmentation();
      output["stack_free_cont"] = ESP.getFreeContStack();
      output["free_block_max_size"] = ESP.getMaxFreeBlockSize();
      // Why the chip last reset. On an exception/WDT this carries the crash
      // cause (and getResetInfo() the EPC/exception details) so a headless
      // client can diagnose a reboot without a serial console.
      const String resetReason = ESP.getResetReason();
      output["reset_reason"] = resetReason;
      output["reset_info"] = ESP.getResetInfo();
      output["last_requested_reboot_source"] =
        resetReason == F("Software/System restart")
          ? lastRequestedRebootSource()
          : "unknown";
#endif
#ifdef ESP32
      output["reset_reason"] = (int) esp_reset_reason();
#endif

      output["sw_version"] = F(RFLINK_FIRMWARE_VERSION);
      output["build_name"] = F(RFLINK_BUILDNAME);
      output["hardware"] = F(RFLINK_HARDWARE_NAME);
      output["rf_event_cursor"] = getRfEventCursor();
      output["raw_learning_armed"] = rawRfLearningArmed;
      output["update_pending"] = RFLink::UpdateSecurity::isPending();
      if(RFLink::UpdateSecurity::lastError()[0] != 0)
        output["update_error"] = RFLink::UpdateSecurity::lastError();
      output["tx_started_count"] = RFLink::Signal::runtime::txStartedCount;
      output["tx_finished_count"] = RFLink::Signal::runtime::txFinishedCount;
      output["tx_failed_count"] = RFLink::Signal::runtime::txFailedCount;
      output["tx_last_started_ms"] = RFLink::Signal::runtime::txLastStartedMs;
      output["tx_last_finished_ms"] = RFLink::Signal::runtime::txLastFinishedMs;
      output["tx_last_elapsed_ms"] = RFLink::Signal::runtime::txLastElapsedMs;
      output["tx_last_estimated_ms"] = RFLink::Signal::runtime::txLastEstimatedMs;
      output["tx_last_pulses"] = RFLink::Signal::runtime::txLastPulses;
      output["tx_last_repeats"] = RFLink::Signal::runtime::txLastRepeats;
      output["tx_last_delay"] = RFLink::Signal::runtime::txLastDelay;
      output["tx_last_multiply"] = RFLink::Signal::runtime::txLastMultiply;
      if(RFLink::Signal::runtime::txLastError[0] != 0)
        output["tx_last_error"] = RFLink::Signal::runtime::txLastError;
    }

}

/*********************************************************************************************/
