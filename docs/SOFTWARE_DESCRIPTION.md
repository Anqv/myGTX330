# myGTX330 — Software Description

**Version:** 1.1  
**Author:** Anders Qvarfordt  
**Platform:** Arduino Mega 2560 + Air Manager 5.2 + X-Plane 12

---

## 1. Overview

myGTX330 is a hardware emulator of the Garmin GTX330 Mode S transponder for use with X-Plane 12. It runs on an Arduino Mega 2560 driving a 256×64 OLED display and a set of physical buttons. A Lua script running inside Air Manager 5.2 bridges the Arduino to X-Plane's transponder datarefs and commands in real time.

The system behaves as a functional second transponder panel: squawk codes and mode changes made on the hardware are immediately reflected in X-Plane, and changes made inside the simulator (e.g. ATC-assigned squawks) are reflected on the display.

---

## 2. System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        X-Plane 12                           │
│   transponder_code  transponder_mode  h_ind  groundspeed    │
│   zulu_time  transponder_id  transponder_ident               │
└──────────────────────────┬──────────────────────────────────┘
                           │  datarefs / commands
                           │  (xpl_dataref_subscribe / write)
                           │  (xpl_command)
┌──────────────────────────▼──────────────────────────────────┐
│              Air Manager 5.2  —  instrument.lua             │
│   Subscribes to X-Plane datarefs, translates modes,         │
│   forwards data to Arduino via SiMessagePort channels       │
└──────────────────────────┬──────────────────────────────────┘
                           │  USB Serial (Serial0)
                           │  SiMessagePort integer channels
┌──────────────────────────▼──────────────────────────────────┐
│              Arduino Mega 2560  —  myGTX330.ino             │
│   Drives OLED display, reads buttons, manages timers,       │
│   sends pilot actions back to Air Manager / X-Plane         │
└─────────────────────────────────────────────────────────────┘
```

### Components

| Component | Role |
|-----------|------|
| `arduino/myGTX330/myGTX330.ino` | Arduino sketch — display, buttons, state, timers |
| `air_manager/myGTX330/instrument.lua` | Air Manager Lua script — X-Plane ↔ Arduino bridge |

---

## 3. Communication Protocol

All data between the Arduino and Air Manager flows through **SiMessagePort integer channels** over USB Serial0. Channel numbers must match exactly in both files.

### Channel Map

| CH | Direction | Data type | Content |
|----|-----------|-----------|---------|
| 1  | AM → ARD  | int32 | Squawk code (decimal BCD, e.g. 7000, 7700) |
| 2  | AM → ARD  | int32 | Transponder mode (GTX330 numbering, see §4) |
| 3  | AM → ARD  | int32 | Pressure altitude in feet |
| 4  | AM → ARD  | int32 | Reply/interrogation light (0 = off, 1 = on) |
| 5  | AM → ARD  | int32 | IDENT active from X-Plane (0/1) |
| 6  | AM → ARD  | int32 | Ground speed in knots |
| 7  | AM → ARD  | int32 | UTC time as seconds-of-day |
| 10 | ARD → AM  | int32 | New squawk code entered by pilot |
| 11 | ARD → AM  | int32 | New transponder mode selected by pilot |
| 12 | ARD → AM  | int32 | IDENT button pressed (one-shot, value = 1) |
| 20 | ARD → AM  | int32 | Startup ping — sent every 4 s until AM replies |
| 21 | AM → ARD  | int32 | AM ready — triggers Arduino to push state to X-Plane |
| 99 | ARD → AM  | String | Debug string (only when `DEBUG_TO_AM = true`) |

### SiMessagePort API

The Arduino uses the Sim Innovations `SiMessagePort` C++ library. The correct callback signature is:

```cpp
static void onMessage(uint16_t message_id, struct SiMessagePortPayload* payload) {
    if (payload == NULL || payload->type != SI_MESSAGE_PORT_DATA_TYPE_INTEGER) return;
    int32_t v = payload->data_int[0];
    // ...
}
```

The Air Manager side uses `hw_message_port_add()` and `hw_message_port_send()` from the AM 5.2 API:

```lua
port_id = hw_message_port_add("Arduino Mega 2560", on_arduino_message)
hw_message_port_send(port_id, channel, "INT", value)
```

---

## 4. Mode Translation

X-Plane and the GTX330 use different mode numbering. Translation is performed in both files.

| GTX330 mode | GTX330 name | X-Plane mode | X-Plane name |
|:-----------:|-------------|:------------:|-------------|
| 0 | OFF | 0 | Off |
| 1 | SBY | 1 | Standby |
| 2 | GND | 1 | Standby *(no XP equivalent — written as SBY)* |
| 3 | ON  | 2 | On (Mode A) |
| 4 | ALT | 3 | Alt (Mode C) |
| 5 | TST | 4 | Test |

**X-Plane → Arduino** (in `instrument.lua`, CH2 subscription):  
`XP 0→GTX 0`, `XP 1→GTX 1`, `XP 2→GTX 3`, `XP 3→GTX 4`, `XP 4→GTX 5`

**Arduino → X-Plane** (in `instrument.lua`, CH11 handler):  
`GTX 2→XP 1`, `GTX 3→XP 2`, `GTX 4→XP 3`, `GTX 5→XP 4`

---

## 5. Startup Sequence

### 5.1 Splash Screen

On power-on, `showSplash()` displays a GTX330-style initialization page for 10 seconds. It shows the firmware version defined by `#define FW_VERSION` near the top of the sketch.

### 5.2 Startup Sync Handshake

After the splash, the Arduino enters a sync loop:

1. Every 4 seconds (`SYNC_RETRY_MS`), Arduino sends a ping on CH20 (`CH_REQUEST_UPDATE`).
2. Air Manager replies with CH21 (`CH_AM_READY`) — both on instrument load and in response to each ping, so either boot order works.
3. On receiving CH21, Arduino:
   - Pushes its current `squawkCode` and `txpMode` to X-Plane via CH10/CH11.
   - Sets `syncDone = true`.
   - Shows a `RDY` badge on the display for 1.5 seconds.

While waiting, the centre zone shows a blinking `SYNC` badge at 1 Hz. Altitude, ground speed, and UTC arrive from X-Plane dataref subscriptions as values change and do not require explicit sync.

---

## 6. Arduino Sketch (`myGTX330.ino`)

### 6.1 Key Defines and Settings

| Symbol | Default | Purpose |
|--------|---------|---------|
| `FW_VERSION` | `"1.1"` | Firmware version shown on splash |
| `DEBUG_TO_AM` | `false` | Send debug strings to AM log (CH99) |
| `DEBUG_TO_SERIAL1` | `true` | Print debug to Serial1 (pins 18/19) |
| `VFR_CODE` | `7000` | VFR squawk (EU/ICAO); change to 1200 for US |
| `SYNC_RETRY_MS` | `4000` | Ping interval during startup sync |
| `IDENT_DURATION_MS` | `18000` | IDENT annunciator hold time (18 s) |
| `DEBOUNCE_MS` | `50` | Button debounce period |

### 6.2 State Variables

| Variable | Type | Description |
|----------|------|-------------|
| `squawkCode` | uint16_t | Current squawk (decimal BCD) |
| `txpMode` | TxpMode | Current mode (OFF/SBY/GND/ON/ALT/TST) |
| `altFeet` | int32_t | Pressure altitude from X-Plane |
| `groundSpeed` | uint16_t | Ground speed in knots from X-Plane |
| `replyLight` | bool | Interrogation reply indicator |
| `identActive` | bool | IDENT pulse active |
| `editMode` | bool | Squawk digit entry in progress |
| `currentView` | View | Active right-zone view (UTC/FLT/CTU/CDT) |
| `syncDone` | bool | AM handshake complete |

### 6.3 Code Structure

Sections in order within the sketch:

1. Version / debug settings (`FW_VERSION`, `DEBUG_TO_AM`, `DEBUG_TO_SERIAL1`)
2. U8g2 display object (4-wire SW-SPI)
3. Channel constants — must match `instrument.lua`
4. Enumerations: `TxpMode`, `View`, `FltMode`
5. Button pin defines and `Button` struct / `btns[]` array
6. Transponder state variables
7. Squawk edit state
8. Right-zone view state (UTC clock, FLT, CTU, CDT)
9. SiMessagePort pointer
10. Startup sync state (`syncDone`, `lastSyncReqMs`, `syncToast`)
11. `dbg()` / `dbgf()` — debug helpers (Serial1 and/or CH99)
12. `onMessage()` — incoming AM callback; dispatches all CH1–CH21 messages
13. Action dispatchers: `commitSquawk()`, `dispatchNumKey()`, `dispatchCRSR()`, `dispatchStartStop()`, `dispatchCLR()`
14. `handleButtons()` — polls physical buttons, calls dispatchers
15. `emulateBtnAction()` — parses `BTN:<name>` from Serial1, calls same dispatchers
16. Draw helpers: `drawRightZoneHeader()`, `drawTimeInRightZone()`
17. Draw functions: `drawLeftZone()`, `drawCentreZone()`, `drawRightUTC/FLT/CTU/CDT()`
18. `showSplash()` — power-on initialization page
19. `debugOutput()` — one-per-second Serial1 status line
20. `setup()` / `loop()`

### 6.4 Button Handling

`handleButtons()` is called every loop iteration. For each of the 20 buttons it:
1. Reads the raw pin state (INPUT_PULLUP, active LOW).
2. Starts a 50 ms debounce timer on any change.
3. Sets `btns[i].pressed = true` for exactly one loop iteration when a debounced press is confirmed.

`dispatchXxx()` functions receive the button index and update state accordingly. The same dispatcher functions are called by `emulateBtnAction()` when a `BTN:` command arrives on Serial1, making the debug interface behaviourally identical to physical buttons.

### 6.5 Display Rendering

The display is refreshed every loop iteration via `u8g2.clearBuffer()` → draw calls → `u8g2.sendBuffer()`. The 256×64 framebuffer is divided into three fixed zones:

| Zone | X range | Content |
|------|---------|---------|
| Left | 1 – 57 | Mode label, IDENT flash + countdown, reply disc |
| Centre | 62 – 185 | Squawk digits, badge (top-left), view name (top-right), edit cursor |
| Right | 189 – 255 | Cycled by FUNC: UTC / FLT / CTU / CDT |

Vertical separator lines are drawn at x = 60 and x = 187.

**Centre zone badge priority** (only one shown at a time):

| Priority | Badge | Condition |
|----------|-------|-----------|
| 1 | `EMER` / `RDOF` / `HJCK` | Squawk 7700 / 7600 / 7500 — flashing inverse |
| 2 | `VFR` | Squawk 7000 — rounded box |
| 3 | `INV` | Invalid digit (8 or 9) pressed — 1 s timeout |
| 4 | `RDY` | Sync just completed — 1.5 s toast |
| 5 | `SYNC` | Waiting for AM handshake — 1 Hz blink |

### 6.6 Timers

All timers use `millis()` — no blocking `delay()` except in `showSplash()`.

| Timer | Variable | Behaviour |
|-------|----------|-----------|
| Flight timer (FLT) | `fltStartMs`, `fltElapsedMs` | AUTO: starts on ON/ALT, stops on OFF/SBY/GND. MANUAL: START/STOP button. |
| Count-up (CTU) | `ctuStartMs`, `ctuElapsedMs` | START/STOP button; CLR resets |
| Countdown (CDT) | `cdtStartMs`, `cdtSetSec` | User enters HHMMSS; flashes EXPRD! at zero |
| IDENT | `identStart` | 18 s from button press or X-Plane IDENT signal |

---

## 7. Air Manager Lua Script (`instrument.lua`)

### 7.1 Responsibilities

- Subscribe to seven X-Plane datarefs and forward changes to the Arduino.
- Receive pilot actions from the Arduino (squawk, mode, IDENT) and write them to X-Plane.
- Respond to startup ping (CH20) with a ready signal (CH21).
- Suppress redundant sends using a `last` cache table (initialized to -1).

### 7.2 X-Plane Datarefs Used

| Dataref | Type | Use |
|---------|------|-----|
| `sim/cockpit/radios/transponder_code` | INT | Squawk code |
| `sim/cockpit/radios/transponder_mode` | INT | Mode 0–4 |
| `sim/flightmodel/misc/h_ind` | FLOAT | Pressure altitude (feet) |
| `sim/cockpit2/radios/indicators/transponder_id` | INT | Reply light |
| `sim/flightmodel/position/groundspeed` | FLOAT | Ground speed (m/s → knots) |
| `sim/cockpit2/clock_timer/zulu_time_hours` | INT | UTC hours |
| `sim/cockpit2/clock_timer/zulu_time_minutes` | INT | UTC minutes |
| `sim/cockpit2/clock_timer/zulu_time_seconds` | INT | UTC seconds |

### 7.3 X-Plane Command Used

| Command | Trigger |
|---------|---------|
| `sim/transponder/transponder_ident` | CH12 received from Arduino (IDENT button press) |

---

## 8. Debug Interface (Serial1)

Connect a USB-TTL adapter to pins 18 (TX1) and 19 (RX1) at **115200 baud**.

**Output** — one status line per second:
```
SQWK:7000  MODE:SBY  ALT:0 ft  FLT:00:00:00  UTC:12:34:56  GS:0 kt  VIEW:UTC  FLT:AUTO
```

**Input** — emulate any button press:
```
BTN:0 … BTN:9        BTN:IDENT    BTN:VFR
BTN:FUNC             BTN:CRSR     BTN:START    BTN:CLR
BTN:MODE_OFF         BTN:MODE_SBY BTN:MODE_ON  BTN:MODE_ALT
BTN:MODE_GND         BTN:MODE_TST              (emulation only)
```

`BTN:MODE_GND` and `BTN:MODE_TST` have no physical buttons but work fully through the debug interface.

---

## 9. Build and Deploy

### Arduino Sketch

1. Install **U8g2** via Arduino IDE Library Manager.
2. Install **SiMessagePort** manually from the Sim Innovations GitHub.
3. Enable `U8G2_16BIT` in `U8g2lib.h` — required for the full 256×64 framebuffer on Mega 2560.
4. Select board **Arduino Mega or Mega 2560**, select COM port, and upload.
5. **Never call `Serial.begin()`** — SiMessagePort owns Serial0.

### Air Manager Script

1. Copy `air_manager/myGTX330/instrument.lua` to the AM instrument directory.  
   Default path: `C:/Users/<user>/Air Manager/instruments/OPEN_DIRECTORY/<uuid>/logic.lua`
2. In Air Manager Hardware settings, add the Arduino and name it exactly `Arduino Mega 2560`.
3. Reload the instrument in Air Manager.

---

## 10. Known Limitations

- **GND mode** has no X-Plane equivalent. When the pilot selects GND, X-Plane is set to Standby (SBY).
- The **VFR code** defaults to 7000 (EU/ICAO). For US operations change `#define VFR_CODE 7000u` to `1200u`.
- The flight timer runs on `millis()` and will drift slightly over long sessions; it is not synchronised to X-Plane's elapsed flight time.
