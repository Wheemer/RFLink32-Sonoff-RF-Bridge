<div align="center">

<img src="https://raw.githubusercontent.com/Wheemer/RF-Bridge-Home-Assistant/main/custom_components/rf_bridge/brand/dark_header_logo.png#gh-dark-mode-only" alt="RF Bridge" width="440">
<img src="https://raw.githubusercontent.com/Wheemer/RF-Bridge-Home-Assistant/main/custom_components/rf_bridge/brand/header_logo.png#gh-light-mode-only" alt="RF Bridge" width="440">

<strong>Local 433 MHz control for a modified Sonoff RF Bridge R2 v2.2 and Home Assistant.</strong>

[![Latest release](https://img.shields.io/github/v/release/Wheemer/RFLink32-Sonoff-RF-Bridge?style=for-the-badge&logo=github&logoColor=white&label=RELEASE&labelColor=555555&color=22C55E)](https://github.com/Wheemer/RFLink32-Sonoff-RF-Bridge/releases/latest)
[![Build](https://img.shields.io/github/actions/workflow/status/Wheemer/RFLink32-Sonoff-RF-Bridge/ci.yml?style=for-the-badge&logo=githubactions&logoColor=white&label=BUILD&labelColor=555555)](https://github.com/Wheemer/RFLink32-Sonoff-RF-Bridge/actions/workflows/ci.yml)
[![CodeQL](https://img.shields.io/github/actions/workflow/status/Wheemer/RFLink32-Sonoff-RF-Bridge/codeql.yml?style=for-the-badge&logo=github&logoColor=white&label=CODEQL&labelColor=555555)](https://github.com/Wheemer/RFLink32-Sonoff-RF-Bridge/security/code-scanning)
[![License](https://img.shields.io/badge/LICENSE-RFLINK%20GATEWAY-64748B?style=for-the-badge&labelColor=555555)](License.txt)

[Home Assistant integration](https://github.com/Wheemer/RF-Bridge-Home-Assistant) · [Modify the bridge](doc/sonoff_rf_bridge.md) · [Flash and connect](#first-install) · [Updates](#firmware-updates) · [Troubleshooting](#troubleshooting)

</div>

---

RF Bridge is a focused [RFLink32](https://github.com/cpainchaud/RFLink32) firmware fork for one purpose: turn a **direct-hack modified Sonoff RF Bridge R2 v2.2** into a dependable local 433 MHz bridge.

It receives, learns, and transmits RF commands through a direct local API. The separate [RF Bridge Home Assistant integration](https://github.com/Wheemer/RF-Bridge-Home-Assistant) provides discovery, configuration, named events, command buttons, diagnostics, and signed OTA updates.

> [!WARNING]
> **This is not a generic RFLink32 firmware.** It supports only the modified Sonoff RF Bridge R2 v2.2 described in the [hardware guide](doc/sonoff_rf_bridge.md). Do not flash this build to an unmodified bridge or another ESP8266/ESP32 board.

## At A Glance

| | |
| --- | --- |
| **Hardware** | Sonoff RF Bridge R2 v2.2 with the required direct-hack radio modification |
| **Radio** | Local 433 MHz receive, learn, and transmit |
| **Home Assistant API** | Direct local API used by the separate [RF Bridge integration](https://github.com/Wheemer/RF-Bridge-Home-Assistant) |
| **Transport** | Direct local WebSocket API on port `8080` |
| **First setup** | Local serial flash, then a temporary Wi-Fi setup access point |
| **Firmware updates** | Signed OTA updates delivered and installed from Home Assistant |

## What It Does

- **Direct local API:** The separate Home Assistant integration communicates with the bridge directly over the LAN through its local WebSocket API, with no broker or serial bridge in between.
- **Discovers itself:** After Wi-Fi setup, the bridge advertises `_rfbridge._tcp.local.` for the companion integration.
- **Receives, learns, and transmits:** The firmware provides the RF transport behind the integration's named buttons, event entity, and learn flow.
- **Updates from Home Assistant:** Signed release assets are installed through the companion integration after the first local flash; no return to serial flashing is required for normal releases.
- **Keeps setup small:** The temporary setup page only handles Wi-Fi onboarding; it turns itself off after a successful connection.

## Home Assistant integration

Install the companion **[RF Bridge Home Assistant integration](https://github.com/Wheemer/RF-Bridge-Home-Assistant)** through HACS. Its releases are independent from this firmware repository: HACS updates the integration; the integration's firmware entity installs signed OTA releases from this repository.

## First Install

### 1. Modify the bridge

This build requires the R2 v2.2 direct-hack radio modification. The full guide includes the board identification, trace cuts, resistor connections, and UART wiring:

**[Open the modified Sonoff RF Bridge R2 v2.2 guide](doc/sonoff_rf_bridge.md)**

### 2. Flash the release image locally

Download `RFBridge-<version>-full.bin` from the [latest release](https://github.com/Wheemer/RFLink32-Sonoff-RF-Bridge/releases/latest). This is the complete production firmware image. The first install is performed over UART; follow the exact `esptool` backup, erase, and flash steps in the [hardware guide](doc/sonoff_rf_bridge.md#2-perform-the-first-local-flash).

### 3. Connect the bridge to Wi-Fi

1. Join the bridge's temporary setup access point from a phone or computer.
2. Choose the local 2.4 GHz Wi-Fi network.
3. Enter the password and save.
4. The bridge joins via DHCP and closes the setup access point.

### 4. Add the discovered device

1. In Home Assistant, open **Settings > Devices & services**.
2. Select the discovered **RF Bridge** card and choose **Add**.
3. Name the device and assign its area.
4. Open **Configure** to define named transmit and received commands. See the integration repository for entity and learning details.

## Firmware Updates

After the local first flash and Home Assistant setup, the **RF Bridge Firmware** update entity checks the signed GitHub release manifest. When a newer stable release is available, install it from Home Assistant.

The integration validates the downloaded image's SHA-256 and gives the bridge a one-time local download URL. The bridge validates the embedded signing key before rebooting into the new image.

## Architecture

| Component | Responsibility |
| --- | --- |
| **Modified Sonoff RF Bridge** | RF receive/transmit, Wi-Fi onboarding, local API, signed OTA validation |
| **[RF Bridge integration](https://github.com/Wheemer/RF-Bridge-Home-Assistant)** | Discovery, device and entity setup, command configuration, learning, and OTA staging |
| **GitHub release workflow** | ESP8285 build, OTA layout checks, signing, release assets, and manifest publication |

The firmware preserves RFLink command compatibility while presenting it through a direct local API built around Home Assistant's current discovery, config-flow, entity, event, button, diagnostic, and update model.

## Troubleshooting

### The bridge is not discovered

1. Confirm the bridge completed Wi-Fi setup and has a DHCP address.
2. Confirm Home Assistant and the bridge can use multicast discovery on the same LAN.
3. Confirm the bridge API is reachable on port `8080` before attempting a manual integration setup.

### A learned command cannot be replayed

Not every 433 MHz transmission decodes into a supported RFLink protocol. A raw replay command is only available when the captured pulse sequence fits the bridge command buffer.

### A command does not transmit

1. Confirm the named command button has the expected RFLink command.
2. Check the device's **Connected** and **Status** diagnostics.
3. Keep the modified bridge within practical RF range of the receiver.

## Development

The only supported firmware target is `sonoff_bridge`:

```bash
platformio run -e sonoff_bridge
```

The validation workflow builds that target and verifies the OTA image layout before a release is published.

## Credits

This project is a focused fork of [RFLink32](https://github.com/cpainchaud/RFLink32) by cpainchaud, itself derived from RFLink-ESP and the original RFLink project. The RFLink Gateway license remains in [License.txt](License.txt).
