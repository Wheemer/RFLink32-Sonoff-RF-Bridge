# Modified Sonoff RF Bridge Setup

RF Bridge is built for one device only: a Sonoff RF Bridge R2 v2.2 with the direct-hack radio modification. It is not compatible with an unmodified bridge or other ESP8266/ESP32 boards.

## 1. Modify the bridge

The Sonoff's original radio decoder must be bypassed so the ESP8285 communicates directly with the receiver and transmitter. This requires a small hardware modification before the firmware can receive or send RF reliably.

1. Open the bridge and confirm the PCB is marked **R2 v2.2**.
2. Cut the three USB-data traces and the trace between the decoder and RF transceiver, as shown below.
3. Solder two 180-680 ohm resistors between `USBRXD` and pin 5 of U4, and between `USBTXD` and pin 4 of U5.

![Back of the modified Sonoff RF Bridge R2 v2.2](sonoff_22_back.png)

![Front of the modified Sonoff RF Bridge R2 v2.2](sonoff_22_front.png)

Do not connect both the UART adapter's 3.3 V rail and the bridge's USB power at the same time.

## 2. Perform the first local flash

The initial install is performed over UART. Solder a header to `3V3`, `RX`, `TX`, and `GND`; hold the pairing button while powering the bridge to enter the bootloader.

Make a backup first:

```shell
python -m esptool --baud 460800 read_flash 0x00000 0x100000 sonoff-rf-bridge-backup.bin
```

Then erase and write the release binary:

```shell
python -m esptool --baud 460800 erase_flash
python -m esptool --baud 460800 write_flash -fs 1MB -fm dout 0x0 RFBridge-<version>-local-recovery.bin
```

![UART wiring for the first local flash](sonoff_22_serial.png)

## 3. Join Wi-Fi and add it to Home Assistant

After the first boot, connect to the bridge setup access point, choose the Wi-Fi network, enter its password, and save. When the bridge joins Wi-Fi, the setup access point turns off and Home Assistant discovers the device.

From then on, install firmware releases from the RF Bridge firmware update entity in Home Assistant. The normal command and learning path is the local RF Bridge integration; there is no MQTT or serial-to-network transport to configure.
