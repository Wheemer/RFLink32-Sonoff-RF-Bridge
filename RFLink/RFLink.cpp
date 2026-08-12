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
#include "6_MQTT.h"
#include "8_OLED.h"
#include "10_Wifi.h"
#include "11_Config.h"
#include "12_Portal.h"

#if (defined(__AVR_ATmega328P__) || defined(__AVR_ATmega2560__))
#include <avr/power.h>
#endif

//****************************************************************************************************************************************
void sendMsgFromBuffer(); // See at bottom

#if (defined(ESP8266) || defined(ESP32))

void CallReboot(void) {
  RFLink::sendMsgFromBuffer();
  delay(1);
  ESP.restart();
}

#endif

namespace RFLink {

    namespace params {
      const char *ntpServer = "pool.ntp.org";
    }

    struct timeval timeAtBoot;
    struct timeval scheduledRebootTime;
    char printBuf[300];

#ifndef RFLINK_SERIAL_RX_BUFFER_SIZE
#define RFLINK_SERIAL_RX_BUFFER_SIZE 512
#endif

#ifndef RFLINK_RAW_MQTT_BUFFER_SIZE
#define RFLINK_RAW_MQTT_BUFFER_SIZE 1500
#endif

#ifndef RFLINK_RF_EVENT_QUEUE_SIZE
#define RFLINK_RF_EVENT_QUEUE_SIZE 4
#endif

#ifndef RFLINK_RF_EVENT_MESSAGE_SIZE
#define RFLINK_RF_EVENT_MESSAGE_SIZE 220
#endif

#ifndef RFLINK_RF_EVENT_COMMAND_SIZE
#define RFLINK_RF_EVENT_COMMAND_SIZE 1500
#endif

    namespace {
      struct RfEventRecord {
        uint32_t sequence = 0;
        unsigned long millis = 0;
        char message[RFLINK_RF_EVENT_MESSAGE_SIZE] = {0};
        char command[RFLINK_RF_EVENT_COMMAND_SIZE] = {0};
        bool raw = false;
        int pulseCount = 0;
        int rssi = 0;
        bool truncated = false;
      };

      RfEventRecord rfEventQueue[RFLINK_RF_EVENT_QUEUE_SIZE];
      uint32_t rfEventSequence = 0;
      uint8_t rfEventWriteIndex = 0;
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

        RfEventRecord &record = rfEventQueue[rfEventWriteIndex];
        record.sequence = ++rfEventSequence;
        record.millis = millis();
        strncpy(record.message, message, sizeof(record.message) - 1);
        record.message[sizeof(record.message) - 1] = 0;
        record.command[0] = 0;
        record.raw = false;
        record.pulseCount = 0;
        record.rssi = 0;
        record.truncated = false;

        rfEventWriteIndex++;
        if(rfEventWriteIndex >= RFLINK_RF_EVENT_QUEUE_SIZE)
          rfEventWriteIndex = 0;
      }

#ifndef RFLINK_MQTT_DISABLED
      char rawMqttBuffer[RFLINK_RAW_MQTT_BUFFER_SIZE];
      size_t rawMqttBufferLength = 0;

      void publishRawMqttBuffer()
      {
        if (rawMqttBufferLength == 0)
          return;

        rawMqttBuffer[rawMqttBufferLength] = 0;
        RFLink::Mqtt::publishRawMsg(rawMqttBuffer);
        rawMqttBufferLength = 0;
        rawMqttBuffer[0] = 0;
      }

      void appendRawMqttChar(char c)
      {
        if (c == '\r')
          return;

        if (c == '\n') {
          publishRawMqttBuffer();
          return;
        }

        if (rawMqttBufferLength >= sizeof(rawMqttBuffer) - 1)
          publishRawMqttBuffer();

        rawMqttBuffer[rawMqttBufferLength++] = c;
      }

      void appendRawMqttText(const char *buf, bool end_of_line = false)
      {
        if (buf == nullptr)
          return;

        while (*buf != 0)
          appendRawMqttChar(*buf++);

        if (end_of_line)
          publishRawMqttBuffer();
      }

      void appendRawMqttFlashText(const __FlashStringHelper *buf, bool end_of_line = false)
      {
        if (buf == nullptr)
          return;

        PGM_P cursor = reinterpret_cast<PGM_P>(buf);
        char c;
        while ((c = pgm_read_byte(cursor++)) != 0)
          appendRawMqttChar(c);

        if (end_of_line)
          publishRawMqttBuffer();
      }
#else
      void appendRawMqttChar(char) {}
      void appendRawMqttText(const char *, bool = false) {}
      void appendRawMqttFlashText(const __FlashStringHelper *, bool = false) {}
#endif
    }

    void setup() {

      delay(250);         // Time needed to switch back from Upload to Console

      Serial.setRxBufferSize(RFLINK_SERIAL_RX_BUFFER_SIZE);
      Serial.setTimeout(1);

      Serial.begin(BAUD); // Initialise the serial port

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
      #ifndef RFLINK_MQTT_DISABLED
      RFLink::Mqtt::setup_MQTT();
      #endif // RFLINK_MQTT_DISABLED
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


#ifdef MQTT_ENABLED
      //RFLink::Mqtt::publishMsg();
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
      #ifndef RFLINK_MQTT_DISABLED
      RFLink::Mqtt::checkMQTTloop();
      #endif // RFLINK_MQTT_DISABLED
      RFLink::sendMsgFromBuffer();

#if defined(RFLINK_WIFI_ENABLED)
      RFLink::Wifi::mainLoop();
#endif

#if defined(SERIAL_ENABLED)
      readSerialAndExecute();
#endif

      if (RFLink::Signal::ScanEvent()) {
        RFLink::sendMsgFromBuffer();
      }

      struct timeval now;
      gettimeofday(&now, nullptr);
      if (scheduledRebootTime.tv_sec != 0 && now.tv_sec > scheduledRebootTime.tv_sec) {
        Serial.println(F("***** Rebooting now for scheduled reboot !!! *****"));
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

#ifndef RFLINK_MQTT_DISABLED
        RFLink::Mqtt::publishMsg();
#endif // !RFLINK_MQTT_DISABLED

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

      RfEventRecord &record = rfEventQueue[rfEventWriteIndex];
      record.sequence = ++rfEventSequence;
      record.millis = millis();
      record.raw = true;
      record.pulseCount = number;
      record.rssi = (int) rssi;
      record.truncated = false;
      snprintf_P(
        record.message,
        sizeof(record.message),
        PSTR("20;XX;RAW;Pulses=%i;RSSI=%i;"),
        number,
        (int) rssi
      );

      size_t offset = snprintf_P(
        record.command,
        sizeof(record.command),
        PSTR("10;signal;sendRF;{\"repeat\":%u,\"delay\":%u,\"multiply\":%u,\"pulses\":["),
        repeat == 0 ? 8 : repeat,
        delay == 0 ? 100 : delay,
        multiply == 0 ? 1 : multiply
      );

      for(int index = 1; index <= number; index++) {
        int written = snprintf_P(
          record.command + offset,
          sizeof(record.command) - offset,
          index == 1 ? PSTR("%u") : PSTR(",%u"),
          pulses[index]
        );
        if(written < 0 || offset + (size_t) written >= sizeof(record.command) - 3) {
          record.truncated = true;
          break;
        }
        offset += written;
      }

      if(record.truncated) {
        record.command[0] = 0;
      } else if(offset < sizeof(record.command) - 3) {
        record.command[offset++] = ']';
        record.command[offset++] = '}';
        record.command[offset] = 0;
      } else {
        record.command[0] = 0;
        record.truncated = true;
      }

      rfEventWriteIndex++;
      if(rfEventWriteIndex >= RFLINK_RF_EVENT_QUEUE_SIZE)
        rfEventWriteIndex = 0;

      rawRfLearningArmed = false;
      rawRfLearningExpiresAt = 0;
    }

    void appendRfEventsJson(JsonArray output, uint32_t since, uint8_t limit) {
      if(limit == 0 || limit > RFLINK_RF_EVENT_QUEUE_SIZE)
        limit = RFLINK_RF_EVENT_QUEUE_SIZE;

      uint32_t firstSequence = since + 1;
      uint32_t queueStart = 0;
      if(rfEventSequence >= RFLINK_RF_EVENT_QUEUE_SIZE)
        queueStart = rfEventSequence - RFLINK_RF_EVENT_QUEUE_SIZE + 1;
      if(firstSequence < queueStart)
        firstSequence = queueStart;

      if(rfEventSequence >= limit && firstSequence < rfEventSequence - limit + 1)
        firstSequence = rfEventSequence - limit + 1;

      for(uint32_t sequence = firstSequence; sequence <= rfEventSequence; sequence++) {
        RfEventRecord *record = nullptr;
        for(uint8_t index = 0; index < RFLINK_RF_EVENT_QUEUE_SIZE; index++) {
          if(rfEventQueue[index].sequence == sequence) {
            record = &rfEventQueue[index];
            break;
          }
        }
        if(record == nullptr)
          continue;

        JsonObject event = output.createNestedObject();
        event[F("sequence")] = record->sequence;
        event[F("millis")] = record->millis;
        event[F("message")] = record->message;
        if(record->raw) {
          event[F("raw")] = true;
          event[F("pulse_count")] = record->pulseCount;
          event[F("rssi")] = record->rssi;
          event[F("truncated")] = record->truncated;
          if(record->command[0] != 0)
            event[F("command")] = record->command;
        }
      }
    }

    void sendRawPrint(const char *buf, bool end_of_line) {
      if (buf[0] != 0) {

#ifdef SERIAL_ENABLED
        Serial.print(buf);
        if(end_of_line)
          Serial.println();
#endif
        appendRawMqttText(buf, end_of_line);

      }
    }

    void sendRawPrint(long n)
    {
#ifdef SERIAL_ENABLED
      Serial.print(n);
#endif
      char numberBuf[16];
      ltoa(n, numberBuf, 10);
      appendRawMqttText(numberBuf);
    }

    void sendRawPrint(unsigned long n)
    {
#ifdef SERIAL_ENABLED
      Serial.print(n);
#endif
      char numberBuf[16];
      ultoa(n, numberBuf, 10);
      appendRawMqttText(numberBuf);
    }

    void sendRawPrint(int n)
    {
#ifdef SERIAL_ENABLED
      Serial.print(n);
#endif
      char numberBuf[16];
      itoa(n, numberBuf, 10);
      appendRawMqttText(numberBuf);
    }

    void sendRawPrint(unsigned int n)
    {
#ifdef SERIAL_ENABLED
      Serial.print(n);
#endif
      char numberBuf[16];
      utoa(n, numberBuf, 10);
      appendRawMqttText(numberBuf);
    }

  void sendRawPrint(float f)
  {
#ifdef SERIAL_ENABLED
    Serial.print(f);
#endif
    char numberBuf[24];
    dtostrf(f, 0, 2, numberBuf);
    appendRawMqttText(numberBuf);
  }

    void sendRawPrint(char c)
    {
#ifdef SERIAL_ENABLED
      Serial.write(c);
#endif
      appendRawMqttChar(c);
    }

    void sendRawPrint(const __FlashStringHelper *buf, bool end_of_line){
      #ifdef SERIAL_ENABLED
      Serial.print(buf);
      if(end_of_line)
        Serial.println();
      #endif
      appendRawMqttFlashText(buf, end_of_line);
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

    void scheduleReboot(unsigned int seconds) {
      gettimeofday(&scheduledRebootTime, NULL);
      scheduledRebootTime.tv_sec += seconds;
    }

    void getStatusJsonString(JsonObject &output) {

      char buffer[90];
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
#endif

      sprintf_P(buffer, PSTR("RF Bridge Local API %d.%d"), BUILDNR, REVNR);
      output["sw_version"] = buffer;
      output["hardware"] = F(RFLINK_HARDWARE_NAME);
      output["rf_event_cursor"] = getRfEventCursor();
      output["raw_learning_armed"] = rawRfLearningArmed;
    }

}

/*********************************************************************************************/
