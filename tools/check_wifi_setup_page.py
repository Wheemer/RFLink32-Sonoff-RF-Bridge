"""Guard the AP Wi-Fi setup page against incomplete response regressions."""

import sys
from pathlib import Path

source = Path(sys.argv[1]).read_text(encoding="utf-8")
required = (
    "static const char wifiSetupPageStable[] PROGMEM",
    "Scanning nearby networks...",
    "id=\"wifi-form\"",
    "id=\"client_password\"",
    "id=\"toggle-password\"",
    "type=\"submit\" class=\"btn\" value=\"Save\"",
    "id=\"toast\"",
    "fetch('/wifi'",
    "request->beginResponse(",
    "strlen_P(wifiSetupPageStable)",
    "fetch('/wifi/networks'",
    "serveWifiSetupNetworks",
    "background:#e4f4f9",
    "color:#183b4a",
    "border-bottom:3px solid #1596c8",
    "@media(max-width:520px)",
)

missing = [fragment for fragment in required if fragment not in source]
if missing:
    raise SystemExit("Wi-Fi setup page is incomplete: " + ", ".join(missing))

if source.count("static const char wifiSetupPage") != 1:
    raise SystemExit("Wi-Fi setup source must contain exactly one portal template")

handler_start = source.index("void serveWifiSetupPage")
handler_end = source.index("void serveWifiSetupPost", handler_start)
handler = source[handler_start:handler_end]
if "AsyncResponseStream" in handler:
    raise SystemExit("Wi-Fi setup page must not retain a streamed fallback renderer")

if "wifiSetupPageStable" not in handler:
    raise SystemExit("Wi-Fi setup handler is not serving the stable portal markup")

active_start = source.index("static const char wifiSetupPageStable[] PROGMEM")
active_end = source.index(")HTML\";", active_start)
active_page = source[active_start:active_end]
if "strlen_P(wifiSetupPageStable),\n                  wifiSetupOptions" in handler:
    raise SystemExit("Wi-Fi setup page must not use the AsyncWebServer template processor")
desktop_styles = active_page.split("@media", 1)[0]
if desktop_styles.count(".bar{") != 1:
    raise SystemExit("Wi-Fi setup page must have one unambiguous header style.")

print("wifi_setup_page=complete_flash_backed")
