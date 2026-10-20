"""Verify the fixed gzip AP portal asset and its C++ transport contract."""

from __future__ import annotations

import gzip
import re
import sys
from pathlib import Path

source = Path(sys.argv[1]).read_text(encoding="utf-8")
page = Path(sys.argv[2]).read_text(encoding="utf-8")
header = Path(sys.argv[3]).read_text(encoding="ascii")
wifi = Path(sys.argv[4]).read_text(encoding="utf-8")

required_page = (
    '<form id="wifi-form"',
    'id="client_ssid"',
    'id="client_password"',
    'id="toggle-password"',
    'class="save" type="submit"',
    'id="toast"',
    "fetch('/wifi/networks'",
    "fetch('/wifi'",
    "response.json()",
    "textContent",
)
missing = [fragment for fragment in required_page if fragment not in page]
if missing:
    raise SystemExit("Wi-Fi portal page is incomplete: " + ", ".join(missing))

values = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-f]{2})", header))
if gzip.decompress(values).decode("utf-8") != page:
    raise SystemExit("Wi-Fi portal gzip header does not match source HTML")

required_source = (
    '#include "WifiSetupPage_gz.h"',
    'wifiSetupPageGzip,',
    'wifiSetupPageGzipLength',
    'addHeader(F("Content-Encoding"), F("gzip"))',
    'F("application/json; charset=utf-8")',
    'createNestedArray(F("networks"))',
)
missing = [fragment for fragment in required_source if fragment not in source]
if missing:
    raise SystemExit("Wi-Fi portal transport is incomplete: " + ", ".join(missing))

if "saved network" in page:
    raise SystemExit("Wi-Fi portal must not include a saved-network fallback")

if "payload.selected" in page or 'root[F("selected")]' in source:
    raise SystemExit("Wi-Fi portal must not restore a configured-network selection")

if "select.options.length === 0" not in page:
    raise SystemExit("Wi-Fi portal must preselect the strongest fresh scan result")

if "wifiSetupPageStable" in source or "wifiSetupOptions" in source or "AsyncResponseStream" in source:
    raise SystemExit("legacy Wi-Fi portal renderer remains active")

handler = source[source.index("void serveWifiSetupPage"):source.index("void serveWifiSetupPost")]
if "wifiSetupPageGzipLength," in handler:
    raise SystemExit("Wi-Fi portal response must not attach a template processor")

required_wifi = (
    "const int MAX_WIFI_SCAN_CACHE = 4;",
    "if(ssid == cachedWifiSsids[j])",
    "cachedWifiNetworkCount--;",
)
missing = [fragment for fragment in required_wifi if fragment not in wifi]
if missing:
    raise SystemExit("Wi-Fi scan cache is incomplete: " + ", ".join(missing))

print("wifi_setup_page=fixed_gzip_asset")
