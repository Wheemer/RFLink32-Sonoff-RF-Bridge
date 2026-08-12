# RFLink32 - Sonoff RF Bridge

This repository is a Sonoff RF Bridge focused build of
[RFLink32](https://github.com/cpainchaud/RFLink32).

It is not a general RFLink32 board-support fork. It is meant for the ESP8285
Sonoff RF Bridge, especially modified bridges using RFLink raw send/receive
through a local HTTP API.

## What This Build Changes

- Adds a larger RFLink command buffer for long raw RF payloads.
- Defaults the `sonoff_bridge` environment for the ESP8285 Sonoff RF Bridge.
- Uses hardcoded RF Bridge radio pins to save space.
- Defaults async signal mode on.
- Provides a minimal embedded setup UI for Wi-Fi name/password only, with the
  strongest scanned networks rendered directly in the page and DHCP forced on
  save.
- Adds captive-portal handling for first setup from the device AP.
- Fixes Wi-Fi form handling so disabled checkboxes, including AP enable, persist.
- Removes MQTT and Serial2Net as Home Assistant command transports.
- Adds a local HTTP command endpoint at `/api/command`.
- Adds a local HTTP receive queue at `/api/events` for decoded RFLink messages.
- Keeps captive Wi-Fi setup on port `80` and serves the local API separately on
  port `8080`.
- Advertises `_rfbridge._tcp.local.` for the bundled `RF Bridge` Home Assistant
  integration.
- Automatically shuts down the setup AP after client Wi-Fi successfully receives
  an IP address.
- Reports the hardware profile as `RFLink32 - Sonoff RF Bridge`.

## What This Build Does Not Include

- OTA firmware updates.
- Web firmware upload.
- MQTT transport.
- Serial2Net transport.
- Generic ESP32/LoRa/CC1101 board setup guidance.
- OLED display support.
- Generic receiver/transmitter pinout setup pages for arbitrary boards.

Flash firmware by serial.

## Firmware

The latest built firmware is published on the GitHub releases page:

[Releases](https://github.com/Wheemer/RFLink32-Sonoff-RF-Bridge/releases)

The current local build artifact is:

`sonoff_bridge-rflink32-local-api-5.3-learn-armed.bin`

Current verified SHA256:

`73bec8ddef9b80f8084be7c9dad51071a6fe0150172f39508e76cc1772816ff0`

## Build

Build the Sonoff RF Bridge firmware with PlatformIO:

```bash
platformio run -e sonoff_bridge
```

The relevant environment is:

```ini
[env:sonoff_bridge]
board = esp8285
```

The build flags for this target include:

```ini
-D TARGET_BOARD_ESP8285
-D RFLINK_WIFI_ENABLED
-D RFLINK_NO_RADIOLIB_SUPPORT
-D RFLINK_SERIAL_RX_BUFFER_SIZE=1600
-D RFLINK_BUILDNAME='"LocalAPI"'
-D RFLINK_HARDWARE_NAME='"RFLink32 - Sonoff RF Bridge"'
-D RFLINK_USE_HARDCODED_RADIO_PINS_ONLY
-D RFLINK_MQTT_DISABLED
```

## Flash

Flash by serial using your preferred ESP8266/ESP8285 flashing tool. The release
asset is a normal ESP8266 firmware image for the Sonoff RF Bridge target.

After flashing, connect to the device AP and configure Wi-Fi from the embedded
setup UI. The setup page exposes only a Wi-Fi network dropdown, Wi-Fi password,
and save; DHCP is automatic. Once the bridge joins the LAN, the bundled Home
Assistant integration can discover it through the firmware's
`_rfbridge._tcp.local.` mDNS service and talk to the bridge directly over HTTP
on the advertised API port.

The setup UI and local HTTP API do not require device web credentials.

## Home Assistant

This repo also includes the custom Home Assistant integration:

```text
custom_components/rf_bridge
```

The integration is named `RF Bridge`. It is discovered by Zeroconf after Wi-Fi
setup and communicates directly with the bridge's local HTTP API. It creates
configurable command buttons and services that post RFLink command lines to
`/api/command`, built-in config buttons for learning the next received RF event
and rebooting the bridge, and an event entity for decoded 433 MHz RFLink
messages received through `/api/events`. It also creates diagnostic sensors for
firmware, hardware, host, API port, uptime, heap, last command result, last
error, and last RF event so the config entry has a real Home Assistant device
even before command buttons are configured. Configure command buttons from the
integration options.

## Command Transport

This build keeps the RFLink32 command protocol and sends commands directly over
local HTTP on API port `8080`:

```http
POST /api/command
{"command":"10;signal;sendRF;..."}
```

That matters for commands shaped like:

```text
10;signal;sendRF;{...}
```

Short test commands are not enough to validate this path. A short command can
fit while a long raw RF payload is still truncated on a stock-sized buffer.

## Web UI

The web UI in this fork is intentionally lightweight. It keeps only Wi-Fi setup
and removes normal user-facing pages that do not apply after Home Assistant
discovery, such as MQTT setup, static IP settings, firmware upload, signal
tuning, advanced JSON editing, and unsupported radio/plugin configuration. The
HTTP API remains available on port `8080` for integration/bootstrap and recovery
tooling.

## Received RF Events

Decoded RFLink messages and undecoded raw captures are stored in a small
in-memory queue and exposed at:

```http
GET /api/events?since=<cursor>&limit=8
```

The Home Assistant integration polls that endpoint and exposes received frames
through an `event` entity named `RF event`. The `Learn next RF event` button
advances the cursor to the current queue head so the next physical 433 MHz press
is easy to identify.

When `Learn next RF event` is pressed, the integration arms a short raw-learning
window on the bridge. If RFLink cannot decode the next received signal, the
firmware emits one raw event and then disarms raw learning. When the raw pulse
list fits the command buffer, the event includes a replayable
`10;signal;sendRF;...` command in the `command` field so unsupported 433 MHz
buttons can still be learned and saved from Home Assistant without flooding
Home Assistant with ambient RF noise.

## Upstream

This work is based on RFLink32 by cpainchaud:

https://github.com/cpainchaud/RFLink32

RFLink32 itself is forked from RFLink-ESP, which is forked from the original
RFLink project.

## License

See [License.txt](License.txt).
