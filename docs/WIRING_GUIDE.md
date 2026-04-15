# myGTX330 — Wiring Setup Guide

---

## Parts Required

| Item | Notes |
|------|-------|
| Arduino Mega 2560 | Any clone works |
| SSD1322 OLED display, 256×64 | NHD (Newhaven Display) or compatible. Must be 4-wire SPI variant |
| Push buttons × 20 | Momentary, normally open |
| 10 kΩ resistors × 20 | Only if buttons do not have external pull-ups (not needed — internal pull-ups are used) |
| USB-A to USB-B cable | Arduino to PC |
| USB-TTL adapter (optional) | 3.3 V or 5 V, for debug serial on pins 18/19 |
| Jumper wires / PCB | For connections |

> The SSD1322 operates at **3.3 V logic**. The Arduino Mega is a 5 V device. Many SSD1322 breakout boards include a 3.3 V regulator and level shifters — check your specific board. If not, add a level shifter on the SPI lines (SDA and SCK), or use a voltage divider on those two lines.

---

## Display Wiring

The sketch uses **4-wire software SPI** (`U8G2_SSD1322_NHD_256X64_F_4W_SW_SPI`).

| SSD1322 pin | Signal | Arduino Mega pin |
|-------------|--------|-----------------|
| CS (chip select) | CS | **47** |
| DC (data/command) | DC | **48** |
| RESET | RES | **49** |
| SDA (MOSI / data in) | SDA | **50** |
| SCK (clock) | SCK | **51** |
| VCC | Power | 3.3 V |
| GND | Ground | GND |

> These are software SPI pins — not the hardware SPI pins (50–53). Using SW-SPI means any digital pins would work, but the above assignments are fixed in the sketch.

---

## Button Wiring

All buttons are wired identically: one side to the Arduino pin, other side to **GND**. Internal pull-ups are enabled in firmware — no external resistors required.

```
Arduino pin ──── button ──── GND
```

### Number Pad

| Button | Pin |
|--------|-----|
| 0 | 2 |
| 1 | 3 |
| 2 | 4 |
| 3 | 5 |
| 4 | 6 |
| 5 | 7 |
| 6 | 8 |
| 7 | 9 |
| 8 | 10 |
| 9 | 11 |

### Function Buttons

| Button | Pin |
|--------|-----|
| IDENT | 12 |
| VFR | 13 |
| FUNC | 20 |
| CRSR | 21 |
| START/STOP | 22 |
| CLR | 23 |

### Mode Buttons

| Button | Pin |
|--------|-----|
| MODE OFF | 24 |
| MODE STBY | 25 |
| MODE ON | 26 |
| MODE ALT | 27 |

---

## Reserved Pins — Do Not Use for Buttons

| Pins | Reason |
|------|--------|
| 0, 1 | Serial0 TX/RX — owned by SiMessagePort / Air Manager USB connection |
| 18, 19 | Serial1 TX/RX — debug output and button emulation |
| 47–51 | Display SPI |

---

## Complete Pin Map

```
Arduino Mega 2560
─────────────────────────────────────────────
 0  (RX0)  ── USB / Air Manager (do not use)
 1  (TX0)  ── USB / Air Manager (do not use)
 2         ── BTN 0
 3         ── BTN 1
 4         ── BTN 2
 5         ── BTN 3
 6         ── BTN 4
 7         ── BTN 5
 8         ── BTN 6
 9         ── BTN 7
10         ── BTN 8
11         ── BTN 9
12         ── BTN IDENT
13         ── BTN VFR
18  (TX1)  ── Debug serial TX  (optional USB-TTL)
19  (RX1)  ── Debug serial RX  (optional USB-TTL)
20         ── BTN FUNC
21         ── BTN CRSR
22         ── BTN START/STOP
23         ── BTN CLR
24         ── BTN MODE OFF
25         ── BTN MODE STBY
26         ── BTN MODE ON
27         ── BTN MODE ALT
47         ── Display CS
48         ── Display DC
49         ── Display RESET
50         ── Display SDA (MOSI)
51         ── Display SCK
─────────────────────────────────────────────
3.3 V      ── Display VCC
GND        ── Display GND, all button commons
```

---

## Debug Serial (Optional)

Connecting a USB-TTL adapter to Serial1 lets you monitor the system and emulate button presses without physical wiring.

| USB-TTL | Arduino Mega |
|---------|-------------|
| RX | Pin 18 (TX1) |
| TX | Pin 19 (RX1) |
| GND | GND |

**Do not connect VCC** — the Mega is powered via USB.

Open the serial monitor at **115200 baud**. A status line is printed once per second:

```
SQWK:7000  MODE:SBY  ALT:0 ft  FLT:00:00:00  UTC:12:34:56  GS:0 kt  VIEW:UTC  FLT:AUTO
```

To emulate a button press, type `BTN:<name>` and press Enter:

```
BTN:IDENT
BTN:VFR
BTN:MODE_ALT
BTN:1  BTN:2  BTN:0  BTN:0    (enters squawk 1200)
BTN:MODE_GND                  (GND — emulation only, no physical button)
BTN:MODE_TST                  (TST — emulation only, no physical button)
```

---

## Software Setup

### 1 — Arduino Libraries

Install via **Arduino IDE → Tools → Manage Libraries**:
- `U8g2` by olikraus

Install manually (download from GitHub, add as ZIP):
- `SiMessagePort` by Sim Innovations

### 2 — Enable U8G2_16BIT

The SSD1322 256×64 full framebuffer requires 16-bit addressing. Without this the display will not initialise correctly on Mega 2560.

Find `U8g2lib.h` in your Arduino libraries folder and uncomment or add:

```cpp
#define U8G2_16BIT
```

Alternatively, add `#define U8G2_16BIT` at the very top of `myGTX330.ino`, before any includes.

### 3 — Upload Sketch

1. Open `arduino/myGTX330/myGTX330.ino` in Arduino IDE
2. Select board: **Arduino Mega or Mega 2560**
3. Select the correct COM port
4. Upload

### 4 — Air Manager Setup

1. In Air Manager 5.2, open **Hardware** settings
2. Add the Arduino Mega 2560 and name it exactly: `Arduino Mega 2560`
   (This name must match `HW_PORT_NAME` in `instrument.lua`)
3. Create a new instrument and add `air_manager/myGTX330/instrument.lua` as the logic script
4. Load X-Plane 12 with a Cessna 172, then enable the instrument in Air Manager

---

## Power

The Arduino Mega is powered via the USB cable connected to the PC running Air Manager. No separate power supply is needed for the board itself.

The SSD1322 display takes 3.3 V from the Mega's onboard 3.3 V pin (max ~150 mA — sufficient for the display alone).

If you add backlighting, encoders, or other peripherals drawing significant current, use an external 5 V supply and share ground with the Mega.
