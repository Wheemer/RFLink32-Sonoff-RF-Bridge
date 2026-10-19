#!/usr/bin/env python3
"""Guard the RF Bridge Wi-Fi page against heap-buffered and incomplete output."""
from pathlib import Path

source = Path("RFLink/12_Portal.cpp").read_text(encoding="utf-8")
required = (
    "static const char wifiSetupPage[] PROGMEM",
    'beginResponse(200, F("text/html; charset=utf-8"), reinterpret_cast<const uint8_t *>(wifiSetupPage), strlen_P(wifiSetupPage), wifiSetupOptions)',
    '<input type="submit" class="btn" value="Save">',
    'id="toggle-password"',
    "v.addEventListener('click'",
    "f.addEventListener('submit'",
    "%WIFI_OPTIONS%",
)
for marker in required:
    if marker not in source:
        raise SystemExit(f"Wi-Fi setup page is missing required marker: {marker}")
if 'beginResponseStream(F("text/html; charset=utf-8"))' in source:
    raise SystemExit("Wi-Fi setup page must not allocate a heap-backed response stream.")
print("wifi_setup_page=flash_backed_complete")
