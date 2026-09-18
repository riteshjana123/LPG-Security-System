# Blynk Setup

## 1. Datastreams

Create these datastreams in the Blynk template.

### Monitoring / status

| Pin | Name | Type | Range | Direction |
|---|---|---|---|---|
| V0 | Gas Raw | Integer | 0–4095 | ESP32 → Blynk |
| V1 | Gas Level | Integer | 0–4095 | ESP32 → Blynk |
| V2 | Temperature | Double | 0–50 °C | ESP32 → Blynk |
| V3 | Humidity | Double | 0–100 % | ESP32 → Blynk |
| V4 | Fire Detection | Integer | 0–1 | ESP32 → Blynk |
| V5 | Emergency | Integer | 0–1 | ESP32 → Blynk |
| V6 | Relay Status | Integer | 0–1 | ESP32 → Blynk |
| V7 | Fan Status | Integer | 0–1 | ESP32 → Blynk |
| V8 | Servo Angle | Integer | 0–180° | ESP32 → Blynk |
| V9 | Wi-Fi Status | Integer | 0–1 | ESP32 → Blynk |
| V10 | Alarm Cause | String | — | ESP32 → Blynk |
| V11 | Blynk Connected | Integer | 0–1 | ESP32 → Blynk |
| V17 | LPG Leakage | Integer | 0–1 | ESP32 → Blynk |

### Testing / manual-control inputs

| Pin | Name | Type | Range | Direction | Widget |
|---|---|---|---|---|---|
| V12 | Testing Mode | Integer | 0–1 | Blynk → ESP32 | Switch |
| V13 | Manual Fan | Integer | 0–1 | Blynk → ESP32 | Switch |
| V14 | Manual Relay | Integer | 0–1 | Blynk → ESP32 | Switch |
| V15 | Manual Servo Angle | Integer | 0–180° | Blynk → ESP32 | Slider |
| V16 | Manual Buzzer | Integer | 0–1 | Blynk → ESP32 | Switch |

Recommended defaults:

- V12 = 0
- V13 = 0
- V14 = 0
- V15 = 90
- V16 = 0

## 2. Operating modes

### Automatic Security Mode — V12 = 0

The ESP32 uses sensor inputs to control the safety system.

Emergency condition:

~~~text
Gas alarm OR Flame detected OR High temperature
                ↓
           EMERGENCY
~~~

Emergency response:

- Relay OFF
- Fan ON
- Servo = 0°
- Buzzer ON
- Green LED OFF
- Red/blue LEDs alternate
- OLED shows emergency
- Blynk emergency event is sent

When all alarm conditions clear, the system returns to normal:

- Relay ON
- Fan OFF
- Servo = 90°
- Buzzer OFF
- Normal green Wi-Fi indication resumes

### Testing Mode — V12 = 1

Testing Mode intentionally overrides automatic actuator control for controlled bench testing.

The dashboard controls:

- V13 Manual Fan
- V14 Manual Relay
- V15 Manual Servo Angle
- V16 Manual Buzzer

Sensor telemetry continues to update while Testing Mode is active. V4 and V17 can still show fire/gas detection, and V2 continues to show temperature.

The automatic emergency event is suppressed while Testing Mode is active.

**Important:** Testing Mode is a prototype/bench-test override. Do not use it as a real-world safety interlock.

## 3. Leaving Testing Mode

When V12 changes from 1 → 0, automatic security control is immediately restored.

The ESP32 evaluates the current sensor values at that moment.

Example:

~~~text
Testing Mode OFF
      ↓
Gas still above threshold?
      ↓
     YES
      ↓
Emergency Mode
      ↓
Relay OFF
Fan ON
Servo 0°
Buzzer ON
Blynk emergency event
~~~

The system therefore returns to automatic evaluation rather than blindly forcing a normal state.

## 4. Emergency event / phone notification

Create a Blynk custom Event:

- **Event Name:** LPG Emergency
- **Event Code:** `lpg_emergency`
- **Type:** Critical
- **Push notification:** Enabled

The firmware calls:

~~~cpp
Blynk.logEvent("lpg_emergency", message);
~~~

The event is generated once when automatic security mode changes from normal to emergency. It is not generated repeatedly on every sensor-reading cycle.

If Blynk is unavailable, the event is held locally with a snapshot of the alarm cause, gas value, and temperature and is sent after Blynk reconnects, unless Testing Mode is activated before delivery.

## 5. Web dashboard

Recommended dashboard sections:

### STATUS

- Emergency
- LPG Leakage
- Fire Detection
- Temperature
- Humidity
- Gas Level

### OUTPUTS

- Relay Status
- Fan Status
- Servo Angle
- Blynk Connected
- Wi-Fi Status
- Alarm Cause

### TESTING

- Testing Mode
- Manual Fan
- Manual Relay
- Manual Servo Angle
- Manual Buzzer

### EVENTS

Add a Latest Events widget and configure it to show the `LPG Emergency` event.

## 6. Widget configuration

Use status widgets for V4, V5, V6, V7, V8, V9, V11, and V17.

Use control widgets for V12–V16.

For V15, use a Slider from 0 to 180° and enable send-on-release when appropriate so the servo is not commanded for every tiny slider movement.

## 7. Firmware synchronization

The firmware calls `Blynk.syncVirtual()` from `BLYNK_CONNECTED()` to restore V12–V16 command values after reconnecting to Blynk.

The actual physical states are reported separately through V6–V8.
