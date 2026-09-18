/************************************************************
 * LPG SECURITY SYSTEM
 * ESP32 + Blynk + MQ-6 + DHT11 + Flame Sensor
 * OLED + MG90S Servo + Relay + Fan + LEDs + Buzzer
 *
 * Relay interface (single BC547 inverter):
 *   ESP32 GPIO26 -> 1k -> BC547 base
 *   BC547 base -> 4.7k -> GND
 *   BC547 collector -> Relay IN
 *   BC547 collector -> 10k -> +5V
 *   BC547 emitter -> GND
 *
 * Relay logic with this circuit:
 *   GPIO26 LOW  -> BC547 OFF -> Relay IN HIGH (~5V) -> RELAY ON
 *   GPIO26 HIGH -> BC547 ON  -> Relay IN LOW        -> RELAY OFF
 *
 * Fan driver:
 *   GPIO18 LOW  -> FAN ON
 *   GPIO18 HIGH -> FAN OFF
 *
 * System logic:
 *   NORMAL:
 *     Relay ON, Fan OFF, Servo 90, Buzzer OFF
 *     Green LED = solid when Wi-Fi connected
 *     Green LED = blinking when Wi-Fi is not connected
 *
 *   EMERGENCY:
 *     Relay OFF, Fan ON, Servo 0, Buzzer ON
 *     Green LED OFF
 *     Red/Blue LEDs alternate
 *     Blynk event = lpg_emergency
 *
 * IMPORTANT:
 * - MQ-6 value is an ADC reading, not calibrated LPG ppm.
 * - Use a voltage divider on MQ-6 AO before ESP32 GPIO34
 *   when the MQ-6 module is powered from 5V.
 * - Keep all grounds common.
 * - Do not connect mains wiring to a breadboard.
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
// PINS
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
// SENSOR / DISPLAY CONFIG
// ==========================================================

#define DHTTYPE DHT11

#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define OLED_ADDRESS    0x3C

DHT dht(DHT_PIN, DHTTYPE);
Servo gasServo;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
BlynkTimer timer;

// ==========================================================
// TUNABLE THRESHOLDS
// ==========================================================

// ADC threshold only. NOT LPG ppm.
int GAS_THRESHOLD = 1800;

// Demonstration threshold.
float TEMPERATURE_THRESHOLD = 50.0;

// Set true if your flame module output goes LOW when flame is detected.
bool FLAME_ACTIVE_LOW = true;

const int SERVO_NORMAL_ANGLE = 90;
const int SERVO_EMERGENCY_ANGLE = 0;

// ==========================================================
// STATE
// ==========================================================

int gasRaw = 0;
float gasFiltered = 0.0f;

float temperature = NAN;
float humidity = NAN;

bool gasAlarm = false;
bool flameDetected = false;
bool temperatureAlarm = false;

bool emergencyMode = false;
bool previousEmergencyMode = false;

bool relayState = false;
bool fanState = false;
int servoAngle = SERVO_NORMAL_ANGLE;

bool greenLedState = false;
bool emergencyFlashState = false;

unsigned long lastGreenBlink = 0;
unsigned long lastEmergencyFlash = 0;
unsigned long lastOLEDUpdate = 0;
unsigned long lastWifiReconnectAttempt = 0;
unsigned long lastBlynkReconnectAttempt = 0;

bool eventPending = false;

// ==========================================================
// FUNCTION DECLARATIONS
// ==========================================================

void readFastSensors();
void readDHT();
void evaluateEmergencyState();
void applyOutputs();
void updateLEDs();
void updateOLED();
void sendBlynkData();
void maintainConnections();
void triggerEmergencyEvent();
String getAlarmCause();

// ==========================================================
// SETUP
// ==========================================================

void setup()
{
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println("========================================");
    Serial.println("       LPG SECURITY SYSTEM");
    Serial.println("========================================");

    // ------------------------------
    // GPIO
    // ------------------------------

    pinMode(FLAME_PIN, INPUT_PULLUP);
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(RED_LED_PIN, OUTPUT);
    pinMode(BLUE_LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(FAN_CTRL_PIN, OUTPUT);

    // Safe low-level startup.
    // Single-BC547 relay stage: HIGH = relay OFF.
    digitalWrite(RELAY_PIN, HIGH);
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(BLUE_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(FAN_CTRL_PIN, HIGH); // inverted fan driver: HIGH = OFF

    // ------------------------------
    // ADC
    // ------------------------------

    analogReadResolution(12);
    analogSetPinAttenuation(MQ6_PIN, ADC_11db);

    // ------------------------------
    // Sensors
    // ------------------------------

    dht.begin();

    // ------------------------------
    // Servo
    // ------------------------------

    gasServo.setPeriodHertz(50);
    gasServo.attach(SERVO_PIN, 500, 2400);
    gasServo.write(SERVO_NORMAL_ANGLE);

    // ------------------------------
    // OLED
    // ------------------------------

    Wire.begin(OLED_SDA, OLED_SCL);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS))
    {
        Serial.println("ERROR: OLED initialization failed.");
    }
    else
    {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println("LPG SECURITY");
        display.setCursor(0, 16);
        display.println("SYSTEM STARTING...");
        display.display();
    }

    // ------------------------------
    // Blynk configuration
    // ------------------------------

    // Configure the Blynk server once, even if Wi-Fi is initially unavailable.
    Blynk.config(BLYNK_AUTH_TOKEN);

    // ------------------------------
    // Start Wi-Fi WITHOUT BLOCKING
    // ------------------------------

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.println("Wi-Fi connection started.");

    // ------------------------------
    // Local system starts immediately.
    // Safety logic must not wait for Wi-Fi.
    // ------------------------------

    readFastSensors();
    readDHT();
    evaluateEmergencyState();

    // ------------------------------
    // Timers
    // ------------------------------

    timer.setInterval(200L, readFastSensors);
    timer.setInterval(2000L, readDHT);
    timer.setInterval(2000L, sendBlynkData);
    timer.setInterval(500L, updateOLED);
    timer.setInterval(100L, updateLEDs);
    timer.setInterval(5000L, maintainConnections);

    Serial.println("Local safety system started.");
    Serial.println("========================================");
}

// ==========================================================
// LOOP
// ==========================================================

void loop()
{
    // Blynk communication when Wi-Fi is available.
    if (WiFi.status() == WL_CONNECTED)
    {
        Blynk.run();
    }

    timer.run();
}

// ==========================================================
// READ MQ-6 + FLAME
// ==========================================================

void readFastSensors()
{
    gasRaw = analogRead(MQ6_PIN);

    if (gasFiltered == 0.0f)
    {
        gasFiltered = gasRaw;
    }
    else
    {
        gasFiltered = (0.7f * gasFiltered) + (0.3f * gasRaw);
    }

    const int flameState = digitalRead(FLAME_PIN);

    flameDetected = FLAME_ACTIVE_LOW
                        ? (flameState == LOW)
                        : (flameState == HIGH);

    evaluateEmergencyState();
}

// ==========================================================
// READ DHT11
// ==========================================================

void readDHT()
{
    const float newHumidity = dht.readHumidity();
    const float newTemperature = dht.readTemperature();

    if (!isnan(newHumidity))
    {
        humidity = newHumidity;
    }

    if (!isnan(newTemperature))
    {
        temperature = newTemperature;
    }

    evaluateEmergencyState();
}

// ==========================================================
// EMERGENCY DECISION
// ==========================================================

void evaluateEmergencyState()
{
    gasAlarm = (gasFiltered >= GAS_THRESHOLD);

    temperatureAlarm = !isnan(temperature) &&
                       (temperature >= TEMPERATURE_THRESHOLD);

    // ANY ONE alarm source activates emergency mode.
    emergencyMode = gasAlarm || flameDetected || temperatureAlarm;

    // Detect NORMAL -> EMERGENCY exactly once.
    if (emergencyMode && !previousEmergencyMode)
    {
        Serial.println();
        Serial.println("!!! EMERGENCY ACTIVATED !!!");
        Serial.print("CAUSE: ");
        Serial.println(getAlarmCause());

        // Request one Blynk notification.
        // If offline, it remains pending until Blynk is available.
        eventPending = true;
    }

    // Detect EMERGENCY -> NORMAL.
    if (!emergencyMode && previousEmergencyMode)
    {
        Serial.println("Emergency cleared.");
    }

    previousEmergencyMode = emergencyMode;

    applyOutputs();
}

// ==========================================================
// PHYSICAL OUTPUT CONTROL
// ==========================================================

void applyOutputs()
{
    if (emergencyMode)
    {
        // ----------------------------------------------
        // RELAY OFF
        // GPIO HIGH -> BC547 ON -> Relay IN LOW
        // ----------------------------------------------
        digitalWrite(RELAY_PIN, HIGH);
        relayState = false;

        // ----------------------------------------------
        // FAN ON (inverted driver)
        // ----------------------------------------------
        digitalWrite(FAN_CTRL_PIN, LOW);
        fanState = true;

        // ----------------------------------------------
        // SERVO -> 0 deg
        // ----------------------------------------------
        if (servoAngle != SERVO_EMERGENCY_ANGLE)
        {
            servoAngle = SERVO_EMERGENCY_ANGLE;
            gasServo.write(servoAngle);
        }

        // ----------------------------------------------
        // BUZZER ON
        // ----------------------------------------------
        digitalWrite(BUZZER_PIN, HIGH);
    }
    else
    {
        // ----------------------------------------------
        // RELAY ON
        // GPIO LOW -> BC547 OFF -> 10k pulls IN HIGH
        // ----------------------------------------------
        digitalWrite(RELAY_PIN, LOW);
        relayState = true;

        // ----------------------------------------------
        // FAN OFF
        // ----------------------------------------------
        digitalWrite(FAN_CTRL_PIN, HIGH);
        fanState = false;

        // ----------------------------------------------
        // SERVO -> 90 deg
        // ----------------------------------------------
        if (servoAngle != SERVO_NORMAL_ANGLE)
        {
            servoAngle = SERVO_NORMAL_ANGLE;
            gasServo.write(servoAngle);
        }

        // ----------------------------------------------
        // BUZZER OFF
        // ----------------------------------------------
        digitalWrite(BUZZER_PIN, LOW);
    }
}

// ==========================================================
// LED MANAGEMENT
// ==========================================================

void updateLEDs()
{
    const unsigned long now = millis();

    if (emergencyMode)
    {
        // Emergency always overrides Wi-Fi status LED.
        digitalWrite(GREEN_LED_PIN, LOW);

        if (now - lastEmergencyFlash >= 400)
        {
            lastEmergencyFlash = now;
            emergencyFlashState = !emergencyFlashState;

            if (emergencyFlashState)
            {
                digitalWrite(RED_LED_PIN, HIGH);
                digitalWrite(BLUE_LED_PIN, LOW);
            }
            else
            {
                digitalWrite(RED_LED_PIN, LOW);
                digitalWrite(BLUE_LED_PIN, HIGH);
            }
        }

        return;
    }

    // Normal mode: emergency LEDs off.
    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(BLUE_LED_PIN, LOW);

    if (WiFi.status() == WL_CONNECTED)
    {
        // Wi-Fi connected -> GREEN SOLID ON.
        digitalWrite(GREEN_LED_PIN, HIGH);
        greenLedState = true;
    }
    else
    {
        // Connecting / disconnected -> GREEN BLINKING.
        if (now - lastGreenBlink >= 500)
        {
            lastGreenBlink = now;
            greenLedState = !greenLedState;
            digitalWrite(GREEN_LED_PIN, greenLedState);
        }
    }
}

// ==========================================================
// OLED
// ==========================================================

void updateOLED()
{
    if (millis() - lastOLEDUpdate < 400)
    {
        return;
    }

    lastOLEDUpdate = millis();

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.println("LPG SECURITY");

    display.setCursor(115, 0);
    display.print(WiFi.status() == WL_CONNECTED ? "W" : "-");

    display.setCursor(0, 10);
    display.println(emergencyMode ? "STATUS: EMERGENCY" : "STATUS: NORMAL");

    display.setCursor(0, 20);
    display.print("GAS : ");
    display.println((int)gasFiltered);

    display.setCursor(0, 30);
    display.print("TEMP: ");

    if (!isnan(temperature))
    {
        display.print(temperature, 1);
        display.println(" C");
    }
    else
    {
        display.println("N/A");
    }

    display.setCursor(0, 40);
    display.print("HUM : ");

    if (!isnan(humidity))
    {
        display.print(humidity, 0);
        display.println(" %");
    }
    else
    {
        display.println("N/A");
    }

    display.setCursor(74, 40);
    display.print("F:");
    display.print(flameDetected ? "YES" : "NO");

    display.setCursor(0, 50);
    display.print("R:");
    display.print(relayState ? "ON " : "OFF");

    display.print(" F:");
    display.print(fanState ? "ON " : "OFF");

    display.print(" S:");
    display.print(servoAngle);
    display.print("d");

    display.display();
}

// ==========================================================
// BLYNK TELEMETRY
// ==========================================================

void sendBlynkData()
{
    if (!Blynk.connected())
    {
        return;
    }

    Blynk.virtualWrite(V0, gasRaw);
    Blynk.virtualWrite(V1, (int)gasFiltered);

    if (!isnan(temperature))
    {
        Blynk.virtualWrite(V2, temperature);
    }

    if (!isnan(humidity))
    {
        Blynk.virtualWrite(V3, humidity);
    }

    Blynk.virtualWrite(V4, flameDetected ? 1 : 0);
    Blynk.virtualWrite(V5, emergencyMode ? 1 : 0);
    Blynk.virtualWrite(V6, relayState ? 1 : 0);
    Blynk.virtualWrite(V7, fanState ? 1 : 0);
    Blynk.virtualWrite(V8, servoAngle);
    Blynk.virtualWrite(V9, WiFi.status() == WL_CONNECTED ? 1 : 0);
    Blynk.virtualWrite(V10, getAlarmCause());
    Blynk.virtualWrite(V11, Blynk.connected() ? 1 : 0);
}

// ==========================================================
// CONNECTION MAINTENANCE
// ==========================================================

void maintainConnections()
{
    // ------------------------------------------------------
    // Wi-Fi disconnected
    // ------------------------------------------------------

    if (WiFi.status() != WL_CONNECTED)
    {
        if (millis() - lastWifiReconnectAttempt >= 10000)
        {
            lastWifiReconnectAttempt = millis();

            Serial.println("Attempting Wi-Fi reconnect...");

            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }

        return;
    }

    // ------------------------------------------------------
    // Wi-Fi connected; connect Blynk if needed.
    // ------------------------------------------------------

    if (!Blynk.connected())
    {
        if (millis() - lastBlynkReconnectAttempt >= 10000)
        {
            lastBlynkReconnectAttempt = millis();

            Serial.println("Attempting Blynk reconnect...");
            Blynk.connect(3000);
        }

        return;
    }

    // ------------------------------------------------------
    // Send pending emergency event once Blynk is online.
    // ------------------------------------------------------

    if (eventPending)
    {
        triggerEmergencyEvent();
        eventPending = false;
    }
}

// ==========================================================
// BLYNK EVENT
// ==========================================================

void triggerEmergencyEvent()
{
    String message = "LPG SECURITY EMERGENCY";

    message += " | Cause: ";
    message += getAlarmCause();

    message += " | Gas: ";
    message += String((int)gasFiltered);

    message += " | Temp: ";

    if (!isnan(temperature))
    {
        message += String(temperature, 1);
        message += " C";
    }
    else
    {
        message += "N/A";
    }

    Serial.println("Sending Blynk emergency event...");

    // Must match the Blynk Event Code exactly.
    Blynk.logEvent("lpg_emergency", message);
}

// ==========================================================
// ALARM CAUSE
// ==========================================================

String getAlarmCause()
{
    if (!emergencyMode)
    {
        return "NONE";
    }

    String cause = "";

    if (gasAlarm)
    {
        cause += "GAS";
    }

    if (flameDetected)
    {
        if (cause.length() > 0)
        {
            cause += "+";
        }

        cause += "FLAME";
    }

    if (temperatureAlarm)
    {
        if (cause.length() > 0)
        {
            cause += "+";
        }

        cause += "TEMP";
    }

    return cause;
}
