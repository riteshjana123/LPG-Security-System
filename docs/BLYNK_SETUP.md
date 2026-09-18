# Blynk Setup

## Datastreams

Create these virtual datastreams in the Blynk template:

| Pin | Name | Type | Suggested range |
|---|---|---|---|
| V0 | Gas Raw | Integer | 0–4095 |
| V1 | Gas Level / Filtered ADC | Integer | 0–4095 |
| V2 | Temperature | Double | 0–80 °C |
| V3 | Humidity | Double | 0–100 % |
| V4 | Flame | Integer | 0–1 |
| V5 | Emergency | Integer | 0–1 |
| V6 | Relay | Integer | 0–1 |
| V7 | Fan | Integer | 0–1 |
| V8 | Servo Angle | Integer | 0–180 ° |
| V9 | Wi-Fi | Integer | 0–1 |
| V10 | Alarm Cause | String | — |
| V11 | Blynk Connected | Integer | 0–1 |

## Emergency event

Create a custom Event in the Blynk template:

- Name: LPG Emergency
- Event code: lpg_emergency
- Type: Critical
- Enable push notifications.

The firmware triggers the event with:

    Blynk.logEvent("lpg_emergency", message);

The notification is generated when the device changes from normal to emergency. If Blynk is temporarily unavailable, the event is held locally and sent after Blynk reconnects.

## Dashboard

Recommended widgets:

- Emergency status
- Gas raw / filtered value
- Temperature
- Humidity
- Flame status
- Relay status
- Fan status
- Servo angle
- Wi-Fi status
- Blynk status
- Alarm cause

Outputs are intentionally presented as status values rather than remote control switches so the local ESP32 safety logic remains authoritative.
