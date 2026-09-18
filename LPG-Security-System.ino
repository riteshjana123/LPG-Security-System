/************************************************************
 * LPG SECURITY SYSTEM
 *
 * ESP32 + Blynk + MQ-6 + DHT11 + Flame Sensor
 * OLED + MG90S Servo + Relay + Fan + LEDs + Buzzer
 *
 * ==========================================================
 * OPERATING MODES
 * ==========================================================
 *
 * AUTOMATIC SECURITY MODE (Testing Mode = 0)
 *   Any one of:
 *      - LPG leakage alarm
 *      - Flame detected
 *      - High temperature
 *   => Emergency mode
 *
 *   Emergency outputs:
 *      Relay OFF
 *      Fan ON
 *      Servo 0°
 *      Buzzer ON
 *      Green LED OFF
 *      Red/Blue alternate
 *      Blynk emergency event
 *
 * TESTING MODE (Testing Mode = 1)
 *   Automatic safety actuation is intentionally overridden
 *   for bench testing.
 *
 *   Dashboard controls:
 *      V13 -> Manual Fan
 *      V14 -> Manual Relay
 *      V15 -> Manual Servo Angle
 *      V16 -> Manual Buzzer
 *
 *   Sensor readings remain visible:
 *      V4  -> Fire Detection
 *      V17 -> LPG Leakage
 *      V2  -> Temperature
 *      V10 -> Alarm Cause
 *
 *   Blynk emergency notification is suppressed while
 *   Testing Mode is active because automatic safety mode
 *   is intentionally disabled.
 *
 *   IMPORTANT:
 *   Testing Mode is for controlled bench/prototype testing.
 *   Do not use it as a substitute for a real safety interlock.
 *
 * ==========================================================
 * RELAY HARDWARE — SINGLE BC547 INVERTER
 * ==========================================================
 *
 * ESP32 GPIO26 -> 1k -> BC547 Base
 * BC547 Base -> 4.7k -> GND
 * BC547 Emitter -> GND
 * BC547 Collector -> Relay IN
 * BC547 Collector -> 10k -> +5V
 *
 * Relay VCC -> +5V
 * Relay GND -> common GND
 *
 * With this inverter:
 *   GPIO26 LOW  -> BC547 OFF -> Relay IN HIGH -> Relay ON
 *   GPIO26 HIGH -> BC547 ON  -> Relay IN LOW  -> Relay OFF
 *
 * ==========================================================
 * FAN HARDWARE — BC547 + MOSFET INVERTED DRIVER
 * ==========================================================
 *
 *   GPIO18 LOW  -> Fan ON
 *   GPIO18 HIGH -> Fan OFF
 *
 * ==========================================================
 * IMPORTANT
 * ==========================================================
 *
 * - MQ-6 value is an ADC/relative reading, not calibrated ppm.
 * - If MQ-6 is powered from 5V, use a divider before GPIO34.
 * - Use an adequate external supply for the MG90S and fan.
 * - Keep all low-voltage grounds common.
 * - Keep mains wiring isolated from the low-voltage circuit.
 ************************************************************/

#define BLYNK_PRINT Serial

#include "config.h"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <ESP32Servo.h>


// ==========================================================
// PIN CONFIGURATION
// ==========================================================

#define MQ6_PIN         34
#define DHT_PIN          4
#define FLAME_PIN       27

#define OLED_SDA        21
#define OLED_SCL        22

#define RELAY_PIN       26
#define SERVO_PIN       13

#define GREEN_LED_PIN   25
#define RED_LED_PIN     33
#define BLUE_LED_PIN    32

#define BUZZER_PIN      14
#define FAN_CTRL_PIN    18


// ==========================================================
// SENSOR / DISPLAY CONFIGURATION
// ==========================================================

#define DHTTYPE DHT11

#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define OLED_ADDRESS    0x3C


// ==========================================================
// OBJECTS
// ==========================================================

DHT dht(DHT_PIN, DHTTYPE);

Servo gasServo;

Adafruit_SSD1306 display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    OLED_RESET
);

BlynkTimer timer;


// ==========================================================
// THRESHOLDS
// ==========================================================

// MQ-6 ADC threshold only.
// NOT a calibrated LPG ppm value.
int GAS_THRESHOLD = 1800;

// Demonstration temperature threshold.
float TEMPERATURE_THRESHOLD = 50.0;


// ==========================================================
// SENSOR POLARITY
// ==========================================================

// Typical flame module:
// LOW = flame detected
bool FLAME_ACTIVE_LOW = true;


// ==========================================================
// SERVO POSITIONS
// ==========================================================

const int SERVO_NORMAL_ANGLE = 90;
const int SERVO_EMERGENCY_ANGLE = 0;


// ==========================================================
// SENSOR STATE
// ==========================================================

int gasRaw = 0;

float gasFiltered = 0.0f;

float temperature = NAN;
float humidity = NAN;

bool gasAlarm = false;
bool flameDetected = false;
bool temperatureAlarm = false;

bool sensorEmergency = false;


// ==========================================================
// SYSTEM STATE
// ==========================================================

bool testingMode = false;

bool emergencyMode = false;
bool previousEmergencyMode = false;


// ==========================================================
// MANUAL TEST CONTROLS
// ==========================================================

bool manualFan = false;
bool manualRelay = false;
bool manualBuzzer = false;

int manualServoAngle =
    SERVO_NORMAL_ANGLE;


// ==========================================================
// ACTUAL OUTPUT STATE
// ==========================================================

bool relayState = false;
bool fanState = false;

int servoAngle =
    SERVO_NORMAL_ANGLE;


// ==========================================================
// LED STATE
// ==========================================================

bool greenLedState = false;
bool emergencyFlashState = false;

unsigned long lastGreenBlink = 0;
unsigned long lastEmergencyFlash = 0;


// ==========================================================
// TIMERS
// ==========================================================

unsigned long lastOLEDUpdate = 0;

unsigned long lastWifiReconnectAttempt = 0;

unsigned long lastBlynkReconnectAttempt = 0;


// ==========================================================
// BLYNK EVENT
// ==========================================================

bool eventPending = false;
String pendingEventMessage = "";


// ==========================================================
// FUNCTION DECLARATIONS
// ==========================================================

void readFastSensors();
void readDHT();

void evaluateSystemState();
void applyOutputs();

void updateLEDs();
void updateOLED();

void sendBlynkData();
void maintainConnections();

void triggerEmergencyEvent();

String getAlarmCause();


// ==========================================================
// BLYNK INPUT CALLBACKS
// ==========================================================

// ----------------------------------------------------------
// TESTING MODE
// ----------------------------------------------------------

BLYNK_WRITE(V12)
{
    bool newTestingMode =
        param.asInt() != 0;

    // No change
    if (newTestingMode == testingMode)
    {
        return;
    }

    testingMode =
        newTestingMode;


    // ------------------------------------------------------
    // ENTER TESTING MODE
    // ------------------------------------------------------

    if (testingMode)
    {
        Serial.println();
        Serial.println(
            "================================"
        );

        Serial.println(
            "TESTING MODE ACTIVATED"
        );

        Serial.println(
            "Automatic safety control is OFF"
        );

        Serial.println(
            "================================"
        );


        // Suppress any pending automatic
        // emergency notification while testing.

        eventPending = false;


        // Force effective emergency state OFF
        // while testing.

        emergencyMode = false;

        previousEmergencyMode = false;


        // Apply manual test outputs.

        applyOutputs();
    }


    // ------------------------------------------------------
    // EXIT TESTING MODE
    // ------------------------------------------------------

    else
    {
        Serial.println();
        Serial.println(
            "================================"
        );

        Serial.println(
            "AUTOMATIC SECURITY MODE"
        );

        Serial.println(
            "Automatic safety control restored"
        );

        Serial.println(
            "================================"
        );


        // Start from a non-emergency previous state.
        // If a hazard is currently present, the next
        // evaluation immediately enters emergency mode.

        previousEmergencyMode = false;

        evaluateSystemState();
    }
}


// ----------------------------------------------------------
// MANUAL FAN
// ----------------------------------------------------------

BLYNK_WRITE(V13)
{
    manualFan =
        param.asInt() != 0;

    Serial.print(
        "Manual Fan: "
    );

    Serial.println(
        manualFan ? "ON" : "OFF"
    );


    if (testingMode)
    {
        applyOutputs();
    }
}


// ----------------------------------------------------------
// MANUAL RELAY
// ----------------------------------------------------------

BLYNK_WRITE(V14)
{
    manualRelay =
        param.asInt() != 0;

    Serial.print(
        "Manual Relay: "
    );

    Serial.println(
        manualRelay ? "ON" : "OFF"
    );


    if (testingMode)
    {
        applyOutputs();
    }
}


// ----------------------------------------------------------
// MANUAL SERVO
// ----------------------------------------------------------

BLYNK_WRITE(V15)
{
    manualServoAngle =
        constrain(
            param.asInt(),
            0,
            180
        );

    Serial.print(
        "Manual Servo Angle: "
    );

    Serial.println(
        manualServoAngle
    );


    if (testingMode)
    {
        applyOutputs();
    }
}


// ----------------------------------------------------------
// MANUAL BUZZER
// ----------------------------------------------------------

BLYNK_WRITE(V16)
{
    manualBuzzer =
        param.asInt() != 0;

    Serial.print(
        "Manual Buzzer: "
    );

    Serial.println(
        manualBuzzer ? "ON" : "OFF"
    );


    if (testingMode)
    {
        applyOutputs();
    }
}


// ==========================================================
// BLYNK CONNECTED
// ==========================================================

BLYNK_CONNECTED()
{
    Serial.println(
        "Blynk connected."
    );

    // Restore dashboard control values.

    Blynk.syncVirtual(V12);
    Blynk.syncVirtual(V13);
    Blynk.syncVirtual(V14);
    Blynk.syncVirtual(V15);
    Blynk.syncVirtual(V16);
}


// ==========================================================
// SETUP
// ==========================================================

void setup()
{
    Serial.begin(115200);

    delay(300);


    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "       LPG SECURITY SYSTEM"
    );

    Serial.println(
        "========================================"
    );


    // ======================================================
    // GPIO
    // ======================================================

    pinMode(
        FLAME_PIN,
        INPUT_PULLUP
    );

    pinMode(
        RELAY_PIN,
        OUTPUT
    );

    pinMode(
        GREEN_LED_PIN,
        OUTPUT
    );

    pinMode(
        RED_LED_PIN,
        OUTPUT
    );

    pinMode(
        BLUE_LED_PIN,
        OUTPUT
    );

    pinMode(
        BUZZER_PIN,
        OUTPUT
    );

    pinMode(
        FAN_CTRL_PIN,
        OUTPUT
    );


    // ======================================================
    // STARTUP OUTPUTS
    // ======================================================

    // Single-BC547 relay:
    // HIGH -> relay OFF

    digitalWrite(
        RELAY_PIN,
        HIGH
    );

    relayState = false;


    // Green OFF
    digitalWrite(
        GREEN_LED_PIN,
        LOW
    );


    // Emergency LEDs OFF

    digitalWrite(
        RED_LED_PIN,
        LOW
    );

    digitalWrite(
        BLUE_LED_PIN,
        LOW
    );


    // Buzzer OFF

    digitalWrite(
        BUZZER_PIN,
        LOW
    );


    // Fan OFF
    // Inverted driver: HIGH = OFF

    digitalWrite(
        FAN_CTRL_PIN,
        HIGH
    );

    fanState = false;


    // ======================================================
    // ADC
    // ======================================================

    analogReadResolution(12);

    analogSetPinAttenuation(
        MQ6_PIN,
        ADC_11db
    );


    // ======================================================
    // DHT11
    // ======================================================

    dht.begin();


    // ======================================================
    // SERVO
    // ======================================================

    gasServo.setPeriodHertz(50);

    gasServo.attach(
        SERVO_PIN,
        500,
        2400
    );

    servoAngle =
        SERVO_NORMAL_ANGLE;

    manualServoAngle =
        SERVO_NORMAL_ANGLE;

    gasServo.write(
        servoAngle
    );


    // ======================================================
    // OLED
    // ======================================================

    Wire.begin(
        OLED_SDA,
        OLED_SCL
    );


    if (
        !display.begin(
            SSD1306_SWITCHCAPVCC,
            OLED_ADDRESS
        )
    )
    {
        Serial.println(
            "ERROR: OLED initialization failed."
        );
    }
    else
    {
        display.clearDisplay();

        display.setTextSize(1);

        display.setTextColor(
            SSD1306_WHITE
        );

        display.setCursor(
            0,
            0
        );

        display.println(
            "LPG SECURITY"
        );

        display.setCursor(
            0,
            16
        );

        display.println(
            "SYSTEM STARTING..."
        );

        display.display();
    }


    // ======================================================
    // BLYNK CONFIGURATION
    // ======================================================

    Blynk.config(
        BLYNK_AUTH_TOKEN
    );


    // ======================================================
    // WIFI
    //
    // NON-BLOCKING:
    // Local safety logic starts immediately.
    // ======================================================

    WiFi.mode(
        WIFI_STA
    );

    WiFi.setAutoReconnect(
        true
    );

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );


    Serial.println(
        "Wi-Fi connection started."
    );


    // ======================================================
    // INITIAL LOCAL SENSOR STATE
    // ======================================================

    readFastSensors();

    readDHT();

    evaluateSystemState();


    // ======================================================
    // TIMERS
    // ======================================================

    // MQ-6 + Flame
    timer.setInterval(
        200L,
        readFastSensors
    );


    // DHT11
    timer.setInterval(
        2000L,
        readDHT
    );


    // Blynk telemetry
    timer.setInterval(
        2000L,
        sendBlynkData
    );


    // OLED
    timer.setInterval(
        500L,
        updateOLED
    );


    // LED management
    timer.setInterval(
        100L,
        updateLEDs
    );


    // Wi-Fi / Blynk maintenance
    timer.setInterval(
        5000L,
        maintainConnections
    );


    Serial.println(
        "Local system started."
    );

    Serial.println(
        "========================================"
    );
}


// ==========================================================
// LOOP
// ==========================================================

void loop()
{
    // ------------------------------------------------------
    // Blynk
    // ------------------------------------------------------

    if (
        WiFi.status() ==
        WL_CONNECTED
    )
    {
        Blynk.run();
    }


    // ------------------------------------------------------
    // Timed tasks
    // ------------------------------------------------------

    timer.run();
}


// ==========================================================
// READ FAST SENSORS
// ==========================================================

void readFastSensors()
{
    // ======================================================
    // MQ-6
    // ======================================================

    gasRaw =
        analogRead(
            MQ6_PIN
        );


    // ------------------------------------------------------
    // Simple moving filter
    // ------------------------------------------------------

    if (
        gasFiltered == 0.0f
    )
    {
        gasFiltered =
            gasRaw;
    }
    else
    {
        gasFiltered =
            (
                0.7f *
                gasFiltered
            )
            +
            (
                0.3f *
                gasRaw
            );
    }


    // ======================================================
    // FLAME
    // ======================================================

    const int flameState =
        digitalRead(
            FLAME_PIN
        );


    if (
        FLAME_ACTIVE_LOW
    )
    {
        flameDetected =
            (
                flameState ==
                LOW
            );
    }
    else
    {
        flameDetected =
            (
                flameState ==
                HIGH
            );
    }


    // Update system
    evaluateSystemState();
}


// ==========================================================
// READ DHT11
// ==========================================================

void readDHT()
{
    const float newHumidity =
        dht.readHumidity();

    const float newTemperature =
        dht.readTemperature();


    if (
        !isnan(newHumidity)
    )
    {
        humidity =
            newHumidity;
    }


    if (
        !isnan(newTemperature)
    )
    {
        temperature =
            newTemperature;
    }


    // Update system
    evaluateSystemState();
}


// ==========================================================
// SYSTEM STATE EVALUATION
// ==========================================================

void evaluateSystemState()
{
    // ======================================================
    // SENSOR CONDITIONS
    // ======================================================

    gasAlarm =
        (
            gasFiltered >=
            GAS_THRESHOLD
        );


    temperatureAlarm =
        (
            !isnan(temperature) &&
            temperature >=
            TEMPERATURE_THRESHOLD
        );


    // ======================================================
    // INDIVIDUAL ALARM COMBINATION
    // ======================================================

    sensorEmergency =
        gasAlarm ||
        flameDetected ||
        temperatureAlarm;


    // ======================================================
    // EFFECTIVE EMERGENCY MODE
    //
    // Testing Mode intentionally overrides automatic
    // emergency actuation.
    // ======================================================

    if (testingMode)
    {
        emergencyMode = false;
    }
    else
    {
        emergencyMode =
            sensorEmergency;
    }


    // ======================================================
    // NORMAL -> EMERGENCY
    //
    // Only possible outside Testing Mode.
    // ======================================================

    if (
        emergencyMode &&
        !previousEmergencyMode
    )
    {
        Serial.println();
        Serial.println(
            "!!! EMERGENCY ACTIVATED !!!"
        );

        Serial.print(
            "Cause: "
        );

        Serial.println(
            getAlarmCause()
        );


        // One notification per automatic emergency activation.
        // Snapshot the event now so it can still be reported if
        // the hazard clears before Blynk reconnects.
        pendingEventMessage = "LPG SECURITY EMERGENCY";
        pendingEventMessage += " | Cause: ";
        pendingEventMessage += getAlarmCause();
        pendingEventMessage += " | Gas: ";
        pendingEventMessage += String((int)gasFiltered);
        pendingEventMessage += " | Temp: ";
        if (!isnan(temperature))
        {
            pendingEventMessage += String(temperature, 1);
            pendingEventMessage += " C";
        }
        else
        {
            pendingEventMessage += "N/A";
        }
        eventPending = true;
    }


    // ======================================================
    // EMERGENCY -> NORMAL
    // ======================================================

    if (
        !emergencyMode &&
        previousEmergencyMode
    )
    {
        Serial.println(
            "Emergency cleared."
        );
    }


    previousEmergencyMode =
        emergencyMode;


    // Apply the appropriate outputs.
    applyOutputs();
}


// ==========================================================
// OUTPUT CONTROL
// ==========================================================

void applyOutputs()
{
    // ======================================================
    // TESTING MODE
    // ======================================================

    if (testingMode)
    {
        // --------------------------------------------------
        // MANUAL RELAY
        //
        // Single BC547 inverter:
        //
        // manualRelay = true
        // -> GPIO LOW
        // -> BC547 OFF
        // -> Relay IN pulled HIGH
        // -> Relay ON
        // --------------------------------------------------

        digitalWrite(
            RELAY_PIN,
            manualRelay ?
            LOW :
            HIGH
        );

        relayState =
            manualRelay;


        // --------------------------------------------------
        // MANUAL FAN
        //
        // Inverted driver:
        // LOW = ON
        // --------------------------------------------------

        digitalWrite(
            FAN_CTRL_PIN,
            manualFan ?
            LOW :
            HIGH
        );

        fanState =
            manualFan;


        // --------------------------------------------------
        // MANUAL SERVO
        // --------------------------------------------------

        if (
            servoAngle !=
            manualServoAngle
        )
        {
            servoAngle =
                manualServoAngle;

            gasServo.write(
                servoAngle
            );
        }


        // --------------------------------------------------
        // MANUAL BUZZER
        // --------------------------------------------------

        digitalWrite(
            BUZZER_PIN,
            manualBuzzer ?
            HIGH :
            LOW
        );


        // Emergency outputs remain OFF.
        // The dashboard controls are authoritative
        // while Testing Mode is active.

        return;
    }


    // ======================================================
    // AUTOMATIC SECURITY MODE
    // ======================================================

    if (emergencyMode)
    {
        // --------------------------------------------------
        // RELAY OFF
        // --------------------------------------------------

        digitalWrite(
            RELAY_PIN,
            HIGH
        );

        relayState =
            false;


        // --------------------------------------------------
        // FAN ON
        // --------------------------------------------------

        digitalWrite(
            FAN_CTRL_PIN,
            LOW
        );

        fanState =
            true;


        // --------------------------------------------------
        // SERVO -> 0°
        // --------------------------------------------------

        if (
            servoAngle !=
            SERVO_EMERGENCY_ANGLE
        )
        {
            servoAngle =
                SERVO_EMERGENCY_ANGLE;

            gasServo.write(
                servoAngle
            );
        }


        // --------------------------------------------------
        // BUZZER ON
        // --------------------------------------------------

        digitalWrite(
            BUZZER_PIN,
            HIGH
        );
    }
    else
    {
        // --------------------------------------------------
        // NORMAL
        // --------------------------------------------------

        // Relay ON:
        // GPIO LOW -> BC547 OFF
        // -> collector pulled HIGH
        // -> relay IN HIGH

        digitalWrite(
            RELAY_PIN,
            LOW
        );

        relayState =
            true;


        // Fan OFF

        digitalWrite(
            FAN_CTRL_PIN,
            HIGH
        );

        fanState =
            false;


        // Servo -> 90°

        if (
            servoAngle !=
            SERVO_NORMAL_ANGLE
        )
        {
            servoAngle =
                SERVO_NORMAL_ANGLE;

            gasServo.write(
                servoAngle
            );
        }


        // Buzzer OFF

        digitalWrite(
            BUZZER_PIN,
            LOW
        );
    }
}


// ==========================================================
// LED MANAGEMENT
// ==========================================================

void updateLEDs()
{
    const unsigned long now =
        millis();


    // ======================================================
    // TESTING MODE
    // ======================================================

    if (testingMode)
    {
        // --------------------------------------------------
        // Green LED is still the Wi-Fi indicator.
        // --------------------------------------------------

        digitalWrite(
            RED_LED_PIN,
            LOW
        );

        digitalWrite(
            BLUE_LED_PIN,
            LOW
        );


        if (
            WiFi.status() ==
            WL_CONNECTED
        )
        {
            // Wi-Fi connected -> solid green.

            digitalWrite(
                GREEN_LED_PIN,
                HIGH
            );

            greenLedState =
                true;
        }
        else
        {
            // Wi-Fi not connected -> blinking green.

            if (
                now -
                lastGreenBlink >=
                500
            )
            {
                lastGreenBlink =
                    now;

                greenLedState =
                    !greenLedState;

                digitalWrite(
                    GREEN_LED_PIN,
                    greenLedState
                );
            }
        }

        return;
    }


    // ======================================================
    // EMERGENCY
    // ======================================================

    if (emergencyMode)
    {
        // Green OFF

        digitalWrite(
            GREEN_LED_PIN,
            LOW
        );


        // Red / Blue alternating

        if (
            now -
            lastEmergencyFlash >=
            400
        )
        {
            lastEmergencyFlash =
                now;

            emergencyFlashState =
                !emergencyFlashState;


            if (
                emergencyFlashState
            )
            {
                digitalWrite(
                    RED_LED_PIN,
                    HIGH
                );

                digitalWrite(
                    BLUE_LED_PIN,
                    LOW
                );
            }
            else
            {
                digitalWrite(
                    RED_LED_PIN,
                    LOW
                );

                digitalWrite(
                    BLUE_LED_PIN,
                    HIGH
                );
            }
        }

        return;
    }


    // ======================================================
    // NORMAL
    // ======================================================

    digitalWrite(
        RED_LED_PIN,
        LOW
    );

    digitalWrite(
        BLUE_LED_PIN,
        LOW
    );


    if (
        WiFi.status() ==
        WL_CONNECTED
    )
    {
        // Wi-Fi connected -> SOLID GREEN.

        digitalWrite(
            GREEN_LED_PIN,
            HIGH
        );

        greenLedState =
            true;
    }
    else
    {
        // Wi-Fi connecting/disconnected -> BLINK GREEN.

        if (
            now -
            lastGreenBlink >=
            500
        )
        {
            lastGreenBlink =
                now;

            greenLedState =
                !greenLedState;

            digitalWrite(
                GREEN_LED_PIN,
                greenLedState
            );
        }
    }
}


// ==========================================================
// OLED
// ==========================================================

void updateOLED()
{
    if (
        millis() -
        lastOLEDUpdate <
        400
    )
    {
        return;
    }

    lastOLEDUpdate =
        millis();


    display.clearDisplay();

    display.setTextSize(1);

    display.setTextColor(
        SSD1306_WHITE
    );


    // ======================================================
    // TITLE
    // ======================================================

    display.setCursor(
        0,
        0
    );

    display.println(
        "LPG SECURITY"
    );


    // Wi-Fi indicator

    display.setCursor(
        115,
        0
    );

    display.print(
        WiFi.status() ==
        WL_CONNECTED ?
        "W" :
        "-"
    );


    // ======================================================
    // STATUS
    // ======================================================

    display.setCursor(
        0,
        10
    );


    if (testingMode)
    {
        display.println(
            "STATUS: TEST MODE"
        );
    }
    else if (emergencyMode)
    {
        display.println(
            "STATUS: EMERGENCY"
        );
    }
    else
    {
        display.println(
            "STATUS: NORMAL"
        );
    }


    // ======================================================
    // GAS
    // ======================================================

    display.setCursor(
        0,
        20
    );

    display.print(
        "GAS : "
    );

    display.println(
        (int)gasFiltered
    );


    // ======================================================
    // TEMPERATURE
    // ======================================================

    display.setCursor(
        0,
        30
    );

    display.print(
        "TEMP: "
    );


    if (
        !isnan(temperature)
    )
    {
        display.print(
            temperature,
            1
        );

        display.println(
            " C"
        );
    }
    else
    {
        display.println(
            "N/A"
        );
    }


    // ======================================================
    // HUMIDITY
    // ======================================================

    display.setCursor(
        0,
        40
    );

    display.print(
        "HUM : "
    );


    if (
        !isnan(humidity)
    )
    {
        display.print(
            humidity,
            0
        );

        display.println(
            " %"
        );
    }
    else
    {
        display.println(
            "N/A"
        );
    }


    // ======================================================
    // FLAME
    // ======================================================

    display.setCursor(
        74,
        40
    );

    display.print(
        "F:"
    );

    display.print(
        flameDetected ?
        "YES" :
        "NO"
    );


    // ======================================================
    // OUTPUT STATUS
    // ======================================================

    display.setCursor(
        0,
        50
    );

    display.print(
        "R:"
    );

    display.print(
        relayState ?
        "ON " :
        "OFF"
    );

    display.print(
        " F:"
    );

    display.print(
        fanState ?
        "ON " :
        "OFF"
    );

    display.print(
        " S:"
    );

    display.print(
        servoAngle
    );

    display.print(
        "d"
    );


    display.display();
}


// ==========================================================
// BLYNK TELEMETRY
// ==========================================================

void sendBlynkData()
{
    if (
        !Blynk.connected()
    )
    {
        return;
    }


    // ------------------------------------------------------
    // Sensor data
    // ------------------------------------------------------

    Blynk.virtualWrite(
        V0,
        gasRaw
    );

    Blynk.virtualWrite(
        V1,
        (int)gasFiltered
    );


    if (
        !isnan(temperature)
    )
    {
        Blynk.virtualWrite(
            V2,
            temperature
        );
    }


    if (
        !isnan(humidity)
    )
    {
        Blynk.virtualWrite(
            V3,
            humidity
        );
    }


    // Fire Detection
    Blynk.virtualWrite(
        V4,
        flameDetected ?
        1 :
        0
    );


    // Effective emergency mode
    Blynk.virtualWrite(
        V5,
        emergencyMode ?
        1 :
        0
    );


    // Actual output states
    Blynk.virtualWrite(
        V6,
        relayState ?
        1 :
        0
    );

    Blynk.virtualWrite(
        V7,
        fanState ?
        1 :
        0
    );

    Blynk.virtualWrite(
        V8,
        servoAngle
    );


    // Wi-Fi status
    Blynk.virtualWrite(
        V9,
        WiFi.status() ==
        WL_CONNECTED ?
        1 :
        0
    );


    // Alarm cause
    Blynk.virtualWrite(
        V10,
        getAlarmCause()
    );


    // Blynk connection state
    Blynk.virtualWrite(
        V11,
        Blynk.connected() ?
        1 :
        0
    );


    // LPG leakage
    Blynk.virtualWrite(
        V17,
        gasAlarm ?
        1 :
        0
    );
}


// ==========================================================
// CONNECTION MAINTENANCE
// ==========================================================

void maintainConnections()
{
    // ======================================================
    // WIFI
    // ======================================================

    if (
        WiFi.status() !=
        WL_CONNECTED
    )
    {
        if (
            millis() -
            lastWifiReconnectAttempt >=
            10000
        )
        {
            lastWifiReconnectAttempt =
                millis();


            Serial.println(
                "Attempting Wi-Fi reconnect..."
            );


            WiFi.disconnect();

            WiFi.begin(
                WIFI_SSID,
                WIFI_PASSWORD
            );
        }


        return;
    }


    // ======================================================
    // BLYNK
    // ======================================================

    if (
        !Blynk.connected()
    )
    {
        if (
            millis() -
            lastBlynkReconnectAttempt >=
            10000
        )
        {
            lastBlynkReconnectAttempt =
                millis();


            Serial.println(
                "Attempting Blynk reconnect..."
            );


            Blynk.connect(
                3000
            );
        }


        return;
    }


    // ======================================================
    // PENDING EMERGENCY EVENT
    //
    // Never send while Testing Mode is active.
    // ======================================================

    if (
        eventPending &&
        !testingMode
    )
    {
        triggerEmergencyEvent();

        eventPending =
            false;

        pendingEventMessage = "";
    }
}


// ==========================================================
// BLYNK EMERGENCY EVENT
// ==========================================================

void triggerEmergencyEvent()
{
    Serial.println(
        "Sending Blynk emergency event..."
    );

    // Must match the Blynk Event Code exactly.
    Blynk.logEvent(
        "lpg_emergency",
        pendingEventMessage
    );
}


// ==========================================================
// ALARM CAUSE
// ==========================================================

String getAlarmCause()
{
    String cause = "";


    if (gasAlarm)
    {
        cause +=
            "GAS";
    }


    if (flameDetected)
    {
        if (
            cause.length() > 0
        )
        {
            cause +=
                "+";
        }

        cause +=
            "FLAME";
    }


    if (temperatureAlarm)
    {
        if (
            cause.length() > 0
        )
        {
            cause +=
                "+";
        }

        cause +=
            "TEMP";
    }


    if (
        cause.length() == 0
    )
    {
        return "NONE";
    }


    return cause;
}
