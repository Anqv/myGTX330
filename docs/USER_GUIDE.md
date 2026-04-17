# myGTX330 — User Guide

A hardware Garmin GTX330 transponder emulator for X-Plane 12, driven by an Arduino Mega 2560 and a 256×64 OLED display.

---

## Power-On and Startup

When the Arduino powers up, the initialization page is displayed for 10 seconds:

```
              GARMIN
           myGTX330
        ──────────────────────────────
           SW VER  1.1
    FOR X-PLANE 12 / AIR MANAGER 5
```

After the splash, the main display appears. The centre zone shows a blinking `SYNC` badge until Air Manager responds. Once connected, `RDY` appears briefly, then normal operation begins.

> The unit works fully without X-Plane or Air Manager connected — it retains the last known state and re-syncs automatically when the connection is restored.

---

## Display Layout

```
┌──────────┬───────────────────────────────┬────────────────────┐
│          │  VFR          CODE            │  UTC               │
│  MODE    │                               │  12:34:56          │
│  SBY     │     7  0  0  0               │                    │
│          │                               │  SYNC              │
└──────────┴───────────────────────────────┴────────────────────┘
  Left zone        Centre zone                  Right zone
  (mode + IDENT)   (squawk — always visible)    (FUNC cycles)
```

### Left Zone
Shows the current transponder mode: **OFF / SBY / GND / ON / ALT / TST**

When IDENT is active:
- `IDNT` label flashes for 18 seconds with a countdown (e.g. `17s`)

When the transponder is being interrogated by ATC:
- A filled circle with an inverted `R` (reply disc) lights up

### Centre Zone

The four squawk digits are always visible in large characters.

**Special badges** (top-left corner, highest priority first):

| Badge | Condition | Meaning |
|-------|-----------|---------|
| `EMER` (flashing) | Squawk 7700 | Emergency |
| `RDOF` (flashing) | Squawk 7600 | Radio failure |
| `HJCK` (flashing) | Squawk 7500 | Hijacking |
| `VFR` (outlined) | Squawk 7000 | VFR flight (EU/ICAO) |
| `INV` | Digit 8 or 9 pressed | Invalid squawk digit (clears after 1 s) |
| `RDY` | On Air Manager connect | Sync complete (clears after 1.5 s) |
| `SYNC` (blinking) | Waiting for Air Manager | Startup handshake in progress |

Emergency codes also flash a double border around the whole centre zone.

During squawk entry the active digit shows an inverse box and a blinking underline cursor. `INV` appears briefly if you press 8 or 9 (invalid for squawk).

The top-right corner shows which right-zone view is currently active (UTC / FLT / CTU / CDT).

### Right Zone

Cycles through four views with the **FUNC** button.

---

## Buttons

### Mode Buttons

Four dedicated buttons set the transponder mode directly:

| Button | Mode | Effect |
|--------|------|--------|
| **OFF** | OFF | Display blanks, transponder silent |
| **STBY** | SBY | Powered but not transmitting |
| **ON** | ON | Mode A — transmits squawk code only |
| **ALT** | ALT | Mode C — transmits squawk code + altitude |

> **Note:** GND and TST modes are not available as physical buttons. They can be set via the debug serial interface (`BTN:MODE_GND`, `BTN:MODE_TST`).

When the transponder is **OFF**, the display goes completely dark.

### IDENT Button

Sends an IDENT pulse to X-Plane and lights the `IDNT` annunciator in the left zone for **18 seconds**. Use when ATC asks you to "squawk ident".

### VFR Button

Immediately sets squawk to **7000** (EU/ICAO VFR) and sends it to X-Plane. Cancels any active squawk edit.

### Number Pad (0–9)

Used for squawk entry.

1. Press any digit **0–7** to begin entering a new squawk code. The first digit goes into the thousands position and a cursor appears.
2. Continue pressing digits — the cursor advances automatically.
3. After the fourth digit the code is committed and sent to X-Plane.
4. Digits **8 and 9** are rejected during squawk entry (`INV` shown briefly). They are valid only during countdown timer setup.

### CRSR Button

Context-sensitive:

| Situation | Action |
|-----------|--------|
| Squawk edit active | Advance cursor to next digit; commits on the 4th |
| Viewing **FLT** | Toggle flight timer between AUTO and MANUAL mode |
| Viewing **CDT** (not running) | Enter countdown set mode (type HHMMSS) |
| CDT set mode active | Commit the entered time early |
| Any other view | No action |

### START/STOP Button

Context-sensitive based on the current right-zone view:

| View | Action |
|------|--------|
| **FLT** (manual mode) | Start or stop the flight timer |
| **FLT** (auto mode) | No effect — timer is controlled by transponder mode |
| **CTU** | Start or stop the count-up stopwatch |
| **CDT** | Start countdown (if a time is set) or pause it |

### CLR Button

Context-sensitive:

| Situation | Action |
|-----------|--------|
| Squawk edit active | Cancel edit, revert to last X-Plane value |
| CDT set mode active | Cancel time entry, restore previous setting |
| Viewing **FLT** | Reset flight timer to 0:00:00 |
| Viewing **CTU** | Reset count-up timer to 0:00:00 |
| Viewing **CDT** | Reset countdown to 0 and stop it |

### FUNC Button

Cycles the right zone through four views:

```
UTC → FLT → CTU → CDT → UTC → …
```

---

## Right Zone Views

### UTC — Universal Time

Shows Zulu (UTC) time synced from X-Plane. Status line shows **SYNC** once time has been received, or **NO SYNC** before first X-Plane contact.

### FLT — Flight Timer

Counts elapsed flight time.

**AUTO mode** (default): the timer starts automatically when mode is set to **ON** or **ALT**, and stops when mode changes to OFF, SBY, or GND. This matches real transponder practice — the flight timer runs when you are actively squawking.

**MANUAL mode**: timer is controlled entirely by the **START/STOP** button.

Toggle between AUTO and MANUAL by pressing **CRSR** while in the FLT view. A brief confirmation banner (`FLT AUTO OK` / `FLT MAN OK`) appears.

Status line shows: `RUN AUTO` / `STP AUTO` (auto mode) or `RUN` / `STP` (manual mode).

### CTU — Count-Up Stopwatch

A simple stopwatch. **START/STOP** to start and stop; **CLR** to reset.

### CDT — Countdown Timer

Counts down from a set time.

**To set the countdown time:**
1. Press **CRSR** while in the CDT view (timer must not be running)
2. The display shows `__:__:__` with a cursor
3. Type 6 digits for HHMMSS (e.g. `0`, `0`, `3`, `0`, `0`, `0` for 30 minutes)
4. After the 6th digit (or pressing **CRSR** early) the time is committed
5. Press **CLR** during entry to cancel

**To start:** Press **START/STOP** (requires a non-zero time to be set).

**To pause:** Press **START/STOP** while running — the remaining time is saved.

When the countdown reaches zero, `EXPRD!` flashes. Press **CLR** to reset.

---

## Squawk Entry Quick Reference

```
Example: enter squawk 2341

Press: 2 → cursor on digit 2
Press: 3 → cursor on digit 3
Press: 4 → cursor on digit 4
Press: 1 → auto-commits → sends 2341 to X-Plane
```

To abort at any point: press **CLR** — reverts to the previous squawk.

---

## X-Plane Integration

All transponder state is two-way:

- **X-Plane → display:** Squawk code, mode, altitude (for ALT mode), ground speed, UTC time, reply light, and IDENT pulses from ATC are all reflected on the display in real time.
- **Display → X-Plane:** Squawk changes, mode changes, and IDENT button presses are immediately written back to X-Plane via Air Manager.

If X-Plane is not running or Air Manager is not connected, the display still works fully — it just shows the last known state and will resync automatically when the connection is restored.
