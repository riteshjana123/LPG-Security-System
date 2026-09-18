# Changelog

## Unreleased

- Added Blynk Testing Mode (V12) with manual fan, relay, servo-angle, and buzzer controls.
- Added live LPG Leakage status on V17 while retaining Fire Detection on V4.
- Restored automatic security control immediately when Testing Mode is turned off.
- Kept sensor monitoring active during Testing Mode while suspending automatic actuator control.
- Added Blynk control synchronization after reconnect.
- Buffered emergency notification content so an alarm event can still be sent after a temporary Blynk outage.

## v1.0.0 — Initial Release

- Published ESP32 LPG/fire security prototype firmware.
- Added Blynk telemetry and emergency event integration.
- Added relay, fan, servo, buzzer, LED, and OLED control logic.
- Added circuit documentation and flowchart.
- Added research-style technical report.
- Added testing documentation.
- Added safe configuration template and license.