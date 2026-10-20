"""Guard the AP Wi-Fi setup page against incomplete response regressions."""

import sys
from pathlib import Path

source = Path(sys.argv[1]).read_text(encoding="utf-8")
required = (
    "static const char wifiSetupPageStable[] PROGMEM",
    "%WIFI_OPTIONS%",
    "id=\"wifi-form\"",
    "id=\"client_password\"",
    "id=\"toggle-password\"",
    "type=\"submit\" class=\"btn\" value=\"Save\"",
    "id=\"toast\"",
    "fetch('/wifi'",
    "request->beginResponse(",
    "strlen_P(wifiSetupPageStable)",
    "wifiSetupOptions",
    "background:#e4f4f9",
    "color:#183b4a",
    "border-bottom:3px solid #1596c8",
    "@media(max-width:520px)",
)

missing = [fragment for fragment in required if fragment not in source]
if missing:
    raise SystemExit("Wi-Fi setup page is incomplete: " + ", ".join(missing))

handler_start = source.index("void serveWifiSetupPage")
handler_end = source.index("void serveWifiSetupPost", handler_start)
handler = source[handler_start:handler_end]
if handler.index("request->send(staticResponse);") > handler.index("AsyncResponseStream"):
    raise SystemExit("Wi-Fi setup page must use the flash-backed response before any response stream")

if "wifiSetupPageStable" not in handler:
    raise SystemExit("Wi-Fi setup handler is not serving the stable portal markup")

active_start = source.index("static const char wifiSetupPageStable[] PROGMEM")
active_end = source.index(")HTML\";", active_start)
active_page = source[active_start:active_end]
desktop_styles = active_page.split("@media", 1)[0]
if desktop_styles.count(".bar{") != 1:
    raise SystemExit("Wi-Fi setup page must have one unambiguous header style.")

print("wifi_setup_page=complete_flash_backed")
