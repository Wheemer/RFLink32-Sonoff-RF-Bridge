"""Prevent the RF Bridge setup and API listeners from being conflated."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def _require(path: str, fragment: str) -> str | None:
    source = (ROOT / path).read_text(encoding="utf-8")
    if fragment not in source:
        return f"missing {fragment!r} in {path}"
    return None


def main() -> int:
    checks = (
        _require("RFLink/12_Portal.cpp", "AsyncWebServer setupServer(80);"),
        _require("RFLink/12_Portal.cpp", "ApiWebServer apiServer(RFLINK_API_PORT);"),
        _require("RFLink/10_Wifi.cpp", "RFLink::Portal::stopSetupServer();"),
        _require(
            "RFLink/10_Wifi.cpp",
            "if((WiFi.getMode() & WIFI_AP) == 0)\n            RFLink::Portal::stopSetupServer();",
        ),
    )
    failures = [check for check in checks if check is not None]
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
