# About myGTX330

myGTX330 is a hardware replica of the Garmin GTX330 Mode S transponder, built for use with the X-Plane 12 flight simulator.

It consists of an Arduino Mega 2560 microcontroller driving a 256×64 OLED display and a set of physical buttons, connected to X-Plane via the Air Manager 5.2 software. The result is a physical panel that looks and behaves like a real transponder — you enter squawk codes on real buttons, the display shows altitude and UTC time from the simulator, and every action is immediately reflected in X-Plane.

## Purpose

Home flight simulator cockpits typically rely on mouse clicks and on-screen panels to operate avionics. myGTX330 replaces the on-screen GTX330 panel with a dedicated physical unit, improving realism and reducing the need to interact with the simulator interface during flight.

## What it does

- Displays the current squawk code, transponder mode, pressure altitude, and UTC time on a backlit OLED panel
- Lets the pilot enter squawk codes and change transponder mode using physical buttons
- Sends an IDENT pulse to X-Plane when the IDENT button is pressed
- Shows emergency code warnings (7700 / 7600 / 7500) and VFR indication (7000) on the display
- Keeps a flight timer, count-up stopwatch, and countdown timer on the display
- Stays in sync with X-Plane in real time — changes in the simulator are reflected on the panel and vice versa
