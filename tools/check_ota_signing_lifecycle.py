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
    if "updater.closeConnectionsOnUpdate(false);" not in source:
        raise SystemExit("OTA must not tear down all TCP clients while fetching the image.")
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
    print("ota_signing_lifecycle=filesystem_and_websocket_handoff_verified")


if __name__ == "__main__":
    main()
