/*
 * myGTX330 — Garmin GTX330 Transponder Emulator  v1.0
 * =====================================================
 * Target  : Arduino Mega 2560
 * Display : SSD1322 OLED 256×64, U8g2 SW-SPI, full framebuffer
 * Comms   : Serial0 (USB)  — SiMessagePort ↔ Air Manager 5.2  [DO NOT Serial.begin()]
 *           Serial1 (18/19) — debug output + button emulation (115200 baud)
 *
 * ── Display wiring ──────────────────────────────────────────────────────────
 *   Pin 47 → CS    Pin 48 → DC    Pin 49 → RES
 *   Pin 50 → SDA   Pin 51 → SCK
 *
 * ── Button wiring (INPUT_PULLUP, active LOW) ─────────────────────────────────
 *   Pin  2 → BTN_0     Pin  3 → BTN_1     Pin  4 → BTN_2     Pin  5 → BTN_3
 *   Pin  6 → BTN_4     Pin  7 → BTN_5     Pin  8 → BTN_6     Pin  9 → BTN_7
 *   Pin 10 → BTN_8     Pin 11 → BTN_9     Pin 12 → BTN_IDENT  Pin 13 → BTN_VFR
 *   Pin 20 → BTN_FUNC  Pin 21 → BTN_CRSR  Pin 22 → BTN_START_STOP
 *   Pin 23 → BTN_CLR
 *   Pin 24 → BTN_MODE_OFF   Pin 25 → BTN_MODE_SBY
 *   Pin 26 → BTN_MODE_ON    Pin 27 → BTN_MODE_ALT
 *
 * ── Debug serial (Serial1, 115200 baud) ─────────────────────────────────────
 *   Output: one status line/second
 *   Input:  BTN:<name>  — emulate button press
 *     BTN:0 … BTN:9         number pad
 *     BTN:IDENT  BTN:VFR    transponder buttons
 *     BTN:FUNC   BTN:CRSR   BTN:START  BTN:CLR
 *     BTN:MODE_OFF  BTN:MODE_SBY  BTN:MODE_GND
 *     BTN:MODE_ON   BTN:MODE_ALT  BTN:MODE_TST
 *   (BTN:MODE_GND and BTN:MODE_TST available via emulation only — no physical button)
 *
 * ── Channel map (must match instrument.lua) ──────────────────────────────────
 *   CH  1  AM→ARD  Squawk code (decimal BCD, e.g. 7700)
 *   CH  2  AM→ARD  Mode  0=OFF 1=SBY 2=GND 3=ON 4=ALT 5=TST
 *   CH  3  AM→ARD  Pressure altitude feet (int32)
 *   CH  4  AM→ARD  Reply/interrogation light (0/1)
 *   CH  5  AM→ARD  IDENT active from X-Plane (0/1)
 *   CH  6  AM→ARD  Ground speed knots (int32)
 *   CH  7  AM→ARD  UTC seconds-of-day (int32)
 *   CH 10  ARD→AM  New squawk code set by pilot
 *   CH 11  ARD→AM  New mode set by pilot
 *   CH 12  ARD→AM  IDENT button pressed (one-shot)
 *   CH 20  ARD→AM  Ping — detect when AM is ready (sent every 2 s until reply)
 *   CH 21  AM→ARD  AM ready — Arduino pushes squawk + mode to X-Plane
 *   CH 99  ARD→AM  Debug string (when DEBUG_TO_AM = true)
 *
 * ── Library requirements ─────────────────────────────────────────────────────
 *   • U8g2 by olikraus (Arduino Library Manager)
 *     NOTE: enable U8G2_16BIT in U8g2lib.h for 256×64 full framebuffer on Mega
 *   • SiMessagePort by Sim Innovations (install manually from GitHub)
 */

#include <U8g2lib.h>
#include <si_message_port.hpp>
#include <stdarg.h>

// =============================================================================
//  DEBUG SETTINGS  — change these to enable / disable debug output
// =============================================================================
//  DEBUG_TO_AM      true  → debug strings sent to Air Manager (CH_DEBUG = 99)
//                           visible in the Air Manager log window
//  DEBUG_TO_SERIAL1 true  → debug strings printed on Serial1 (pins 18/19, 115200 baud)
//  Set both to false to disable all debug output (saves RAM and CPU).
// =============================================================================

static const bool DEBUG_TO_AM      = false;
static const bool DEBUG_TO_SERIAL1 = true;

// =============================================================================
//  DISPLAY — SW-SPI, full framebuffer (requires U8G2_16BIT)
// =============================================================================

U8G2_SSD1322_NHD_256X64_F_4W_SW_SPI u8g2(
    U8G2_R0, /*clk*/51, /*data*/50, /*cs*/47, /*dc*/48, /*rst*/49);

// =============================================================================
//  CHANNEL CONSTANTS  (must match instrument.lua)
// =============================================================================

#define CH_SQUAWK        1
#define CH_MODE          2
#define CH_ALTITUDE      3
#define CH_REPLY_LIGHT   4
#define CH_IDENT_XP      5
#define CH_GROUND_SPEED  6
#define CH_SIM_TIME      7
#define CH_SQUAWK_SET     10
#define CH_MODE_SET       11
#define CH_IDENT_BTN      12
#define CH_REQUEST_UPDATE 20  // ARD→AM: ping — are you there?
#define CH_AM_READY       21  // AM→ARD: yes, ready — Arduino now pushes state to X-Plane
#define CH_DEBUG          99  // ARD→AM debug string (SendMessage with String)

// =============================================================================
//  ENUMERATIONS
// =============================================================================

enum TxpMode : uint8_t {
    MODE_OFF = 0, MODE_SBY, MODE_GND, MODE_ON, MODE_ALT, MODE_TST
};
#define MODE_COUNT 6
static const char* const MODE_NAMES[] = { "OFF", "SBY", "GND", " ON", "ALT", "TST" };

enum View : uint8_t {
    VIEW_UTC = 0, VIEW_FLT, VIEW_CTU, VIEW_CDT
};
#define VIEW_COUNT 4
static const char* const VIEW_NAMES[] = { "UTC", "FLT", "CTU", "CDT" };

enum FltMode : uint8_t { FLT_AUTO = 0, FLT_MANUAL };

// =============================================================================
//  BUTTON PINS
// =============================================================================

#define BTN_0           2
#define BTN_1           3
#define BTN_2           4
#define BTN_3           5
#define BTN_4           6
#define BTN_5           7
#define BTN_6           8
#define BTN_7           9
#define BTN_8          10
#define BTN_9          11
#define BTN_IDENT      12
#define BTN_VFR        13
#define BTN_FUNC       20
#define BTN_CRSR       21
#define BTN_START_STOP 22
#define BTN_CLR        23
#define BTN_MODE_OFF   24   // OFF  — dedicated mode button
#define BTN_MODE_SBY   25   // STBY — dedicated mode button
#define BTN_MODE_ON    26   // ON   — dedicated mode button
#define BTN_MODE_ALT   27   // ALT  — dedicated mode button

#define BTN_COUNT      20

static const uint8_t BTN_PINS[BTN_COUNT] = {
    BTN_0, BTN_1, BTN_2, BTN_3, BTN_4, BTN_5, BTN_6, BTN_7, BTN_8, BTN_9,
    BTN_IDENT, BTN_VFR, BTN_FUNC, BTN_CRSR, BTN_START_STOP, BTN_CLR,
    BTN_MODE_OFF, BTN_MODE_SBY, BTN_MODE_ON, BTN_MODE_ALT
};
enum BtnIdx : uint8_t {
    IDX_0=0,IDX_1,IDX_2,IDX_3,IDX_4,IDX_5,IDX_6,IDX_7,IDX_8,IDX_9,
    IDX_IDENT,IDX_VFR,IDX_FUNC,IDX_CRSR,IDX_START_STOP,IDX_CLR,
    IDX_MODE_OFF,IDX_MODE_SBY,IDX_MODE_ON,IDX_MODE_ALT
};

#define DEBOUNCE_MS 50u

struct Button {
    bool     lastRaw;
    bool     state;
    uint32_t debounceAt;
    bool     pressed;
};
static Button btns[BTN_COUNT];

// =============================================================================
//  TRANSPONDER STATE  (mirrored from X-Plane via Air Manager)
// =============================================================================

static uint16_t squawkCode  = 7000;
static TxpMode  txpMode     = MODE_SBY;
static int32_t  altFeet     = 0;
static int32_t  prevAlt     = 0;
static bool     replyLight  = false;
static uint16_t groundSpeed = 0;

static bool     identActive = false;
static uint32_t identStart  = 0;
#define IDENT_DURATION_MS 18000u

#define VFR_CODE 7000u

// =============================================================================
//  SQUAWK EDIT STATE
// =============================================================================

static bool     editMode      = false;
static uint8_t  editDigit     = 0;     // next digit position to fill (0=thousands)
static uint16_t editPrevCode  = 7000;  // saved to revert on CLR
static bool     editInvalid   = false; // true for 1 s after digit 8/9 rejected
static uint32_t editInvalidMs = 0;

// =============================================================================
//  RIGHT-ZONE VIEW STATE
// =============================================================================

static View currentView = VIEW_UTC;

// ─── UTC clock (seeded from X-Plane) ─────────────────────────────────────────
static bool     clockSeeded    = false;
static uint32_t clockSeededMs  = 0;
static uint32_t clockOffsetSec = 0;

// ─── Flight timer ─────────────────────────────────────────────────────────────
static FltMode  fltMode      = FLT_AUTO;
static bool     fltRunning   = false;
static uint32_t fltStartMs   = 0;
static uint32_t fltElapsedMs = 0;

static bool     fltModeToast   = false;  // brief "AUTO"/"MAN" confirmation banner
static uint32_t fltModeToastMs = 0;
#define FLT_TOAST_MS 1500u

// ─── Count-up timer ───────────────────────────────────────────────────────────
static bool     ctuRunning   = false;
static uint32_t ctuStartMs   = 0;
static uint32_t ctuElapsedMs = 0;

// ─── Countdown timer ──────────────────────────────────────────────────────────
static uint32_t cdtSetSec     = 0;     // target duration in seconds
static bool     cdtRunning    = false;
static uint32_t cdtStartMs    = 0;
static bool     cdtExpired    = false;
static bool     cdtSetMode    = false; // true while user is entering target time
static uint8_t  cdtSetBuf[6]  = {};    // digit buffer: H H M M S S
static uint8_t  cdtSetPos     = 0;     // next digit position (0–5)
static uint32_t cdtPrevSetSec = 0;     // saved for CLR-cancel

// =============================================================================
//  SiMessagePort
// =============================================================================

SiMessagePort* messagePort;

// =============================================================================
//  STARTUP SYNC
//  CH_REQUEST_UPDATE (ping) is sent every SYNC_RETRY_MS until AM replies with
//  CH_AM_READY. On receipt Arduino pushes squawk + mode to X-Plane and sets
//  syncDone, showing a brief RDY badge on the display.
// =============================================================================

#define SYNC_RETRY_MS 4000u

static bool     syncDone      = false;
static uint32_t lastSyncReqMs = 0;
static bool     syncToast     = false;
static uint32_t syncToastMs   = 0;
#define SYNC_TOAST_MS 1500u

// =============================================================================
//  DEBUG HELPERS
//  dbg(msg)         — send a literal string
//  dbgf(fmt, ...)   — send a printf-formatted string (max 79 chars)
// =============================================================================

static char _dbgBuf[80];

static void dbg(const char* msg) {
    if (DEBUG_TO_SERIAL1) Serial1.println(msg);
    if (DEBUG_TO_AM && messagePort) messagePort->SendMessage(CH_DEBUG, String(msg));
}

static void dbgf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(_dbgBuf, sizeof(_dbgBuf), fmt, ap);
    va_end(ap);
    dbg(_dbgBuf);
}

// =============================================================================
//  SQUAWK DIGIT HELPERS
// =============================================================================

static const uint16_t DIG_DIV[4] = { 1000, 100, 10, 1 };

static uint8_t getDigit(uint16_t code, uint8_t pos) {
    return (code / DIG_DIV[pos]) % 10;
}
static uint16_t setDigit(uint16_t code, uint8_t pos, uint8_t val) {
    return code - (uint16_t)getDigit(code, pos)*DIG_DIV[pos]
                + (uint16_t)val*DIG_DIV[pos];
}
static bool isOctalSquawk(uint16_t code) {
    for (uint8_t i = 0; i < 4; i++)
        if (getDigit(code, i) > 7) return false;
    return true;
}

// =============================================================================
//  TIMER ELAPSED HELPERS
// =============================================================================

static uint32_t fltSec() {
    uint32_t ms = fltRunning ? (millis()-fltStartMs)+fltElapsedMs : fltElapsedMs;
    return ms / 1000;
}
static uint32_t ctuSec() {
    uint32_t ms = ctuRunning ? (millis()-ctuStartMs)+ctuElapsedMs : ctuElapsedMs;
    return ms / 1000;
}
static uint32_t cdtRemainSec() {
    if (!cdtRunning) return cdtSetSec;
    uint32_t el = (millis()-cdtStartMs) / 1000;
    return (el >= cdtSetSec) ? 0 : cdtSetSec - el;
}

// =============================================================================
//  UTC CLOCK
// =============================================================================

static void clockHMS(uint8_t& h, uint8_t& m, uint8_t& s) {
    uint32_t elapsed = clockSeeded ? (millis()-clockSeededMs)/1000 : 0;
    uint32_t tod = (clockOffsetSec + elapsed) % 86400u;
    h = (uint8_t)(tod / 3600);
    m = (uint8_t)((tod % 3600) / 60);
    s = (uint8_t)(tod % 60);
}

// =============================================================================
//  AUTO FLIGHT TIMER
// =============================================================================

static void checkAutoFlt() {
    if (fltMode != FLT_AUTO) return;
    bool shouldRun = (txpMode == MODE_ON || txpMode == MODE_ALT);
    if (shouldRun && !fltRunning) {
        fltStartMs = millis() - fltElapsedMs;
        fltRunning = true;
        dbg("FLT:START auto");
    } else if (!shouldRun && fltRunning) {
        fltElapsedMs = millis() - fltStartMs;
        fltRunning   = false;
        dbg("FLT:STOP auto");
    }
}

// =============================================================================
//  MESSAGE CALLBACK  (Air Manager → Arduino, little-endian int32)
// =============================================================================

static void onMessage(uint16_t message_id, struct SiMessagePortPayload* payload) {
    if (payload == NULL || payload->type != SI_MESSAGE_PORT_DATA_TYPE_INTEGER) return;
    int32_t v = payload->data_int[0];
    switch (message_id) {
        case CH_SQUAWK:
            if (!editMode) {
                squawkCode   = (uint16_t)v;
                editPrevCode = squawkCode;
                dbgf("AM>SQWK:%04u", squawkCode);
            } else {
                dbgf("AM>SQWK:%04u (ignored, edit active)", (uint16_t)v);
            }
            break;
        case CH_MODE:
            if (v >= 0 && v < MODE_COUNT) {
                txpMode = (TxpMode)v;
                dbgf("AM>MODE:%s", MODE_NAMES[v]);
                checkAutoFlt();
            } else {
                dbgf("AM>MODE:%ld (out of range, ignored)", v);
            }
            break;
        case CH_ALTITUDE:
            prevAlt = altFeet;
            altFeet = v;
            dbgf("AM>ALT:%ld ft  FL%03d", altFeet, (int)(altFeet / 100));
            break;
        case CH_REPLY_LIGHT:
            replyLight = (v != 0);
            dbgf("AM>REPLY:%s", replyLight ? "ON" : "off");
            break;
        case CH_IDENT_XP:
            if (v) {
                identActive = true;
                identStart  = millis();
                dbg("AM>IDENT_XP: active");
            }
            break;
        case CH_GROUND_SPEED:
            groundSpeed = (uint16_t)v;
            dbgf("AM>GS:%u kt", groundSpeed);
            break;
        case CH_SIM_TIME: {
            clockOffsetSec = (uint32_t)v;
            clockSeededMs  = millis();
            clockSeeded    = true;
            uint8_t h = (uint8_t)(v / 3600);
            uint8_t m = (uint8_t)((v % 3600) / 60);
            uint8_t s = (uint8_t)(v % 60);
            // Log only on new minute to avoid spam
            if (s == 0) dbgf("AM>UTC:%02u:%02u:%02u", h, m, s);

            break;
        }
        case CH_AM_READY:
            Serial1.println("CH_AM_READY\n");
            if (!syncDone) {
                syncDone    = true;
                syncToast   = true;
                syncToastMs = millis();
                // Push Arduino's current state to X-Plane via Air Manager
                messagePort->SendMessage(CH_SQUAWK_SET, (int32_t)squawkCode);
                messagePort->SendMessage(CH_MODE_SET,   (int32_t)txpMode);
                dbgf("SYNC: AM ready — pushed SQWK:%04u MODE:%s to XP",
                     squawkCode, MODE_NAMES[(uint8_t)txpMode]);
            }
            break;
        default:
            dbgf("AM>UNKNOWN ch=%u val=%ld", message_id, v);
            break;
    }
}

// =============================================================================
//  BUTTON INIT + UPDATE
// =============================================================================

static void initButtons() {
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        pinMode(BTN_PINS[i], INPUT_PULLUP);
        btns[i]         = {};
        btns[i].lastRaw = btns[i].state = HIGH;
    }
}

static void updateButtons() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        Button& b   = btns[i];
        bool    raw = (bool)digitalRead(BTN_PINS[i]);
        b.pressed   = false;
        if (raw != b.lastRaw) b.debounceAt = now;
        b.lastRaw = raw;
        if ((now - b.debounceAt) < DEBOUNCE_MS) continue;
        if (raw != b.state) {
            b.state = raw;
            if (raw == LOW) b.pressed = true;
        }
    }
}

static inline bool pressed(uint8_t i) { return btns[i].pressed; }

// =============================================================================
//  ACTION DISPATCHERS  (shared by physical buttons and debug serial emulation)
// =============================================================================

static void commitSquawk() {
    if (isOctalSquawk(squawkCode)) {
        messagePort->SendMessage(CH_SQUAWK_SET, (int32_t)squawkCode);
        editPrevCode = squawkCode;
        dbgf("SQWK:COMMIT %04u → XP", squawkCode);
    } else {
        squawkCode = editPrevCode;
        dbgf("SQWK:REVERT (non-octal) → %04u", squawkCode);
    }
    editMode  = false;
    editDigit = 0;
}

static void dispatchNumKey(uint8_t digit) {
    // CDT set mode takes priority over squawk entry
    if (cdtSetMode) {
        if (cdtSetPos < 6) {
            cdtSetBuf[cdtSetPos++] = digit;
            dbgf("KEY:%u → CDT pos%u", digit, cdtSetPos - 1);
        }
        if (cdtSetPos >= 6) {
            uint32_t hh = (uint32_t)cdtSetBuf[0]*10 + cdtSetBuf[1];
            uint32_t mm = (uint32_t)cdtSetBuf[2]*10 + cdtSetBuf[3];
            uint32_t ss = (uint32_t)cdtSetBuf[4]*10 + cdtSetBuf[5];
            if (mm > 59) mm = 59;
            if (ss > 59) ss = 59;
            cdtSetSec  = hh*3600 + mm*60 + ss;
            cdtSetMode = false;
            dbgf("CDT:SET %02lu:%02lu:%02lu (%lu s)", hh, mm, ss, cdtSetSec);
        }
        return;
    }

    // Squawk: only digits 0–7 (octal)
    if (digit > 7) {
        editInvalid   = true;
        editInvalidMs = millis();
        dbgf("KEY:%u INVALID for squawk (must be 0-7)", digit);
        return;
    }

    if (!editMode) {
        editMode     = true;
        editDigit    = 0;
        editPrevCode = squawkCode;
        dbgf("SQWK:EDIT start (prev=%04u)", squawkCode);
    }

    squawkCode = setDigit(squawkCode, editDigit, digit);
    dbgf("KEY:%u → SQWK:%04u dig=%u", digit, squawkCode, editDigit);
    editDigit++;

    if (editDigit > 3) {
        commitSquawk();
    }
}

static void dispatchCRSR() {
    if (editMode) {
        dbgf("KEY:CRSR → SQWK dig%u advance", editDigit);
        editDigit++;
        if (editDigit > 3) commitSquawk();
        return;
    }
    if (cdtSetMode) {
        while (cdtSetPos < 6) cdtSetBuf[cdtSetPos++] = 0;
        uint32_t hh = (uint32_t)cdtSetBuf[0]*10 + cdtSetBuf[1];
        uint32_t mm = (uint32_t)cdtSetBuf[2]*10 + cdtSetBuf[3];
        uint32_t ss = (uint32_t)cdtSetBuf[4]*10 + cdtSetBuf[5];
        if (mm > 59) mm = 59;
        if (ss > 59) ss = 59;
        cdtSetSec  = hh*3600 + mm*60 + ss;
        cdtSetMode = false;
        dbgf("KEY:CRSR → CDT:SET %02lu:%02lu:%02lu (%lu s)", hh, mm, ss, cdtSetSec);
        return;
    }

    switch (currentView) {
        case VIEW_FLT:
            fltMode = (fltMode == FLT_AUTO) ? FLT_MANUAL : FLT_AUTO;
            if (fltMode == FLT_AUTO) {
                checkAutoFlt();
            } else {
                if (fltRunning) {
                    fltElapsedMs = millis() - fltStartMs;
                    fltRunning   = false;
                }
            }
            fltModeToast   = true;
            fltModeToastMs = millis();
            dbgf("KEY:CRSR → FLT:MODE %s", fltMode == FLT_AUTO ? "AUTO" : "MAN");
            break;

        case VIEW_CDT:
            if (!cdtRunning) {
                cdtPrevSetSec = cdtSetSec;
                cdtSetMode    = true;
                cdtSetPos     = 0;
                memset(cdtSetBuf, 0, sizeof(cdtSetBuf));
                dbg("KEY:CRSR → CDT:SET enter HHMMSS");
            }
            break;

        default:
            dbgf("KEY:CRSR (no action in view %s)", VIEW_NAMES[(uint8_t)currentView]);
            break;
    }
}

static void dispatchStartStop() {
    switch (currentView) {
        case VIEW_FLT:
            if (fltMode == FLT_MANUAL) {
                if (!fltRunning) {
                    fltStartMs = millis() - fltElapsedMs;
                    fltRunning = true;
                    dbg("KEY:START → FLT:START manual");
                } else {
                    fltElapsedMs = millis() - fltStartMs;
                    fltRunning   = false;
                    dbgf("KEY:STOP → FLT:STOP manual (%lu s)", fltSec());
                }
            } else {
                dbg("KEY:START/STOP ignored (FLT in AUTO mode)");
            }
            break;

        case VIEW_CTU:
            if (!ctuRunning) {
                ctuStartMs = millis() - ctuElapsedMs;
                ctuRunning = true;
                dbg("KEY:START → CTU:START");
            } else {
                ctuElapsedMs = millis() - ctuStartMs;
                ctuRunning   = false;
                dbgf("KEY:STOP → CTU:STOP (%lu s)", ctuSec());
            }
            break;

        case VIEW_CDT:
            if (!cdtSetMode) {
                if (!cdtRunning && cdtSetSec > 0) {
                    cdtStartMs = millis();
                    cdtRunning = true;
                    cdtExpired = false;
                    dbgf("KEY:START → CDT:START (%lu s)", cdtSetSec);
                } else if (cdtRunning) {
                    cdtSetSec  = cdtRemainSec();
                    cdtRunning = false;
                    dbgf("KEY:STOP → CDT:PAUSE (%lu s remain)", cdtSetSec);
                }
            }
            break;

        default:
            dbgf("KEY:START/STOP (no action in view %s)", VIEW_NAMES[(uint8_t)currentView]);
            break;
    }
}

static void dispatchCLR() {
    if (editMode) {
        squawkCode = editPrevCode;
        editMode   = false;
        editDigit  = 0;
        dbgf("KEY:CLR → SQWK:CANCEL (reverted to %04u)", squawkCode);
        return;
    }
    if (cdtSetMode) {
        cdtSetSec  = cdtPrevSetSec;
        cdtSetMode = false;
        dbg("KEY:CLR → CDT:SET cancelled");
        return;
    }

    switch (currentView) {
        case VIEW_FLT:
            fltElapsedMs = 0;
            fltRunning   = false;
            dbg("KEY:CLR → FLT:RESET");
            break;
        case VIEW_CTU:
            ctuElapsedMs = 0;
            ctuRunning   = false;
            dbg("KEY:CLR → CTU:RESET");
            break;
        case VIEW_CDT:
            cdtSetSec  = 0;
            cdtRunning = false;
            cdtExpired = false;
            dbg("KEY:CLR → CDT:RESET");
            break;
        default:
            dbgf("KEY:CLR (no action in view %s)", VIEW_NAMES[(uint8_t)currentView]);
            break;
    }
}

// =============================================================================
//  PHYSICAL BUTTON HANDLER
// =============================================================================

static void handleButtons() {
    // ── Global buttons (always active regardless of view/edit) ───────────────

    if (pressed(IDX_IDENT)) {
        identActive = true;
        identStart  = millis();
        messagePort->SendMessage(CH_IDENT_BTN, (int32_t)1);
        dbg("KEY:IDENT → XP IDENT");
        return;
    }

    if (pressed(IDX_VFR)) {
        squawkCode   = VFR_CODE;
        editMode     = false;
        editDigit    = 0;
        editPrevCode = VFR_CODE;
        messagePort->SendMessage(CH_SQUAWK_SET, (int32_t)VFR_CODE);
        dbgf("KEY:VFR → SQWK:%04u", VFR_CODE);
        return;
    }

    if (pressed(IDX_FUNC)) {
        currentView = (View)((currentView + 1) % VIEW_COUNT);
        cdtSetMode  = false;
        dbgf("KEY:FUNC → VIEW:%s", VIEW_NAMES[(uint8_t)currentView]);
        return;
    }

    if (pressed(IDX_MODE_OFF)) {
        txpMode = MODE_OFF; checkAutoFlt();
        messagePort->SendMessage(CH_MODE_SET, (int32_t)0);
        dbg("KEY:MODE_OFF");
        return;
    }
    if (pressed(IDX_MODE_SBY)) {
        txpMode = MODE_SBY; checkAutoFlt();
        messagePort->SendMessage(CH_MODE_SET, (int32_t)1);
        dbg("KEY:MODE_SBY");
        return;
    }
    if (pressed(IDX_MODE_ON)) {
        txpMode = MODE_ON; checkAutoFlt();
        messagePort->SendMessage(CH_MODE_SET, (int32_t)3);
        dbg("KEY:MODE_ON");
        return;
    }
    if (pressed(IDX_MODE_ALT)) {
        txpMode = MODE_ALT; checkAutoFlt();
        messagePort->SendMessage(CH_MODE_SET, (int32_t)4);
        dbg("KEY:MODE_ALT");
        return;
    }

    // ── Number pad ───────────────────────────────────────────────────────────
    for (uint8_t i = IDX_0; i <= IDX_9; i++) {
        if (pressed(i)) { dispatchNumKey(i); return; }
    }

    // ── Context-sensitive buttons ─────────────────────────────────────────────
    if (pressed(IDX_CRSR))       { dispatchCRSR();      return; }
    if (pressed(IDX_START_STOP)) { dispatchStartStop(); return; }
    if (pressed(IDX_CLR))        { dispatchCLR();       return; }
}

// =============================================================================
//  DEBUG SERIAL — button emulation from Serial1
// =============================================================================

static String dbgRxBuf;

static void emulateBtnAction(const String& name) {
    // Single digit key
    if (name.length() == 1 && name[0] >= '0' && name[0] <= '9') {
        dispatchNumKey((uint8_t)(name[0] - '0'));
        return;  // dispatchNumKey already calls dbg
    }

    if (name == "IDENT") {
        identActive = true; identStart = millis();
        messagePort->SendMessage(CH_IDENT_BTN, (int32_t)1);
        dbg("EMU:IDENT → XP IDENT");
    } else if (name == "VFR") {
        squawkCode = VFR_CODE; editMode = false; editDigit = 0;
        editPrevCode = VFR_CODE;
        messagePort->SendMessage(CH_SQUAWK_SET, (int32_t)VFR_CODE);
        dbgf("EMU:VFR → SQWK:%04u", VFR_CODE);
    } else if (name == "FUNC") {
        currentView = (View)((currentView + 1) % VIEW_COUNT);
        cdtSetMode  = false;
        dbgf("EMU:FUNC → VIEW:%s", VIEW_NAMES[(uint8_t)currentView]);
    } else if (name == "CRSR")  { dispatchCRSR();     }
    else if   (name == "START") { dispatchStartStop(); }
    else if   (name == "CLR")   { dispatchCLR();       }
    // Mode buttons — individual (no generic cycle command)
    else if (name == "MODE_OFF") { txpMode = MODE_OFF; checkAutoFlt(); messagePort->SendMessage(CH_MODE_SET, (int32_t)0); dbg("EMU:MODE_OFF"); }
    else if (name == "MODE_SBY") { txpMode = MODE_SBY; checkAutoFlt(); messagePort->SendMessage(CH_MODE_SET, (int32_t)1); dbg("EMU:MODE_SBY"); }
    else if (name == "MODE_GND") { txpMode = MODE_GND; checkAutoFlt(); messagePort->SendMessage(CH_MODE_SET, (int32_t)2); dbg("EMU:MODE_GND"); }
    else if (name == "MODE_ON")  { txpMode = MODE_ON;  checkAutoFlt(); messagePort->SendMessage(CH_MODE_SET, (int32_t)3); dbg("EMU:MODE_ON");  }
    else if (name == "MODE_ALT") { txpMode = MODE_ALT; checkAutoFlt(); messagePort->SendMessage(CH_MODE_SET, (int32_t)4); dbg("EMU:MODE_ALT"); }
    else if (name == "MODE_TST") { txpMode = MODE_TST; checkAutoFlt(); messagePort->SendMessage(CH_MODE_SET, (int32_t)5); dbg("EMU:MODE_TST"); }
    else {
        dbgf("EMU:UNKNOWN cmd '%s'", name.c_str());
    }
}

static void pollDebugSerial() {
    while (Serial1.available()) {
        char c = (char)Serial1.read();
        if (c == '\n' || c == '\r') {
            dbgRxBuf.trim();
            if (dbgRxBuf.length() > 0) {
                String upper = dbgRxBuf;
                upper.toUpperCase();
                if (upper.startsWith("BTN:"))
                    emulateBtnAction(upper.substring(4));
                dbgRxBuf = "";
            }
        } else if (dbgRxBuf.length() < 32) {
            dbgRxBuf += c;
        }
    }
}

// =============================================================================
//  EXPIRE CHECKS
// =============================================================================

static void checkIdentExpiry() {
    if (identActive && (millis()-identStart >= IDENT_DURATION_MS))
        identActive = false;
}

static void checkCdtExpiry() {
    if (cdtRunning && cdtRemainSec() == 0) {
        cdtRunning = false;
        cdtExpired = true;
        Serial1.println(F("DBG: CDT expired!"));
    }
}

// =============================================================================
//  DISPLAY LAYOUT CONSTANTS
//
//   x=0    x=58   x=60        x=186 x=187       x=255
//   ┌───────┬─────────────────────┬──────────────────┐  y=0
//   │ LEFT  │   CENTRE            │  RIGHT ZONE      │
//   │ MODE  │   squawk (always)   │  FUNC cycles     │
//   │  ALT  │   4 large digits    │  UTC/FLT/CTU/CDT │
//   │[IDNT] │   badge top-left    │                  │
//   │  [R]  │   view name top-right│                  │
//   └───────┴─────────────────────┴──────────────────┘  y=63
// =============================================================================

#define LZONE_X   1
#define LZONE_W  57
#define SEP_L_X  60
#define CZONE_X  62
#define CZONE_W 123
#define SEP_R_X 187
#define RZONE_X 189
#define RZONE_W  66

// =============================================================================
//  DRAW — LEFT ZONE  (mode annunciator + IDENT/reply)
// =============================================================================

static void drawLeftZone() {
    u8g2.drawRFrame(LZONE_X, 1, LZONE_W, 62, 3);

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(15, 12, "MODE");

    // Mode value, centred in the zone width
    const char* ml = MODE_NAMES[(uint8_t)txpMode];
    u8g2.setFont(u8g2_font_helvB14_tr);
    int16_t mw = u8g2.getStrWidth(ml);
    u8g2.drawStr(LZONE_X + (LZONE_W - mw) / 2, 40, ml);

    if (txpMode == MODE_OFF || txpMode == MODE_SBY) return;

    if (identActive) {
        uint32_t el    = millis() - identStart;
        uint16_t remSec = (el < IDENT_DURATION_MS)
                        ? (uint16_t)((IDENT_DURATION_MS - el + 999) / 1000) : 0;

        if ((millis() / 350) & 1) {
            u8g2.setFont(u8g2_font_7x13B_tr);
            u8g2.drawStr(5, 55, "IDNT");
        }
        char rbuf[5];
        snprintf(rbuf, sizeof(rbuf), "%2us", remSec);
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(9, 63, rbuf);
    } else if (replyLight) {
        u8g2.drawDisc(LZONE_X + LZONE_W/2, 56, 7);
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.setDrawColor(0);
        u8g2.drawStr(LZONE_X + LZONE_W/2 - 3, 61, "R");
        u8g2.setDrawColor(1);
    }
}

// =============================================================================
//  DRAW — CENTRE ZONE  (squawk digits, always visible)
// =============================================================================

static void drawCentreZone() {
    uint32_t now = millis();

    // View name label — top-right corner of zone
    u8g2.setFont(u8g2_font_5x7_tr);
    const char* vn = VIEW_NAMES[(uint8_t)currentView];
    int16_t vnW = (int16_t)u8g2.getStrWidth(vn);
    u8g2.drawStr(CZONE_X + CZONE_W - vnW - 1, 10, vn);

    // Special badges — top-left, mutually exclusive
    bool isEmrg   = (squawkCode == 7700);
    bool isRFail  = (squawkCode == 7600);
    bool isHijack = (squawkCode == 7500);
    bool isVFR    = (squawkCode == 7000);

    if (isEmrg || isRFail || isHijack) {
        if ((now / 400) & 1) {
            // Flashing double border
            u8g2.drawFrame(CZONE_X - 2, 0, CZONE_W + 4, 64);
            u8g2.drawFrame(CZONE_X,     1, CZONE_W,     62);
        }
        const char* lbl = isEmrg ? "EMER" : (isRFail ? "RDOF" : "HJCK");
        if ((now / 400) & 1) {
            u8g2.drawBox(CZONE_X, 1, 28, 12);
            u8g2.setDrawColor(0);
            u8g2.drawStr(CZONE_X + 2, 10, lbl);
            u8g2.setDrawColor(1);
        } else {
            u8g2.setFont(u8g2_font_5x7_tr);
            u8g2.drawStr(CZONE_X + 2, 10, lbl);
        }
    } else if (isVFR) {
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawRFrame(CZONE_X, 1, 20, 11, 2);
        u8g2.drawStr(CZONE_X + 3, 10, "VFR");
    } else if (editInvalid) {
        if ((now - editInvalidMs) < 1000u) {
            u8g2.setFont(u8g2_font_5x7_tr);
            u8g2.drawStr(CZONE_X + 2, 10, "INV");
        } else {
            editInvalid = false;
        }
    } else if (syncToast) {
        if ((now - syncToastMs) < SYNC_TOAST_MS) {
            u8g2.setFont(u8g2_font_5x7_tr);
            u8g2.drawRFrame(CZONE_X, 1, 20, 11, 2);
            u8g2.drawStr(CZONE_X + 3, 10, "RDY");
        } else {
            syncToast = false;
        }
    } else if (!syncDone) {
        if ((now / 500) & 1) {
            u8g2.setFont(u8g2_font_5x7_tr);
            u8g2.drawStr(CZONE_X + 2, 10, "SYNC");
        }
    }

    // ── Four squawk digits, auto-centred in CZONE_W ──────────────────────────
    u8g2.setFont(u8g2_font_logisoso32_tn);

    int16_t offsets[4], totalW = 0;
    for (uint8_t i = 0; i < 4; i++) {
        offsets[i] = totalW;
        char tmp[2] = { (char)('0' + getDigit(squawkCode, i)), '\0' };
        totalW += (int16_t)u8g2.getStrWidth(tmp);
    }
    int16_t baseX = CZONE_X + (CZONE_W - totalW) / 2;

    for (uint8_t i = 0; i < 4; i++) {
        char tmp[2] = { (char)('0' + getDigit(squawkCode, i)), '\0' };
        int16_t dw = (int16_t)u8g2.getStrWidth(tmp);
        int16_t dx = baseX + offsets[i];

        bool isCursor = (editMode && editDigit == i);

        if (isCursor) {
            u8g2.drawBox(dx - 1, 18, dw + 2, 45);
            u8g2.setDrawColor(0);
            u8g2.drawStr(dx, 60, tmp);
            u8g2.setDrawColor(1);
            if ((now / 500) & 1)
                u8g2.drawHLine(dx - 1, 63, dw + 2);
        } else {
            u8g2.drawStr(dx, 60, tmp);
        }
    }

    // Show "next" cursor when at the end (editDigit points past last digit)
    // This happens briefly before commitSquawk clears editMode — no action needed.
}

// =============================================================================
//  DRAW — RIGHT ZONE helpers
// =============================================================================

static void drawRightZoneHeader(const char* label) {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(RZONE_X, 11, label);
    u8g2.drawHLine(RZONE_X, 13, RZONE_W);
}

// Draw HH:MM:SS all in the same bold font, centred in the right zone
static void drawTimeInRightZone(uint8_t h, uint8_t m, uint8_t s, uint8_t yBase = 40) {
    char buf[9];
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", h, m, s);
    u8g2.setFont(u8g2_font_helvB12_tr);
    int16_t w = u8g2.getStrWidth(buf);
    int16_t startX = RZONE_X + max((int16_t)0, (int16_t)(RZONE_W - w) / 2);
    u8g2.drawStr(startX, yBase, buf);
}

// =============================================================================
//  DRAW — RIGHT ZONE: VIEW_UTC
// =============================================================================

static void drawRightUTC() {
    drawRightZoneHeader("UTC");
    uint8_t h, m, s;
    clockHMS(h, m, s);
    drawTimeInRightZone(h, m, s);
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(RZONE_X, 63, clockSeeded ? "SYNC" : "NO SYNC");
}

// =============================================================================
//  DRAW — RIGHT ZONE: VIEW_FLT
// =============================================================================

static void drawRightFLT() {
    if (fltModeToast && (millis()-fltModeToastMs < FLT_TOAST_MS)) {
        char toast[12];
        snprintf(toast, sizeof(toast), "FLT %s OK", fltMode == FLT_AUTO ? "AUTO" : "MAN");
        drawRightZoneHeader(toast);
    } else {
        fltModeToast = false;
        drawRightZoneHeader("FLIGHT TIME");
    }

    uint32_t sec = fltSec();
    uint8_t h = (uint8_t)min((uint32_t)99, sec / 3600);
    uint8_t m = (uint8_t)((sec % 3600) / 60);
    uint8_t s = (uint8_t)(sec % 60);
    drawTimeInRightZone(h, m, s);

    u8g2.setFont(u8g2_font_5x7_tr);
    if (fltMode == FLT_MANUAL) {
        u8g2.drawStr(RZONE_X, 63, fltRunning ? "RUN" : "STP");
    } else {
        // AUTO mode: show CRSR=MAN hint
        u8g2.drawStr(RZONE_X, 63, fltRunning ? "RUN AUTO" : "STP AUTO");
    }
}

// =============================================================================
//  DRAW — RIGHT ZONE: VIEW_CTU
// =============================================================================

static void drawRightCTU() {
    drawRightZoneHeader("CTU");
    uint32_t sec = ctuSec();
    uint8_t h = (uint8_t)min((uint32_t)99, sec / 3600);
    uint8_t m = (uint8_t)((sec % 3600) / 60);
    uint8_t s = (uint8_t)(sec % 60);
    drawTimeInRightZone(h, m, s);
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(RZONE_X, 63, ctuRunning ? "RUN" : "STP");
}

// =============================================================================
//  DRAW — RIGHT ZONE: VIEW_CDT
// =============================================================================

static void drawRightCDT() {
    drawRightZoneHeader("CDT");

    if (cdtSetMode) {
        // Show entry template: __:__:__ with filled digits
        // Positions in the 8-char string "HH:MM:SS": H=0,H=1,:=2,M=3,M=4,:=5,S=6,S=7
        char buf[9] = { '_','_',':','_','_',':','_','_','\0' };
        const uint8_t MAP[6] = { 0, 1, 3, 4, 6, 7 };
        for (uint8_t i = 0; i < cdtSetPos && i < 6; i++)
            buf[MAP[i]] = '0' + cdtSetBuf[i];

        u8g2.setFont(u8g2_font_7x13B_tr);
        int16_t tw = u8g2.getStrWidth(buf);
        u8g2.drawStr(RZONE_X + max((int16_t)0, (int16_t)(RZONE_W-tw)/2), 38, buf);

        // Blinking underline under current position
        if (cdtSetPos < 6 && ((millis()/500)&1)) {
            uint8_t curCharPos = MAP[cdtSetPos];
            // Approximate x: each char ~7px in 7x13B font
            int16_t ulX = RZONE_X + max((int16_t)0, (int16_t)(RZONE_W-tw)/2)
                          + (int16_t)curCharPos * 7;
            u8g2.drawHLine(ulX, 39, 7);
        }

        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(RZONE_X, 63, "SET HH:MM:SS");

    } else if (cdtExpired) {
        if ((millis() / 250) & 1) {
            u8g2.setFont(u8g2_font_7x13B_tr);
            int16_t ew = u8g2.getStrWidth("EXPRD!");
            u8g2.drawStr(RZONE_X + (RZONE_W-ew)/2, 38, "EXPRD!");
        }
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(RZONE_X, 63, "CLR=RESET");

    } else {
        uint32_t rem = cdtRemainSec();
        uint8_t h = (uint8_t)min((uint32_t)99, rem / 3600);
        uint8_t m = (uint8_t)((rem % 3600) / 60);
        uint8_t s = (uint8_t)(rem % 60);
        drawTimeInRightZone(h, m, s);

        u8g2.setFont(u8g2_font_5x7_tr);
        if (cdtRunning) {
            u8g2.drawStr(RZONE_X, 63, "RUN");
        } else if (cdtSetSec > 0) {
            u8g2.drawStr(RZONE_X, 63, "STP CRSR=SET");
        } else {
            u8g2.drawStr(RZONE_X, 63, "CRSR TO SET");
        }
    }
}

// =============================================================================
//  MASTER RENDER
// =============================================================================

static void updateDisplay() {
    u8g2.clearBuffer();

    if (txpMode == MODE_OFF) {
        u8g2.sendBuffer();
        return;
    }

    if (txpMode == MODE_TST) {
        static uint8_t  tstFrame   = 0;
        static uint32_t tstFrameMs = 0;
        if (millis() - tstFrameMs > 150) { tstFrame++; tstFrameMs = millis(); }
        switch (tstFrame % 5) {
            case 0: u8g2.drawFrame(0, 0, 256, 64); break;
            case 1: u8g2.drawBox(0, 0, 256, 64);   break;
            case 2:
                u8g2.setDrawColor(0);
                u8g2.drawBox(0, 0, 256, 64);
                u8g2.setDrawColor(1);
                break;
            case 3:
                for (uint16_t x = 0; x < 256; x += 8)
                    u8g2.drawVLine(x, 0, 64);
                break;
            case 4:
                u8g2.setFont(u8g2_font_10x20_tr);
                u8g2.drawStr(40, 38, "GTX 330 TEST");
                break;
        }
        u8g2.sendBuffer();
        return;
    }

    // Vertical separators
    u8g2.drawVLine(SEP_L_X, 1, 62);
    u8g2.drawVLine(SEP_R_X, 1, 62);

    drawLeftZone();
    drawCentreZone();

    switch (currentView) {
        case VIEW_UTC: drawRightUTC(); break;
        case VIEW_FLT: drawRightFLT(); break;
        case VIEW_CTU: drawRightCTU(); break;
        case VIEW_CDT: drawRightCDT(); break;
    }

    u8g2.sendBuffer();
}

// =============================================================================
//  SPLASH SCREEN
// =============================================================================

static void showSplash() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_10x20_tr);
    u8g2.drawStr(60, 28, "GTX 330");
    u8g2.setFont(u8g2_font_7x13B_tr);
    u8g2.drawStr(52, 50, "INITIALIZING");
    u8g2.sendBuffer();
    delay(1500);
}

// =============================================================================
//  DEBUG OUTPUT  (Serial1, once/second)
// =============================================================================

static void debugOutput() {
    static uint32_t lastMs = 0;
    if (millis() - lastMs < 1000) return;
    lastMs = millis();

    uint8_t h, m, s;
    clockHMS(h, m, s);
    uint32_t fs = fltSec();

    char line[128];
    snprintf(line, sizeof(line),
        "SQWK:%04u  MODE:%-3s  ALT:%ld ft"
        "  FLT:%02lu:%02lu:%02lu  UTC:%02u:%02u:%02u"
        "  GS:%u kt  VIEW:%s  FLT:%s",
        squawkCode,
        MODE_NAMES[(uint8_t)txpMode],
        altFeet,
        fs/3600, (fs%3600)/60, fs%60,
        h, m, s,
        groundSpeed,
        VIEW_NAMES[(uint8_t)currentView],
        fltMode == FLT_AUTO ? "AUTO" : "MAN");
    Serial1.println(line);
}

// =============================================================================
//  SETUP & LOOP
// =============================================================================

void setup() {
    // IMPORTANT: Do NOT call Serial.begin() — SiMessagePort owns Serial0 (USB).

    Serial1.begin(115200);
    Serial1.println(F("myGTX330 v1.0 — debug port on Serial1 (pins 18/19)"));
    Serial1.println(F("Commands: BTN:<name>"));
    Serial1.println(F("  Digits: BTN:0..9"));
    Serial1.println(F("  Keys:   BTN:IDENT  BTN:VFR  BTN:FUNC  BTN:CRSR"));
    Serial1.println(F("          BTN:START  BTN:CLR"));
    Serial1.println(F("  Modes:  BTN:MODE_OFF  BTN:MODE_SBY  BTN:MODE_ON  BTN:MODE_ALT"));
    Serial1.println(F("          (emulation only: BTN:MODE_GND  BTN:MODE_TST)"));

    messagePort = new SiMessagePort(
        SI_MESSAGE_PORT_DEVICE_ARDUINO_MEGA_2560,
        SI_MESSAGE_PORT_CHANNEL_A,
        onMessage);
    lastSyncReqMs = millis();  // trigger first request on first loop()

    initButtons();

    u8g2.begin();
    u8g2.setContrast(190);
    showSplash();

    editPrevCode = squawkCode;

    Serial1.println(F("GTX330 ready."));
}

void loop() {
    messagePort->Tick();

    // Startup sync: ping AM every SYNC_RETRY_MS until it replies with CH_AM_READY
    if (!syncDone) {
        uint32_t now = millis();
        if (now - lastSyncReqMs >= SYNC_RETRY_MS) {
            messagePort->SendMessage(CH_REQUEST_UPDATE, (int32_t)1);
            lastSyncReqMs = now;
            dbg("SYNC: pinging AM...");
        }
    }

    pollDebugSerial();
    checkIdentExpiry();
    checkCdtExpiry();
    updateButtons();
    handleButtons();
    updateDisplay();
    debugOutput();
}
