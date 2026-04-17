# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**myGTX330** — A hardware Garmin GTX330 transponder emulator for X-Plane 12.

**Stack:**
- Arduino Mega 2560 + SSD1322 256×64 OLED (4-wire SW-SPI, U8g2 library)
- Air Manager 5.2 (Lua instrument script) ↔ X-Plane 12 Cessna 172
- SiMessagePort protocol (Sim Innovations library) over USB Serial (Serial0)
- Serial1 (pins 18/19, 115200 baud) for debug output and button emulation input

## File Structure

```
arduino/myGTX330/myGTX330.ino       — Arduino Mega sketch (single file)
air_manager/myGTX330/instrument.lua — Air Manager 5.2 Lua instrument script
```

The Lua script must be copied into Air Manager's instrument directory manually. The production install path is:
`C:/Users/Anders Qvarfordt/Air Manager/instruments/OPEN_DIRECTORY/0da9621c-964c-450a-1152-0942877fefc3/logic.lua`

## Building and Deploying

**Arduino sketch** — compile and upload via Arduino IDE:
- Board: Arduino Mega 2560
- Required libraries (install via Library Manager): `U8g2` by olikraus
- Required library (manual install from Sim Innovations GitHub): `SiMessagePort`
- **Critical:** Enable `U8G2_16BIT` in `U8g2lib.h` before compiling — required for full 256×64 framebuffer on Mega 2560
- **Never call `Serial.begin()`** — SiMessagePort owns Serial0 (USB); calling it corrupts communication

**Air Manager script** — copy `instrument.lua` to the AM instrument directory, then reload the instrument in Air Manager 5.2.

## Architecture

### Communication Protocol

All Arduino↔Air Manager data flows through SiMessagePort integer channels. Channel numbers **must match exactly** between `myGTX330.ino` and `instrument.lua`:

| CH | Direction | Content |
|----|-----------|---------|
| 1  | AM→ARD    | Squawk code (decimal BCD, e.g. 7000, 7700) |
| 2  | AM→ARD    | Mode: 0=OFF 1=SBY 2=GND 3=ON 4=ALT 5=TST |
| 3  | AM→ARD    | Pressure altitude feet (int32) |
| 4  | AM→ARD    | Reply/interrogation light (0/1) |
| 5  | AM→ARD    | IDENT active from X-Plane (0/1) |
| 6  | AM→ARD    | Ground speed knots (int32) |
| 7  | AM→ARD    | UTC seconds-of-day (int32) |
| 10 | ARD→AM    | New squawk code set by pilot |
| 11 | ARD→AM    | New mode set by pilot |
| 12 | ARD→AM    | IDENT button pressed (one-shot, value=1) |
| 20 | ARD→AM    | Ping — sent every 4 s until AM replies |
| 21 | AM→ARD    | AM ready — Arduino pushes squawk + mode to X-Plane |
| 99 | ARD→AM    | Debug string (when DEBUG_TO_AM = true) |

### Mode Translation

X-Plane and GTX330 use different mode numbering — translation happens in both files:

| X-Plane | GTX330 |
|---------|--------|
| 0 Off   | 0 OFF  |
| 1 SBY   | 1 SBY  |
| 2 On(A) | 3 ON   |
| 3 Alt(C)| 4 ALT  |
| 4 Test  | 5 TST  |
| —       | 2 GND  |

GTX330 GND (2) has no X-Plane equivalent — Lua writes SBY (1) to X-Plane when pilot selects GND.

### SiMessagePort API

The correct message callback signature (Sim Innovations library):

```cpp
static void onMessage(uint16_t message_id, struct SiMessagePortPayload* payload) {
    if (payload == NULL || payload->type != SI_MESSAGE_PORT_DATA_TYPE_INTEGER) return;
    int32_t v = payload->data_int[0];
    // ...
}
```

Key points: payload can be NULL; integer data is at `payload->data_int[0]` (array), not `payload->integer_value`.

### Arduino Sketch Structure

Key sections in order:
1. **Version / Debug settings** (`FW_VERSION`, `DEBUG_TO_AM false`, `DEBUG_TO_SERIAL1 true`) — top of file
2. **Channel constants** (`CH_SQUAWK` … `CH_DEBUG`) — must match Lua
3. **Enumerations** — `TxpMode`, `View`, `FltMode`
4. **Button pin defines** and `Button` struct / `btns[]` array
5. **State variables** — squawk, mode, alt, timers, view
6. **Startup sync state** — `syncDone`, `syncToast`, `lastSyncReqMs`
7. **`dbg()` / `dbgf()`** — debug helpers routing to Serial1 and/or CH_DEBUG
8. **`onMessage()`** — Air Manager → Arduino callback; handles `CH_AM_READY` by pushing squawk + mode to X-Plane and setting `syncDone`
9. **Action dispatchers** — `commitSquawk()`, `dispatchNumKey()`, `dispatchCRSR()`, `dispatchStartStop()`, `dispatchCLR()`
10. **`handleButtons()`** — reads physical button state, calls dispatchers
11. **`emulateBtnAction()`** — Serial1 debug input handler, calls same dispatchers
12. **Draw functions** — `drawLeftZone()`, `drawCentreZone()`, `drawRightUTC/FLT/CTU/CDT()`
13. **`setup()` / `loop()`**

### Splash Screen

On power-on `showSplash()` displays a GTX330-style initialization page for 10 seconds:
- Row 1: `GARMIN` (small, centred)
- Row 2: `myGTX330` (large, centred)
- Separator line
- Row 3: `SW VER <FW_VERSION>` (medium, centred)
- Row 4: `FOR X-PLANE 12 / AIR MANAGER 5` (small, centred)

To bump the firmware version, change `#define FW_VERSION` near the top of the sketch.

### Startup Sync

Arduino sends `CH_REQUEST_UPDATE` (ch 20, ping) every 4 s while `!syncDone`. Air Manager responds with `CH_AM_READY` (ch 21) — both immediately on instrument load and in reply to every ping, so either boot order works.

When Arduino receives `CH_AM_READY` it pushes its current `squawkCode` and `txpMode` to X-Plane via `CH_SQUAWK_SET` / `CH_MODE_SET`, sets `syncDone`, and shows a brief `RDY` badge. Altitude, ground speed and UTC arrive naturally from X-Plane subscription callbacks as values change.

### Display Layout (256×64)

Three fixed zones separated by vertical lines at x=60 and x=187:

- **Left zone (x 1–57):** Mode label, IDENT flash with countdown, reply disc
- **Centre zone (x 62–185):** Squawk digits (always), badge top-left, view name top-right, edit cursor
- **Right zone (x 189–255):** Cycled by FUNC — UTC / FLT / CTU / CDT

**Centre zone badge priority** (mutually exclusive, top-left corner):
1. EMER / RDOF / HJCK — flashing inverse, based on squawk code 7700 / 7600 / 7500
2. VFR — rounded box, squawk 7000
3. INV — invalid digit entry (1 s)
4. RDY — sync complete toast (1.5 s)
5. SYNC — blinking at 1 Hz while waiting for startup sync

### Button Wiring

All INPUT_PULLUP, active LOW, 50 ms debounce.

| Pins 2–11  | Number pad BTN_0–BTN_9 |
| Pin 12     | BTN_IDENT |
| Pin 13     | BTN_VFR |
| Pin 20     | BTN_FUNC |
| Pin 21     | BTN_CRSR |
| Pin 22     | BTN_START_STOP |
| Pin 23     | BTN_CLR |
| Pin 24     | BTN_MODE_OFF |
| Pin 25     | BTN_MODE_SBY |
| Pin 26     | BTN_MODE_ON |
| Pin 27     | BTN_MODE_ALT |
| Pins 47–51 | Display SPI (CS/DC/RES/SDA/SCK) — not buttons |

### Debug / Testing via Serial1

Connect a USB-TTL adapter to pins 18 (TX) and 19 (RX) at 115200 baud to:
- Read the status line printed once per second
- Send button emulation commands: `BTN:<name>`

```
BTN:0 … BTN:9           number pad
BTN:IDENT  BTN:VFR
BTN:FUNC   BTN:CRSR   BTN:START   BTN:CLR
BTN:MODE_OFF  BTN:MODE_SBY  BTN:MODE_ON  BTN:MODE_ALT
BTN:MODE_GND  BTN:MODE_TST   (emulation only — no physical button)
```

### Air Manager Lua Script

- Uses AM 5.2 API: `hw_message_port_add()`, `hw_message_port_send()`, `xpl_dataref_subscribe()`, `xpl_dataref_write()`, `xpl_command(cmd, "ONCE")`
- `hw_message_port_send` requires **4 arguments**: `hw_message_port_send(port_id, channel, "INT", value)`
- Hardware port name `"Arduino Mega 2560"` must match the name assigned in Air Manager's Hardware settings panel
- `last` table suppresses redundant sends (initialized to -1 so first subscription fire always sends)
- On load, sends `CH_AM_READY` immediately; also replies with `CH_AM_READY` to every `CH_REQUEST_UPDATE` ping from Arduino
- VFR squawk code is **7000** (EU/ICAO). Change `#define VFR_CODE 7000u` in the sketch for other regions (e.g. 1200 for US)
