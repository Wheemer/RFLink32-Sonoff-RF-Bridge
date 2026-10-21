#ifndef _12_PORTAL_H_
#define _12_PORTAL_H_

#include "RFLink.h"

#ifndef RFLINK_PORTAL_DISABLED

#include "11_Config.h"
#include <ESPAsyncWebServer.h>

namespace RFLink {
    namespace Portal {

        extern Config::ConfigItem configItems[];
        /**
         * puts things together but doesn't start server *yet*
         * */
        void init();
        void start();
        void stop();
        // Release the API listener and its client allocations before a
        // memory-constrained ESP8266 OTA download starts.
        void prepareForUpdate();
        // Called after the station has an address. The API listener must be
        // created from the main loop, never directly from a Wi-Fi callback.
        void notifyNetworkReady();
        void maintainApiServer();
        void startSetupServer();
        void stopSetupServer();

        void paramsUpdatedCallback();
        void refreshParametersFromConfig(bool triggerChanges=true);

        void initWebSocket(AsyncWebServer &server);
        void processWebSocketJobs();
        void broadcastRfEvent(
          uint32_t sequence,
          unsigned long eventMillis,
          const char *message,
          bool raw,
          int pulseCount,
          int rssi,
          bool truncated,
          const char *command
        );
        void sendRawRfEvent(
          uint32_t clientId,
          uint32_t sequence,
          unsigned long eventMillis,
          int pulseCount,
          byte repeat,
          byte delay,
          byte multiply,
          int rssi,
          bool pulseCountTrimmed,
          const uint16_t *pulses
        );
        void cleanupWebSocketClients();
        void closeWebSocketClients();
    }
}

#endif // RFLINK_PORTAL_DISABLED

#endif
