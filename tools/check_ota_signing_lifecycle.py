"""Reject firmware that initializes its OTA verifier only after an update is requested."""

from __future__ import annotations

from pathlib import Path

SOURCE = Path("RFLink/13_Update.cpp")


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    setup = """void setup()
  {
    // BearSSL allocates a 6.2 KB secondary stack for signature verification.
    // Reserve it before Wi-Fi and websocket activity fragment the ESP8285 heap.
    char error[sizeof(updateLastError)] = {};
    if(!initialiseSigningVerifier(error, sizeof(error))) {"""
    if setup not in source:
        raise SystemExit("OTA signing verifier must be initialized during firmware setup.")
    if "static SigningContext context;" not in source:
        raise SystemExit("OTA signing context must persist for the firmware lifetime.")
    if "Update.installSignature(&context.hash, &context.verifier);" not in source:
        raise SystemExit("OTA update path does not use the prepared signing context.")
    print("ota_signing_lifecycle=setup_initialized")


if __name__ == "__main__":
    main()
