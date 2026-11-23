# A1708 trackpad: SPI protocol notes

*[Italiano](../it/protocol.md)*

What we learned by reading the Linux `applespi` driver and capturing a real MacBook Pro
(MacBookPro14,1, trackpad `05ac:0276`) with a logic analyser. Everything here is implemented in
[`firmware/main.c`](../../firmware/main.c).

## Electrical

- SPI **mode 0**, MSB first. Reads work at 4 MHz; commands are sent at 1 MHz.
- **CS lead time**: wait ~35–50 µs between CS going low and the first clock edge, and ~50 µs after
  the last one. Without it the data comes out shifted.
- Every transfer is exactly **256 bytes**. Do not pause the clock in the middle of a packet;
  the firmware disables interrupts during a transfer.
- `IF_EN` enables the SPI interface. `LID` must be **high** ("lid open"): with LID low the trackpad
  stops scanning, ignores fingers and does not vibrate (~11 mA saved, used as rest mode).
- Haptic feedback needs **12 V on PPVIN** and is generated autonomously by the trackpad on press
  and release. The supply must be stiff: brown-outs during the pulse corrupt touch and force data.

## Start-up

1. 3.3 V and 5 V present, `IF_EN` low for 100 ms.
2. `LID` high, `IF_EN` high, wait 200 ms.
3. Read a few packets (the trackpad announces itself).
4. Command `0x1020` to device `0xD0` (info request). Expect status `ac 27 68 d5`.
5. Command `0x0252` with payload `02 01` to device `0x02`: **multitouch mode on**. Status `ac 27 68 d5`.
6. Poll: send the read packet (`0x80`, zeros, CRC) and look at what comes back.

## Packet (256 bytes)

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | flags: `0x20` = data from the trackpad, `0x40` = write, `0x80` = read request / nothing |
| 1 | 1 | device: `0x01` keyboard, `0x02` touchpad, `0xD0` info |
| 2 | 2 | offset of this piece in the message (LE) |
| 4 | 2 | bytes remaining after this piece (LE) |
| 6 | 2 | length of the message data in this packet (LE, ≤ 246) |
| 8 | 246 | message (or a piece of it) |
| 254 | 2 | CRC-16 of bytes 0–253 (LE) |

The CRC is **CRC-16/ARC** (poly `0xA001` reflected, init 0). Computing it over data plus its CRC
gives 0. Long messages (many contacts) are split over several packets: reassemble by offset
until "remaining" is 0.

## Message

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | type (`0x0210` for touchpad data) |
| 2 | 1 | 0 |
| 3 | 1 | counter |
| 4 | 2 | response length |
| 6 | 2 | data length = message length − 10 |
| 8 | n | data |
| 8+n | 2 | CRC-16 of the message |

A touchpad message is valid when `data length + 2 == 48 + 30 × number_of_fingers`.

### Touchpad data

| Offset | Field |
|---|---|
| 1 | clicked (physical click, decided by the trackpad's force sensors) |
| 30 | number of fingers |
| 31 | clicked (second copy) |
| 48 + 30·i | finger *i*, 30 bytes |

Finger (little endian, 16-bit): `origin, abs_x, abs_y, rel_x, rel_y, tool_major, tool_minor,
orientation, touch_major, touch_minor, unused[2], pressure, multi, crc16`.

- Coordinate range: **x −6243…6749, y −170…7685**; y grows towards the hinge (invert it for
  screen coordinates). Physical size about 134 × 81 mm.
- A finger is present when **`touch_major > 0`**. Do not rely on pressure: with 3–4 fingers, light
  fingers report pressure 0.
- `touch_major` or `touch_minor` ≥ ~2500 is a palm (no confidence).

## Quirks

- During each haptic pulse (press and release) the data is unreliable for up to ~100 ms. The
  click bit can flicker, phantom fingers appear (often in a row at the same y), and real fingers can jump a
  few mm for a frame or two. 99 % of phantom fingers start within 100 ms of a click-bit change. This
  gets much worse with a weak 12 V supply.
- Frames arrive at ~125 Hz while touched. When untouched, read requests return nothing (`0x80`).
