#!/usr/bin/env python3
"""Guard the AP Wi-Fi setup page against incomplete response regressions."""

from pathlib import Path
import sys


source = Path(sys.argv[1]).read_text(encoding="utf-8")
required = (
    "static const char wifiSetupPage[] PROGMEM",
    "%WIFI_OPTIONS%",
    "id=\"wifi-form\"",
    "id=\"client_password\"",
    "id=\"toggle-password\"",
    "type=\"submit\" class=\"btn\" value=\"Save\"",
    "id=\"toast\"",
    "fetch('/wifi'",
    "request->beginResponse(",
    "strlen_P(wifiSetupPage)",
    "wifiSetupOptions",
)

missing = [fragment for fragment in required if fragment not in source]
if missing:
    raise SystemExit("Wi-Fi setup page is incomplete: " + ", ".join(missing))

handler_start = source.index("void serveWifiSetupPage")
handler_end = source.index("void serveWifiSetupPost", handler_start)
handler = source[handler_start:handler_end]
if handler.index("request->send(staticResponse);") > handler.index("AsyncResponseStream"):
    raise SystemExit("Wi-Fi setup page must use the flash-backed response before any response stream")

print("wifi_setup_page=complete_flash_backed")
