# RFLink32 - Sonoff RF Bridge

This repository is a Sonoff RF Bridge focused build of
[RFLink32](https://github.com/cpainchaud/RFLink32).

It is not a general RFLink32 board-support fork. It is meant for the ESP8285
Sonoff RF Bridge, especially modified bridges using RFLink raw send/receive
over serial, MQTT, or Serial2Net.

## What This Build Changes

- Adds configurable long-command buffers:
  - `RFLINK_MQTT_BUFFER_SIZE=1600`
  - `RFLINK_SERIAL2NET_BUFFER_SIZE=1600`
  - `RFLINK_SERIAL_RX_BUFFER_SIZE=1600`
- Defaults the `sonoff_bridge` environment for the ESP8285 Sonoff RF Bridge.
- Uses hardcoded RF Bridge radio pins to save space.
- Defaults Serial2Net and async signal mode on.
- Provides a lighter embedded web UI that fits the constrained device better.
- Adds captive-portal handling for first setup from the device AP.
- Publishes raw RF debug/capture output over MQTT as well as Serial2Net/serial.
- Fixes Wi-Fi form handling so disabled checkboxes, including AP enable, persist.
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

The first release asset is:

`sonoff_bridge-rflink32-custom-ui-captive-p6-buffer1600-mqtt-raw-capture.bin`

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
-D RFLINK_SERIAL2NET_BUFFER_SIZE=1600
-D RFLINK_SERIAL_RX_BUFFER_SIZE=1600
-D RFLINK_BUILDNAME='"Sonoff_custom_ui_captive_p6_buffer1600"'
-D RFLINK_HARDWARE_NAME='"RFLink32 - Sonoff RF Bridge"'
-D RFLINK_USE_HARDCODED_RADIO_PINS_ONLY
```

## Flash

Flash by serial using your preferred ESP8266/ESP8285 flashing tool. The release
asset is a normal ESP8266 firmware image for the Sonoff RF Bridge target.

After flashing, connect to the device AP and configure Wi-Fi from the embedded
web UI.

Default web credentials are inherited from RFLink32:

- username: `rflink32`
- password: `433mhz`

## Command Transport

This build keeps the RFLink32 command protocol and increases the transport
buffers so long raw RF payloads can survive MQTT, Serial2Net, and serial RX.
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
configuration surfaces for this build and removes pages that do not apply to the
Sonoff RF Bridge target, such as firmware upload and unsupported radio/plugin
configuration.

## Upstream

This work is based on RFLink32 by cpainchaud:

https://github.com/cpainchaud/RFLink32

RFLink32 itself is forked from RFLink-ESP, which is forked from the original
RFLink project.

## License

See [License.txt](License.txt).
