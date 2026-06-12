/*
 * Access Control for the BYUI ESP32 Dev Board (v4.0)
 * 
 * Hardware: 
 * - ST7789 240x320 TFT Display (rotated to 320x240)
 * - SG90 MicroServo
 * - Buttons: Left (1), Up (2), Right (3), Down (4)
 * - RGB LED for status
 * - Piezo Buzzer
 */

// Software Libraries
#include <ESP32Servo.h> 
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Preferences.h>

// Pins — Board Version 4.0
const int PIN_UP = 11;
const int PIN_DOWN = 47;
const int PIN_LEFT = 21;
const int PIN_RIGHT = 10;
const int PIN_A = 34;
const int PIN_B = 33;

const int PIN_RED = 2;
const int PIN_GREEN = 4;
const int PIN_BLUE = 5;
const int PIN_BUZZER = 48;
const int PIN_SERVO = 15;

// Display pins
#define TFT_CS   0
#define TFT_RST  1
#define TFT_DC   45
#define TFT_MOSI 3
#define TFT_SCLK 46
#define TFT_MISO -1 

// Constants
const char* ADMIN_CODE = "1324";
const int CODE_LENGTH = 4;

// State Machine
enum SystemState {
  STATE_CODE_ENTRY,
  STATE_ACCESS_GRANTED,
  STATE_ACCESS_DENIED,
  STATE_ADMIN_MENU
};

SystemState currentState = STATE_CODE_ENTRY;

// Hardware objects
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
Servo myServo;
Preferences preferences;

// Settings (persistable)
struct Config {
  char userCode[5];
  bool buzzerEnabled;
  bool ledEnabled;
} config;

// Input variables
String enteredCode = "";
bool lastButtonStates[4] = {false, false, false, false}; // Left, Up, Right, Down
int buttonPins[4] = {PIN_LEFT, PIN_UP, PIN_RIGHT, PIN_DOWN};

// UI Colors
const uint16_t COLOR_BG = ST77XX_BLACK;
const uint16_t COLOR_TEXT = ST77XX_WHITE;
const uint16_t COLOR_HIGHLIGHT = ST77XX_YELLOW;
const uint16_t COLOR_SUCCESS = ST77XX_GREEN;
const uint16_t COLOR_FAILURE = ST77XX_RED;

void loadConfig() {
  preferences.begin("access-ctrl", false);
  String code = preferences.getString("userCode", "1212");
  strncpy(config.userCode, code.c_str(), 5);
  config.buzzerEnabled = preferences.getBool("buzzerOn", true);
  config.ledEnabled = preferences.getBool("ledOn", true);
  preferences.end();
}

void saveConfig() {
  preferences.begin("access-ctrl", false);
  preferences.putString("userCode", config.userCode);
  preferences.putBool("buzzerOn", config.buzzerEnabled);
  preferences.putBool("ledOn", config.ledEnabled);
  preferences.end();
}

void setRgbColor(int r, int g, int b) {
  if (!config.ledEnabled && (r + g + b) > 0) return;
  // Scale for visibility (0-255 -> 0-30 as per demo)
  analogWrite(PIN_RED, map(r, 0, 255, 0, 30));
  analogWrite(PIN_GREEN, map(g, 0, 255, 0, 30));
  analogWrite(PIN_BLUE, map(b, 0, 255, 0, 30));
}

void playTone(int freq, int duration) {
  if (!config.buzzerEnabled) return;
  tone(PIN_BUZZER, freq, duration);
}

void drawDiamond(int activeNum = 0) {
  int centerX = 160;
  int centerY = 140;
  int offset = 40;

  tft.setTextSize(3);
  
  // 1: Left
  tft.setTextColor(activeNum == 1 ? COLOR_HIGHLIGHT : COLOR_TEXT);
  tft.setCursor(centerX - offset - 10, centerY);
  tft.print("1");

  // 2: Up
  tft.setTextColor(activeNum == 2 ? COLOR_HIGHLIGHT : COLOR_TEXT);
  tft.setCursor(centerX - 5, centerY - offset - 10);
  tft.print("2");

  // 3: Right
  tft.setTextColor(activeNum == 3 ? COLOR_HIGHLIGHT : COLOR_TEXT);
  tft.setCursor(centerX + offset, centerY);
  tft.print("3");

  // 4: Down
  tft.setTextColor(activeNum == 4 ? COLOR_HIGHLIGHT : COLOR_TEXT);
  tft.setCursor(centerX - 5, centerY + offset);
  tft.print("4");
}

void drawCodeProgress() {
  tft.setCursor(220, 30);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_TEXT);
  tft.fillRect(220, 30, 100, 20, COLOR_BG); // Clear progress area
  for (int i = 0; i < CODE_LENGTH; i++) {
    if (i < enteredCode.length()) {
      tft.print("*");
    } else {
      tft.print("_");
    }
  }
}

void resetCodeEntry() {
  enteredCode = "";
  tft.fillScreen(COLOR_BG);
  tft.setCursor(40, 30);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_TEXT);
  tft.print("Enter Passcode: ");
  drawCodeProgress();
  drawDiamond();
}

void setup() {
  Serial.begin(115200);
  
  // Hardware timers for ESP32 PWM
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  
  myServo.setPeriodHertz(50);
  myServo.attach(PIN_SERVO, 500, 2400); 
  myServo.write(0); // Lock

  pinMode(PIN_UP, INPUT);
  pinMode(PIN_DOWN, INPUT);
  pinMode(PIN_LEFT, INPUT);
  pinMode(PIN_RIGHT, INPUT);
  pinMode(PIN_A, INPUT);
  pinMode(PIN_B, INPUT);
  
  pinMode(PIN_RED, OUTPUT);
  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_BLUE, OUTPUT);
  setRgbColor(0, 0, 0);

  // Initialize display
  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI);
  tft.init(240, 320);
  tft.setRotation(1);
  tft.fillScreen(COLOR_BG);

  // Splash Screen
  tft.setCursor(0, 80);
  tft.setTextColor(ST77XX_RED);
  tft.setTextSize(3);
  tft.println("    MARS BASE\n");
  tft.setTextColor(ST77XX_GREEN);
  tft.println("  Access Control");
  delay(5000);

  loadConfig();
  resetCodeEntry();
}

void handleCodeEntry() {
  bool changed = false;
  int pressedButton = 0;

  for (int i = 0; i < 4; i++) {
    bool state = digitalRead(buttonPins[i]) == HIGH;
    if (state && !lastButtonStates[i]) {
      pressedButton = i + 1;
      enteredCode += String(pressedButton);
      changed = true;
      playTone(2000, 50);
    }
    lastButtonStates[i] = state;
  }

  if (changed) {
    drawDiamond(pressedButton);
    drawCodeProgress();
    delay(200); // Visual feedback time
    drawDiamond(0);

    if (enteredCode.length() == CODE_LENGTH) {
      delay(300);
      if (enteredCode == String(config.userCode)) {
        currentState = STATE_ACCESS_GRANTED;
      } else if (enteredCode == String(ADMIN_CODE)) {
        currentState = STATE_ADMIN_MENU;
      } else {
        currentState = STATE_ACCESS_DENIED;
      }
    }
  }
}

void handleAccessGranted() {
  tft.fillScreen(COLOR_BG);
  tft.setCursor(60, 100);
  tft.setTextSize(3);
  tft.setTextColor(COLOR_SUCCESS);
  tft.print("ACCESS GRANTED");
  setRgbColor(0, 255, 0); // Green
  
  myServo.write(180); // Unlock
  playTone(2500, 500);
  delay(5000);
  
  myServo.write(0); // Lock
  setRgbColor(0, 0, 0);
  currentState = STATE_CODE_ENTRY;
  resetCodeEntry();
}

void handleAccessDenied() {
  tft.fillScreen(COLOR_BG);
  tft.setCursor(65, 100);
  tft.setTextSize(3);
  tft.setTextColor(COLOR_FAILURE);
  tft.print("ACCESS DENIED");
  setRgbColor(255, 0, 0); // Red
  
  for(int i=0; i<3; i++) {
    playTone(1000, 200);
    delay(100);
    playTone(500, 200);
    delay(100);
  }
  delay(2000);
  
  setRgbColor(0, 0, 0);
  currentState = STATE_CODE_ENTRY;
  resetCodeEntry();
}

void handleAdminMenu() {
  int selection = 0;
  bool exitMenu = false;
  
  auto drawMenu = [&](int sel) {
    tft.fillScreen(COLOR_BG);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_HIGHLIGHT);
    tft.setCursor(80, 20);
    tft.println("ADMIN SETTINGS");
    
    const char* options[] = {"Change User Code", "Toggle Buzzer", "Toggle LED", "Exit"};
    for (int i = 0; i < 4; i++) {
      tft.setCursor(20, 60 + i * 30);
      if (i == sel) tft.print("> ");
      else tft.print("  ");
      tft.print(options[i]);
      
      if (i == 1) tft.print(config.buzzerEnabled ? " [ON]" : " [OFF]");
      if (i == 2) tft.print(config.ledEnabled ? " [ON]" : " [OFF]");
    }
  };

  drawMenu(selection);

  while (!exitMenu) {
    if (digitalRead(PIN_UP) == HIGH) {
      selection = (selection - 1 + 4) % 4;
      drawMenu(selection);
      delay(200);
    }
    if (digitalRead(PIN_DOWN) == HIGH) {
      selection = (selection + 1) % 4;
      drawMenu(selection);
      delay(200);
    }
    if (digitalRead(PIN_A) == HIGH) {
      delay(200);
      if (selection == 0) {
        // Change Code
        enteredCode = "";
        tft.fillScreen(COLOR_BG);
        tft.setCursor(20, 40);
        tft.println("New 4-digit Code:");
        while(enteredCode.length() < CODE_LENGTH) {
          for (int i = 0; i < 4; i++) {
            if (digitalRead(buttonPins[i]) == HIGH) {
              enteredCode += String(i+1);
              tft.setCursor(20 + enteredCode.length()*20, 80);
              tft.print("*");
              playTone(2000, 50);
              delay(300);
            }
          }
        }
        strncpy(config.userCode, enteredCode.c_str(), 5);
        saveConfig();
        tft.setCursor(20, 120);
        tft.println("Code Saved!");
        delay(1500);
        drawMenu(selection);
      } else if (selection == 1) {
        config.buzzerEnabled = !config.buzzerEnabled;
        saveConfig();
        drawMenu(selection);
      } else if (selection == 2) {
        config.ledEnabled = !config.ledEnabled;
        saveConfig();
        drawMenu(selection);
      } else if (selection == 3) {
        exitMenu = true;
      }
    }
    if (digitalRead(PIN_B) == HIGH) {
      exitMenu = true;
      delay(200);
    }
    delay(10);
  }

  currentState = STATE_CODE_ENTRY;
  resetCodeEntry();
}

void loop() {
  switch (currentState) {
    case STATE_CODE_ENTRY:
      handleCodeEntry();
      break;
    case STATE_ACCESS_GRANTED:
      handleAccessGranted();
      break;
    case STATE_ACCESS_DENIED:
      handleAccessDenied();
      break;
    case STATE_ADMIN_MENU:
      handleAdminMenu();
      break;
  }
  delay(10);
}





