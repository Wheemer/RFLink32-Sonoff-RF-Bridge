<div align="center">

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/Wheemer/RFLink32-Sonoff-RF-Bridge/7b471a1/custom_components/rf_bridge/brand/dark_logo.png">
    <img src="https://raw.githubusercontent.com/Wheemer/RFLink32-Sonoff-RF-Bridge/aebac12/custom_components/rf_bridge/brand/logo.png" alt="RF Bridge" width="440">
  </picture>
</p>

### Local 433 MHz control for a modified Sonoff RF Bridge and Home Assistant

[![HACS Custom](https://img.shields.io/badge/HACS-CUSTOM-FD7E14?style=for-the-badge&logo=home-assistant&logoColor=white&labelColor=555555)](https://github.com/hacs/integration)
[![Home Assistant](https://img.shields.io/badge/HOME%20ASSISTANT-CUSTOM%20INTEGRATION-41BDF5?style=for-the-badge&logo=home-assistant&logoColor=white&labelColor=555555)](https://www.home-assistant.io/)
[![Latest release](https://img.shields.io/github/v/release/Wheemer/RFLink32-Sonoff-RF-Bridge?style=for-the-badge&logo=github&logoColor=white&label=RELEASE&labelColor=555555&color=22C55E)](https://github.com/Wheemer/RFLink32-Sonoff-RF-Bridge/releases/latest)
[![Build](https://img.shields.io/github/actions/workflow/status/Wheemer/RFLink32-Sonoff-RF-Bridge/ci.yml?style=for-the-badge&logo=githubactions&logoColor=white&label=BUILD&labelColor=555555)](https://github.com/Wheemer/RFLink32-Sonoff-RF-Bridge/actions/workflows/ci.yml)
[![CodeQL](https://img.shields.io/github/actions/workflow/status/Wheemer/RFLink32-Sonoff-RF-Bridge/codeql.yml?style=for-the-badge&logo=github&logoColor=white&label=CODEQL&labelColor=555555)](https://github.com/Wheemer/RFLink32-Sonoff-RF-Bridge/security/code-scanning)
[![License](https://img.shields.io/badge/LICENSE-RFLINK%20GATEWAY-64748B?style=for-the-badge&labelColor=555555)](License.txt)

[Install](#install) • [Hardware modification](doc/sonoff_rf_bridge.md) • [Flash](#2-flash-the-bridge) • [Configure](#configure-home-assistant) • [Use](#use-the-bridge) • [Troubleshooting](#troubleshooting) • [Releases](https://github.com/Wheemer/RFLink32-Sonoff-RF-Bridge/releases)

</div>

RF Bridge is a focused [RFLink32](https://github.com/cpainchaud/RFLink32) fork for the **modified ESP8285 Sonoff RF Bridge**. It replaces the old serial and MQTT paths with a local WebSocket API and a bundled Home Assistant integration.

It is built for a single job: reliable local 433 MHz receive, learn, and transmit from Home Assistant without a broker, serial bridge, cloud account, or a permanent device web UI.

> [!WARNING]
> **Modified Sonoff RF Bridge hardware required.** This firmware targets the ESP8285 Sonoff RF Bridge after the required RF modification. Do not flash it to an unmodified bridge or another RFLink32 board.

## What It Does

- **Local WebSocket control:** Home Assistant talks directly to the bridge over the LAN on port `8080`.
- **Automatic discovery:** The bridge advertises itself after Wi-Fi setup and appears as **RF Bridge** in Home Assistant.
- **433 MHz receive:** Decoded RFLink messages arrive through one Home Assistant event entity.
- **Learn mode:** Capture the next physical RF press and retain a replayable command when the waveform fits the bridge command buffer.
- **Transmit commands:** Add named command buttons in the integration options and call the same commands from automations or scripts.
- **Signed firmware updates:** After the initial local flash, Home Assistant checks GitHub releases and installs signed OTA updates.
- **Useful diagnostics:** Connectivity, status, uptime, and installed firmware are visible in Home Assistant.
- **Small setup surface:** The temporary access point exposes only Wi-Fi selection and password entry. It closes after the bridge joins the LAN.

## Install

[![Open your Home Assistant instance and add this repository to HACS.](https://my.home-assistant.io/badges/hacs_repository.svg)](https://my.home-assistant.io/redirect/hacs_repository/?owner=Wheemer&repository=RFLink32-Sonoff-RF-Bridge&category=integration)

### 1. Install the Home Assistant integration

1. Open **HACS** in Home Assistant.
2. Open the three-dot menu, then choose **Custom repositories**.
3. Add `https://github.com/Wheemer/RFLink32-Sonoff-RF-Bridge` as an **Integration** repository.
4. Install **RF Bridge**.
5. Restart Home Assistant once so it loads the new integration code.

### 2. Flash the bridge

Before flashing, complete the [modified Sonoff RF Bridge hardware instructions](doc/sonoff_rf_bridge.md). Download the `RFBridge-<version>-local-recovery.bin` asset from the [latest release](https://github.com/Wheemer/RFLink32-Sonoff-RF-Bridge/releases/latest). Flash it with a suitable ESP8266/ESP8285 serial flashing tool.

The first flash is local. OTA updates begin only after the bridge has joined Wi-Fi and Home Assistant has added it.

### 3. Join Wi-Fi

1. Connect a phone or computer to the bridge access point.
2. Open the setup page if it does not appear automatically.
3. Choose the Wi-Fi network from the list of nearby networks.
4. Enter the Wi-Fi password and save.
5. The bridge joins the network by DHCP and turns off its setup access point.

## Configure Home Assistant

After the bridge joins Wi-Fi, Home Assistant should discover **RF Bridge** automatically.

1. Open **Settings > Devices & services**.
2. Select the discovered **RF Bridge** card and choose **Add**.
3. Give the device a name and assign its area.
4. Open **Configure** on the integration to define named transmit commands and known received commands.

The integration uses the bridge's local WebSocket API. There is no MQTT broker, RFLink serial integration, or device username/password to configure.

## Use The Bridge

### Receive and learn

- The **RF event** entity reports configured decoded commands.
- Press **Learn next RF event**, then press one physical remote button once.
- The bridge exits learn mode after the first received command.
- Use the captured data to add a meaningful received-command name in the integration configuration.

### Send

Named transmit commands are exposed as Home Assistant buttons. They can be pressed from the device page, dashboard, scripts, or automations.

The integration also exposes services for sending RFLink command lines when a configurable button is not the right fit.

### Update firmware

The **RF Bridge Firmware** update entity checks the signed GitHub release manifest. When a newer stable release is published, install it from Home Assistant. The integration downloads the signed image, verifies its SHA-256, and hands the bridge a one-time local download URL. The bridge verifies the embedded signing key before it reboots.

## How It Works

| Component | Responsibility |
| --- | --- |
| Modified Sonoff RF Bridge | RF receive/transmit, Wi-Fi onboarding, local API, signed OTA validation |
| RF Bridge integration | Discovery, Home Assistant entities, named command configuration, learning, OTA staging |
| GitHub release workflow | ESP8285 build, OTA layout validation, artifact signing, release and manifest publication |

The firmware preserves RFLink command compatibility. Commands use the local WebSocket API rather than MQTT or serial transport.

## Troubleshooting

### The bridge is not discovered

- Confirm the bridge completed Wi-Fi setup and received a DHCP address.
- Confirm Home Assistant and the bridge are on a network where multicast discovery is allowed.
- Add the integration manually only after confirming the bridge's API is reachable on port `8080`.

### A learned command is not replayable

Some signals may not decode into a supported RFLink protocol. During learning, the bridge can provide a raw replay command when the captured pulse sequence fits the supported command buffer.

### A command does not transmit

- Confirm the named command button is enabled and points at the expected RFLink command.
- Check the RF Bridge device's **Connected** and **Status** diagnostics.
- Keep the modified bridge within practical RF range of the receiver being controlled.

## Build From Source

The supported firmware target is `sonoff_bridge`:

```bash
platformio run -e sonoff_bridge
```

The repository validation workflow builds this exact target and checks the OTA image layout before publishing a release.

## Credits

This repository is a focused fork of [RFLink32](https://github.com/cpainchaud/RFLink32) by cpainchaud, itself derived from RFLink-ESP and the original RFLink project. The RFLink Gateway license remains in [License.txt](License.txt).
