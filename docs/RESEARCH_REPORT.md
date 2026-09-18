# Research-Style Technical Report

## Title

**IoT-Based LPG and Fire Security System Using ESP32 with Local Emergency Control and Blynk Monitoring**

## Abstract

This project presents an embedded IoT prototype for monitoring potentially hazardous LPG leakage, flame presence, and elevated temperature. An ESP32 acts as the central controller and evaluates three sensor inputs using a simple OR-based emergency rule: activation of any one hazard condition places the system into emergency mode. In normal operation, a prototype relay load remains energized, the fan is off, and the servo remains at 90°. During emergency operation, the relay is de-energized, the exhaust fan is activated, the servo moves to 0°, the buzzer sounds, and red/blue alarm LEDs alternate. A 0.96-inch I2C OLED provides local status information, while Blynk provides cloud telemetry and a push-notification event. The architecture intentionally keeps the physical emergency response local to the ESP32 so that loss of Wi-Fi or cloud connectivity does not stop the local control logic. This repository documents the hardware interface, firmware, cloud configuration, testing plan, and current limitations of the prototype.

**Keywords:** ESP32, LPG monitoring, gas detection, fire detection, IoT, Blynk, MQ-6, DHT11, flame sensor, embedded systems, safety prototype

## 1. Introduction

LPG is widely used for cooking and heating, but leakage combined with an ignition source can create a hazardous condition. A low-cost embedded monitoring prototype can demonstrate how multiple sensing modalities can be combined with local actuation and remote notification.

The objective of this project is to build a compact ESP32-based prototype that continuously observes gas level, temperature, and flame status; reacts locally to abnormal conditions; presents the current state on an OLED; and reports sensor and actuator states through Blynk.

## 2. Problem Statement

The prototype addresses the following engineering problem:

> How can a low-cost embedded controller continuously monitor multiple LPG/fire-related indicators and provide a coordinated local emergency response together with remote IoT notification?

The design emphasizes local response first and cloud monitoring second.

## 3. Objectives

1. Monitor LPG-related gas concentration using an MQ-6 module.
2. Monitor temperature and humidity using a DHT11.
3. Detect flame using a digital flame sensor.
4. Enter emergency mode when any configured hazard condition is active.
5. Control a relay, exhaust fan, servo, buzzer, and status LEDs according to system state.
6. Display live status locally using an I2C OLED.
7. Publish telemetry to Blynk over Wi-Fi.
8. Generate one Blynk emergency event for each normal-to-emergency transition.
9. Continue local emergency control if Wi-Fi/Blynk becomes unavailable.

## 4. Proposed System Architecture

The architecture is divided into five layers:

- **Sensing layer:** MQ-6, DHT11, and flame sensor.
- **Control layer:** ESP32 firmware, filtering, threshold evaluation, and state management.
- **Actuation layer:** relay, fan driver, servo, buzzer, and LEDs.
- **Local visualization layer:** 0.96-inch I2C OLED.
- **IoT layer:** Wi-Fi and Blynk telemetry/event notification.

See [FLOWCHART.svg](FLOWCHART.svg) and [LPG_SYSTEM_ARCHITECTURE.svg](LPG_SYSTEM_ARCHITECTURE.svg).

## 5. Hardware Design

### 5.1 ESP32

The ESP32 is the central controller. It reads sensor inputs, evaluates the emergency state, drives the local actuators, refreshes the OLED, and communicates with Blynk over Wi-Fi.

### 5.2 MQ-6 gas sensor

The MQ-6 is connected to ESP32 GPIO34 through a voltage divider when the sensor module is powered from 5 V. The firmware uses the resulting ADC value as a relative sensor signal and compares it with a configurable threshold.

**Important:** the current implementation does not convert the ADC reading to a calibrated LPG concentration in ppm.

### 5.3 DHT11

The DHT11 provides temperature and humidity. Temperature is used as one of the independent emergency triggers, while humidity is monitored and reported but is not itself an emergency trigger.

### 5.4 Flame sensor

The flame module provides a digital indication. The current firmware defaults to active-low operation and exposes this assumption as a configuration variable.

### 5.5 Relay interface

A single BC547 is used as an inverting level/interface stage for the 5 V relay module input. The collector is pulled up to 5 V and connected to the relay IN pin. Because the stage inverts logic, the firmware uses GPIO26 LOW for relay ON and GPIO26 HIGH for relay OFF.

### 5.6 Fan driver

The fan is controlled through the previously designed BC547 plus MOSFET low-side driver. The firmware assumes an inverted driver where GPIO18 LOW turns the fan ON and GPIO18 HIGH turns it OFF.

### 5.7 Servo

The TowerPro MG90S servo represents the prototype gas-regulator mechanism. It is commanded to 90° in normal operation and 0° during emergency operation. The servo should use an appropriate external supply rather than relying on an ESP32 GPIO.

### 5.8 LEDs, buzzer, and OLED

The green LED indicates Wi-Fi/system status, while red and blue LEDs provide an alternating emergency indication. The buzzer provides an audible local alarm. The OLED displays sensor values and the current operating state.

## 6. Pin Configuration

| Function | GPIO |
|---|---:|
| MQ-6 analog | 34 |
| DHT11 data | 4 |
| Flame digital output | 27 |
| OLED SDA | 21 |
| OLED SCL | 22 |
| Relay interface | 26 |
| Servo signal | 13 |
| Green LED | 25 |
| Red LED | 33 |
| Blue LED | 32 |
| Buzzer | 14 |
| Fan driver | 18 |

## 7. Control Algorithm

The main emergency rule is:

~~~text
Emergency = Gas Alarm OR Temperature Alarm OR Flame Alarm
~~~

### Normal state

- Relay ON
- Fan OFF
- Servo 90°
- Buzzer OFF
- Red and blue LEDs OFF
- Green LED solid when Wi-Fi is connected
- Green LED blinking when Wi-Fi is not connected

### Emergency state

- Relay OFF
- Fan ON
- Servo 0°
- Buzzer ON
- Green LED OFF
- Red and blue LEDs alternate
- OLED shows emergency condition
- Blynk event is sent

The controller continuously monitors the sensors and returns to normal only after all currently active alarm conditions clear.

## 8. IoT and Blynk Implementation

Blynk is used for remote observation and notification. The firmware periodically publishes sensor and actuator states using virtual datastreams.

Configured telemetry channels:

| Pin | Data |
|---|---|
| V0 | Raw MQ-6 ADC |
| V1 | Filtered MQ-6 ADC |
| V2 | Temperature |
| V3 | Humidity |
| V4 | Flame state |
| V5 | Emergency state |
| V6 | Relay state |
| V7 | Fan state |
| V8 | Servo angle |
| V9 | Wi-Fi state |
| V10 | Alarm cause |
| V11 | Blynk connection state |

The emergency event code is `lpg_emergency`.

See [BLYNK_SETUP.md](BLYNK_SETUP.md).

## 9. Software Design

The firmware uses timed tasks rather than putting all work in the main loop. Separate timers handle fast sensor acquisition, DHT11 sampling, Blynk telemetry, OLED refresh, LED animation, and connection maintenance.

The event notification is edge-triggered: it is requested only when the system transitions from normal to emergency. This avoids repeatedly logging the same emergency state every control cycle.

Local actuation is independent of Blynk. A cloud outage therefore does not prevent the ESP32 from changing the relay, fan, servo, buzzer, or alarm LEDs.

## 10. Experimental Methodology

To turn the prototype into a measured engineering study, tests should be performed under controlled conditions and repeated where practical.

### Test A — Wi-Fi behavior

Record connection time and confirm the green LED changes from blinking during connection to solid ON after connection.

### Test B — Relay interface

With the mains load disconnected, measure relay IN relative to ground for both GPIO26 states and verify relay activation/release.

### Test C — Sensor threshold behavior

Record raw and filtered MQ-6 values under the chosen test conditions. Record DHT11 temperature. Record flame sensor state.

### Test D — Emergency response

For each trigger source, record the time between detection and actuator response. Verify relay OFF, fan ON, servo 0°, buzzer ON, red/blue flashing, OLED update, and Blynk event generation.

### Test E — Connectivity fault

Disconnect Wi-Fi during operation and verify local emergency logic still operates. Restore Wi-Fi and record reconnection behavior.

## 11. Results and Data Recording

**No experimental values are invented in this repository.** The results section should be completed from measurements taken from the real prototype.

Recommended result table:

| Test | Condition | Measurement | Result |
|---|---|---|---|
| Wi-Fi | Initial connection | Connection time | To be measured |
| MQ-6 | Baseline | Raw / filtered ADC | To be measured |
| Gas trigger | Threshold crossing | Response time | To be measured |
| Temperature trigger | Threshold crossing | Response time | To be measured |
| Flame trigger | Flame detected | Response time | To be measured |
| Relay | GPIO state change | IN voltage | To be measured |
| Fan | Emergency | Activation time | To be measured |
| Servo | Emergency | Movement time | To be measured |
| Blynk | Emergency event | Notification delay | To be measured |

## 12. Current Limitations

1. The MQ-6 reading is not a calibrated LPG ppm measurement.
2. DHT11 has limited temperature resolution and response speed.
3. The flame sensor is a simple module and is not a certified fire detector.
4. The prototype is not certified for real-world LPG safety deployment.
5. The current emergency thresholds require testing and calibration for the physical setup.
6. The hardware is still a prototype and should not be treated as a safety-critical controller.

## 13. Future Improvements

Potential improvements include:

- Calibrated gas sensing and characterization.
- A faster and more accurate temperature sensor.
- Certified gas/fire sensing hardware for real deployment.
- Hardware watchdog and brownout/fault-handling strategy.
- Non-volatile event logging.
- Enclosed PCB/terminal design instead of breadboard wiring.
- Independent redundant safety cutoff circuitry.
- Quantitative false-trigger and repeatability testing.
- Mobile dashboard screenshots and measured notification latency.

## 14. Conclusion

This project demonstrates an ESP32-centered approach to multi-sensor LPG/fire monitoring with local actuation and IoT notification. Its defining design choice is to keep the physical emergency decision on the microcontroller rather than depending on the cloud. The repository provides the firmware, hardware notes, Blynk configuration, flowchart, and testing framework needed to reproduce and extend the prototype.

## 15. Reproducibility

Reproduction requires the hardware listed in the main README, the pin assignments in this report, the Arduino libraries listed in the README, and a local `config.h` containing the user's own Wi-Fi and Blynk credentials.

See also:

- [README](../README.md)
- [Circuit Notes](CIRCUIT_NOTES.md)
- [Blynk Setup](BLYNK_SETUP.md)
- [Testing Guide](TESTING.md)
- [Flowchart](FLOWCHART.svg)
- [Circuit Diagram](CIRCUIT_DIAGRAM.svg)
