# Testing Mode

Testing Mode is controlled by Blynk V12.

- V12 = 0: automatic LPG security control is active.
- V12 = 1: automatic actuator control is suspended for controlled bench testing.

While Testing Mode is ON, the dashboard controls V13 (fan), V14 (relay), V15 (servo angle), and V16 (buzzer). Sensor telemetry remains active, including V4 Fire Detection and V17 LPG Leakage.

Switching V12 OFF immediately restores automatic evaluation. If a hazard is currently detected, the controller enters Emergency Mode rather than blindly returning to normal.

The OLED displays `STATUS: TEST MODE` while the override is active.

**Safety:** Testing Mode is a prototype bench-test override and should not be used as a real-world safety interlock.

---

# Testing Guide

The project should be tested in stages. Verify the low-voltage behavior before connecting any mains load.

## 1. Boot and Wi-Fi

Expected LED behavior:

- Wi-Fi connecting/disconnected: green LED blinks.
- Wi-Fi connected and normal mode: green LED is solid ON.

## 2. Relay interface

Disconnect the AC load. Measure the relay IN pin relative to GND:

- GPIO26 LOW → approximately 5 V at IN → relay should activate.
- GPIO26 HIGH → approximately 0 V at IN → relay should release.

If the relay polarity differs on the actual module, confirm the module's input circuit before changing the firmware.

## 3. Servo

Normal state: 90°.

Emergency state: 0°.

Use an adequate external servo supply and common ground with the ESP32.

## 4. Fan

With the fan supply connected correctly:

- GPIO18 HIGH → fan OFF.
- GPIO18 LOW → fan ON.

Do not power a high-current or 12 V fan from the ESP32 rail.

## 5. Flame

The default firmware assumes the flame module is active LOW. Confirm this with the actual module and change `FLAME_ACTIVE_LOW` if required.

## 6. MQ-6

Watch the raw and filtered ADC readings in Serial Monitor. Tune `GAS_THRESHOLD` for the specific sensor/module and environment.

Do not interpret the ADC value as a calibrated ppm measurement.

## 7. Temperature

The default demonstration threshold is 50 °C. Change `TEMPERATURE_THRESHOLD` to match the intended prototype test condition.

## 8. Emergency test

When any alarm source becomes active, verify:

- Relay OFF
- Fan ON
- Servo 0°
- Buzzer ON
- Green LED OFF
- Red and blue LEDs alternate
- OLED reports EMERGENCY
- Blynk Emergency datastream becomes 1
- Blynk event `lpg_emergency` is generated once per emergency activation

## 9. Recovery

When all alarm conditions clear, the controller returns to normal:

- Relay ON
- Fan OFF
- Servo 90°
- Buzzer OFF
- Green Wi-Fi indication resumes

## Safety

This is an academic prototype, not a certified safety system. Do not intentionally release LPG around electronics, sparks, motors, or flame sources. Keep any mains demonstration load enclosed and isolated from the low-voltage circuit.
