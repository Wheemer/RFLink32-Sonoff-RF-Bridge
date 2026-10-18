# RFLink32 - Sonoff RF Bridge

This repository is a Sonoff RF Bridge focused build of
[RFLink32](https://github.com/cpainchaud/RFLink32).

It is not a general RFLink32 board-support fork. It is meant for the ESP8285
Sonoff RF Bridge, especially modified bridges using RFLink raw send/receive
through a local WebSocket API.

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
- Removes MQTT and Serial2Net entirely; the supported Home Assistant path is
  the local WebSocket API and the bundled `rf_bridge` integration.
- Adds a local WebSocket API at `/api/ws` for command transmit, learning,
  reboot, status, and RF event push.
- Keeps captive Wi-Fi setup on port `80` and serves the local API separately on
  port `8080`.
- Advertises `_rfbridge._tcp.local.` for the bundled `RF Bridge` Home Assistant
  integration.
- Automatically shuts down the setup AP after client Wi-Fi successfully receives
  an IP address.
- Reports the hardware profile as `RFLink32 - Sonoff RF Bridge`.

## What This Build Does Not Include

- Web firmware upload.
- MQTT support or configuration.
- Serial2Net transport.
- Generic ESP32/LoRa/CC1101 board setup guidance.
- OLED display support.
- Generic receiver/transmitter pinout setup pages for arbitrary boards.

The initial bootstrap is by serial. Subsequent signed updates are delivered by
the bundled Home Assistant integration; firmware accepts no web upload.

## Firmware

The latest built firmware is published on the GitHub releases page:

[Releases](https://github.com/Wheemer/RFLink32-Sonoff-RF-Bridge/releases)

Build locally from this tree when preparing a test image. Do not flash old
artifacts from earlier development passes.

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
-D RFLINK_BUILDNAME='"RFBridgeLocal"'
-D RFLINK_FIRMWARE_VERSION='"0.7.0"'
-D RFLINK_HARDWARE_NAME='"RFLink32 - Sonoff RF Bridge"'
-D RFLINK_USE_HARDCODED_RADIO_PINS_ONLY
```

## Flash

Flash the initial bootstrap image by serial using your preferred ESP8266/ESP8285
flashing tool. The release asset is a normal ESP8266 firmware image for the
modified Sonoff RF Bridge target.

After flashing, connect to the device AP and configure Wi-Fi from the embedded
setup UI. The setup page exposes only a Wi-Fi network dropdown, Wi-Fi password,
and save; DHCP is automatic. Once the bridge joins the LAN, the bundled Home
Assistant integration can discover it through the firmware's
`_rfbridge._tcp.local.` mDNS service and talk to the bridge directly over the
local WebSocket API on the advertised API port.

The setup UI and local WebSocket API do not require device web credentials.

## Signed Updates

The bridge accepts firmware only through its local WebSocket API and verifies a
RSA-2048 signature before rebooting. Home Assistant downloads the release asset
over HTTPS, verifies its SHA-256 from `manifest.json`, exposes that exact signed
artifact through a one-time local HTTP URL, and then tells the bridge to fetch
it. The bridge verifies the signature independently. There is no device web
upload endpoint and no device username/password.

Create release assets with `tools/package_sonoff_bridge_update.py`. Supply
`--version` and the final HTTPS `--artifact-url` to generate the matching
`manifest.json`; publish both alongside the signed artifact. The update entity
will stay unknown until a valid published release manifest exists.

## Home Assistant

This repo also includes the custom Home Assistant integration:

```text
custom_components/rf_bridge
```

The integration is named `RF Bridge`. It is discovered by Zeroconf after Wi-Fi
setup and communicates directly with the bridge's local WebSocket API. It
creates configurable command buttons and services that send RFLink command
lines through `/api/ws`, built-in config buttons for learning the next received
RF event and rebooting the bridge, and an event entity for decoded 433 MHz
RFLink messages pushed by the bridge. It also creates diagnostic sensors for
firmware, hardware, host, API port, uptime, heap, reset reason, command
diagnostics, last command result, last error, and last RF event so the config
entry has a real Home Assistant device even before command buttons are
configured. Configure command buttons from the integration options.

Command definitions may use either one RFLink command string or a list of
command strings. Multi-command definitions wait `sequence_delay` seconds between
entries, defaulting to `0.6` seconds so devices that beep or wake before the
next command are not driven too quickly.

## Command Transport

This build keeps the RFLink32 command protocol and sends commands directly over
the local WebSocket API on API port `8080`:

```json
{"type":"command","id":1,"command":"10;signal;sendRF;..."}
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
discovery, such as static IP settings, firmware upload, signal
tuning, advanced JSON editing, and unsupported radio/plugin configuration. The
WebSocket API remains available on port `8080` for integration/bootstrap and
recovery tooling.

## Received RF Events

Decoded RFLink messages and undecoded raw captures are pushed over `/api/ws`
and also kept in a small in-memory queue. The Home Assistant integration
exposes configured received commands through an `event` entity named `RF
event`. Normal decoded RFLink frames only trigger the event entity when they
match an entry in the integration's known received command JSON. Exact keys of
the form `protocol_id_switch_cmd` are preferred, with `protocol_id_switch` as a
fallback. The `Learn next RF event` button arms a short raw-learning window so
the next physical 433 MHz press is easy to identify.

When `Learn next RF event` is pressed, the integration arms a short raw-learning
window on the bridge through its existing persistent WebSocket connection. If
RFLink cannot decode the next received signal, the firmware emits one raw event
over that same connection and then disarms raw learning. When the raw pulse
list fits the command buffer, the event includes a replayable
`10;signal;sendRF;...` command in the `command` field. Learned replay commands
store real microsecond pulses and do not include a `multiply` field, matching
the firmware parser and Home Assistant validator.

## Upstream

This work is based on RFLink32 by cpainchaud:

https://github.com/cpainchaud/RFLink32

RFLink32 itself is forked from RFLink-ESP, which is forked from the original
RFLink project.

## License

See [License.txt](License.txt).
