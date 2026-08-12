# RFLink32 - Sonoff RF Bridge

This repository is a Sonoff RF Bridge focused build of
[RFLink32](https://github.com/cpainchaud/RFLink32).

It is not a general RFLink32 board-support fork. It is meant for the ESP8285
Sonoff RF Bridge, especially modified bridges using RFLink raw send/receive
over MQTT.

## What This Build Changes

- Adds configurable long-command buffers:
  - `RFLINK_MQTT_BUFFER_SIZE=1600`
  - `RFLINK_SERIAL_RX_BUFFER_SIZE=1600`
- Defaults the `sonoff_bridge` environment for the ESP8285 Sonoff RF Bridge.
- Uses hardcoded RF Bridge radio pins to save space.
- Defaults async signal mode on.
- Provides a minimal embedded setup UI for Wi-Fi and MQTT only.
- Adds captive-portal handling for first setup from the device AP.
- Publishes raw RF debug/capture output over MQTT.
- Fixes Wi-Fi form handling so disabled checkboxes, including AP enable, persist.
- Removes Serial2Net from this Sonoff RF Bridge build; MQTT is the network command and capture path.
- Publishes retained Home Assistant MQTT discovery for the bridge reboot button.
- Publishes retained `rflink32/<mac>/discovery` metadata for the bundled
  `RF Bridge to MQTT` Home Assistant integration.
- Automatically shuts down the setup AP after client Wi-Fi successfully receives
  an IP address.
- Reports the hardware profile as `RFLink32 - Sonoff RF Bridge`.

## What This Build Does Not Include

- OTA firmware updates.
- Web firmware upload.
- Generic ESP32/LoRa/CC1101 board setup guidance.
- OLED display support.
- Generic receiver/transmitter pinout setup pages for arbitrary boards.

Flash firmware by serial.

## Firmware

The latest built firmware is published on the GitHub releases page:

[Releases](https://github.com/Wheemer/RFLink32-Sonoff-RF-Bridge/releases)

The current local build artifact is:

`sonoff_bridge-rflink32-mqtt-setup-only-no-serial2net.bin`

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
-D RFLINK_MQTT_BUFFER_SIZE=1600
-D RFLINK_SERIAL_RX_BUFFER_SIZE=1600
-D RFLINK_BUILDNAME='"Sonoff_mqtt_raw_capture_no_serial2net"'
-D RFLINK_HARDWARE_NAME='"RFLink32 - Sonoff RF Bridge"'
-D RFLINK_USE_HARDCODED_RADIO_PINS_ONLY
```

## Flash

Flash by serial using your preferred ESP8266/ESP8285 flashing tool. The release
asset is a normal ESP8266 firmware image for the Sonoff RF Bridge target.

After flashing, connect to the device AP and configure Wi-Fi plus MQTT from the
embedded setup UI.

Default web credentials are inherited from RFLink32:

- username: `rflink32`
- password: `433mhz`

## Home Assistant

This repo also includes the custom Home Assistant integration:

```text
custom_components/rf_bridge_to_mqtt
```

The integration is named `RF Bridge to MQTT`. It can be discovered from the
firmware's retained MQTT metadata once the bridge is connected to MQTT. It also
creates configurable command buttons and services that publish RFLink command
lines to the bridge MQTT command topic. Configure command buttons from the
integration options.

## Command Transport

This build keeps the RFLink32 command protocol and increases the transport
buffers so long raw RF payloads can survive MQTT and serial RX.
It also routes `RFDEBUG` and `RFUDEBUG` raw receive output to the configured
MQTT output topic, so MQTT-only installs can capture raw waveforms.

That matters for commands shaped like:

```text
10;signal;sendRF;{...}
```

Short test commands are not enough to validate this path. A short command can
fit while a long raw RF payload is still truncated on a stock-sized buffer.

## Web UI

The web UI in this fork is intentionally lightweight. It keeps the useful
setup surfaces for this build and removes normal user-facing pages that do not
apply after Home Assistant discovery, such as firmware upload, signal tuning,
advanced JSON editing, and unsupported radio/plugin configuration. The HTTP API
remains available for integration/bootstrap and recovery tooling.

## Upstream

This work is based on RFLink32 by cpainchaud:

https://github.com/cpainchaud/RFLink32

RFLink32 itself is forked from RFLink-ESP, which is forked from the original
RFLink project.

## License

See [License.txt](License.txt).
