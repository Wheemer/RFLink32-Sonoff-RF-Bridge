#include "RFLink.h"

#ifndef RFLINK_PORTAL_DISABLED

#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include "2_Signal.h"
#include "3_Serial.h"
#include "12_Portal.h"
#include "13_Update.h"

namespace RFLink { namespace Portal {

        namespace {
          AsyncWebSocket apiWebSocket("/api/ws");

          enum class WsJobType : uint8_t {
            None,
            Command,
            Learn,
            Reboot,
            Update,
          };

          struct WsJob {
            WsJobType type = WsJobType::None;
            uint32_t clientId = 0;
            uint32_t requestId = 0;
            char command[INPUT_COMMAND_SIZE] = {0};
            unsigned int learnSeconds = 30;
            bool ok = false;
            unsigned long elapsedMs = 0;
          };

          WsJob activeJob;
          bool jobInProgress = false;

          static char wsOutbound[2300];
          static bool wsInboundActive = false;
          static uint32_t wsInboundClientId = 0;
          // Incoming WebSocket JSON is parsed in-place in activeJob.command.
          // Only the object metadata needs ArduinoJson storage; keeping a
          // second 2 KB receive buffer here starved TCP allocations.
          static StaticJsonDocument<512> wsMessageDoc;
          static StaticJsonDocument<1280> wsStatusDoc;

          bool appendJsonText(char *&cursor, size_t &remaining, const char *text) {
            if(text == nullptr)
              return true;
            while(*text != 0) {
              if(remaining <= 1)
                return false;
              *cursor++ = *text++;
              remaining--;
            }
            *cursor = 0;
            return true;
          }

          bool appendJsonNumber(char *&cursor, size_t &remaining, unsigned long value) {
            int written = snprintf(cursor, remaining, "%lu", value);
            if(written < 0 || (size_t)written >= remaining)
              return false;
            cursor += written;
            remaining -= written;
            return true;
          }

          bool appendJsonSignedNumber(char *&cursor, size_t &remaining, long value) {
            int written = snprintf(cursor, remaining, "%ld", value);
            if(written < 0 || (size_t)written >= remaining)
              return false;
            cursor += written;
            remaining -= written;
            return true;
          }

          bool appendJsonEscaped(char *&cursor, size_t &remaining, const char *text) {
            if(!appendJsonText(cursor, remaining, "\""))
              return false;
            if(text != nullptr) {
              while(*text != 0) {
                char c = *text++;
                const char *escaped = nullptr;
                if(c == '"' || c == '\\') {
                  if(remaining <= 2)
                    return false;
                  *cursor++ = '\\';
                  *cursor++ = c;
                  remaining -= 2;
                  *cursor = 0;
                  continue;
                }
                if(c == '\r')
                  escaped = "\\r";
                else if(c == '\n')
                  escaped = "\\n";
                else if(c == '\t')
                  escaped = "\\t";
                if(escaped != nullptr) {
                  if(!appendJsonText(cursor, remaining, escaped))
                    return false;
                  continue;
                }
                if(remaining <= 1)
                  return false;
                *cursor++ = c;
                remaining--;
                *cursor = 0;
              }
            }
            return appendJsonText(cursor, remaining, "\"");
          }

          void writeRuntimeDebugJson(JsonObject root) {
            root[F("millis")] = millis();
            root[F("heap_free")] = ESP.getFreeHeap();
            root[F("tx_started_count")] = RFLink::Signal::runtime::txStartedCount;
            root[F("tx_finished_count")] = RFLink::Signal::runtime::txFinishedCount;
            root[F("tx_failed_count")] = RFLink::Signal::runtime::txFailedCount;
            root[F("tx_last_started_ms")] = RFLink::Signal::runtime::txLastStartedMs;
            root[F("tx_last_finished_ms")] = RFLink::Signal::runtime::txLastFinishedMs;
            root[F("tx_last_elapsed_ms")] = RFLink::Signal::runtime::txLastElapsedMs;
            root[F("tx_last_estimated_ms")] = RFLink::Signal::runtime::txLastEstimatedMs;
            root[F("tx_last_pulses")] = RFLink::Signal::runtime::txLastPulses;
            root[F("tx_last_repeats")] = RFLink::Signal::runtime::txLastRepeats;
            root[F("tx_last_delay")] = RFLink::Signal::runtime::txLastDelay;
            root[F("tx_last_multiply")] = RFLink::Signal::runtime::txLastMultiply;
            if(RFLink::Signal::runtime::txLastError[0] != 0)
              root[F("tx_last_error")] = RFLink::Signal::runtime::txLastError;
          #ifdef ESP8266
            root[F("heap_frag")] = ESP.getHeapFragmentation();
            root[F("free_block_max_size")] = ESP.getMaxFreeBlockSize();
            root[F("reset_reason")] = ESP.getResetReason();
          #endif
          #ifdef ESP32
            root[F("heap_min_free")] = ESP.getMinFreeHeap();
            root[F("heap_max_alloc")] = ESP.getMaxAllocHeap();
            root[F("reset_reason")] = (int) esp_reset_reason();
          #endif
          }

          void wsSendJsonToClient(AsyncWebSocketClient *client, JsonDocument &doc) {
            if(client == nullptr || !client->canSend())
              return;
            size_t length = serializeJson(doc, wsOutbound, sizeof(wsOutbound) - 1);
            if(length == 0)
              return;
            wsOutbound[length] = 0;
            client->text(wsOutbound);
          }

          bool buildRfEventJson(uint32_t sequence, unsigned long eventMillis,
                                const char *message, bool raw, int pulseCount,
                                int rssi, bool truncated, const char *command,
                                bool includeCommand) {
            char *cursor = wsOutbound;
            size_t remaining = sizeof(wsOutbound);

            return appendJsonText(cursor, remaining, "{\"type\":\"rf_event\",\"event\":{\"sequence\":") &&
              appendJsonNumber(cursor, remaining, sequence) &&
              appendJsonText(cursor, remaining, ",\"millis\":") &&
              appendJsonNumber(cursor, remaining, eventMillis) &&
              appendJsonText(cursor, remaining, ",\"message\":") &&
              appendJsonEscaped(cursor, remaining, message) &&
              (!raw || (
                appendJsonText(cursor, remaining, ",\"raw\":true,\"pulse_count\":") &&
                appendJsonSignedNumber(cursor, remaining, pulseCount) &&
                appendJsonText(cursor, remaining, ",\"rssi\":") &&
                appendJsonSignedNumber(cursor, remaining, rssi) &&
                appendJsonText(cursor, remaining, ",\"truncated\":") &&
                appendJsonText(cursor, remaining, truncated ? "true" : "false") &&
                ((!includeCommand || command == nullptr || command[0] == 0) ||
                  (appendJsonText(cursor, remaining, ",\"command\":") &&
                   appendJsonEscaped(cursor, remaining, command))) &&
                ((includeCommand || command == nullptr || command[0] == 0) ||
                  appendJsonText(cursor, remaining, ",\"command_omitted\":true"))
              )) &&
              appendJsonText(cursor, remaining, "}}");
          }

          bool buildRawRfEventJson(uint32_t sequence, unsigned long eventMillis,
                                   int pulseCount, byte repeat, byte delay,
                                   byte multiply, int rssi, bool pulseCountTrimmed,
                                   const uint16_t *pulses, bool includeCommand) {
            char message[64];
            snprintf_P(
              message,
              sizeof(message),
              PSTR("20;XX;RAW;Pulses=%i;RSSI=%i;"),
              pulseCount,
              rssi
            );

            char *cursor = wsOutbound;
            size_t remaining = sizeof(wsOutbound);
            bool truncated = pulseCountTrimmed || pulseCount < 2 || !includeCommand;

            if(!(appendJsonText(cursor, remaining, "{\"type\":\"rf_event\",\"event\":{\"sequence\":") &&
                 appendJsonNumber(cursor, remaining, sequence) &&
                 appendJsonText(cursor, remaining, ",\"millis\":") &&
                 appendJsonNumber(cursor, remaining, eventMillis) &&
                 appendJsonText(cursor, remaining, ",\"message\":") &&
                 appendJsonEscaped(cursor, remaining, message) &&
                 appendJsonText(cursor, remaining, ",\"raw\":true,\"pulse_count\":") &&
                 appendJsonSignedNumber(cursor, remaining, pulseCount) &&
                 appendJsonText(cursor, remaining, ",\"rssi\":") &&
                 appendJsonSignedNumber(cursor, remaining, rssi) &&
                 appendJsonText(cursor, remaining, ",\"truncated\":") &&
                 appendJsonText(cursor, remaining, truncated ? "true" : "false")))
              return false;

            if(!includeCommand)
              return appendJsonText(cursor, remaining, ",\"command_omitted\":true}}");

            if(!(appendJsonText(cursor, remaining, ",\"command\":\"10;signal;sendRF;{\\\"repeat\\\":") &&
                 appendJsonNumber(cursor, remaining, repeat) &&
                 appendJsonText(cursor, remaining, ",\\\"delay\\\":") &&
                 appendJsonNumber(cursor, remaining, delay) &&
                 appendJsonText(cursor, remaining, ",\\\"pulses\\\":[")))
              return false;

            int commandPulseCount = pulseCount - (pulseCount % 2);
            for(int index = 1; index <= commandPulseCount; index++) {
              if((index & 0x1F) == 0) {
                yield();
                ESP.wdtFeed();
              }
              unsigned long pulseUs = (unsigned long)pulses[index] *
                (unsigned long)(multiply == 0 ? 1 : multiply);
              if(!(appendJsonText(cursor, remaining, index == 1 ? "" : ",") &&
                   appendJsonNumber(cursor, remaining, pulseUs)))
                return false;
            }

            return appendJsonText(cursor, remaining, "]}\"") &&
              appendJsonText(cursor, remaining, "}}");
          }

          void wsSendHello(AsyncWebSocketClient *client) {
            wsStatusDoc.clear();
            JsonObject root = wsStatusDoc.to<JsonObject>();
            root[F("type")] = F("hello");
            root[F("cursor")] = RFLink::getRfEventCursor();
            JsonObject status = root.createNestedObject(F("status"));
            RFLink::getStatusJsonString(status);
            if(wsStatusDoc.overflowed()) {
              wsStatusDoc.clear();
              root = wsStatusDoc.to<JsonObject>();
              root[F("type")] = F("hello");
              root[F("cursor")] = RFLink::getRfEventCursor();
              status = root.createNestedObject(F("status"));
              status[F("status_truncated")] = true;
              status[F("sw_version")] = F(RFLINK_FIRMWARE_VERSION);
              status[F("build_name")] = F(RFLINK_BUILDNAME);
              status[F("hardware")] = F(RFLINK_HARDWARE_NAME);
              status[F("heap_free")] = ESP.getFreeHeap();
              status[F("rf_event_cursor")] = RFLink::getRfEventCursor();
            }
            wsSendJsonToClient(client, wsStatusDoc);
          }

          void wsSendError(AsyncWebSocketClient *client, uint32_t requestId, const __FlashStringHelper *message) {
            StaticJsonDocument<192> doc;
            JsonObject root = doc.to<JsonObject>();
            root[F("type")] = F("error");
            root[F("id")] = requestId;
            root[F("message")] = message;
            wsSendJsonToClient(client, doc);
          }

          void wsFinishJob() {
            AsyncWebSocketClient *client = apiWebSocket.client(activeJob.clientId);
            wsStatusDoc.clear();
            JsonObject root = wsStatusDoc.to<JsonObject>();

            if(activeJob.type == WsJobType::Command) {
              root[F("type")] = F("command_result");
              root[F("id")] = activeJob.requestId;
              root[F("ok")] = activeJob.ok;
              root[F("elapsed_ms")] = activeJob.elapsedMs;
              JsonObject runtime = root.createNestedObject(F("runtime"));
              writeRuntimeDebugJson(runtime);
            } else if(activeJob.type == WsJobType::Learn) {
              root[F("type")] = F("learn_armed");
              root[F("id")] = activeJob.requestId;
              root[F("cursor")] = RFLink::getRfEventCursor();
              root[F("raw_learning_armed")] = true;
              JsonObject runtime = root.createNestedObject(F("runtime"));
              writeRuntimeDebugJson(runtime);
            } else if(activeJob.type == WsJobType::Reboot) {
              root[F("type")] = F("reboot_scheduled");
              root[F("id")] = activeJob.requestId;
              root[F("ok")] = true;
              JsonObject runtime = root.createNestedObject(F("runtime"));
              writeRuntimeDebugJson(runtime);
            } else if(activeJob.type == WsJobType::Update) {
              root[F("type")] = F("update_scheduled");
              root[F("id")] = activeJob.requestId;
              root[F("ok")] = activeJob.ok;
            } else {
              activeJob.type = WsJobType::None;
              jobInProgress = false;
              return;
            }

            if(wsStatusDoc.overflowed()) {
              bool ok = activeJob.ok;
              WsJobType type = activeJob.type;
              uint32_t requestId = activeJob.requestId;
              unsigned long elapsedMs = activeJob.elapsedMs;
              wsStatusDoc.clear();
              root = wsStatusDoc.to<JsonObject>();
              root[F("id")] = requestId;
              root[F("status_truncated")] = true;
              if(type == WsJobType::Command) {
                root[F("type")] = F("command_result");
                root[F("ok")] = ok;
                root[F("elapsed_ms")] = elapsedMs;
              } else if(type == WsJobType::Learn) {
                root[F("type")] = F("learn_armed");
                root[F("cursor")] = RFLink::getRfEventCursor();
                root[F("raw_learning_armed")] = true;
              } else if(type == WsJobType::Reboot) {
                root[F("type")] = F("reboot_scheduled");
                root[F("ok")] = true;
              } else if(type == WsJobType::Update) {
                root[F("type")] = F("update_scheduled");
                root[F("ok")] = ok;
              }
            }

            wsSendJsonToClient(client, wsStatusDoc);
            activeJob.type = WsJobType::None;
            jobInProgress = false;
          }

          void wsHandleMessage(AsyncWebSocketClient *client, char *data, size_t len) {
            if(client == nullptr || data == nullptr || len == 0)
              return;

            wsMessageDoc.clear();
            DeserializationError err = deserializeJson(wsMessageDoc, data, len);
            if(err) {
              wsSendError(client, 0, F("Invalid JSON"));
              return;
            }

            JsonObject root = wsMessageDoc.as<JsonObject>();
            const char *type = root[F("type")];
            if(type == nullptr) {
              wsSendError(client, root[F("id")] | 0, F("Missing type"));
              return;
            }

            uint32_t requestId = root[F("id")] | 0;

            if(strcmp(type, "ping") == 0) {
              StaticJsonDocument<96> out;
              JsonObject pong = out.to<JsonObject>();
              pong[F("type")] = F("pong");
              pong[F("id")] = requestId;
              wsSendJsonToClient(client, out);
              return;
            }

            if(strcmp(type, "command") == 0) {
              if(jobInProgress) {
                wsSendError(client, requestId, F("Bridge busy"));
                return;
              }
              const char *command = root[F("command")];
              if(command == nullptr)
                command = root[F("cmd")];
              if(command == nullptr || command[0] == 0) {
                wsSendError(client, requestId, F("Missing command"));
                return;
              }
              size_t commandLength = strlen(command);
              if(commandLength >= sizeof(activeJob.command) - 1) {
                wsSendError(client, requestId, F("Command too long"));
                return;
              }
              for(size_t i = 0; i < commandLength; i++) {
                if(command[i] == '\r' || command[i] == '\n') {
                  wsSendError(client, requestId, F("Command must be a single line"));
                  return;
                }
              }
              memmove(activeJob.command, command, commandLength + 1);
              activeJob.clientId = client->id();
              activeJob.requestId = requestId;
              activeJob.type = WsJobType::Command;
              activeJob.learnSeconds = 30;
              activeJob.ok = false;
              activeJob.elapsedMs = 0;
              jobInProgress = true;
              return;
            }

            if(strcmp(type, "learn") == 0) {
              if(jobInProgress) {
                wsSendError(client, requestId, F("Bridge busy"));
                return;
              }
              unsigned int learnSeconds = root[F("seconds")] | 30;
              activeJob = WsJob{};
              activeJob.clientId = client->id();
              activeJob.requestId = requestId;
              activeJob.type = WsJobType::Learn;
              activeJob.learnSeconds = learnSeconds;
              if(activeJob.learnSeconds == 0)
                activeJob.learnSeconds = 30;
              jobInProgress = true;
              return;
            }

            if(strcmp(type, "reboot") == 0) {
              if(jobInProgress) {
                wsSendError(client, requestId, F("Bridge busy"));
                return;
              }
              activeJob = WsJob{};
              activeJob.clientId = client->id();
              activeJob.requestId = requestId;
              activeJob.type = WsJobType::Reboot;
              jobInProgress = true;
              return;
            }

            if(strcmp(type, "update") == 0) {
              if(jobInProgress) {
                wsSendError(client, requestId, F("Bridge busy"));
                return;
              }
              const char *url = root[F("url")];
              const char *error = nullptr;
              if(!RFLink::UpdateSecurity::schedule(url, error)) {
                Serial.print(F("Update request rejected: "));
                Serial.println(error == nullptr ? "Unknown error" : error);
                wsSendError(client, requestId, F("Update rejected"));
                return;
              }
              activeJob = WsJob{};
              activeJob.clientId = client->id();
              activeJob.requestId = requestId;
              activeJob.type = WsJobType::Update;
              activeJob.ok = true;
              jobInProgress = true;
              return;
            }

            wsSendError(client, requestId, F("Unknown message type"));
          }

          void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
                         void *arg, uint8_t *data, size_t len) {
            (void) server;
            (void) arg;
            if(type == WS_EVT_CONNECT) {
              wsSendHello(client);
              return;
            }
            if(type == WS_EVT_DISCONNECT) {
              if(client != nullptr && wsInboundActive && wsInboundClientId == client->id()) {
                wsInboundActive = false;
                wsInboundClientId = 0;
                activeJob.command[0] = 0;
              }
              return;
            }
            if(type == WS_EVT_DATA) {
              AwsFrameInfo *info = (AwsFrameInfo *) arg;
              if(info == nullptr)
                return;
              if(info->index == 0) {
                if(info->opcode != WS_TEXT)
                  return;
                if(jobInProgress || wsInboundActive) {
                  wsSendError(client, 0, F("Bridge busy"));
                  return;
                }
                wsInboundActive = true;
                wsInboundClientId = client->id();
                activeJob.command[0] = 0;
              } else if(!wsInboundActive || wsInboundClientId != client->id()) {
                wsSendError(client, 0, F("Unexpected WebSocket continuation"));
                return;
              }
              if(info->index + len >= sizeof(activeJob.command)) {
                wsInboundActive = false;
                wsInboundClientId = 0;
                activeJob.command[0] = 0;
                wsSendError(client, 0, F("WebSocket message too long"));
                return;
              }
              memcpy(activeJob.command + info->index, data, len);
              activeJob.command[info->index + len] = 0;
              if(!info->final)
                return;
              wsInboundActive = false;
              wsInboundClientId = 0;
              wsHandleMessage(client, activeJob.command, info->index + len);
              return;
            }
            if(type == WS_EVT_ERROR) {
              Serial.println(F("WebSocket client error"));
            }
          }
        } // namespace

        void initWebSocket(AsyncWebServer &server) {
          apiWebSocket.onEvent(onWsEvent);
          server.addHandler(&apiWebSocket);
        }

        void processWebSocketJobs() {
          if(!jobInProgress || activeJob.type == WsJobType::None)
            return;

          unsigned long started = millis();

          if(activeJob.type == WsJobType::Command) {
            yield();
            ESP.wdtFeed();
            // WebSocket clients send full RFLink lines such as
            // "10;signal;sendRF;{...}". Keep this routed through the top-level
            // RFLink command dispatcher, not the lower-level signal parser.
            activeJob.ok = ::RFLink::executeCliCommand(activeJob.command);
            yield();
            ESP.wdtFeed();
          } else if(activeJob.type == WsJobType::Learn) {
            RFLink::Signal::AsyncSignalScanner::enableAsyncReceiver();
            RFLink::armRawRfLearning(activeJob.learnSeconds);
            activeJob.ok = true;
          } else if(activeJob.type == WsJobType::Reboot) {
            RFLink::scheduleReboot(5);
            activeJob.ok = true;
          } else if(activeJob.type == WsJobType::Update) {
            activeJob.ok = true;
          }

          activeJob.elapsedMs = millis() - started;
          wsFinishJob();
        }

        void broadcastRfEvent(uint32_t sequence, unsigned long eventMillis, const char *message,
                              bool raw, int pulseCount, int rssi, bool truncated,
                              const char *command) {
          if(apiWebSocket.count() == 0)
            return;

          if(!buildRfEventJson(sequence, eventMillis, message, raw, pulseCount, rssi,
                               truncated, command, true)) {
            if(!buildRfEventJson(sequence, eventMillis, message, raw, pulseCount, rssi,
                                 true, command, false))
              return;
          }
          apiWebSocket.textAll(wsOutbound);
        }

        void broadcastRawRfEvent(uint32_t sequence, unsigned long eventMillis,
                                 int pulseCount, byte repeat, byte delay,
                                 byte multiply, int rssi, bool pulseCountTrimmed,
                                 const uint16_t *pulses) {
          if(apiWebSocket.count() == 0 || pulses == nullptr)
            return;

          bool canIncludeCommand = pulseCount >= 2;
          if(!((canIncludeCommand && buildRawRfEventJson(sequence, eventMillis, pulseCount,
                                                          repeat, delay, multiply, rssi,
                                                          pulseCountTrimmed, pulses, true)) ||
               buildRawRfEventJson(sequence, eventMillis, pulseCount, repeat, delay,
                                   multiply, rssi, true, pulses, false)))
            return;
          apiWebSocket.textAll(wsOutbound);
        }

        void cleanupWebSocketClients() {
          apiWebSocket.cleanupClients();
        }

        void closeWebSocketClients() {
          apiWebSocket.closeAll();
        }

      } // namespace Portal
} // namespace RFLink

#endif // RFLINK_PORTAL_DISABLED
