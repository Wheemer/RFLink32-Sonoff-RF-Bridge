"""Verify the native ESP8266 signed-OTA and API-handoff contracts."""

from __future__ import annotations

from pathlib import Path

SOURCE = Path("RFLink/13_Update.cpp")


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    platformio = Path("platformio.ini").read_text(encoding="utf-8")
    if "-I RFLink" not in platformio:
        raise SystemExit(
            "Updater_Signing.h must be visible while the ESP8266 framework compiles."
        )
    if "SigningContext" in source or "Update.installSignature(" in source:
        raise SystemExit(
            "OTA must use the ESP8266 core signer, not a parallel application signer."
        )
    if '#include "Updater_Signing.h"' in source:
        raise SystemExit("The application must not instantiate the core signing header itself.")
    if "updater.closeConnectionsOnUpdate(true);" not in source:
        raise SystemExit("OTA must close non-update connections before flash writes.")
    if "void setupPersistentState()" not in source:
        raise SystemExit("OTA state must be initialized after the persistent filesystem mounts.")
    main_source = Path("RFLink/RFLink.cpp").read_text(encoding="utf-8")
    if main_source.index("RFLink::Config::setup();") > main_source.index(
        "RFLink::UpdateSecurity::setupPersistentState();"
    ):
        raise SystemExit("OTA persistent state is initialized before LittleFS is mounted.")
    portal_source = Path("RFLink/12_Portal_ws.cpp").read_text(encoding="utf-8")
    acknowledgement = portal_source.index('root[F("type")] = F("update_scheduled");')
    handoff = portal_source.index("RFLink::UpdateSecurity::beginHandoff();")
    if handoff <= acknowledgement:
        raise SystemExit("OTA handoff begins before the update acknowledgement is queued.")
    if "apiWebSocket.closeAll();" not in portal_source[acknowledgement:handoff + 128]:
        raise SystemExit("OTA handoff must release API WebSocket allocations before flashing.")
    portal_header = Path("RFLink/12_Portal.h").read_text(encoding="utf-8")
    portal_runtime = Path("RFLink/12_Portal.cpp").read_text(encoding="utf-8")
    if "void prepareForUpdate();" not in portal_header:
        raise SystemExit("OTA handoff must expose a main-loop API shutdown path.")
    prepare_start = portal_runtime.index("void prepareForUpdate()")
    prepare_end = portal_runtime.index("void notifyNetworkReady()", prepare_start)
    prepare = portal_runtime[prepare_start:prepare_end]
    for required in (
        "stopSetupServer();",
        "closeWebSocketClients();",
        "cleanupWebSocketClients();",
        "apiServer.end();",
    ):
        if required not in prepare:
            raise SystemExit("OTA handoff must release all API-server allocations before download.")
    update_handoff = source.index("void maintain()")
    update_call = source.index("fetchAndApplyUpdate(", update_handoff)
    if "RFLink::Portal::prepareForUpdate();" not in source[update_handoff:update_call]:
        raise SystemExit("OTA update must stop the API server before constructing its HTTP client.")
    print("ota_signing_lifecycle=filesystem_and_api_handoff_verified")


if __name__ == "__main__":
    main()
