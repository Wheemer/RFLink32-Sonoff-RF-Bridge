"""Reject firmware that initializes its OTA verifier only after an update is requested."""

from __future__ import annotations

from pathlib import Path

SOURCE = Path("RFLink/13_Update.cpp")


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    setup_start = """void setup()
  {"""
    verifier_initialization = """if(!initialiseSigningVerifier(error, sizeof(error))) {"""
    if setup_start not in source or verifier_initialization not in source:
        raise SystemExit("OTA signing verifier must be initialized during firmware setup.")
    if "static SigningContext context;" not in source:
        raise SystemExit("OTA signing context must persist for the firmware lifetime.")
    if "Update.installSignature(&context.hash, &context.verifier);" not in source:
        raise SystemExit("OTA update path does not use the prepared signing context.")
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
    hub_source = Path("custom_components/rf_bridge/__init__.py").read_text(
        encoding="utf-8"
    )
    update_source = Path("custom_components/rf_bridge/update.py").read_text(
        encoding="utf-8"
    )
    if "FIRMWARE_UPDATE_QUIET_SECONDS = 15" not in hub_source:
        raise SystemExit("Home Assistant must suspend WebSocket reconnects during OTA.")
    if "self._ota_quiet_until" not in hub_source:
        raise SystemExit("Home Assistant OTA quiet-window state is missing.")
    if "FIRMWARE_UPDATE_SETTLE_SECONDS = 15" not in update_source:
        raise SystemExit("Home Assistant must defer OTA status probes until flashing ends.")
    print("ota_signing_lifecycle=filesystem_and_api_handoff_verified")


if __name__ == "__main__":
    main()
