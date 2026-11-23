# Firmware

*[Italiano](../it/firmware.md)*

C firmware for the Raspberry Pi Pico W (RP2040), built with pico-sdk, TinyUSB and BTstack.
Sources in [`firmware/`](../../firmware), ready-made files in
[`firmware/prebuilt/`](../../firmware/prebuilt):

| File | Board |
|---|---|
| `trackpad-a1708_pico_w.uf2` | Raspberry Pi Pico W / WH (RP2040), **tested** |
| `trackpad-a1708_pico2_w.uf2` | Raspberry Pi Pico 2 W (RP2350), builds fine, not tested with this version |

## Flashing

**First time** (or with the case open): hold the Pico's **BOOTSEL** button, connect the Pico's
micro-USB to the computer, and drag the `.uf2` file onto the **RPI-RP2** drive.

**Afterwards, from the board's USB-C** (requires mod 2, the USB data wires): with the board off,
hold **buttons 1 and 3** and plug the USB-C cable into the computer. The three LEDs light up for a
moment and the RPI-RP2 drive appears. Drag the file onto it.

Pairings and settings are stored in flash and survive updates.

## Using it

### Connections

- **USB**: cable to a computer → USB HID. The cable also charges the battery.
- **Bluetooth**: no cable, or a wall charger. Two technologies, chosen at power-on:
  - **Bluetooth Low Energy** (recommended): HID over GATT, with the standard Battery Service, so the
    level shows up in the macOS battery widget and in Windows.
  - **Classic Bluetooth**: HID over BR/EDR, like the Magic Trackpad.

  **To switch** between them: switch off, hold **button 2**, switch on. LED 2 flashes **3 times
  quickly** = now BLE; it stays **on for 1 second** = now classic. The choice is saved. Each
  technology keeps its own pairings.
- With the cable plugged into a computer, a short press on a button switches to Bluetooth (the
  cable then only charges and carries the diagnostic serial port). Unplug and replug to go back
  to USB.

### Three computers

Buttons 1, 2 and 3 select a slot, as on Logitech Easy-Switch devices:

- **Short press**: switch to that slot. The trackpad reconnects to that computer in 1–4 s.
- **Hold 3 s**: pairing mode for that slot (3 minutes). Then look for **"Trackpad A1708"** in the
  computer's Bluetooth settings.

In BLE each slot uses its own Bluetooth address. A computer only reconnects when its slot is
selected, and pairing a slot again gives it a new address (remove the old entry from that
computer).

### LEDs

| What you see | Meaning |
|---|---|
| 1, 2 or 3 LEDs on for 2 s at power-on | battery level (> ⅔, ⅓–⅔, < ⅓) |
| Slow blink (~1 per second) | looking for the computer of that slot |
| Fast blink | pairing mode |
| On for 4 s, then off | just connected |
| On while the cable is plugged in | charging; turns off when charged |
| 2 short flashes every 5 s | battery low (< 3.50 V) |
| 3 short flashes every 2 s | battery almost empty (< 3.35 V) |
| All three on for a moment, buttons 1+3 held at power-on | update mode (RPI-RP2 drive) |
| Off while working | normal: connected and running |

### Power saving

- With no fingers for 1 s, the trackpad is read every 8 ms instead of every 1 ms, and the Pico
  sleeps between reads.
- **Rest mode** after 15 minutes without fingers or buttons. The trackpad is told the "lid is
  closed" and stops scanning. In this state it does not feel fingers and does not vibrate, so
  **press any button to wake it** (that press does not change the slot).

## What the firmware does

- **Reads the trackpad** over SPI (mode 0, 4 MHz, 256-byte packets, interrupts disabled during a
  transfer) at 1 kHz while in use. The trackpad sends ~125 frames/s.
- **Validates every message** as the Linux `applespi` driver does: packet CRC, message CRC,
  message length vs. finger count, reassembly of multi-packet messages.
- **Click handling**: a state machine on the raw click bit. A press counts immediately, but within
  120 ms of a release it must last ≥ 40 ms. That rejects the short "ghost" press the trackpad can
  produce ~60 ms after a release. A release needs 30 ms without the bit.
- **Haptic window**: for 120 ms after every change of the click bit (when the trackpad vibrates),
  no new fingers are created or removed and finger positions are frozen. During the vibration the
  trackpad can report phantom fingers and move a real finger by a few mm for a frame or two. Like
  a Mac, the pointer does not move while you click.
- **New fingers** must be seen in 2 consecutive frames.
- **Reports** a Windows Precision Touchpad (5 contacts, 134 × 81 mm, scan time, button) plus a
  mouse collection used until the host switches to PTP mode. For BLE the report map is compacted
  to 511 bytes: a BLE Report Map may not exceed 512 bytes.
- **Battery**: TP4056 status pins and the divider on GP28; the percentage is estimated from the
  voltage.

## Building

Requirements: [pico-sdk](https://github.com/raspberrypi/pico-sdk) 2.1.1 (with its submodules:
TinyUSB, BTstack, cyw43-driver), CMake, Ninja or Make, `arm-none-eabi-gcc` (tested with
14.2.1), Python 3 (to compile the GATT database).

```
export PICO_SDK_PATH=/path/to/pico-sdk
cd firmware
mkdir build && cd build
cmake -G Ninja -DPICO_BOARD=pico_w ..     # or -DPICO_BOARD=pico2_w
ninja
# -> trackpad_bt.uf2
```

| File | Contents |
|---|---|
| `main.c` | SPI, trackpad init, frame processing, filters, buttons, LEDs, battery, power saving, USB |
| `usb_descriptors.c`, `ptp.h` | USB descriptors and the PTP report layout |
| `bt.c` | classic Bluetooth HID (BTstack), 3 slots |
| `ble.c`, `trackpad.gatt` | BLE HID over GATT, Battery and Device Information services, 3 slots |
| `bt_scelta.c` | picks classic or BLE at power-on |

## Diagnostics

The firmware prints a log on a USB serial port (115200 baud, but any value works). To read it
while using Bluetooth: plug the cable into the computer, press a button briefly (the trackpad
switches to Bluetooth and the cable stays for the log), then:

```
cat /dev/cu.usbmodem*          # macOS
```

Every 5 s it logs frames, clicks, filtered ghosts and phantom fingers, battery voltage and,
in BLE, the connection interval.
