#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
LiquidCrystal_I2C lcd(0x27, 16, 2);  
// Pin Definitions
#define trigPin 9
#define echoPin 10
#define ENA 5          
#define IN1 6          
#define IN2 7          
#define rpmSensorPin 2 
#define greenLED 3
#define yellowLED 4
#define redLED 8          
#define screenBuzzerPin 11        
#define rpmAlertGreenLED A2   
#define rpmAlertRedLED A0     
#define redSpeedAlertLED 12
#define yellowSpeedAlertLED A3
#define potPin A6             
#define touchSensorPin A1     
#define directionTouchPin 13  
#define batteryPin A7       
// --- Variables ---
float totalDistance = 0.0;    
float currentSpeedKMH = 0.0;  
const int EEPROM_ADDR = 0;    
unsigned long lastEepromSave = 0;
const float WHEEL_CIRCUMFERENCE = 0.1885 / 1000.0; 
#define PULSES_PER_REVOLUTION 20
volatile int pulseCount = 0;
volatile int totalPulsesForDist = 0; 
String driveStatus = "Stopped"; 
int currentScreen = 1;
unsigned long lastRpmUpdate = 0;
int rpm = 0;
unsigned long lastPulseTime = 0;
unsigned long lastBuzzerMillis = 0;
int potValue = 0, potSpeed = 0, potPercentage = 0;
unsigned long previousLoopMillis = 0;
const unsigned long loopInterval = 100;
int lastTouchState = LOW, currentTouchState = LOW;
unsigned long lastDebounceTime = 0;
float lastValidDistance = 10;
float batteryVoltage = 0.0;
int batteryPercentage = 0;
int directionState = 0; 
int lastDirectionTouchState = LOW;
unsigned long lastDirectionDebounceTime = 0;
bool highThrottleAlertTriggered = false;
unsigned long lastReverseBeepMillis = 0;
const unsigned long reverseBeepInterval = 2000;
int rpmValues[3] = {0};
int rpmIndex = 0;
unsigned long lastAnimUpdate = 0;
const unsigned long animInterval = 200;
int animFrame = 0;
const String forwardArrows = ">>>>>>>>>>>>>>";
const String reverseArrows = "<<<<<<<<<<<<<<";
// BLE Settings
unsigned long lastBLESend = 0;
const unsigned long bleInterval = 30000;   // Har 30 second mein update
void setup() {
  lcd.init();
  lcd.backlight();
  
  EEPROM.get(EEPROM_ADDR, totalDistance);
  if (isnan(totalDistance) || totalDistance < 0) totalDistance = 0.0;
  Serial.begin(9600);   // HM-10 BLE
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(rpmSensorPin, INPUT_PULLUP);
  pinMode(greenLED, OUTPUT);
  pinMode(yellowLED, OUTPUT);
  pinMode(redLED, OUTPUT);
  pinMode(screenBuzzerPin, OUTPUT);
  pinMode(rpmAlertGreenLED, OUTPUT);
  pinMode(rpmAlertRedLED, OUTPUT);
  pinMode(redSpeedAlertLED, OUTPUT);
  pinMode(yellowSpeedAlertLED, OUTPUT);
  pinMode(touchSensorPin, INPUT);
  pinMode(directionTouchPin, INPUT);
  pinMode(batteryPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(rpmSensorPin), countPulse, FALLING);
  
  lcd.setCursor(0, 0);
  lcd.print("VEHICLE MONITOR ");
  lcd.setCursor(0, 1);
  lcd.print("SYSTEM STARTING.");
  delay(1500);
  for(int i = 0; i < 3; i++) {
    for(int freq = 600; freq <= 1200; freq += 100) {
      tone(screenBuzzerPin, freq, 80);
      delay(90);
    }
    delay(200);
  }
  noTone(screenBuzzerPin);
  lcd.clear();
  lcd.print("BLE Ready @10s");
  delay(1000);
}
void loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousLoopMillis >= loopInterval) {
    previousLoopMillis = currentMillis;
    noInterrupts();
    int pulsesToProcess = totalPulsesForDist;
    totalPulsesForDist = 0;
    interrupts();
    
    float distanceCovered = (pulsesToProcess / (float)PULSES_PER_REVOLUTION) * WHEEL_CIRCUMFERENCE;
    totalDistance += distanceCovered;
    if (currentMillis - lastEepromSave >= 5000) {
      EEPROM.put(EEPROM_ADDR, totalDistance);
      lastEepromSave = currentMillis;
    }
    int rawBatt = analogRead(batteryPin);
    batteryVoltage = (rawBatt * 5.0 / 1023.0) * 2.0; 
    batteryPercentage = map(constrain(batteryVoltage * 10, 60, 84), 60, 84, 0, 100);
    
    potValue = analogRead(potPin);
    potPercentage = map(potValue, 0, 1023, 0, 100);
    
    checkDirection(currentMillis);
    float distance = getDistance();
    // High Throttle Alert
    if (potPercentage > 90 && !highThrottleAlertTriggered) {
      tone(screenBuzzerPin, 1200, 80);
      delay(100);
      tone(screenBuzzerPin, 1200, 80);
      highThrottleAlertTriggered = true;
    } else if (potPercentage <= 85) {
      highThrottleAlertTriggered = false;
    }
    // Reverse Beep
    if (directionState == 1 && currentMillis - lastReverseBeepMillis >= reverseBeepInterval) {
      lastReverseBeepMillis = currentMillis;
      tone(screenBuzzerPin, 1500, 100);
      delay(120);
      tone(screenBuzzerPin, 1500, 100);
    }
    bool closeObstacle = (directionState == 0 && distance > 0 && distance <= 11);
    if (directionState == 1) {  
      driveStatus = "REVERSE";
      digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
      potSpeed = map(potValue, 0, 1023, 0, 200);
      analogWrite(ENA, potSpeed);
      digitalWrite(redLED, HIGH); digitalWrite(yellowLED, LOW); digitalWrite(greenLED, LOW);
    } 
    else {  
      if (closeObstacle) {
        driveStatus = "STOPPED";
        digitalWrite(IN1, HIGH); digitalWrite(IN2, HIGH);
        analogWrite(ENA, 255);
        digitalWrite(redLED, HIGH); digitalWrite(yellowLED, LOW); digitalWrite(greenLED, LOW);
        if (currentMillis - lastBuzzerMillis >= 50) {
          lastBuzzerMillis = currentMillis;
          static bool beepState = false;
          beepState = !beepState;
          if (beepState) tone(screenBuzzerPin, 1200, 40);
          else noTone(screenBuzzerPin);
        }
      } 
      else if (distance > 11 && distance <= 40) {
        driveStatus = "WARNING";
        digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
        digitalWrite(redLED, LOW); digitalWrite(yellowLED, HIGH); digitalWrite(greenLED, LOW);
        potSpeed = map(potValue, 0, 1023, 0, 120);
        analogWrite(ENA, potSpeed);
        noTone(screenBuzzerPin);
      } 
      else {
        driveStatus = "SAFE";
        digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
        digitalWrite(redLED, LOW); digitalWrite(yellowLED, LOW); digitalWrite(greenLED, HIGH);
        potSpeed = map(potValue, 0, 1023, 0, 255);
        analogWrite(ENA, potSpeed);
        noTone(screenBuzzerPin);
      }
    }
    if (!closeObstacle && rpm > 500) {
      if (currentMillis - lastBuzzerMillis >= 80) {
        lastBuzzerMillis = currentMillis;
        static bool highPitch = true;
        tone(screenBuzzerPin, highPitch ? 1200 : 1000, 70);
        highPitch = !highPitch;
      }
    } else if (!closeObstacle) {
      noTone(screenBuzzerPin);
    }
    updateRPM();
    handleScreens(currentMillis);
    sendDataToBLE(currentMillis);     // BLE Data
    digitalWrite(redSpeedAlertLED, (potPercentage > 75));
    digitalWrite(yellowSpeedAlertLED, (potPercentage < 40 && potPercentage > 5));
  }
}
// ==================== BLE DATA (Har 10 Second) ====================
void sendDataToBLE(unsigned long currentMillis) {
  if (currentMillis - lastBLESend >= bleInterval) {
    lastBLESend = currentMillis;
    Serial.print("=== VEHICLE MONITOR ===\r\n");
    Serial.print("Time       : "); Serial.print(millis()/1000); Serial.print(" sec\r\n");
    Serial.print("Status     : "); Serial.print(driveStatus); Serial.print("\r\n");
    Serial.print("Distance   : "); Serial.print((int)lastValidDistance); Serial.print(" cm\r\n");
    Serial.print("Total KM   : "); Serial.print(totalDistance, 3); Serial.print(" km\r\n");
    Serial.print("Speed      : "); Serial.print(currentSpeedKMH, 1); Serial.print(" km/h\r\n");
    Serial.print("RPM        : "); Serial.print(rpm); Serial.print("\r\n");
    Serial.print("Throttle   : "); Serial.print(potPercentage); Serial.print("%\r\n");
    Serial.print("Battery    : "); Serial.print(batteryPercentage); 
    Serial.print("% ("); Serial.print(batteryVoltage, 1); Serial.print("V)\r\n");
    Serial.print("Direction  : "); Serial.print(directionState == 0 ? "FORWARD" : "REVERSE");
    Serial.print("\r\n========================\r\n\r\n");
  }
}
// ==================== Other Functions ====================
void handleScreens(unsigned long m) {
  currentTouchState = digitalRead(touchSensorPin);
  
  if (currentTouchState == HIGH && lastTouchState == LOW && (m - lastDebounceTime > 400)) {
    lastDebounceTime = m;
    tone(screenBuzzerPin, 1000, 100);
    currentScreen = (currentScreen % 6) + 1;
    lcd.clear();
    animFrame = 0;
    lastAnimUpdate = m;
  }
  lastTouchState = currentTouchState;
  if (currentScreen == 1) {
    lcd.setCursor(0, 0);
    lcd.print("Dist: ");
    lcd.print((int)lastValidDistance);
    lcd.print("cm ");
    lcd.print(lastValidDistance/100.0, 1);
    lcd.print("m   ");
    lcd.setCursor(0, 1);
    lcd.print("Stat: ");
    lcd.print(driveStatus);
    lcd.print("   ");
  } 
  else if (currentScreen == 2) {
    lcd.setCursor(0, 0);
    lcd.print("RPM: "); lcd.print(rpm); lcd.print("   ");
    lcd.setCursor(0, 1);
    lcd.print("Throttle: "); lcd.print(potPercentage); lcd.print("%   ");
  } 
  else if (currentScreen == 3) {
    lcd.setCursor(0, 0);
    lcd.print("MODE: ");
    if (m - lastAnimUpdate >= animInterval) {
      lastAnimUpdate = m;
      animFrame = (animFrame + 1) % 14;
    }
    String arrows = directionState == 0 ? 
      forwardArrows.substring(animFrame) + forwardArrows.substring(0, animFrame) :
      reverseArrows.substring(animFrame) + reverseArrows.substring(0, animFrame);
    lcd.setCursor(6, 0);
    lcd.print(arrows.substring(0, 10));
    lcd.setCursor(0, 1);
    lcd.print(directionState == 0 ? ">>> FORWARD >>>" : "<<< REVERSE <<<");
  } 
  else if (currentScreen == 4) {
    String battStatus = batteryPercentage >= 95 ? "FULL" : batteryPercentage >= 70 ? "HIGH" : batteryPercentage >= 40 ? "MED " : "LOW ";
    lcd.setCursor(0, 0);
    lcd.print("BAT: "); lcd.print(batteryPercentage); lcd.print("% "); lcd.print(battStatus);
    lcd.setCursor(0, 1);
    lcd.print("V: "); lcd.print(batteryVoltage, 1); lcd.print("V");
  } 
  else if (currentScreen == 5) {
    lcd.setCursor(0,0); lcd.print("TOTAL DISTANCE");
    lcd.setCursor(0,1); lcd.print("KM: "); lcd.print(totalDistance, 4);
  } 
  else if (currentScreen == 6) {
    lcd.setCursor(0,0); lcd.print("SPEEDOMETER");
    lcd.setCursor(0,1); lcd.print("SPD: "); lcd.print(currentSpeedKMH, 1); lcd.print(" km/h");
  }
}
void checkDirection(unsigned long m) {
  int dirTouch = digitalRead(directionTouchPin);
  if (dirTouch == HIGH && lastDirectionTouchState == LOW && (m - lastDirectionDebounceTime > 400)) {
    lastDirectionDebounceTime = m;
    tone(screenBuzzerPin, 800, 100);
    delay(150);
    tone(screenBuzzerPin, 800, 100);
    directionState = !directionState;
    lcd.clear();
    animFrame = 0;
  }
  lastDirectionTouchState = dirTouch;
}
void updateRPM() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastRpmUpdate >= 500) {
    lastRpmUpdate = currentMillis;
    noInterrupts();
    int currentRpm = (pulseCount * 120) / PULSES_PER_REVOLUTION;
    pulseCount = 0;
    interrupts();
    rpmValues[rpmIndex % 3] = currentRpm;
    rpmIndex++;
    rpm = (rpmValues[0] + rpmValues[1] + rpmValues[2]) / 3;
    currentSpeedKMH = (rpm * WHEEL_CIRCUMFERENCE * 60.0);
    digitalWrite(rpmAlertRedLED, (rpm > 300));
    digitalWrite(rpmAlertGreenLED, (rpm <= 300));
  }
}
float getDistance() {
  digitalWrite(trigPin, LOW); delayMicroseconds(2);
  digitalWrite(trigPin, HIGH); delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 25000);
  if (duration == 0) return lastValidDistance;
  float d = duration * 0.034 / 2;
  if (d > 2 && d < 400) lastValidDistance = d;
  return lastValidDistance;
}
void countPulse() {
  unsigned long currentTime = millis();
  if (currentTime - lastPulseTime > 5) {
    pulseCount++;
    totalPulsesForDist++;
    lastPulseTime = currentTime;
  }
}