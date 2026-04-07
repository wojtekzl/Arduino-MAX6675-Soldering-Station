#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "max6675.h"

// Hardware Configuration
const int soPin = 12;
const int csPin = 10;
const int sckPin = 13;
const int PWM_pin = 5;
const int modeButtonPin = 4;
const int pot_pin = A0;

// Objects
LiquidCrystal_I2C lcd(0x27, 16, 2);
MAX6675 thermo(sckPin, csPin, soPin);

// Custom Characters
byte l_polskie[8] = { B01100, B00100, B00110, B00100, B01100, B00100, B01110, B00000 };
byte a_polskie[8] = { B00000, B01110, B00001, B01111, B10001, B01111, B00001, B00010 };

// Control Variables
float temperature_read = 0.0;
int set_temperature = 100;
int heat_temperature = 100;
float PID_error = 0, previous_error = 0, PID_i = 0;
int PID_value = 0;

// PID Tuning Parameters
const int kp = 20; 
const float ki = 0.3; 
const int kd = 10; 

// Limits
const int tempMin = 100;
const int tempMax = 300; 

// State & Timing
unsigned long lastLoop = 0;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;
bool isSleep = false;
bool sensorError = false;
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;

// Potentiometer Filter
int lastPotValue = 0;
float smoothedPot = 0;

void setup() {
    lcd.init();
    lcd.backlight();
    lcd.createChar(0, l_polskie);
    lcd.createChar(1, a_polskie);
    
    showWelcomeScreen();

    pinMode(PWM_pin, OUTPUT);
    digitalWrite(PWM_pin, LOW);
    pinMode(modeButtonPin, INPUT_PULLUP);

    smoothedPot = analogRead(pot_pin);
    delay(2000);
    lcd.clear();
    refresh_screen();
}

void loop() {
    unsigned long currentTime = millis();

    // 1. Potentiometer logic (EMA Filter)
    int rawPot = analogRead(pot_pin);
    smoothedPot = (smoothedPot * 0.9) + (rawPot * 0.1);
    
    if (abs((int)smoothedPot - lastPotValue) > 8) {
        int mapped = map((int)smoothedPot, 0, 490, tempMin, tempMax); 
        int new_set = (constrain(mapped, tempMin, tempMax) / 5) * 5;

        if (new_set != set_temperature) {
            set_temperature = new_set;
            if (!isSleep) heat_temperature = set_temperature;
            lastPotValue = (int)smoothedPot;
            refresh_screen(); 
        }
    }

    handleButton(currentTime);

    // 2. Main Control Loop (Sensor & PID)
    if (currentTime - lastLoop >= 2000) {
        digitalWrite(PWM_pin, LOW); 
        delay(250); 

        float raw_t = thermo.readCelsius();

        if (raw_t <= 1 || raw_t > 500 || isnan(raw_t)) {
            sensorError = true;
            PID_value = 0;
        } else {
            sensorError = false;
            processTemperature(raw_t);
            calculatePID();
        }

        updateDynamicDisplay();
        lastLoop = millis();
    }

    // 3. Power Output (PWM)
    applyPower(currentTime);
}

void processTemperature(float raw_t) {
    float compensation = (set_temperature < 150) ? 1.15 : 1.3;
    float candidate = raw_t * compensation;

    if (abs(candidate - temperature_read) < 80 || temperature_read == 0) {
        temperature_read = candidate;
    }
}

void calculatePID() {
    PID_error = heat_temperature - temperature_read;
    
    if (abs(PID_error) < 15) PID_i += (ki * PID_error);
    else PID_i = 0; 
    
    float p_term = kp * PID_error;
    float d_term = kd * (PID_error - previous_error);

    int max_p = (PID_error < 10) ? 75 : (PID_error < 20) ? 120 : 200;
    PID_value = constrain((int)(p_term + PID_i + d_term), 0, max_p);
    previous_error = PID_error;
}

void applyPower(unsigned long now) {
    static unsigned long heatTimer = 0;
    if (now - heatTimer >= 1000) heatTimer = now;

    unsigned long onTime = map(PID_value, 0, 255, 0, 1000);
    bool isMeasuring = (now - lastLoop > 1600);

    if (!sensorError && !isMeasuring && (now - heatTimer < onTime) && PID_value > 0) {
        digitalWrite(PWM_pin, HIGH);
    } else {
        digitalWrite(PWM_pin, LOW);
    }
}
 
void handleButton(unsigned long now) {
    bool reading = digitalRead(modeButtonPin);
    if (reading != lastButtonReading) lastDebounceTime = now;

    if (now - lastDebounceTime > debounceDelay) {
        if (stableButtonState == HIGH && reading == LOW) {
            isSleep = !isSleep;
            heat_temperature = isSleep ? 100 : set_temperature;
            PID_i = 0; 
            refresh_screen();
        }
        stableButtonState = reading;
    }
    lastButtonReading = reading;
}

void showWelcomeScreen() {
    lcd.setCursor(5, 0); lcd.print("Stacja");
    lcd.setCursor(3, 1); lcd.print("lutownicza");
}

void updateDynamicDisplay() {
    lcd.setCursor(9, 0);
    lcd.print("T:");
    if (sensorError) lcd.print("error");
    else {
        lcd.print((int)temperature_read);
        lcd.print((char)223); lcd.print("C  "); 
    }
}

void refresh_screen() {
    lcd.setCursor(0, 0);
    lcd.print("S:"); lcd.print(set_temperature);
    lcd.print((char)223); lcd.print("C  "); 

    lcd.setCursor(0, 1);
    lcd.print("M:");
    lcd.print(isSleep ? "Sleep   " : "Heat    ");
}
