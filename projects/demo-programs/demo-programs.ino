/*
 * Demo Programs Launcher for ESP32-S3-Mini e-Badge — Board Version 4.0
 * 
 * This program provides a menu to launch several programs:
 * 1. Etch-a-Sketch - Drawing program
 * 2. Hardware Test - Comprehensive hardware testing
 * 3. Tetris - Classic Tetris game
 * 4. BYU-I Demo - Addressable LED showcase with branded splash screen
 * 
 * Hardware (v4.0 pinout):
 * - ST7789 240x320 TFT Display SPI2 (CS:0, D/C:45, RST:1, CLK:46, MOSI:3)
 * - Analog joystick (X: GPIO8, Y: GPIO9)
 * - Digital buttons (Left:24, Right:10, Down:47, Up:11, B:33, A:34)
 * - RGB LED (R:2, G:4, B:5)
 * - Piezo buzzer: GPIO48
 * - Addressable LEDs: GPIO7
 * - Single-color LED: GPIO6
 * - Accelerometer (MMA8452QR1) I2C 0x1C, SDA:41, SCL:42
 * - Battery voltage indicator: GPIO12
 * - WiFi, Bluetooth Low Energy (BLE)
 *
 * Libraries (using the Arduino IDE's Library Manager):
 * - Adafruit GFX Library
 * - Adafruit ST7735 and ST7789 Library
 * - FastLED Library (for addressable LEDs)
 * - MMA8453_n0m1 Library (for accelerometer)
 * - WiFi (built-in ESP32)
 * - BLEDevice (built-in ESP32)
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <FastLED.h>
#include <Wire.h>
#include <MMA8453_n0m1.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// ============================================================================
// PIN DEFINITIONS — Board Version 4.0
// ============================================================================
const int PIN_JOYSTICK_X = 8;
const int PIN_JOYSTICK_Y = 9;
const int PIN_RIGHT = 10;
const int PIN_LEFT = 21;
const int PIN_DOWN = 47;
const int PIN_UP = 11;
const int PIN_B = 33;
const int PIN_A = 34;
const int PIN_RED = 2;
const int PIN_GREEN = 4;
const int PIN_BLUE = 5;
const int PIN_BUZZER = 48;
const int PIN_ADDR_LEDS = 7;       // Addressable LEDs data pin
const int PIN_SINGLE_LED = 6;      // Single-color LED
const int PIN_I2C_SDA = 41;        // I2C SDA (accelerometer, expansion)
const int PIN_I2C_SCL = 42;        // I2C SCL (accelerometer, expansion)
const int PIN_X = 8;               // Joystick X (for randomSeed)
const int PIN_Y = 9;               // Joystick Y
const int PIN_BATTERY_VOLTAGE = 12; // Battery voltage indicator (analog input)
const int PIN_MINIBADGE_CLK = 13;   // Minibadge A CLK (slot A: 13,14,15; B: 16,17,18)
const int PIN_LED = 6;             // Single-color LED (alias)
const int PIN_TX = 43;             // UART0 TX (USB bridge)
const int PIN_RX = 44;             // UART0 RX (USB bridge)

// TFT Display pins (SPI2)
#define TFT_CS   0
#define TFT_RST  1
#define TFT_DC   45
#define TFT_MOSI 3
#define TFT_SCLK 46

// ============================================================================
// PROGRAM STATE MANAGEMENT
// ============================================================================
enum ProgramState {
  STATE_MAIN_MENU,
  STATE_ETCH_A_SKETCH,
  STATE_HARDWARE_DEBUG,
  STATE_TETRIS,
  STATE_BYU_I_DEMO
};

ProgramState currentState = STATE_MAIN_MENU;
int menuSelection = 0;
const int NUM_MENU_ITEMS = 4;
unsigned long lastMenuNavTime = 0;
const unsigned long MENU_NAV_DELAY = 300;
bool lastUpState = false;
bool lastDownState = false;
bool lastAState = false;
bool byuDemoLastBState = false;
// Joystick main-menu navigation (same scale as Etch-a-Sketch: 12-bit ADC, ~2048 center)
static bool menuJoyLatchUp = false;
static bool menuJoyLatchDown = false;
const int MAIN_MENU_JOY_CENTER = 2048;
const int MAIN_MENU_JOY_THRESHOLD = 600;
/** Hardware Test menus share one pair; only one submenu runs at a time */
static bool hwMenuJoyLatchUp = false;
static bool hwMenuJoyLatchDown = false;

// ============================================================================
// COMMON CONSTANTS
// ============================================================================
const int PWM_RESOLUTION_BITS = 10;
const int PWM_MAX_DUTY = 30;
const int DISPLAY_WIDTH = 320;
const int DISPLAY_HEIGHT = 240;
const unsigned long BUTTON_DEBOUNCE = 200;

// Custom color constants (not in standard ST77XX library)
const uint16_t ST77XX_GRAY = 0x8410;
const uint16_t ST77XX_DARKGRAY = 0x2104;
const uint16_t ST77XX_DARKGREEN = 0x0320;
const uint16_t ST77XX_LIGHTBLUE = 0x7BFF;
/** High-luminance light blue for BYU splash title (readable on black) */
const uint16_t ST77XX_BRIGHTSKY = 0x967E;

// ============================================================================
// COMMON GLOBAL VARIABLES
// ============================================================================
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ============================================================================
// COMMON UTILITY FUNCTIONS
// ============================================================================

/**
 * Set RGB LED color
 */
void setRgbColor(int r, int g, int b) {
  int dutyRed = map(r, 0, 255, 0, PWM_MAX_DUTY);
  int dutyGreen = map(g, 0  , 255, 0, PWM_MAX_DUTY);
  int dutyBlue = map(b, 0, 255, 0, PWM_MAX_DUTY);
  
  analogWrite(PIN_RED, dutyRed);
  analogWrite(PIN_GREEN, dutyGreen);
  analogWrite(PIN_BLUE, dutyBlue);
}

/**
 * Check if digital button is pressed
 * Returns true when button is pressed (HIGH = pressed with external pull-down)
 */
bool isButtonPressed(int pin) {
  return digitalRead(pin) == HIGH;
}

/**
 * Check if button was just pressed (edge detection)
 */
bool isButtonJustPressed(int pin, bool &lastState) {
  bool currentState = isButtonPressed(pin);
  bool justPressed = currentState && !lastState;
  lastState = currentState;
  return justPressed;
}

/**
 * Return to main menu
 */
void returnToMainMenu() {
  currentState = STATE_MAIN_MENU;
  menuSelection = 0;
  menuJoyLatchUp = false;
  menuJoyLatchDown = false;
  noTone(PIN_BUZZER);
  setRgbColor(0, 0, 0);
  FastLED.clear();
  FastLED.show();
  analogWrite(PIN_SINGLE_LED, 0);
  drawMainMenu();
}

// ============================================================================
// MAIN MENU
// ============================================================================

/**
 * Draw main menu
 */
void drawMainMenu() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(80, 18);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(3);
  tft.println("DEMO");
  tft.setCursor(70, 50);
  tft.println("PROGRAMS");
  
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  
  const char* menuItems[] = {
    "Etch-a-Sketch",
    "Hardware Test",
    "Tetris",
    "BYU-I Demo"
  };
  
  /* Title (~72px tall) stays above menu rows; keep footer visible at y≈220 */
  int yStart = 82;
  int lineHeight = 26;
  
  for (int i = 0; i < NUM_MENU_ITEMS; i++) {
    int yPos = yStart + i * lineHeight;
    
    // Draw selection indicator
    if (i == menuSelection) {
      tft.fillRect(5, yPos - 2, 310, lineHeight - 4, ST77XX_DARKGREEN);
      tft.setTextColor(ST77XX_WHITE);
      tft.setCursor(20, yPos);
      tft.print("> ");
    } else {
      tft.setTextColor(ST77XX_WHITE);
      tft.setCursor(20, yPos);
      tft.print("  ");
    }
    
    tft.println(menuItems[i]);
  }
  
  // Instructions
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_GRAY);
  tft.setCursor(10, 220);
  tft.println("Joystick / Up / Down: Navigate  A: Select");
}

/**
 * Handle main menu input (physical Up/Down buttons or joystick Y axis)
 */
void handleMainMenuInput() {
  unsigned long currentTime = millis();
  
  int joyRaw = analogRead(PIN_JOYSTICK_Y);
  bool joyUpZone = joyRaw > MAIN_MENU_JOY_CENTER + MAIN_MENU_JOY_THRESHOLD;
  bool joyDownZone = joyRaw < MAIN_MENU_JOY_CENTER - MAIN_MENU_JOY_THRESHOLD;
  // Allow another joystick step after returning toward center (release latch)
  if (!joyUpZone) {
    menuJoyLatchUp = false;
  }
  if (!joyDownZone) {
    menuJoyLatchDown = false;
  }
  
  if (currentTime - lastMenuNavTime >= MENU_NAV_DELAY) {
    bool navChanged = false;
    
    if (isButtonJustPressed(PIN_UP, lastUpState)
        || (joyUpZone && !menuJoyLatchUp)) {
      menuSelection = (menuSelection - 1 + NUM_MENU_ITEMS) % NUM_MENU_ITEMS;
      navChanged = true;
      lastMenuNavTime = currentTime;
      if (joyUpZone) {
        menuJoyLatchUp = true;
      }
    } else if (isButtonJustPressed(PIN_DOWN, lastDownState)
               || (joyDownZone && !menuJoyLatchDown)) {
      menuSelection = (menuSelection + 1) % NUM_MENU_ITEMS;
      navChanged = true;
      lastMenuNavTime = currentTime;
      if (joyDownZone) {
        menuJoyLatchDown = true;
      }
    }
    
    if (navChanged) {
      drawMainMenu();
    }
  }
  
  if (currentTime - lastMenuNavTime < BUTTON_DEBOUNCE) {
    return;
  }
  
  if (isButtonJustPressed(PIN_A, lastAState)) {
    lastMenuNavTime = currentTime;
    switch (menuSelection) {
      case 0:
        currentState = STATE_ETCH_A_SKETCH;
        initEtchASketch();
        break;
      case 1:
        currentState = STATE_HARDWARE_DEBUG;
        initHardwareDebug();
        break;
      case 2:
        currentState = STATE_TETRIS;
        initTetris();
        break;
      case 3:
        currentState = STATE_BYU_I_DEMO;
        initBYUIDemo();
        break;
    }
  }
}

// ============================================================================
// ETCH-A-SKETCH PROGRAM
// ============================================================================

// Etch-a-Sketch constants
const int DRAW_AREA_MARGIN = 10;
const int TOP_TEXT_MARGIN = 18;
const int JOYSTICK_CENTER = 2048;
const int JOYSTICK_DEADZONE = 600;
const int JOYSTICK_MAX = 4095;
const float CURSOR_SPEED = 0.3;
const float JOYSTICK_CURVE = 0.5;
const float BUTTON_MOVE_SPEED = 2.0;
const unsigned long BUTTON_REPEAT_DELAY = 50;

// Etch-a-Sketch color palette
const uint16_t ETCH_COLORS[] = {
  ST77XX_WHITE, 0x07FF, 0xFFE0, 0xF81F, 0x07E0, 0xF800, 0x001F, 0xFD20, 0x8410, 0xFF80
};
const int NUM_ETCH_COLORS = sizeof(ETCH_COLORS) / sizeof(ETCH_COLORS[0]);

const uint8_t ETCH_RGB_COLORS[][3] = {
  {255, 255, 255}, {0, 255, 255}, {255, 255, 0}, {255, 0, 255}, {0, 255, 0},
  {255, 0, 0}, {0, 0, 255}, {255, 165, 0}, {128, 128, 128}, {255, 215, 0}
};

// Etch-a-Sketch state
struct EtchState {
  float cursorX, cursorY;
  float lastDrawnX, lastDrawnY;
  int currentColor;
  bool isDrawing;
  bool eraserMode;
  unsigned long lastButtonTime;
  unsigned long lastButtonRepeatTime;
  bool joystickMoving;
  bool lastAState;
  bool lastBState;
  int lastCursorX;
  int lastCursorY;
} etchState;

static float lastJoyValue[2] = {0.0, 0.0};

float readJoystickAxis(int pin) {
  int raw = analogRead(pin);
  float value = (raw - JOYSTICK_CENTER) / (float)(JOYSTICK_MAX / 2);
  
  float deadZoneThreshold = JOYSTICK_DEADZONE / (float)(JOYSTICK_MAX / 2);
  int pinIndex = (pin == PIN_JOYSTICK_X) ? 0 : 1;
  
  if (abs(value) >= deadZoneThreshold) {
    if (value > 1.0) value = 1.0;
    if (value < -1.0) value = -1.0;
    
    float sign = (value >= 0) ? 1.0 : -1.0;
    value = sign * pow(abs(value), 1.0 / (1.0 + JOYSTICK_CURVE));
    
    lastJoyValue[pinIndex] = value;
    return value;
  }
  
  if (abs(lastJoyValue[pinIndex]) < deadZoneThreshold * 1.5) {
    lastJoyValue[pinIndex] = 0.0;
    return 0.0;
  }
  
  lastJoyValue[pinIndex] *= 0.9;
  if (abs(lastJoyValue[pinIndex]) < deadZoneThreshold) {
    lastJoyValue[pinIndex] = 0.0;
  }
  return lastJoyValue[pinIndex];
}

bool updateEtchCursor() {
  float moveX = 0.0;
  float moveY = 0.0;
  bool isMoving = false;
  
  float joyX = readJoystickAxis(PIN_JOYSTICK_X);
  float joyY = -readJoystickAxis(PIN_JOYSTICK_Y);  // invert Y only for Etch-a-Sketch

  if (abs(joyX) > 0.0001 || abs(joyY) > 0.0001) {
    moveX += joyX * CURSOR_SPEED;
    moveY += joyY * CURSOR_SPEED;
    isMoving = true;
  }
  
  unsigned long currentTime = millis();
  bool canRepeat = (currentTime - etchState.lastButtonRepeatTime >= BUTTON_REPEAT_DELAY);
  
  if (isButtonPressed(PIN_LEFT)) {
    if (canRepeat) {
      moveX -= BUTTON_MOVE_SPEED;
      isMoving = true;
      etchState.lastButtonRepeatTime = currentTime;
    }
  }
  if (isButtonPressed(PIN_RIGHT)) {
    if (canRepeat) {
      moveX += BUTTON_MOVE_SPEED;
      isMoving = true;
      etchState.lastButtonRepeatTime = currentTime;
    }
  }
  if (isButtonPressed(PIN_UP)) {
    if (canRepeat) {
      moveY -= BUTTON_MOVE_SPEED;
      isMoving = true;
      etchState.lastButtonRepeatTime = currentTime;
    }
  }
  if (isButtonPressed(PIN_DOWN)) {
    if (canRepeat) {
      moveY += BUTTON_MOVE_SPEED;
      isMoving = true;
      etchState.lastButtonRepeatTime = currentTime;
    }
  }
  
  if (isMoving) {
    etchState.cursorX += moveX;
    etchState.cursorY += moveY;
    
    if (etchState.cursorX < DRAW_AREA_MARGIN) etchState.cursorX = DRAW_AREA_MARGIN;
    if (etchState.cursorX >= DISPLAY_WIDTH - DRAW_AREA_MARGIN) etchState.cursorX = DISPLAY_WIDTH - DRAW_AREA_MARGIN - 1;
    if (etchState.cursorY < TOP_TEXT_MARGIN) etchState.cursorY = TOP_TEXT_MARGIN;
    if (etchState.cursorY >= DISPLAY_HEIGHT - DRAW_AREA_MARGIN) etchState.cursorY = DISPLAY_HEIGHT - DRAW_AREA_MARGIN - 1;
  }
  
  etchState.joystickMoving = isMoving;
  return isMoving;
}

void drawEtchPixels(float prevX, float prevY, float currX, float currY) {
  uint16_t color = etchState.eraserMode ? ST77XX_BLACK : ETCH_COLORS[etchState.currentColor];
  
  int startX = (int)(prevX + 0.5);
  int startY = (int)(prevY + 0.5);
  int endX = (int)(currX + 0.5);
  int endY = (int)(currY + 0.5);
  
  if (startX == endX && startY == endY) {
    tft.drawPixel(startX, startY, color);
    return;
  }
  
  int dx = abs(endX - startX);
  int dy = abs(endY - startY);
  int sx = (startX < endX) ? 1 : -1;
  int sy = (startY < endY) ? 1 : -1;
  int err = dx - dy;
  
  int x = startX;
  int y = startY;
  
  while (true) {
    tft.drawPixel(x, y, color);
    
    if (x == endX && y == endY) {
      break;
    }
    
    int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x += sx;
    }
    if (e2 < dx) {
      err += dx;
      y += sy;
    }
  }
}

void clearEtchScreen() {
  tft.fillScreen(ST77XX_BLACK);
  etchState.cursorX = DISPLAY_WIDTH / 2;
  etchState.cursorY = DISPLAY_HEIGHT / 2;
  etchState.lastDrawnX = -1.0;
  etchState.lastDrawnY = -1.0;
  noTone(PIN_BUZZER);
  delay(10);
  tone(PIN_BUZZER, 400, 100);
  delay(50);
  noTone(PIN_BUZZER);
  delay(10);
  tone(PIN_BUZZER, 300, 100);
}

void nextEtchColor() {
  etchState.currentColor = (etchState.currentColor + 1) % NUM_ETCH_COLORS;
  etchState.eraserMode = false;
  
  setRgbColor(ETCH_RGB_COLORS[etchState.currentColor][0],
              ETCH_RGB_COLORS[etchState.currentColor][1],
              ETCH_RGB_COLORS[etchState.currentColor][2]);
  
  noTone(PIN_BUZZER);
  delay(5);
  tone(PIN_BUZZER, 600 + (etchState.currentColor * 50), 50);
}

void toggleEraser() {
  etchState.eraserMode = !etchState.eraserMode;
  
  noTone(PIN_BUZZER);
  delay(5);
  
  if (etchState.eraserMode) {
    setRgbColor(64, 64, 64);
    tone(PIN_BUZZER, 300, 100);
  } else {
    setRgbColor(ETCH_RGB_COLORS[etchState.currentColor][0],
                ETCH_RGB_COLORS[etchState.currentColor][1],
                ETCH_RGB_COLORS[etchState.currentColor][2]);
    tone(PIN_BUZZER, 500, 100);
  }
}

void drawEtchUI() {
  tft.drawRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, ST77XX_WHITE);
  tft.fillRect(1, 1, DISPLAY_WIDTH - 2, TOP_TEXT_MARGIN - 1, ST77XX_BLACK);
  
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  
  if (etchState.eraserMode) {
    tft.fillRect(5, 5, 30, 10, ST77XX_BLACK);
    tft.drawRect(5, 5, 30, 10, ST77XX_WHITE);
  } else {
    tft.fillRect(5, 5, 30, 10, ETCH_COLORS[etchState.currentColor]);
  }
  
  tft.fillRect(38, 5, 100, 10, ST77XX_BLACK);
  tft.setCursor(38, 6);
  if (etchState.eraserMode) {
    tft.print("ERASER");
  } else {
    tft.print("Color: ");
    tft.print(etchState.currentColor);
  }
  
  tft.setCursor(150, 6);
  tft.print("A:Color B:Eraser A+B:Clear");
}

void processEtchButtons() {
  unsigned long currentTime = millis();
  
  if (currentTime - etchState.lastButtonTime < BUTTON_DEBOUNCE) {
    etchState.lastAState = isButtonPressed(PIN_A);
    etchState.lastBState = isButtonPressed(PIN_B);
    return;
  }
  
  bool aPressed = isButtonPressed(PIN_A);
  bool bPressed = isButtonPressed(PIN_B);
  
  bool aJustPressed = aPressed && !etchState.lastAState;
  bool bJustPressed = bPressed && !etchState.lastBState;
  
  etchState.lastAState = aPressed;
  etchState.lastBState = bPressed;
  
  if (!aJustPressed && !bJustPressed) {
    return;
  }
  
  if (aJustPressed && bPressed) {
    clearEtchScreen();
    etchState.lastButtonTime = currentTime;
    drawEtchUI();
    return;
  }
  if (bJustPressed && aPressed) {
    clearEtchScreen();
    etchState.lastButtonTime = currentTime;
    drawEtchUI();
    return;
  }
  
  if (aJustPressed) {
    nextEtchColor();
    etchState.lastButtonTime = currentTime;
    drawEtchUI();
    return;
  }
  
  if (bJustPressed) {
    toggleEraser();
    etchState.lastButtonTime = currentTime;
    drawEtchUI();
    return;
  }
}

void drawEtchCursorBox() {
  int x = (int)etchState.cursorX;
  int y = (int)etchState.cursorY;
  
  if (etchState.lastCursorX == x && etchState.lastCursorY == y) {
    return;
  }
  
  if (etchState.lastCursorX != -1 && etchState.lastCursorY != -1) {
    tft.fillRect(etchState.lastCursorX - 4, etchState.lastCursorY - 4, 9, 9, ST77XX_BLACK);
  }
  
  tft.drawRect(x - 3, y - 3, 7, 7, ST77XX_WHITE);
  tft.drawRect(x - 2, y - 2, 5, 5, ST77XX_WHITE);
  
  etchState.lastCursorX = x;
  etchState.lastCursorY = y;
}

void initEtchASketch() {
  tft.fillScreen(ST77XX_BLACK);
  
  etchState.cursorX = DISPLAY_WIDTH / 2;
  etchState.cursorY = DISPLAY_HEIGHT / 2;
  etchState.lastDrawnX = -1.0;
  etchState.lastDrawnY = -1.0;
  etchState.currentColor = 0;
  etchState.isDrawing = false;
  etchState.eraserMode = false;
  etchState.lastButtonTime = 0;
  etchState.lastButtonRepeatTime = 0;
  etchState.joystickMoving = false;
  etchState.lastAState = false;
  etchState.lastBState = false;
  etchState.lastCursorX = -1;
  etchState.lastCursorY = -1;
  
  noTone(PIN_BUZZER);
  setRgbColor(ETCH_RGB_COLORS[0][0], ETCH_RGB_COLORS[0][1], ETCH_RGB_COLORS[0][2]);
  
  clearEtchScreen();
  drawEtchUI();
}

void loopEtchASketch() {
  noTone(PIN_BUZZER);
  
  processEtchButtons();
  
  bool cursorMoving = updateEtchCursor();
  
  if (etchState.eraserMode) {
    drawEtchCursorBox();
  } else {
    if (etchState.lastCursorX != -1 && etchState.lastCursorY != -1) {
      tft.fillRect(etchState.lastCursorX - 3, etchState.lastCursorY - 3, 7, 7, ST77XX_BLACK);
      etchState.lastCursorX = -1;
      etchState.lastCursorY = -1;
    }
  }
  
  if (cursorMoving) {
    if (etchState.lastDrawnX < 0 || etchState.lastDrawnY < 0) {
      tft.drawPixel((int)etchState.cursorX, (int)etchState.cursorY, 
                    etchState.eraserMode ? ST77XX_BLACK : ETCH_COLORS[etchState.currentColor]);
      etchState.lastDrawnX = etchState.cursorX;
      etchState.lastDrawnY = etchState.cursorY;
    } else {
      if (etchState.lastDrawnX != etchState.cursorX || etchState.lastDrawnY != etchState.cursorY) {
        drawEtchPixels(etchState.lastDrawnX, etchState.lastDrawnY, etchState.cursorX, etchState.cursorY);
        etchState.lastDrawnX = etchState.cursorX;
        etchState.lastDrawnY = etchState.cursorY;
      }
    }
  } else {
    if (etchState.lastDrawnX < 0 || etchState.lastDrawnY < 0) {
      etchState.lastDrawnX = etchState.cursorX;
      etchState.lastDrawnY = etchState.cursorY;
    } else {
      etchState.lastDrawnX = etchState.cursorX;
      etchState.lastDrawnY = etchState.cursorY;
    }
  }
  
  delay(1);
}

// ============================================================================
// HARDWARE TEST PROGRAM
// ============================================================================

// Hardware Test - Complete implementation

enum MenuState {
  MENU_MAIN,
  MENU_INPUTS,
  MENU_OUTPUTS,
  MENU_WIRELESS_MENU,
  MENU_BUZZER,
  MENU_RGB_LED,
  MENU_RGB_INDIVIDUAL,
  MENU_ADDR_LEDS,
  MENU_SINGLE_LED,
  MENU_WIFI,
  MENU_BLUETOOTH
};

enum OutputTest {
  TEST_BUZZER_PITCHES,
  TEST_RGB_COLORS,
  TEST_RGB_INDIVIDUAL,
  TEST_ADDR_LEDS,
  TEST_SINGLE_LED,
  NUM_OUTPUT_TESTS
};

enum WirelessTest {
  TEST_WIFI_SEL,
  TEST_BLE_SEL,
  NUM_WIRELESS_TESTS
};

const int NUM_ADDR_LEDS = 24;
const int ACCEL_I2C_ADDR = 0x1C;
const unsigned long MENU_NAV_DELAY_DEBUG = 300;

// Musical note frequencies
#define NOTE_C4  262
#define NOTE_CS4 277
#define NOTE_D4  294
#define NOTE_DS4 311
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_FS4 370
#define NOTE_G4  392
#define NOTE_GS4 415
#define NOTE_A4  440
#define NOTE_AS4 466
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_D5  587
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_G5  784
#define NOTE_A5  880
#define NOTE_REST 0

#define WHOLE_NOTE    1000
#define HALF_NOTE     500
#define QUARTER_NOTE  250
#define EIGHTH_NOTE   125

struct Note {
  int frequency;
  int duration;
};

const Note POPCORN_MELODY[] = {
  {NOTE_G4, QUARTER_NOTE}, {NOTE_G4, QUARTER_NOTE}, {NOTE_A4, QUARTER_NOTE}, {NOTE_G4, HALF_NOTE},
  {NOTE_E4, QUARTER_NOTE}, {NOTE_G4, QUARTER_NOTE}, {NOTE_G4, HALF_NOTE},
  {NOTE_G4, QUARTER_NOTE}, {NOTE_G4, QUARTER_NOTE}, {NOTE_A4, QUARTER_NOTE}, {NOTE_G4, HALF_NOTE},
  {NOTE_E4, QUARTER_NOTE}, {NOTE_G4, HALF_NOTE}, {NOTE_G4, HALF_NOTE},
  {NOTE_G4, QUARTER_NOTE}, {NOTE_G4, QUARTER_NOTE}, {NOTE_A4, QUARTER_NOTE}, {NOTE_B4, HALF_NOTE},
  {NOTE_A4, QUARTER_NOTE}, {NOTE_G4, QUARTER_NOTE}, {NOTE_E4, HALF_NOTE},
  {NOTE_G4, QUARTER_NOTE}, {NOTE_G4, QUARTER_NOTE}, {NOTE_A4, QUARTER_NOTE}, {NOTE_G4, HALF_NOTE},
  {NOTE_E4, QUARTER_NOTE}, {NOTE_G4, HALF_NOTE}, {NOTE_G4, HALF_NOTE},
  {NOTE_C5, QUARTER_NOTE}, {NOTE_C5, QUARTER_NOTE}, {NOTE_B4, QUARTER_NOTE}, {NOTE_A4, HALF_NOTE},
  {NOTE_B4, QUARTER_NOTE}, {NOTE_A4, QUARTER_NOTE}, {NOTE_G4, HALF_NOTE},
  {NOTE_G4, QUARTER_NOTE}, {NOTE_G4, QUARTER_NOTE}, {NOTE_A4, QUARTER_NOTE}, {NOTE_G4, HALF_NOTE},
  {NOTE_E4, QUARTER_NOTE}, {NOTE_G4, HALF_NOTE}, {NOTE_G4, HALF_NOTE},
  {NOTE_G4, WHOLE_NOTE}
};

const int NUM_POPCORN_NOTES = sizeof(POPCORN_MELODY) / sizeof(POPCORN_MELODY[0]);

const uint8_t DEBUG_RGB_COLORS[][3] = {
  {255, 255, 255}, {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0},
  {255, 0, 255}, {0, 255, 255}, {255, 165, 0}, {128, 0, 128}, {255, 192, 203}
};
const int NUM_DEBUG_RGB_COLORS = sizeof(DEBUG_RGB_COLORS) / sizeof(DEBUG_RGB_COLORS[0]);

MenuState debugMenu = MENU_MAIN;
int selectedHardwareCategory = 0;
const int NUM_HARDWARE_CATEGORIES = 3;
int selectedWirelessTest = 0;
int selectedOutputTest = 0;
int selectedRgbColor = 0;
int rgbIndividualChannel = 0;
int rgbIndividualValue = 128;

CRGB addrLedsArray[NUM_ADDR_LEDS];
MMA8453_n0m1 accel;
BLEScan* pBLEScan = nullptr;

bool accelerometerInitialized = false;
float accelX = 0, accelY = 0, accelZ = 0;
bool singleLedState = false;
int singleLedBrightness = 128;
bool wifiScanning = false;
int wifiNetworkCount = 0;
unsigned long wifiLastScan = 0;
const unsigned long WIFI_SCAN_INTERVAL = 10000;
const int MAX_WIFI_NETWORKS = 20;
String wifiSSIDs[MAX_WIFI_NETWORKS];
int wifiRSSIs[MAX_WIFI_NETWORKS];
int wifiScrollPos = 0;
const int WIFI_LIST_ITEMS = 8;

bool bluetoothScanning = false;
int bleDeviceCount = 0;
unsigned long bleLastScan = 0;
const unsigned long BLE_SCAN_INTERVAL = 10000;
const int MAX_BLE_DEVICES = 20;
String bleAddresses[MAX_BLE_DEVICES];
String bleNames[MAX_BLE_DEVICES];
int bleRSSIs[MAX_BLE_DEVICES];
int bleScrollPos = 0;
const int BLE_LIST_ITEMS = 8;

int currentNoteIndex = 0;
unsigned long noteStartTime = 0;
bool isPlayingSong = false;
bool songFinished = false;

int addrLedMode = 0;
int addrLedColorIndex = 0;
int addrLedChasePos = 0;
unsigned long addrLedLastUpdate = 0;

unsigned long debugLastButtonTime = 0;
unsigned long debugLastMenuNavTime = 0;
bool debugLastAState = false;
bool debugLastBState = false;
bool debugLastUpState = false;
bool debugLastDownState = false;
bool debugLastLeftState = false;
bool debugLastRightState = false;

bool inputTestInitialized = false;
unsigned long debugLastInputUpdate = 0;
const unsigned long INPUT_UPDATE_INTERVAL = 100;

/** millis() when Hardware Test splash was last drawn — ignore A/B until arm delay elapsed */
unsigned long debugSplashShownAt = 0;
const unsigned long DEBUG_SPLASH_ARM_DELAY_MS = 500;

uint16_t rgbTo565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

const char* getButtonStateString(bool pressed) {
  return pressed ? "High" : "Low";
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // Callback is used but we'll store data from scan results instead
  }
};

void drawDebugMainMenu() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(80, 38);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(3);
  tft.println("HARDWARE");
  tft.setCursor(100, 73);
  tft.println("TEST");
  
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  
  const char* categories[] = {
    "Input Test",
    "Output Test",
    "Wireless Test"
  };
  int yStart = 118;
  int lineHeight = 28;
  for (int i = 0; i < NUM_HARDWARE_CATEGORIES; i++) {
    int yPos = yStart + i * lineHeight;
    if (i == selectedHardwareCategory) {
      tft.fillRect(5, yPos - 2, 310, lineHeight - 4, ST77XX_DARKGREEN);
      tft.setTextColor(ST77XX_WHITE);
      tft.setCursor(20, yPos);
      tft.print("> ");
    } else {
      tft.setTextColor(ST77XX_WHITE);
      tft.setCursor(20, yPos);
      tft.print("  ");
    }
    tft.println(categories[i]);
  }
  
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_GRAY);
  tft.setCursor(10, 208);
  tft.println("Joystick / Up / Down   A: Select   B: Back");
  tft.setCursor(40, 222);
  tft.println("Use Reset button to restart");

  debugSplashShownAt = millis();
}

void drawInputTestStatic() {
  tft.fillScreen(ST77XX_BLACK);
  
  tft.setCursor(90, 2);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.println("INPUT TEST");
  
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  
  int yPos = 26;
  const int lineHeight = 13;
  
  tft.setCursor(10, yPos);
  tft.println("Button A:");
  yPos += lineHeight;
  tft.setCursor(10, yPos);
  tft.println("Button B:");
  yPos += lineHeight;
  tft.setCursor(10, yPos);
  tft.println("Button Up:");
  yPos += lineHeight;
  tft.setCursor(10, yPos);
  tft.println("Button Down:");
  yPos += lineHeight;
  tft.setCursor(10, yPos);
  tft.println("Button Left:");
  yPos += lineHeight;
  tft.setCursor(10, yPos);
  tft.println("Button Right:");
  yPos += lineHeight;
  tft.setCursor(10, yPos);
  tft.println("Joystick X:");
  yPos += lineHeight;
  tft.setCursor(10, yPos);
  tft.println("Joystick Y:");
  yPos += lineHeight;
  tft.setCursor(10, yPos);
  tft.println("Battery level:");
  yPos += lineHeight;
  tft.setCursor(10, yPos);
  tft.println("Accel status:");
  yPos += lineHeight;
  tft.setCursor(10, yPos);
  tft.println("Accel X:");
  yPos += lineHeight;
  tft.setCursor(10, yPos);
  tft.println("Accel Y:");
  yPos += lineHeight;
  tft.setCursor(10, yPos);
  tft.println("Accel Z:");
  
  tft.setTextColor(ST77XX_GRAY);
  tft.setCursor(10, 220);
  tft.println("B: Back");
  
  inputTestInitialized = true;
}

void updateInputTestValues() {
  if (!inputTestInitialized) {
    drawInputTestStatic();
  }
  
  int yPos = 26;
  const int lineHeight = 13;
  const int valueX = 95;
  
  bool buttonStates[] = {
    isButtonPressed(PIN_A),
    isButtonPressed(PIN_B),
    isButtonPressed(PIN_UP),
    isButtonPressed(PIN_DOWN),
    isButtonPressed(PIN_LEFT),
    isButtonPressed(PIN_RIGHT)
  };
  
  for (int i = 0; i < 6; i++) {
    tft.fillRect(valueX, yPos, 200, lineHeight, ST77XX_BLACK);
    tft.setCursor(valueX, yPos);
    tft.setTextColor(buttonStates[i] ? ST77XX_RED : ST77XX_LIGHTBLUE);
    tft.println(getButtonStateString(buttonStates[i]));
    yPos += lineHeight;
  }
  
  int joyX = analogRead(PIN_JOYSTICK_X);
  int joyY = analogRead(PIN_JOYSTICK_Y);
  
  tft.fillRect(valueX, yPos, 100, lineHeight, ST77XX_BLACK);
  tft.setCursor(valueX, yPos);
  tft.setTextColor(ST77XX_YELLOW);
  tft.print(joyX);
  tft.print(" / 4095");
  
  yPos += lineHeight;
  
  tft.fillRect(valueX, yPos, 100, lineHeight, ST77XX_BLACK);
  tft.setCursor(valueX, yPos);
  tft.setTextColor(ST77XX_YELLOW);
  tft.print(joyY);
  tft.print(" / 4095");
  
  yPos += lineHeight;
  
  int battRaw = analogRead(PIN_BATTERY_VOLTAGE);
  tft.fillRect(valueX, yPos, 120, lineHeight, ST77XX_BLACK);
  tft.setCursor(valueX, yPos);
  tft.setTextColor(ST77XX_YELLOW);
  tft.print(battRaw);
  tft.print(" / 4095");
  
  yPos += lineHeight;
  
  tft.fillRect(valueX, yPos, 200, lineHeight, ST77XX_BLACK);
  tft.setCursor(valueX, yPos);
  if (accelerometerInitialized) {
    tft.setTextColor(ST77XX_GREEN);
    tft.println("OK");
  } else {
    tft.setTextColor(ST77XX_RED);
    tft.println("--");
  }
  
  yPos += lineHeight;
  
  tft.fillRect(valueX, yPos, 90, lineHeight, ST77XX_BLACK);
  tft.setCursor(valueX, yPos);
  tft.setTextColor(ST77XX_YELLOW);
  tft.print(accelX, 2);
  tft.println(" g");
  
  yPos += lineHeight;
  
  tft.fillRect(valueX, yPos, 90, lineHeight, ST77XX_BLACK);
  tft.setCursor(valueX, yPos);
  tft.setTextColor(ST77XX_YELLOW);
  tft.print(accelY, 2);
  tft.println(" g");
  
  yPos += lineHeight;
  
  tft.fillRect(valueX, yPos, 90, lineHeight, ST77XX_BLACK);
  tft.setCursor(valueX, yPos);
  tft.setTextColor(ST77XX_YELLOW);
  tft.print(accelZ, 2);
  tft.println(" g");
}

void drawInputTest() {
  inputTestInitialized = false;
  Wire.setPins(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!accelerometerInitialized) {
    accel.setI2CAddr(ACCEL_I2C_ADDR);
    accel.dataMode(true, 2);
    accelerometerInitialized = true;
  }
  drawInputTestStatic();
  updateInputTestValues();
}

void drawOutputMenu() {
  tft.fillScreen(ST77XX_BLACK);
  
  tft.setCursor(90, 5);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.println("OUTPUT TEST");
  
  tft.setTextSize(1);
  
  const char* menuItems[] = {
    "Popcorn Song",
    "RGB LED Colors",
    "RGB Individual",
    "Addressable LEDs",
    "Single LED"
  };
  
  int yStart = 40;
  int lineHeight = 25;
  
  for (int i = 0; i < NUM_OUTPUT_TESTS; i++) {
    int yPos = yStart + i * lineHeight;
    
    if (i == selectedOutputTest) {
      tft.fillRect(5, yPos - 2, 310, lineHeight - 4, ST77XX_DARKGREEN);
      tft.setTextColor(ST77XX_WHITE);
      tft.setCursor(20, yPos);
      tft.print("> ");
    } else {
      tft.setTextColor(ST77XX_WHITE);
      tft.setCursor(20, yPos);
      tft.print("  ");
    }
    
    tft.println(menuItems[i]);
  }
  
  tft.setTextColor(ST77XX_GRAY);
  tft.setCursor(10, 220);
  tft.println("Joystick / Up / Down  A: Select  B: Back");
}

void drawWirelessMenu() {
  tft.fillScreen(ST77XX_BLACK);
  
  tft.setCursor(65, 5);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.println("WIRELESS TEST");
  
  tft.setTextSize(1);
  
  const char* menuItems[] = {
    "WiFi Scan",
    "Bluetooth Scan"
  };
  
  int yStart = 70;
  int lineHeight = 40;
  
  for (int i = 0; i < NUM_WIRELESS_TESTS; i++) {
    int yPos = yStart + i * lineHeight;
    
    if (i == selectedWirelessTest) {
      tft.fillRect(5, yPos - 2, 310, lineHeight - 4, ST77XX_DARKGREEN);
      tft.setTextColor(ST77XX_WHITE);
      tft.setCursor(20, yPos + 8);
      tft.print("> ");
    } else {
      tft.setTextColor(ST77XX_WHITE);
      tft.setCursor(20, yPos + 8);
      tft.print("  ");
    }
    
    tft.setTextSize(2);
    tft.println(menuItems[i]);
    tft.setTextSize(1);
  }
  
  tft.setTextColor(ST77XX_GRAY);
  tft.setCursor(10, 220);
  tft.println("Joystick / Up / Down  A: Select  B: Back");
}

void handleWirelessMenuInput() {
  unsigned long currentTime = millis();

  int joyRaw = analogRead(PIN_JOYSTICK_Y);
  bool joyUpZone = joyRaw > MAIN_MENU_JOY_CENTER + MAIN_MENU_JOY_THRESHOLD;
  bool joyDownZone = joyRaw < MAIN_MENU_JOY_CENTER - MAIN_MENU_JOY_THRESHOLD;
  if (!joyUpZone) {
    hwMenuJoyLatchUp = false;
  }
  if (!joyDownZone) {
    hwMenuJoyLatchDown = false;
  }

  if (currentTime - debugLastMenuNavTime < MENU_NAV_DELAY_DEBUG) {
    return;
  }
  bool navChanged = false;
  if (isButtonJustPressed(PIN_UP, debugLastUpState)
      || (joyUpZone && !hwMenuJoyLatchUp)) {
    selectedWirelessTest = (selectedWirelessTest - 1 + NUM_WIRELESS_TESTS) % NUM_WIRELESS_TESTS;
    navChanged = true;
    debugLastMenuNavTime = currentTime;
    if (joyUpZone) {
      hwMenuJoyLatchUp = true;
    }
  } else if (isButtonJustPressed(PIN_DOWN, debugLastDownState)
             || (joyDownZone && !hwMenuJoyLatchDown)) {
    selectedWirelessTest = (selectedWirelessTest + 1) % NUM_WIRELESS_TESTS;
    navChanged = true;
    debugLastMenuNavTime = currentTime;
    if (joyDownZone) {
      hwMenuJoyLatchDown = true;
    }
  }
  if (navChanged) {
    drawWirelessMenu();
  }
  if (currentTime - debugLastButtonTime < BUTTON_DEBOUNCE) {
    return;
  }
  if (isButtonJustPressed(PIN_A, debugLastAState)) {
    debugLastButtonTime = currentTime;
    switch (selectedWirelessTest) {
      case TEST_WIFI_SEL:
        debugMenu = MENU_WIFI;
        wifiScanning = false;
        wifiNetworkCount = 0;
        drawWiFiTest();
        break;
      case TEST_BLE_SEL:
        debugMenu = MENU_BLUETOOTH;
        bluetoothScanning = false;
        bleDeviceCount = 0;
        if (pBLEScan == nullptr) {
          BLEDevice::init("e-Badge Debug");
          pBLEScan = BLEDevice::getScan();
          pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
          pBLEScan->setActiveScan(true);
          pBLEScan->setInterval(100);
          pBLEScan->setWindow(99);
        }
        drawBluetoothTest();
        break;
    }
  } else if (isButtonJustPressed(PIN_B, debugLastBState)) {
    debugMenu = MENU_MAIN;
    debugLastButtonTime = currentTime;
    drawDebugMainMenu();
  }
}

void handleDebugMainMenuInput() {
  unsigned long currentTime = millis();

  if (currentTime - debugSplashShownAt < DEBUG_SPLASH_ARM_DELAY_MS) {
    debugLastAState = isButtonPressed(PIN_A);
    debugLastBState = isButtonPressed(PIN_B);
    debugLastUpState = isButtonPressed(PIN_UP);
    debugLastDownState = isButtonPressed(PIN_DOWN);
    int jr = analogRead(PIN_JOYSTICK_Y);
    if (jr <= MAIN_MENU_JOY_CENTER + MAIN_MENU_JOY_THRESHOLD) {
      hwMenuJoyLatchUp = false;
    }
    if (jr >= MAIN_MENU_JOY_CENTER - MAIN_MENU_JOY_THRESHOLD) {
      hwMenuJoyLatchDown = false;
    }
    return;
  }

  int joyRaw = analogRead(PIN_JOYSTICK_Y);
  bool joyUpZone = joyRaw > MAIN_MENU_JOY_CENTER + MAIN_MENU_JOY_THRESHOLD;
  bool joyDownZone = joyRaw < MAIN_MENU_JOY_CENTER - MAIN_MENU_JOY_THRESHOLD;
  if (!joyUpZone) {
    hwMenuJoyLatchUp = false;
  }
  if (!joyDownZone) {
    hwMenuJoyLatchDown = false;
  }

  if (currentTime - debugLastMenuNavTime >= MENU_NAV_DELAY_DEBUG) {
    bool catChanged = false;
    if (isButtonJustPressed(PIN_UP, debugLastUpState)
        || (joyUpZone && !hwMenuJoyLatchUp)) {
      selectedHardwareCategory = (selectedHardwareCategory - 1 + NUM_HARDWARE_CATEGORIES) % NUM_HARDWARE_CATEGORIES;
      catChanged = true;
      debugLastMenuNavTime = currentTime;
      if (joyUpZone) {
        hwMenuJoyLatchUp = true;
      }
    } else if (isButtonJustPressed(PIN_DOWN, debugLastDownState)
               || (joyDownZone && !hwMenuJoyLatchDown)) {
      selectedHardwareCategory = (selectedHardwareCategory + 1) % NUM_HARDWARE_CATEGORIES;
      catChanged = true;
      debugLastMenuNavTime = currentTime;
      if (joyDownZone) {
        hwMenuJoyLatchDown = true;
      }
    }
    if (catChanged) {
      drawDebugMainMenu();
    }
  }

  if (currentTime - debugLastButtonTime < BUTTON_DEBOUNCE) {
    return;
  }

  if (isButtonJustPressed(PIN_A, debugLastAState)) {
    debugLastButtonTime = currentTime;
    switch (selectedHardwareCategory) {
      case 0:
        debugMenu = MENU_INPUTS;
        drawInputTest();
        break;
      case 1:
        debugMenu = MENU_OUTPUTS;
        selectedOutputTest = 0;
        drawOutputMenu();
        break;
      case 2:
        debugMenu = MENU_WIRELESS_MENU;
        selectedWirelessTest = 0;
        drawWirelessMenu();
        break;
    }
  } else if (isButtonJustPressed(PIN_B, debugLastBState)) {
    debugLastButtonTime = currentTime;
    returnToMainMenu();
  }
}

void drawBuzzerTest() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(40, 5);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.println("BUZZER SONG");
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(40, 40);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW);
  tft.println("Popcorn Popping");
  tft.setCursor(60, 60);
  tft.setTextSize(1);
  tft.println("on the Apricot Tree");
  tft.setTextSize(1);
  tft.setCursor(80, 100);
  if (songFinished) {
    tft.setTextColor(ST77XX_GREEN);
    tft.println("SONG FINISHED");
  } else if (isPlayingSong) {
    tft.setTextColor(ST77XX_CYAN);
    tft.print("PLAYING... ");
    tft.print(currentNoteIndex);
    tft.print(" / ");
    tft.println(NUM_POPCORN_NOTES);
  } else {
    tft.setTextColor(ST77XX_WHITE);
    tft.println("Press A to play");
  }
  int progressWidth = map(currentNoteIndex, 0, NUM_POPCORN_NOTES, 0, 280);
  tft.fillRect(20, 130, 280, 20, ST77XX_DARKGRAY);
  tft.fillRect(20, 130, progressWidth, 20, ST77XX_GREEN);
  tft.drawRect(20, 130, 280, 20, ST77XX_WHITE);
  if (isPlayingSong && currentNoteIndex < NUM_POPCORN_NOTES) {
    tft.setCursor(80, 165);
    tft.setTextColor(ST77XX_WHITE);
    tft.print("Note: ");
    if (POPCORN_MELODY[currentNoteIndex].frequency == NOTE_REST) {
      tft.println("REST");
    } else {
      tft.print(POPCORN_MELODY[currentNoteIndex].frequency);
      tft.println(" Hz");
    }
  }
  tft.setTextColor(ST77XX_GRAY);
  tft.setCursor(10, 220);
  tft.println("A: Play  B: Back");
}

void drawRgbColorTest() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(80, 5);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.println("RGB LED TEST");
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  int r = DEBUG_RGB_COLORS[selectedRgbColor][0];
  int g = DEBUG_RGB_COLORS[selectedRgbColor][1];
  int b = DEBUG_RGB_COLORS[selectedRgbColor][2];
  tft.setCursor(10, 50);
  tft.print("Color: ");
  tft.print(selectedRgbColor + 1);
  tft.print(" / ");
  tft.println(NUM_DEBUG_RGB_COLORS);
  tft.setCursor(10, 70);
  tft.print("R: ");
  tft.println(r);
  tft.setCursor(10, 88);
  tft.print("G: ");
  tft.println(g);
  tft.setCursor(10, 106);
  tft.print("B: ");
  tft.println(b);
  tft.fillRect(200, 50, 100, 80, rgbTo565(r, g, b));
  tft.drawRect(199, 49, 102, 82, ST77XX_WHITE);
  const char* colorNames[] = {
    "White", "Red", "Green", "Blue", "Yellow",
    "Magenta", "Cyan", "Orange", "Purple", "Pink"
  };
  tft.setCursor(10, 140);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW);
  tft.println(colorNames[selectedRgbColor]);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_GRAY);
  tft.setCursor(10, 220);
  tft.println("Up/Down: Change  B: Back");
  setRgbColor(r, g, b);
}

void drawRgbIndividualTest() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(50, 5);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.println("RGB INDIVIDUAL");
  tft.setTextSize(1);
  const char* channelNames[] = {"Red", "Green", "Blue"};
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 50);
  tft.print("Channel: ");
  tft.setTextColor(ST77XX_YELLOW);
  tft.println(channelNames[rgbIndividualChannel]);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 70);
  tft.print("Value: ");
  tft.setTextColor(ST77XX_YELLOW);
  tft.print(rgbIndividualValue);
  tft.setTextColor(ST77XX_WHITE);
  tft.println(" / 255");
  int barWidth = map(rgbIndividualValue, 0, 255, 0, 280);
  int barColor = ST77XX_RED;
  if (rgbIndividualChannel == 1) barColor = ST77XX_GREEN;
  if (rgbIndividualChannel == 2) barColor = ST77XX_BLUE;
  tft.fillRect(20, 100, 280, 30, ST77XX_DARKGRAY);
  tft.fillRect(20, 100, barWidth, 30, barColor);
  tft.drawRect(20, 100, 280, 30, ST77XX_WHITE);
  int r = (rgbIndividualChannel == 0) ? rgbIndividualValue : 0;
  int g = (rgbIndividualChannel == 1) ? rgbIndividualValue : 0;
  int b = (rgbIndividualChannel == 2) ? rgbIndividualValue : 0;
  tft.setCursor(10, 150);
  tft.setTextColor(ST77XX_RED);
  tft.print("R: ");
  tft.println(r);
  tft.setCursor(10, 168);
  tft.setTextColor(ST77XX_GREEN);
  tft.print("G: ");
  tft.println(g);
  tft.setCursor(10, 186);
  tft.setTextColor(ST77XX_BLUE);
  tft.print("B: ");
  tft.println(b);
  tft.fillRect(200, 140, 100, 60, rgbTo565(r, g, b));
  tft.drawRect(199, 139, 102, 62, ST77XX_WHITE);
  tft.setTextColor(ST77XX_GRAY);
  tft.setCursor(10, 220);
  tft.println("Up/Down: Value  Left/Right: Channel  B: Back");
  setRgbColor(r, g, b);
}

void drawAddrLedTest() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(60, 5);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.println("ADDR LEDs");
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  const char* modeNames[] = {"Rainbow", "Solid Color", "Chase", "Off"};
  tft.setCursor(10, 50);
  tft.print("Mode: ");
  tft.setTextColor(ST77XX_YELLOW);
  tft.println(modeNames[addrLedMode]);
  if (addrLedMode == 1) {
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, 70);
    tft.print("Color: ");
    tft.setTextColor(ST77XX_YELLOW);
    tft.print(addrLedColorIndex + 1);
    tft.setTextColor(ST77XX_WHITE);
    tft.print(" / ");
    tft.println(NUM_DEBUG_RGB_COLORS);
  }
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 90);
  tft.print("LEDs: ");
  tft.setTextColor(ST77XX_YELLOW);
  tft.println(NUM_ADDR_LEDS);
  tft.setTextColor(ST77XX_GRAY);
  tft.setCursor(10, 220);
  tft.println("Up/Down: Mode  Left/Right: Color  B: Back");
}

void drawAccelerometerTest() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(50, 5);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.println("ACCELEROMETER");
  tft.setTextSize(1);
  tft.setCursor(10, 40);
  if (accelerometerInitialized) {
    tft.setTextColor(ST77XX_GREEN);
    tft.println("Status: OK");
  } else {
    tft.setTextColor(ST77XX_RED);
    tft.println("Status: Not Initialized");
  }
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 65);
  tft.print("X: ");
  tft.setTextColor(ST77XX_YELLOW);
  tft.print(accelX, 2);
  tft.setTextColor(ST77XX_WHITE);
  tft.println(" g");
  tft.setCursor(10, 85);
  tft.setTextColor(ST77XX_WHITE);
  tft.print("Y: ");
  tft.setTextColor(ST77XX_YELLOW);
  tft.print(accelY, 2);
  tft.setTextColor(ST77XX_WHITE);
  tft.println(" g");
  tft.setCursor(10, 105);
  tft.setTextColor(ST77XX_WHITE);
  tft.print("Z: ");
  tft.setTextColor(ST77XX_YELLOW);
  tft.print(accelZ, 2);
  tft.setTextColor(ST77XX_WHITE);
  tft.println(" g");
  int centerX = 160;
  int centerY = 150;
  int maxBar = 50;
  int barX = constrain(map(accelX * 100, -100, 100, -maxBar, maxBar), -maxBar, maxBar);
  tft.fillRect(centerX - maxBar, centerY - 5, maxBar * 2, 10, ST77XX_DARKGRAY);
  if (barX >= 0) {
    tft.fillRect(centerX, centerY - 5, barX, 10, ST77XX_RED);
  } else {
    tft.fillRect(centerX + barX, centerY - 5, -barX, 10, ST77XX_RED);
  }
  tft.drawFastVLine(centerX, centerY - 10, 20, ST77XX_WHITE);
  int barY = constrain(map(accelY * 100, -100, 100, -maxBar, maxBar), -maxBar, maxBar);
  tft.fillRect(centerX - maxBar, centerY + 10, maxBar * 2, 10, ST77XX_DARKGRAY);
  if (barY >= 0) {
    tft.fillRect(centerX, centerY + 10, barY, 10, ST77XX_GREEN);
  } else {
    tft.fillRect(centerX + barY, centerY + 10, -barY, 10, ST77XX_GREEN);
  }
  tft.drawFastVLine(centerX, centerY + 5, 20, ST77XX_WHITE);
  tft.setTextColor(ST77XX_GRAY);
  tft.setCursor(10, 220);
  tft.println("A: Reinit  B: Back");
}

void drawSingleLedTest() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(80, 5);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.println("SINGLE LED");
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 50);
  tft.print("State: ");
  tft.setTextColor(singleLedState ? ST77XX_GREEN : ST77XX_RED);
  tft.println(singleLedState ? "ON" : "OFF");
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 70);
  tft.print("Brightness: ");
  tft.setTextColor(ST77XX_YELLOW);
  tft.print(singleLedBrightness);
  tft.setTextColor(ST77XX_WHITE);
  tft.println(" / 255");
  int barWidth = map(singleLedBrightness, 0, 255, 0, 280);
  tft.fillRect(20, 100, 280, 30, ST77XX_DARKGRAY);
  if (singleLedState) {
    tft.fillRect(20, 100, barWidth, 30, ST77XX_GREEN);
  }
  tft.drawRect(20, 100, 280, 30, ST77XX_WHITE);
  tft.fillCircle(160, 160, 30, singleLedState ? ST77XX_GREEN : ST77XX_DARKGRAY);
  tft.drawCircle(160, 160, 30, ST77XX_WHITE);
  tft.setTextColor(ST77XX_GRAY);
  tft.setCursor(10, 220);
  tft.println("A: Toggle  Up/Down: Brightness  B: Back");
}

void drawWiFiTest() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(100, 5);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.println("WIFI SCAN");
  tft.setTextSize(1);
  tft.setCursor(10, 30);
  if (wifiScanning) {
    tft.setTextColor(ST77XX_YELLOW);
    tft.println("Scanning...");
  } else {
    tft.setTextColor(ST77XX_GREEN);
    tft.print("Found: ");
    tft.setTextColor(ST77XX_YELLOW);
    tft.print(wifiNetworkCount);
    tft.setTextColor(ST77XX_GREEN);
    tft.println(" networks");
  }
  if (!wifiScanning && wifiNetworkCount > 0) {
    int startY = 50;
    int lineHeight = 20;
    int maxVisible = min(WIFI_LIST_ITEMS, wifiNetworkCount - wifiScrollPos);
    for (int i = 0; i < maxVisible; i++) {
      int idx = wifiScrollPos + i;
      if (idx >= wifiNetworkCount) break;
      int yPos = startY + i * lineHeight;
      tft.fillRect(5, yPos - 2, 310, lineHeight - 2, ST77XX_BLACK);
      tft.setTextColor(ST77XX_GRAY);
      tft.setCursor(5, yPos);
      tft.print(idx + 1);
      tft.print(": ");
      String ssid = wifiSSIDs[idx];
      if (ssid.length() == 0) {
        ssid = "(hidden)";
      }
      if (ssid.length() > 15) {
        ssid = ssid.substring(0, 15);
      }
      tft.setTextColor(ST77XX_WHITE);
      tft.print(ssid);
      tft.setTextColor(ST77XX_YELLOW);
      tft.setCursor(200, yPos);
      tft.print(wifiRSSIs[idx]);
      tft.print("dBm");
    }
    if (wifiNetworkCount > WIFI_LIST_ITEMS) {
      tft.setTextColor(ST77XX_GRAY);
      tft.setCursor(280, 30);
      tft.print(wifiScrollPos + 1);
      tft.print("/");
      tft.print(wifiNetworkCount);
    }
  } else if (!wifiScanning && wifiNetworkCount == 0) {
    tft.setTextColor(ST77XX_GRAY);
    tft.setCursor(10, 80);
    tft.println("No networks found");
  }
  tft.setTextColor(ST77XX_GRAY);
  tft.setCursor(10, 220);
  if (wifiNetworkCount > WIFI_LIST_ITEMS) {
    tft.println("A: Scan  Up/Down: Scroll  B: Back");
  } else {
    tft.println("A: Scan  B: Back");
  }
}

void drawBluetoothTest() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(70, 5);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.println("BLE SCAN");
  tft.setTextSize(1);
  tft.setCursor(10, 30);
  if (bluetoothScanning) {
    tft.setTextColor(ST77XX_YELLOW);
    tft.println("Scanning...");
  } else {
    tft.setTextColor(ST77XX_GREEN);
    tft.print("Found: ");
    tft.setTextColor(ST77XX_YELLOW);
    tft.print(bleDeviceCount);
    tft.setTextColor(ST77XX_GREEN);
    tft.println(" devices");
  }
  if (!bluetoothScanning && bleDeviceCount > 0) {
    int startY = 50;
    int lineHeight = 20;
    int maxVisible = min(BLE_LIST_ITEMS, bleDeviceCount - bleScrollPos);
    for (int i = 0; i < maxVisible; i++) {
      int idx = bleScrollPos + i;
      if (idx >= bleDeviceCount) break;
      int yPos = startY + i * lineHeight;
      tft.fillRect(5, yPos - 2, 310, lineHeight - 2, ST77XX_BLACK);
      tft.setTextColor(ST77XX_GRAY);
      tft.setCursor(5, yPos);
      tft.print(idx + 1);
      tft.print(": ");
      String displayText = bleNames[idx];
      if (displayText.length() == 0) {
        displayText = bleAddresses[idx];
      }
      if (displayText.length() > 18) {
        displayText = displayText.substring(0, 18);
      }
      tft.setTextColor(ST77XX_WHITE);
      tft.print(displayText);
      if (bleRSSIs[idx] != 0) {
        tft.setTextColor(ST77XX_YELLOW);
        tft.setCursor(200, yPos);
        tft.print(bleRSSIs[idx]);
        tft.print("dBm");
      }
    }
    if (bleDeviceCount > BLE_LIST_ITEMS) {
      tft.setTextColor(ST77XX_GRAY);
      tft.setCursor(280, 30);
      tft.print(bleScrollPos + 1);
      tft.print("/");
      tft.print(bleDeviceCount);
    }
  } else if (!bluetoothScanning && bleDeviceCount == 0) {
    tft.setTextColor(ST77XX_GRAY);
    tft.setCursor(10, 80);
    tft.println("No devices found");
  }
  tft.setTextColor(ST77XX_GRAY);
  tft.setCursor(10, 220);
  if (bleDeviceCount > BLE_LIST_ITEMS) {
    tft.println("A: Scan  Up/Down: Scroll  B: Back");
  } else {
    tft.println("A: Scan  B: Back");
  }
}

void updateAddrLeds() {
  if (debugMenu != MENU_ADDR_LEDS) {
    return;
  }
  unsigned long currentTime = millis();
  if (currentTime - addrLedLastUpdate < 50) {
    return;
  }
  addrLedLastUpdate = currentTime;
  switch (addrLedMode) {
    case 0: {
      static uint8_t rainbowHue = 0;
      rainbowHue++;
      fill_rainbow(addrLedsArray, NUM_ADDR_LEDS, rainbowHue, 7);
      FastLED.show();
      break;
    }
    case 1: {
      CRGB color = CRGB(DEBUG_RGB_COLORS[addrLedColorIndex][0], 
                        DEBUG_RGB_COLORS[addrLedColorIndex][1], 
                        DEBUG_RGB_COLORS[addrLedColorIndex][2]);
      fill_solid(addrLedsArray, NUM_ADDR_LEDS, color);
      FastLED.show();
      break;
    }
    case 2: {
      addrLedChasePos = (addrLedChasePos + 1) % (NUM_ADDR_LEDS * 2);
      fill_solid(addrLedsArray, NUM_ADDR_LEDS, CRGB::Black);
      int pos = addrLedChasePos % NUM_ADDR_LEDS;
      addrLedsArray[pos] = CRGB::White;
      if (addrLedChasePos < NUM_ADDR_LEDS) {
        addrLedsArray[(pos + 1) % NUM_ADDR_LEDS] = CRGB(128, 128, 128);
      }
      FastLED.show();
      break;
    }
    case 3:
      fill_solid(addrLedsArray, NUM_ADDR_LEDS, CRGB::Black);
      FastLED.show();
      break;
  }
}

void updateAccelerometer() {
  if (!accelerometerInitialized) {
    return;
  }
  accel.update();
  accelX = accel.x();
  accelY = accel.y();
  accelZ = accel.z();
}

void updateSingleLed() {
  if (singleLedState) {
    analogWrite(PIN_SINGLE_LED, singleLedBrightness);
  } else {
    analogWrite(PIN_SINGLE_LED, 0);
  }
}

void performWiFiScan() {
  if (wifiScanning) {
    return;
  }
  wifiScanning = true;
  wifiScrollPos = 0;
  drawWiFiTest();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  wifiNetworkCount = WiFi.scanNetworks();
  if (wifiNetworkCount > MAX_WIFI_NETWORKS) {
    wifiNetworkCount = MAX_WIFI_NETWORKS;
  }
  for (int i = 0; i < wifiNetworkCount; i++) {
    wifiSSIDs[i] = WiFi.SSID(i);
    wifiRSSIs[i] = WiFi.RSSI(i);
  }
  wifiScanning = false;
  wifiLastScan = millis();
  drawWiFiTest();
}

void performBluetoothScan() {
  if (bluetoothScanning || pBLEScan == nullptr) {
    return;
  }
  bluetoothScanning = true;
  bleScrollPos = 0;
  bleDeviceCount = 0;
  drawBluetoothTest();
  for (int i = 0; i < MAX_BLE_DEVICES; i++) {
    bleAddresses[i] = "";
    bleNames[i] = "";
    bleRSSIs[i] = 0;
  }
  BLEScanResults foundDevices = *pBLEScan->start(5, false);
  bleDeviceCount = foundDevices.getCount();
  if (bleDeviceCount > MAX_BLE_DEVICES) {
    bleDeviceCount = MAX_BLE_DEVICES;
  }
  for (int i = 0; i < bleDeviceCount; i++) {
    BLEAdvertisedDevice device = foundDevices.getDevice(i);
    bleAddresses[i] = device.getAddress().toString().c_str();
    if (device.haveName()) {
      bleNames[i] = device.getName().c_str();
    } else {
      bleNames[i] = "";
    }
    bleRSSIs[i] = device.getRSSI();
  }
  pBLEScan->clearResults();
  bluetoothScanning = false;
  bleLastScan = millis();
  drawBluetoothTest();
}

void updateSongPlayback() {
  if (!isPlayingSong) {
    return;
  }
  unsigned long currentTime = millis();
  if (currentNoteIndex < NUM_POPCORN_NOTES) {
    unsigned long noteDuration = POPCORN_MELODY[currentNoteIndex].duration;
    if (currentTime - noteStartTime >= noteDuration) {
      currentNoteIndex++;
      if (currentNoteIndex >= NUM_POPCORN_NOTES) {
        isPlayingSong = false;
        songFinished = true;
        noTone(PIN_BUZZER);
        drawBuzzerTest();
        return;
      }
      noteStartTime = currentTime;
      int frequency = POPCORN_MELODY[currentNoteIndex].frequency;
      if (frequency == NOTE_REST) {
        noTone(PIN_BUZZER);
      } else {
        tone(PIN_BUZZER, frequency);
      }
      if (currentNoteIndex % 3 == 0) {
        drawBuzzerTest();
      }
    }
  }
}

void startSong() {
  isPlayingSong = true;
  songFinished = false;
  currentNoteIndex = 0;
  noteStartTime = millis();
  int frequency = POPCORN_MELODY[0].frequency;
  if (frequency != NOTE_REST) {
    tone(PIN_BUZZER, frequency);
  }
  drawBuzzerTest();
}

void handleOutputMenuInput() {
  unsigned long currentTime = millis();

  int joyRaw = analogRead(PIN_JOYSTICK_Y);
  bool joyUpZone = joyRaw > MAIN_MENU_JOY_CENTER + MAIN_MENU_JOY_THRESHOLD;
  bool joyDownZone = joyRaw < MAIN_MENU_JOY_CENTER - MAIN_MENU_JOY_THRESHOLD;
  if (!joyUpZone) {
    hwMenuJoyLatchUp = false;
  }
  if (!joyDownZone) {
    hwMenuJoyLatchDown = false;
  }

  if (currentTime - debugLastMenuNavTime < MENU_NAV_DELAY_DEBUG) {
    return;
  }
  bool navChanged = false;
  if (isButtonJustPressed(PIN_UP, debugLastUpState)
      || (joyUpZone && !hwMenuJoyLatchUp)) {
    selectedOutputTest = (selectedOutputTest - 1 + NUM_OUTPUT_TESTS) % NUM_OUTPUT_TESTS;
    navChanged = true;
    debugLastMenuNavTime = currentTime;
    if (joyUpZone) {
      hwMenuJoyLatchUp = true;
    }
  } else if (isButtonJustPressed(PIN_DOWN, debugLastDownState)
             || (joyDownZone && !hwMenuJoyLatchDown)) {
    selectedOutputTest = (selectedOutputTest + 1) % NUM_OUTPUT_TESTS;
    navChanged = true;
    debugLastMenuNavTime = currentTime;
    if (joyDownZone) {
      hwMenuJoyLatchDown = true;
    }
  }
  if (navChanged) {
    drawOutputMenu();
  }
  if (currentTime - debugLastButtonTime < BUTTON_DEBOUNCE) {
    return;
  }
  if (isButtonJustPressed(PIN_A, debugLastAState)) {
    debugLastButtonTime = currentTime;
    switch (selectedOutputTest) {
      case TEST_BUZZER_PITCHES:
        debugMenu = MENU_BUZZER;
        isPlayingSong = false;
        songFinished = false;
        currentNoteIndex = 0;
        drawBuzzerTest();
        break;
      case TEST_RGB_COLORS:
        debugMenu = MENU_RGB_LED;
        selectedRgbColor = 0;
        drawRgbColorTest();
        break;
      case TEST_RGB_INDIVIDUAL:
        debugMenu = MENU_RGB_INDIVIDUAL;
        rgbIndividualChannel = 0;
        rgbIndividualValue = 128;
        drawRgbIndividualTest();
        break;
      case TEST_ADDR_LEDS:
        debugMenu = MENU_ADDR_LEDS;
        addrLedMode = 0;
        addrLedColorIndex = 0;
        addrLedChasePos = 0;
        FastLED.addLeds<WS2812, PIN_ADDR_LEDS, GRB>(addrLedsArray, NUM_ADDR_LEDS);
        FastLED.setBrightness(50);
        FastLED.clear();
        FastLED.show();
        addrLedLastUpdate = 0;
        updateAddrLeds();
        drawAddrLedTest();
        break;
      case TEST_SINGLE_LED:
        debugMenu = MENU_SINGLE_LED;
        singleLedState = false;
        singleLedBrightness = 128;
        pinMode(PIN_SINGLE_LED, OUTPUT);
        updateSingleLed();
        drawSingleLedTest();
        break;
    }
  } else if (isButtonJustPressed(PIN_B, debugLastBState)) {
    debugMenu = MENU_MAIN;
    debugLastButtonTime = currentTime;
    drawDebugMainMenu();
  }
}

void initHardwareDebug() {
  tft.fillScreen(ST77XX_BLACK);
  
  debugMenu = MENU_MAIN;
  selectedHardwareCategory = 0;
  selectedWirelessTest = 0;
  selectedOutputTest = 0;
  hwMenuJoyLatchUp = false;
  hwMenuJoyLatchDown = false;
  noTone(PIN_BUZZER);
  setRgbColor(0, 0, 0);
  FastLED.clear();
  FastLED.show();
  analogWrite(PIN_SINGLE_LED, 0);
  
  inputTestInitialized = false;
  debugLastInputUpdate = 0;
  
  drawDebugMainMenu();
}

void handleBuzzerTestInput() {
  unsigned long currentTime = millis();
  if (currentTime - debugLastButtonTime < BUTTON_DEBOUNCE) {
    return;
  }
  if (isButtonJustPressed(PIN_A, debugLastAState)) {
    if (!isPlayingSong) {
      startSong();
      debugLastButtonTime = currentTime;
    }
  }
  if (isButtonJustPressed(PIN_B, debugLastBState)) {
    debugMenu = MENU_OUTPUTS;
    debugLastButtonTime = currentTime;
    isPlayingSong = false;
    songFinished = false;
    noTone(PIN_BUZZER);
    setRgbColor(0, 0, 0);
    drawOutputMenu();
  }
}

void handleRgbColorTestInput() {
  unsigned long currentTime = millis();
  if (currentTime - debugLastMenuNavTime < MENU_NAV_DELAY_DEBUG) {
    return;
  }
  bool changed = false;
  if (isButtonJustPressed(PIN_UP, debugLastUpState)) {
    selectedRgbColor = (selectedRgbColor - 1 + NUM_DEBUG_RGB_COLORS) % NUM_DEBUG_RGB_COLORS;
    changed = true;
    debugLastMenuNavTime = currentTime;
  } else if (isButtonJustPressed(PIN_DOWN, debugLastDownState)) {
    selectedRgbColor = (selectedRgbColor + 1) % NUM_DEBUG_RGB_COLORS;
    changed = true;
    debugLastMenuNavTime = currentTime;
  }
  if (changed) {
    drawRgbColorTest();
  }
  if (currentTime - debugLastButtonTime < BUTTON_DEBOUNCE) {
    return;
  }
  if (isButtonJustPressed(PIN_B, debugLastBState)) {
    debugMenu = MENU_OUTPUTS;
    debugLastButtonTime = currentTime;
    setRgbColor(0, 0, 0);
    drawOutputMenu();
  }
}

void handleRgbIndividualInput() {
  unsigned long currentTime = millis();
  if (currentTime - debugLastMenuNavTime < MENU_NAV_DELAY_DEBUG) {
    return;
  }
  bool changed = false;
  if (isButtonJustPressed(PIN_UP, debugLastUpState)) {
    rgbIndividualValue = constrain(rgbIndividualValue + 5, 0, 255);
    changed = true;
    debugLastMenuNavTime = currentTime;
  } else if (isButtonJustPressed(PIN_DOWN, debugLastDownState)) {
    rgbIndividualValue = constrain(rgbIndividualValue - 5, 0, 255);
    changed = true;
    debugLastMenuNavTime = currentTime;
  } else if (isButtonJustPressed(PIN_LEFT, debugLastLeftState)) {
    rgbIndividualChannel = (rgbIndividualChannel - 1 + 3) % 3;
    changed = true;
    debugLastMenuNavTime = currentTime;
  } else if (isButtonJustPressed(PIN_RIGHT, debugLastRightState)) {
    rgbIndividualChannel = (rgbIndividualChannel + 1) % 3;
    changed = true;
    debugLastMenuNavTime = currentTime;
  }
  if (changed) {
    drawRgbIndividualTest();
  }
  if (currentTime - debugLastButtonTime < BUTTON_DEBOUNCE) {
    return;
  }
  if (isButtonJustPressed(PIN_B, debugLastBState)) {
    debugMenu = MENU_OUTPUTS;
    debugLastButtonTime = currentTime;
    setRgbColor(0, 0, 0);
    drawOutputMenu();
  }
}

void handleAddrLedInput() {
  unsigned long currentTime = millis();
  if (currentTime - debugLastMenuNavTime < MENU_NAV_DELAY_DEBUG) {
    return;
  }
  bool changed = false;
  if (isButtonJustPressed(PIN_UP, debugLastUpState)) {
    addrLedMode = (addrLedMode + 1) % 4;
    changed = true;
    debugLastMenuNavTime = currentTime;
  } else if (isButtonJustPressed(PIN_DOWN, debugLastDownState)) {
    addrLedMode = (addrLedMode - 1 + 4) % 4;
    changed = true;
    debugLastMenuNavTime = currentTime;
  } else if (isButtonJustPressed(PIN_LEFT, debugLastLeftState) && addrLedMode == 1) {
    addrLedColorIndex = (addrLedColorIndex - 1 + NUM_DEBUG_RGB_COLORS) % NUM_DEBUG_RGB_COLORS;
    changed = true;
    debugLastMenuNavTime = currentTime;
  } else if (isButtonJustPressed(PIN_RIGHT, debugLastRightState) && addrLedMode == 1) {
    addrLedColorIndex = (addrLedColorIndex + 1) % NUM_DEBUG_RGB_COLORS;
    changed = true;
    debugLastMenuNavTime = currentTime;
  }
  if (changed) {
    addrLedLastUpdate = 0;
    updateAddrLeds();
    drawAddrLedTest();
  }
  if (currentTime - debugLastButtonTime < BUTTON_DEBOUNCE) {
    return;
  }
  if (isButtonJustPressed(PIN_B, debugLastBState)) {
    debugMenu = MENU_OUTPUTS;
    debugLastButtonTime = currentTime;
    addrLedMode = 3;
    FastLED.clear();
    FastLED.show();
    delay(10);
    drawOutputMenu();
  }
}

void handleSingleLedInput() {
  unsigned long currentTime = millis();
  if (currentTime - debugLastMenuNavTime < MENU_NAV_DELAY_DEBUG) {
    return;
  }
  bool changed = false;
  if (isButtonJustPressed(PIN_UP, debugLastUpState)) {
    singleLedBrightness = constrain(singleLedBrightness + 10, 0, 255);
    changed = true;
    debugLastMenuNavTime = currentTime;
  } else if (isButtonJustPressed(PIN_DOWN, debugLastDownState)) {
    singleLedBrightness = constrain(singleLedBrightness - 10, 0, 255);
    changed = true;
    debugLastMenuNavTime = currentTime;
  }
  if (changed) {
    updateSingleLed();
    drawSingleLedTest();
  }
  if (currentTime - debugLastButtonTime < BUTTON_DEBOUNCE) {
    return;
  }
  if (isButtonJustPressed(PIN_A, debugLastAState)) {
    singleLedState = !singleLedState;
    debugLastButtonTime = currentTime;
    updateSingleLed();
    drawSingleLedTest();
  }
  if (isButtonJustPressed(PIN_B, debugLastBState)) {
    debugMenu = MENU_OUTPUTS;
    debugLastButtonTime = currentTime;
    singleLedState = false;
    updateSingleLed();
    drawOutputMenu();
  }
}

void handleWiFiInput() {
  unsigned long currentTime = millis();
  if (currentTime - debugLastMenuNavTime < MENU_NAV_DELAY_DEBUG) {
  } else {
    bool scrollChanged = false;
    if (isButtonJustPressed(PIN_UP, debugLastUpState)) {
      if (wifiScrollPos > 0) {
        wifiScrollPos--;
        scrollChanged = true;
        debugLastMenuNavTime = currentTime;
      }
    } else if (isButtonJustPressed(PIN_DOWN, debugLastDownState)) {
      int maxScroll = max(0, wifiNetworkCount - WIFI_LIST_ITEMS);
      if (wifiScrollPos < maxScroll) {
        wifiScrollPos++;
        scrollChanged = true;
        debugLastMenuNavTime = currentTime;
      }
    }
    if (scrollChanged) {
      drawWiFiTest();
    }
  }
  if (currentTime - debugLastButtonTime < BUTTON_DEBOUNCE) {
    return;
  }
  if (isButtonJustPressed(PIN_A, debugLastAState)) {
    debugLastButtonTime = currentTime;
    performWiFiScan();
  }
  if (isButtonJustPressed(PIN_B, debugLastBState)) {
    debugMenu = MENU_WIRELESS_MENU;
    debugLastButtonTime = currentTime;
    drawWirelessMenu();
  }
}

void handleBluetoothInput() {
  unsigned long currentTime = millis();
  if (currentTime - debugLastMenuNavTime < MENU_NAV_DELAY_DEBUG) {
  } else {
    bool scrollChanged = false;
    if (isButtonJustPressed(PIN_UP, debugLastUpState)) {
      if (bleScrollPos > 0) {
        bleScrollPos--;
        scrollChanged = true;
        debugLastMenuNavTime = currentTime;
      }
    } else if (isButtonJustPressed(PIN_DOWN, debugLastDownState)) {
      int maxScroll = max(0, bleDeviceCount - BLE_LIST_ITEMS);
      if (bleScrollPos < maxScroll) {
        bleScrollPos++;
        scrollChanged = true;
        debugLastMenuNavTime = currentTime;
      }
    }
    if (scrollChanged) {
      drawBluetoothTest();
    }
  }
  if (currentTime - debugLastButtonTime < BUTTON_DEBOUNCE) {
    return;
  }
  if (isButtonJustPressed(PIN_A, debugLastAState)) {
    debugLastButtonTime = currentTime;
    performBluetoothScan();
  }
  if (isButtonJustPressed(PIN_B, debugLastBState)) {
    debugMenu = MENU_WIRELESS_MENU;
    debugLastButtonTime = currentTime;
    drawWirelessMenu();
  }
}

void loopHardwareDebug() {
  unsigned long currentTime = millis();
  
  switch (debugMenu) {
    case MENU_MAIN:
      handleDebugMainMenuInput();
      break;
    case MENU_INPUTS:
      if (isButtonJustPressed(PIN_B, debugLastBState)
          && currentTime - debugLastButtonTime >= BUTTON_DEBOUNCE) {
        debugMenu = MENU_MAIN;
        debugLastButtonTime = currentTime;
        drawDebugMainMenu();
        break;
      }
      if (currentTime - debugLastInputUpdate >= INPUT_UPDATE_INTERVAL) {
        updateInputTestValues();
        if (accelerometerInitialized) {
          updateAccelerometer();
        }
        debugLastInputUpdate = currentTime;
      }
      break;
    case MENU_OUTPUTS:
      handleOutputMenuInput();
      break;
    case MENU_WIRELESS_MENU:
      handleWirelessMenuInput();
      break;
    case MENU_BUZZER:
      handleBuzzerTestInput();
      updateSongPlayback();
      break;
    case MENU_RGB_LED:
      handleRgbColorTestInput();
      break;
    case MENU_RGB_INDIVIDUAL:
      handleRgbIndividualInput();
      break;
    case MENU_ADDR_LEDS:
      handleAddrLedInput();
      updateAddrLeds();
      break;
    case MENU_SINGLE_LED:
      handleSingleLedInput();
      break;
    case MENU_WIFI:
      handleWiFiInput();
      break;
    case MENU_BLUETOOTH:
      handleBluetoothInput();
      break;
  }
  
  delay(10);
}

// ============================================================================
// TETRIS PROGRAM
// ============================================================================

// Tetris constants
const int GRID_WIDTH = 10;
const int GRID_HEIGHT = 18;
const int BLOCK_SIZE = 11;
const int GRID_OFFSET_X = 50;
const int GRID_OFFSET_Y = 15;
const unsigned long NORMAL_MOVE_DELAY = 500;
const unsigned long FAST_MOVE_DELAY = 50;
const unsigned long PAUSE_DEBOUNCE = 200;
const unsigned long LINE_FLASH_DELAY = 50;
const int LINE_FLASH_COUNT = 6;
const int POINTS_PER_LINE = 100;

// Tetromino shapes
const uint8_t TETRIS_SHAPES[7][4][4][4] = {
  {{{0,0,0,0}, {1,1,1,1}, {0,0,0,0}, {0,0,0,0}},
   {{0,0,1,0}, {0,0,1,0}, {0,0,1,0}, {0,0,1,0}},
   {{0,0,0,0}, {0,0,0,0}, {1,1,1,1}, {0,0,0,0}},
   {{0,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,1,0,0}}},
  {{{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
   {{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
   {{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
   {{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}}},
  {{{0,1,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
   {{0,1,0,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0}},
   {{0,0,0,0}, {1,1,1,0}, {0,1,0,0}, {0,0,0,0}},
   {{0,1,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0}}},
  {{{0,1,1,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0}},
   {{0,1,0,0}, {0,1,1,0}, {0,0,1,0}, {0,0,0,0}},
   {{0,0,0,0}, {0,1,1,0}, {1,1,0,0}, {0,0,0,0}},
   {{1,0,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0}}},
  {{{1,1,0,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
   {{0,0,1,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0}},
   {{0,0,0,0}, {1,1,0,0}, {0,1,1,0}, {0,0,0,0}},
   {{0,1,0,0}, {1,1,0,0}, {1,0,0,0}, {0,0,0,0}}},
  {{{1,0,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
   {{0,1,1,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0}},
   {{0,0,0,0}, {1,1,1,0}, {0,0,1,0}, {0,0,0,0}},
   {{0,1,0,0}, {0,1,0,0}, {1,1,0,0}, {0,0,0,0}}},
  {{{0,0,1,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
   {{0,1,0,0}, {0,1,0,0}, {0,1,1,0}, {0,0,0,0}},
   {{0,0,0,0}, {1,1,1,0}, {1,0,0,0}, {0,0,0,0}},
   {{1,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0}}}
};

const uint16_t TETRIS_COLORS[7] = {
  0x07FF, 0xFFE0, 0xF81F, 0x07E0, 0xF800, 0x001F, 0xFD20
};

const uint8_t TETRIS_RGB_COLORS[7][3] = {
  {0, 255, 255}, {255, 255, 0}, {255, 0, 255}, {0, 255, 0},
  {255, 0, 0}, {0, 0, 255}, {255, 165, 0}
};

uint8_t tetrisGrid[GRID_HEIGHT][GRID_WIDTH];

struct TetrisPiece {
  int type;
  int rotation;
  int x;
  int y;
  int color;
} currentTetrisPiece;

struct TetrisPrevPiece {
  int x;
  int y;
  int rotation;
} prevTetrisPiece;

struct TetrisGameState {
  unsigned long lastMoveTime;
  unsigned long lastButtonTime;
  unsigned long moveDelay;
  int score;
  bool isOver;
  bool isPaused;
} tetrisGameState;

void drawTetrisBlock(int x, int y, uint16_t color) {
  int screenX = GRID_OFFSET_X + x * BLOCK_SIZE;
  int screenY = GRID_OFFSET_Y + y * BLOCK_SIZE;
  tft.fillRect(screenX, screenY, BLOCK_SIZE - 1, BLOCK_SIZE - 1, color);
}

void drawTetrisGrid() {
  for (int y = 0; y < GRID_HEIGHT; y++) {
    for (int x = 0; x < GRID_WIDTH; x++) {
      if (tetrisGrid[y][x] > 0) {
        drawTetrisBlock(x, y, TETRIS_COLORS[tetrisGrid[y][x] - 1]);
      } else {
        drawTetrisBlock(x, y, ST77XX_BLACK);
      }
    }
  }
}

void drawTetrisCurrentPiece() {
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {
      if (TETRIS_SHAPES[currentTetrisPiece.type][currentTetrisPiece.rotation][y][x]) {
        int gridX = currentTetrisPiece.x + x;
        int gridY = currentTetrisPiece.y + y;
        if (gridY >= 0 && gridY < GRID_HEIGHT && gridX >= 0 && gridX < GRID_WIDTH) {
          drawTetrisBlock(gridX, gridY, TETRIS_COLORS[currentTetrisPiece.color]);
        }
      }
    }
  }
}

void eraseTetrisPreviousPiece() {
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {
      if (TETRIS_SHAPES[currentTetrisPiece.type][prevTetrisPiece.rotation][y][x]) {
        int gridX = prevTetrisPiece.x + x;
        int gridY = prevTetrisPiece.y + y;
        if (gridY >= 0 && gridY < GRID_HEIGHT && gridX >= 0 && gridX < GRID_WIDTH) {
          if (tetrisGrid[gridY][gridX] > 0) {
            drawTetrisBlock(gridX, gridY, TETRIS_COLORS[tetrisGrid[gridY][gridX] - 1]);
          } else {
            drawTetrisBlock(gridX, gridY, ST77XX_BLACK);
          }
        }
      }
    }
  }
}

void drawTetrisScore() {
  tft.fillRect(200, 10, 110, 30, ST77XX_BLACK);
  tft.setCursor(200, 10);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.print("Score:");
  tft.setCursor(200, 30);
  tft.print(tetrisGameState.score);
}

void drawTetrisControls() {
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(200, 60);
  tft.println("Controls:");
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(200, 75);
  tft.println("L/R: Move");
  tft.setCursor(200, 90);
  tft.println("Up: Rotate");
  tft.setCursor(200, 105);
  tft.println("Dn: Drop");
  tft.setCursor(200, 120);
  tft.println("A: Pause");
  tft.setCursor(200, 135);
  tft.println("B: Restart");
}

void drawTetrisBorder() {
  tft.drawRect(GRID_OFFSET_X - 2, GRID_OFFSET_Y - 2, 
               GRID_WIDTH * BLOCK_SIZE + 2, GRID_HEIGHT * BLOCK_SIZE + 2, 
               ST77XX_WHITE);
}

bool checkTetrisCollision(int offsetX, int offsetY, int rotation) {
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {
      if (TETRIS_SHAPES[currentTetrisPiece.type][rotation][y][x]) {
        int newX = currentTetrisPiece.x + x + offsetX;
        int newY = currentTetrisPiece.y + y + offsetY;
        
        if (newX < 0 || newX >= GRID_WIDTH || newY >= GRID_HEIGHT) {
          return true;
        }
        
        if (newY >= 0 && tetrisGrid[newY][newX] > 0) {
          return true;
        }
      }
    }
  }
  return false;
}

void lockTetrisPiece() {
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {
      if (TETRIS_SHAPES[currentTetrisPiece.type][currentTetrisPiece.rotation][y][x]) {
        int gridX = currentTetrisPiece.x + x;
        int gridY = currentTetrisPiece.y + y;
        if (gridY >= 0 && gridY < GRID_HEIGHT && gridX >= 0 && gridX < GRID_WIDTH) {
          tetrisGrid[gridY][gridX] = currentTetrisPiece.color + 1;
        }
      }
    }
  }
}

void clearTetrisLines() {
  int linesCleared = 0;
  
  for (int y = GRID_HEIGHT - 1; y >= 0; y--) {
    bool fullLine = true;
    
    for (int x = 0; x < GRID_WIDTH; x++) {
      if (tetrisGrid[y][x] == 0) {
        fullLine = false;
        break;
      }
    }
    
    if (fullLine) {
      linesCleared++;
      
      for (int flash = 0; flash < LINE_FLASH_COUNT; flash++) {
        uint16_t flashColor = (flash % 2 == 0) ? 0x07E0 : 0x001F;
        
        for (int x = 0; x < GRID_WIDTH; x++) {
          drawTetrisBlock(x, y, flashColor);
        }
        
        if (flash % 2 == 0) {
          setRgbColor(0, 255, 0);
        } else {
          setRgbColor(0, 0, 255);
        }
        
        delay(LINE_FLASH_DELAY);
      }
      
      tetrisGameState.score += POINTS_PER_LINE;
      tone(PIN_BUZZER, 800, 100);
      
      for (int moveY = y; moveY > 0; moveY--) {
        for (int x = 0; x < GRID_WIDTH; x++) {
          tetrisGrid[moveY][x] = tetrisGrid[moveY - 1][x];
        }
      }
      
      for (int x = 0; x < GRID_WIDTH; x++) {
        tetrisGrid[0][x] = 0;
      }
      
      y++;
    }
  }
  
  if (linesCleared > 0) {
    setRgbColor(TETRIS_RGB_COLORS[currentTetrisPiece.color][0], 
                TETRIS_RGB_COLORS[currentTetrisPiece.color][1], 
                TETRIS_RGB_COLORS[currentTetrisPiece.color][2]);
  }
}

void spawnTetrisNewPiece() {
  currentTetrisPiece.type = random(7);
  currentTetrisPiece.color = currentTetrisPiece.type;
  currentTetrisPiece.rotation = 0;
  currentTetrisPiece.x = 3;
  currentTetrisPiece.y = 0;
  
  prevTetrisPiece.x = currentTetrisPiece.x;
  prevTetrisPiece.y = currentTetrisPiece.y;
  prevTetrisPiece.rotation = currentTetrisPiece.rotation;
  
  setRgbColor(TETRIS_RGB_COLORS[currentTetrisPiece.color][0], 
              TETRIS_RGB_COLORS[currentTetrisPiece.color][1], 
              TETRIS_RGB_COLORS[currentTetrisPiece.color][2]);
  
  if (checkTetrisCollision(0, 0, 0)) {
    tetrisGameState.isOver = true;
  }
}

void resetTetrisGame() {
  memset(tetrisGrid, 0, sizeof(tetrisGrid));
  tetrisGameState.score = 0;
  tetrisGameState.isOver = false;
  tetrisGameState.isPaused = false;
  tetrisGameState.moveDelay = NORMAL_MOVE_DELAY;
  
  tft.fillScreen(ST77XX_BLACK);
  drawTetrisBorder();
  spawnTetrisNewPiece();
  drawTetrisScore();
  drawTetrisControls();
}

void handleTetrisGameOver() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(80, 100);
  tft.setTextColor(ST77XX_RED);
  tft.setTextSize(3);
  tft.println("GAME");
  tft.setCursor(80, 130);
  tft.println("OVER");
  tft.setTextSize(2);
  tft.setCursor(60, 170);
  tft.setTextColor(ST77XX_WHITE);
  tft.print("Score: ");
  tft.println(tetrisGameState.score);
  tft.setTextSize(1);
  tft.setCursor(50, 200);
  tft.println("Press B to Restart");
  
  setRgbColor(255, 0, 0);
  tone(PIN_BUZZER, 200, 1000);
  
  while (true) {
    if (isButtonPressed(PIN_B)) {
      delay(200);
      resetTetrisGame();
      return;
    }
    delay(50);
  }
}

void toggleTetrisPause() {
  tetrisGameState.isPaused = !tetrisGameState.isPaused;
  
  if (tetrisGameState.isPaused) {
    tft.fillRect(80, 100, 120, 40, ST77XX_BLACK);
    tft.drawRect(79, 99, 122, 42, ST77XX_WHITE);
    tft.setCursor(100, 110);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setTextSize(2);
    tft.println("PAUSED");
    setRgbColor(255, 255, 0);
  } else {
    tft.fillRect(79, 99, 122, 42, ST77XX_BLACK);
    drawTetrisBorder();
    drawTetrisGrid();
    drawTetrisCurrentPiece();
    drawTetrisControls();
    setRgbColor(TETRIS_RGB_COLORS[currentTetrisPiece.color][0], 
                TETRIS_RGB_COLORS[currentTetrisPiece.color][1], 
                TETRIS_RGB_COLORS[currentTetrisPiece.color][2]);
  }
}

void processTetrisInput() {
  unsigned long currentTime = millis();
  
  if (currentTime - tetrisGameState.lastButtonTime < BUTTON_DEBOUNCE) {
    return;
  }
  
  bool leftPressed = isButtonPressed(PIN_LEFT);
  bool rightPressed = isButtonPressed(PIN_RIGHT);
  bool downPressed = isButtonPressed(PIN_DOWN);
  bool upPressed = isButtonPressed(PIN_UP);
  bool aPressed = isButtonPressed(PIN_A);
  bool bPressed = isButtonPressed(PIN_B);
  
  if (aPressed) {
    toggleTetrisPause();
    tetrisGameState.lastButtonTime = currentTime;
    delay(PAUSE_DEBOUNCE);
    return;
  }
  
  if (bPressed) {
    delay(200);
    resetTetrisGame();
    tetrisGameState.lastButtonTime = currentTime;
    return;
  }
  
  if (tetrisGameState.isPaused) {
    return;
  }
  
  bool pieceMoved = false;
  
  if (leftPressed && !checkTetrisCollision(-1, 0, currentTetrisPiece.rotation)) {
    prevTetrisPiece.x = currentTetrisPiece.x;
    prevTetrisPiece.y = currentTetrisPiece.y;
    prevTetrisPiece.rotation = currentTetrisPiece.rotation;
    currentTetrisPiece.x--;
    tone(PIN_BUZZER, 400, 50);
    tetrisGameState.lastButtonTime = currentTime;
    pieceMoved = true;
  }
  
  if (rightPressed && !checkTetrisCollision(1, 0, currentTetrisPiece.rotation)) {
    prevTetrisPiece.x = currentTetrisPiece.x;
    prevTetrisPiece.y = currentTetrisPiece.y;
    prevTetrisPiece.rotation = currentTetrisPiece.rotation;
    currentTetrisPiece.x++;
    tone(PIN_BUZZER, 450, 50);
    tetrisGameState.lastButtonTime = currentTime;
    pieceMoved = true;
  }
  
  if (downPressed) {
    tetrisGameState.moveDelay = FAST_MOVE_DELAY;
  } else {
    tetrisGameState.moveDelay = NORMAL_MOVE_DELAY;
  }
  
  if (upPressed) {
    int newRotation = (currentTetrisPiece.rotation + 1) % 4;
    if (!checkTetrisCollision(0, 0, newRotation)) {
      prevTetrisPiece.x = currentTetrisPiece.x;
      prevTetrisPiece.y = currentTetrisPiece.y;
      prevTetrisPiece.rotation = currentTetrisPiece.rotation;
      currentTetrisPiece.rotation = newRotation;
      tone(PIN_BUZZER, 500, 50);
      tetrisGameState.lastButtonTime = currentTime;
      pieceMoved = true;
    }
  }
  
  if (pieceMoved) {
    eraseTetrisPreviousPiece();
    drawTetrisCurrentPiece();
  }
}

void updateTetrisGame() {
  if (tetrisGameState.isPaused) {
    return;
  }
  
  unsigned long currentTime = millis();
  
  if (currentTime - tetrisGameState.lastMoveTime > tetrisGameState.moveDelay) {
    if (!checkTetrisCollision(0, 1, currentTetrisPiece.rotation)) {
      prevTetrisPiece.x = currentTetrisPiece.x;
      prevTetrisPiece.y = currentTetrisPiece.y;
      prevTetrisPiece.rotation = currentTetrisPiece.rotation;
      currentTetrisPiece.y++;
      eraseTetrisPreviousPiece();
      drawTetrisCurrentPiece();
    } else {
      lockTetrisPiece();
      clearTetrisLines();
      spawnTetrisNewPiece();
      drawTetrisGrid();
      drawTetrisScore();
    }
    tetrisGameState.lastMoveTime = currentTime;
  }
}

void initTetris() {
  tft.fillScreen(ST77XX_BLACK);
  
  // Initialize RGB LED PWM
  analogWriteResolution(PIN_RED, PWM_RESOLUTION_BITS);
  analogWriteResolution(PIN_GREEN, PWM_RESOLUTION_BITS);
  analogWriteResolution(PIN_BLUE, PWM_RESOLUTION_BITS);
  
  // Seed random number generator
  randomSeed(analogRead(PIN_X));
  
  // Show splash screen
  tft.setCursor(60, 100);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(3);
  tft.println("TETRIS");
  tft.setCursor(40, 140);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.println("Arrows: Move/Rotate");
  tft.setCursor(40, 155);
  tft.println("Down: Drop faster");
  tft.setCursor(40, 170);
  tft.println("A: Pause  B: Restart");
  
  delay(2000);
  
  resetTetrisGame();
}

void loopTetris() {
  if (tetrisGameState.isOver) {
    handleTetrisGameOver();
  }
  
  processTetrisInput();
  updateTetrisGame();
  
  delay(10);
}

// ============================================================================
// BYU–IDAHO DEMO PROGRAM
// ==========================================================================

void drawBYUISplashScreen() {
  tft.fillScreen(ST77XX_BLACK);

  /* "BYU-I" at text size 10 → 5×(6×10) ≈ 300px wide on 320px display */
  tft.setTextSize(10);
  tft.setTextColor(ST77XX_BRIGHTSKY);
  tft.setCursor(10, 4);
  tft.println("BYU-I");

  tft.setTextSize(3);
  tft.setTextColor(ST77XX_WHITE);
  const int deptLineStep = 25;
  int y = 96;
  tft.setCursor(6, y);
  tft.println("Computer");
  tft.setCursor(6, y + deptLineStep);
  tft.println("S and");
  tft.setCursor(6, y + 2 * deptLineStep);
  tft.println("Engineering");
  tft.setCursor(6, y + 3 * deptLineStep);
  tft.println("Department");

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_GRAY);
  tft.setCursor(10, 220);
  tft.println("B: Back");
}

void initBYUIDemo() {
  noTone(PIN_BUZZER);
  setRgbColor(0, 0, 0);
  analogWrite(PIN_SINGLE_LED, 0);
  byuDemoLastBState = isButtonPressed(PIN_B);
  drawBYUISplashScreen();
}

void loopBYUIDemo() {
  if (isButtonJustPressed(PIN_B, byuDemoLastBState)) {
    FastLED.clear();
    FastLED.show();
    FastLED.setBrightness(50);
    returnToMainMenu();
    return;
  }

  unsigned long t = millis();
  const unsigned long PAT_MS = 7000;
  uint8_t pat = (t / PAT_MS) % 5;

  CRGB byuBlue(0, 70, 255);
  CRGB byuWhite(235, 238, 255);

  switch (pat) {
    case 0: {
      /* Two thirds of cycle blue, one third white → 2:1 blue:white when both visible */
      uint32_t phaseMs = t % (280 * 3);
      bool bluePhase = phaseMs < (280 * 2);
      fill_solid(addrLedsAay, NUM_ADDR_LEDS, bluePhase ? byuBlue : byuWhite);
      break;
    }
    case 1: {
      fill_solid(addrLedsArray, NUM_ADDR_LEDS, CRGB::Black);
      int head = (t / 48) % NUM_ADDR_LEDS;
      addrLedsArray[head] = byuWhite;
      addrLedsArray[(head + 1) % NUM_ADDR_LEDS] = byuBlue;
      addrLedsArray[(head + 2) % NUM_ADDR_LEDS] = byuBlue;
      break;
    }
    case 2: {
      int shift = (t / 70) % NUM_ADDR_LEDS;
      for (int i = 0; i < NUM_ADDR_LEDS; i++) {
        int wave = ((i + shift) % 3);
        addrLedsArray[i] = (wave < 2) ? byuBlue : byuWhite;
      }
      break;
    }
    case 3: {
      int shift = (t / 380) % NUM_ADDR_LEDS;
      for (int i = 0; i < NUM_ADDR_LEDS; i++) {
        int tri = ((i + shift) % 3);
        addrLedsArray[i] = (tri < 2) ? byuBlue : byuWhite;
      }
      break;
    }
    case 4: {
      uint8_t br = beatsin8(14, 28, 255);
      CRGB c = CRGB(
        scale8(byuWhite.r, br),
        scale8(byuWhite.g, br),
        scale8(byuBlue.b, br));
      fill_solid(addrLedsArray, NUM_ADDR_LEDS, c);
      break;
    }
    default:
      fill_solid(addrLedsArray, NUM_ADDR_LEDS, CRGB::Black);
      break;
  }

  FastLED.setBrightness(105);
  FastLED.show();
  FastLED.setBrightness(50);
  delay(15);
}

// ============================================================================
// ARDUINO SETUP & LOOP
// ============================================================================

void setup() {
  Serial.begin(9600);
  Serial.println("Starting Demo Programs Launcher...");
  
  // Initialize display (ST7789 is write-only — no MISO)
  SPI.begin(TFT_SCLK, -1, TFT_MOSI);
  tft.init(240, 320);
  tft.setRotation(1); // 320x240 landscape
  tft.fillScreen(ST77XX_BLACK);
  
  // Initialize RGB LED PWM
  analogWriteResolution(PIN_RED, PWM_RESOLUTION_BITS);
  analogWriteResolution(PIN_GREEN, PWM_RESOLUTION_BITS);
  analogWriteResolution(PIN_BLUE, PWM_RESOLUTION_BITS);
  
  // Initialize input pins
  pinMode(PIN_JOYSTICK_X, INPUT);
  pinMode(PIN_JOYSTICK_Y, INPUT);
  pinMode(PIN_RIGHT, INP);
  pinMode(PIN_LEFT, INPUT);
  pinMode(PIN_DOWN, INPUT);
  pinMode(PIN_UP, INPUT);
  pinMode(PIN_B, INPUT);
  pinMode(PIN_A, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_SINGLE_LED, OUTPUT);
  
  // Initialize addressable LEDs
  FastLED.addLeds<WS2812, PIN_ADDR_LEDS, GRB>(addrLedsArray, NUM_ADDR_LEDS);
  FastLED.setBrightness(50);
  FastLED.clear();
  FastLED.show();
  
  // Ensure everything is off at start
  noTone(PIN_BUZZER);
  setRgbColor(0, 0, 0);
  analogWrite(PIN_SINGLE_LED, 0);
  
  // Show initial menu
  drawMainMenu();
  
  Serial.println("Demo Programs Launcher initialized!");
}

void loop() {
  switch (currentState) {
    case STATE_MAIN_MENU:
      handleMainMenuInput();
      break;
    case STATE_ETCH_A_SKETCH:
      loopEtchASketch();
      break;
    case STATE_HARDWARE_DEBUG:
      loopHardwareDebug();
      break;
    case STATE_TETRIS:
      loopTetris();
      break;
    case STATE_BYU_I_DEMO:
      loopBYUIDemo();
      break;
  }
  
  delay(1);
}


