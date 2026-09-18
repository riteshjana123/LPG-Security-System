# Circuit Notes

## Core connections

| Device | Connection |
|---|---|
| MQ-6 AO | 10 kΩ / 20 kΩ divider → GPIO34 |
| DHT11 DATA | GPIO4 |
| Flame sensor DO | GPIO27 |
| OLED SDA | GPIO21 |
| OLED SCL | GPIO22 |
| Relay interface | GPIO26 |
| Servo signal | GPIO13 |
| Green LED | GPIO25 through series resistor |
| Red LED | GPIO33 through series resistor |
| Blue LED | GPIO32 through series resistor |
| Buzzer | GPIO14 |
| Fan driver | GPIO18 |

## Single-BC547 relay interface

The external BC547 is an inverting interface between the 3.3 V ESP32 GPIO and the 5 V relay-module input.

~~~text
GPIO26 ---- 1 kΩ ----> BC547 base
BC547 base ----- 4.7 kΩ ----- GND
BC547 emitter ---------------- GND
BC547 collector -------------- Relay IN
BC547 collector -- 10 kΩ ---- +5 V

Relay VCC -------------------- +5 V
Relay GND -------------------- GND
ESP32 GND -------------------- common GND
~~~

### Relay truth table

| ESP32 GPIO26 | BC547 | Relay IN | Relay state |
|---:|---|---:|---|
| LOW | OFF | ~5 V | ON |
| HIGH | ON | ~0 V | OFF |

Always confirm the behavior of the exact relay module before connecting a mains load.

## MQ-6 input protection

If the MQ-6 module is powered from 5 V, use a voltage divider before the ESP32 ADC:

~~~text
MQ-6 AO ---- 10 kΩ ----+---- GPIO34
                       |
                      20 kΩ
                       |
                      GND
~~~

The divider scales 5 V to approximately 3.33 V. The firmware treats the reading as an ADC value, not calibrated LPG ppm.

## Fan driver

The fan uses a separate BC547 + MOSFET low-side switch. The firmware assumes an inverted control signal:

~~~text
GPIO18 LOW  -> Fan ON
GPIO18 HIGH -> Fan OFF
~~~

The fan supply must be appropriate for the fan voltage and current. Use suitable flyback protection and local decoupling for the load.

## Power and grounding

- Use an adequate regulated 5 V supply for the relay module and servo as required.
- Power a 12 V fan from its own suitable supply when applicable.
- Tie low-voltage grounds together so the ESP32 signal references are defined.
- Do not use an ESP32 GPIO to power a servo, relay coil, or high-current fan.
- Keep AC mains wiring physically and electrically isolated from the low-voltage prototype.
