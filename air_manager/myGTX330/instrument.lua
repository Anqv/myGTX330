-- =============================================================================
--  GTX330 Transponder — Air Manager 5.2 Instrument Script  v3.0
--  Bridges X-Plane 11/12 transponder datarefs ↔ Arduino Mega 2560
--  via si_message_port (Arduino C++ library / hw_message_port Lua API).
--
--  IMPORTANT — hardware port name
--  --------------------------------
--  The string passed to hw_message_port_add() must match the name you gave
--  this Arduino device in Air Manager's Hardware settings panel.
--  Default assumed here: "Arduino Mega 2560".  Change HW_PORT_NAME below
--  if you named it something else.
--
--  Channel map  (MUST match myGTX330.ino)
--  -----------------------------------------------
--  CH  1  AM→ARD  Squawk code (decimal BCD, e.g. 7700)
--  CH  2  AM→ARD  Transponder mode  0=OFF 1=SBY 2=GND 3=ON 4=ALT 5=TST
--  CH  3  AM→ARD  Pressure altitude feet (int32)
--  CH  4  AM→ARD  Reply / interrogation light (0/1)
--  CH  5  AM→ARD  Ident active from X-Plane (0/1)
--  CH  6  AM→ARD  Ground speed knots (int32)
--  CH  7  AM→ARD  UTC seconds-of-day (int32)
--  CH 10  ARD→AM  New squawk code from pilot
--  CH 11  ARD→AM  New transponder mode from pilot
--  CH 12  ARD→AM  IDENT button pressed (one-shot)
--
--  Mode translation
--  ----------------
--  X-Plane transponder_mode:  0=Off  1=SBY  2=On(A)  3=Alt(C)  4=Test
--  GTX330 modes:              0=OFF  1=SBY  2=GND    3=ON      4=ALT  5=TST
--  GND (2) has no X-Plane equivalent → written as SBY (1) when pilot selects GND.
--
--  AM5 API changes vs AM4
--  -----------------------
--  si_message_port_set_callback(cb)    → hw_message_port_add(name, cb)
--  si_message_port_send(ch, val)       → hw_message_port_send(port_id, ch, "INT", val)
--  xp_dataref_subscribe(...)           → xpl_dataref_subscribe(...)
--  xp_dataref_write(...)               → xpl_dataref_write(...)
--  xp_command(cmd)                     → xpl_command(cmd, "ONCE")
-- =============================================================================

-- ---------------------------------------------------------------------------
--  Hardware port name — must match Air Manager hardware settings
-- ---------------------------------------------------------------------------
local HW_PORT_NAME = "Arduino Mega 2560"

print("[GTX330] Instrument script loading — port: " .. HW_PORT_NAME)

-- ---------------------------------------------------------------------------
--  Channel constants
-- ---------------------------------------------------------------------------
local CH_SQUAWK       =  1
local CH_MODE         =  2
local CH_ALTITUDE     =  3
local CH_REPLY_LIGHT  =  4
local CH_IDENT_XP     =  5
local CH_GROUND_SPEED =  6
local CH_SIM_TIME     =  7
local CH_SQUAWK_SET   = 10
local CH_MODE_SET     = 11
local CH_IDENT_BTN    = 12
local CH_DEBUG        = 99  -- ARD→AM debug string (payload is a string)

-- ---------------------------------------------------------------------------
--  X-Plane dataref paths
-- ---------------------------------------------------------------------------
local DR_SQUAWK = "sim/cockpit/radios/transponder_code"           -- int  BCD decimal
local DR_MODE   = "sim/cockpit/radios/transponder_mode"           -- int  0-4
local DR_ALT    = "sim/flightmodel/misc/h_ind"                    -- float feet (pressure alt)
local DR_REPLY  = "sim/cockpit2/radios/indicators/transponder_id" -- int  0/1
local DR_GS     = "sim/flightmodel/position/groundspeed"          -- float m/s
local DR_ZLH    = "sim/cockpit2/clock_timer/zulu_time_hours"
local DR_ZLM    = "sim/cockpit2/clock_timer/zulu_time_minutes"
local DR_ZLS    = "sim/cockpit2/clock_timer/zulu_time_seconds"

local CMD_IDENT = "sim/transponder/transponder_ident"

-- ---------------------------------------------------------------------------
--  State cache — suppress redundant sends when value is unchanged
-- ---------------------------------------------------------------------------
local last = {
    squawk  = -1,
    mode    = -1,
    alt     = -1,
    reply   = -1,
    gs      = -1,
    tod_sec = -1,
}

local zulu_h, zulu_m, zulu_s = 0, 0, 0

-- Mode name table for readable debug output
local GTX_MODE_NAMES = { [0]="OFF", [1]="SBY", [2]="GND", [3]="ON", [4]="ALT", [5]="TST" }
local XP_MODE_NAMES  = { [0]="Off", [1]="SBY", [2]="On(A)", [3]="Alt(C)", [4]="Test" }

-- ---------------------------------------------------------------------------
--  Hardware port handle (set by hw_message_port_add below)
-- ---------------------------------------------------------------------------
local port_id

-- ---------------------------------------------------------------------------
--  Arduino → Air Manager: handle pilot-initiated changes
--  AM5 callback signature: function(message_id, payload)
-- ---------------------------------------------------------------------------
local function on_arduino_message(message_id, payload)
    print("[GTX330] Arduino→AM  ch=" .. message_id .. "  payload=" .. tostring(payload))

    if message_id == CH_SQUAWK_SET then
        -- Validate: every decimal digit must be 0-7 (octal squawk)
        local value = payload
        local d1 = math.floor(value / 1000) % 10
        local d2 = math.floor(value /  100) % 10
        local d3 = math.floor(value /   10) % 10
        local d4 =             value         % 10
        if d1 <= 7 and d2 <= 7 and d3 <= 7 and d4 <= 7 then
            print("[GTX330] Squawk write → XP: " .. string.format("%04d", value))
            xpl_dataref_write(DR_SQUAWK, "INT", value)
        else
            print("[GTX330] Squawk REJECTED (non-octal digit): " .. tostring(value))
        end

    elseif message_id == CH_MODE_SET then
        -- GTX330 GND (2) → X-Plane SBY (1); no GND equivalent in X-Plane
        local xp_mode = payload
        if payload == 2 then xp_mode = 1 end      -- GND → SBY
        local gtx_name = GTX_MODE_NAMES[payload]  or ("?(" .. tostring(payload) .. ")")
        local xp_name  = XP_MODE_NAMES[xp_mode]  or ("?(" .. tostring(xp_mode)  .. ")")
        if xp_mode >= 0 and xp_mode <= 4 then
            print("[GTX330] Mode write → XP: GTX " .. gtx_name .. " → XP " .. xp_name)
            xpl_dataref_write(DR_MODE, "INT", xp_mode)
        else
            print("[GTX330] Mode REJECTED (out of range): " .. tostring(payload))
        end

    elseif message_id == CH_IDENT_BTN then
        print("[GTX330] IDENT button → XP command")
        xpl_command(CMD_IDENT, "ONCE")             -- single-shot command (AM5)

    elseif message_id == CH_DEBUG then
        print("[GTX330] ARD: " .. tostring(payload))

    else
        print("[GTX330] Unknown message from Arduino: ch=" .. message_id)
    end
end

-- Register hardware message port — returns port_id used for sending
port_id = hw_message_port_add(HW_PORT_NAME, on_arduino_message)
print("[GTX330] hw_message_port_add returned port_id=" .. tostring(port_id))

-- ---------------------------------------------------------------------------
--  Squawk code: X-Plane → Arduino
-- ---------------------------------------------------------------------------
print("[GTX330] Subscribing to datarefs...")
xpl_dataref_subscribe(DR_SQUAWK, "INT", function(value)
    if value ~= last.squawk then
        last.squawk = value
        print("[GTX330] XP→ARD  Squawk: " .. string.format("%04d", value))
        hw_message_port_send(port_id, CH_SQUAWK, "INT", value)
    end
end)

-- ---------------------------------------------------------------------------
--  Transponder mode: X-Plane → Arduino
--  X-Plane:  0=Off  1=SBY  2=On(A)  3=Alt(C)  4=Test
--  GTX330:   0=OFF  1=SBY  2=GND    3=ON      4=ALT   5=TST
-- ---------------------------------------------------------------------------
xpl_dataref_subscribe(DR_MODE, "INT", function(value)
    local gtx = value                     -- 0→0  1→1  (direct)
    if value == 2 then gtx = 3 end        -- XP "On"   → GTX "ON"
    if value == 3 then gtx = 4 end        -- XP "Alt"  → GTX "ALT"
    if value == 4 then gtx = 5 end        -- XP "Test" → GTX "TST"
    if gtx ~= last.mode then
        last.mode = gtx
        local xp_name  = XP_MODE_NAMES[value] or tostring(value)
        local gtx_name = GTX_MODE_NAMES[gtx]  or tostring(gtx)
        print("[GTX330] XP→ARD  Mode: XP " .. xp_name .. " → GTX " .. gtx_name .. " (ch=" .. CH_MODE .. " val=" .. gtx .. ")")
        hw_message_port_send(port_id, CH_MODE, "INT", gtx)
    end
end)

-- ---------------------------------------------------------------------------
--  Pressure altitude: X-Plane → Arduino  (float feet → int32, clamped 0-65535)
-- ---------------------------------------------------------------------------
xpl_dataref_subscribe(DR_ALT, "FLOAT", function(value)
    local ft = math.floor(value)
    ft = math.max(0, math.min(65535, ft))
    if ft ~= last.alt then
        last.alt = ft
        print("[GTX330] XP→ARD  Altitude: " .. ft .. " ft  FL" .. string.format("%03d", math.floor(ft/100)))
        hw_message_port_send(port_id, CH_ALTITUDE, "INT", ft)
    end
end)

-- ---------------------------------------------------------------------------
--  Reply / interrogation light: X-Plane → Arduino
-- ---------------------------------------------------------------------------
xpl_dataref_subscribe(DR_REPLY, "INT", function(value)
    local v = (value ~= 0) and 1 or 0
    if v ~= last.reply then
        last.reply = v
        print("[GTX330] XP→ARD  Reply light: " .. (v == 1 and "ON" or "off"))
        hw_message_port_send(port_id, CH_REPLY_LIGHT, "INT", v)
    end
end)

-- ---------------------------------------------------------------------------
--  Ground speed: X-Plane → Arduino  (m/s → knots, clamped 0-65535)
-- ---------------------------------------------------------------------------
xpl_dataref_subscribe(DR_GS, "FLOAT", function(value)
    local kts = math.floor(value * 1.94384)
    kts = math.max(0, math.min(65535, kts))
    if kts ~= last.gs then
        last.gs = kts
        print("[GTX330] XP→ARD  Ground speed: " .. kts .. " kt")
        hw_message_port_send(port_id, CH_GROUND_SPEED, "INT", kts)
    end
end)

-- ---------------------------------------------------------------------------
--  UTC clock: combine H/M/S into seconds-of-day and send
-- ---------------------------------------------------------------------------
local function send_tod()
    local tod = zulu_h * 3600 + zulu_m * 60 + zulu_s
    if tod ~= last.tod_sec then
        last.tod_sec = tod
        -- Log only when minutes change (avoids one print/second in the log)
        if tod % 60 == 0 then
            print(string.format("[GTX330] XP→ARD  UTC: %02d:%02d:%02d (tod=%d)",
                zulu_h, zulu_m, zulu_s, tod))
        end
        hw_message_port_send(port_id, CH_SIM_TIME, "INT", tod)
    end
end

xpl_dataref_subscribe(DR_ZLH, "INT", function(v) zulu_h = v; send_tod() end)
xpl_dataref_subscribe(DR_ZLM, "INT", function(v) zulu_m = v; send_tod() end)
xpl_dataref_subscribe(DR_ZLS, "INT", function(v) zulu_s = v; send_tod() end)

print("[GTX330] All dataref subscriptions registered — instrument ready.")

-- ---------------------------------------------------------------------------
--  Note: all xpl_dataref_subscribe callbacks fire once at instrument load
--  with the current X-Plane state, so the Arduino is immediately synced
--  without needing a separate init sequence.
-- ---------------------------------------------------------------------------
