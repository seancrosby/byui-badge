/*
 * Access Control for the BYUI ESP32 Dev Board
 * 
 * Hardware: 
 * - ST7789 240x320 TFT Display (rotated to 320x240)
 * - SG90 MicroServo
 */

// Software Libraries
#include <ESP32Servo.h> 
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// Display pins
#define TFT_CS   0
#define TFT_RST  1
#define TFT_DC   45
#define TFT_MOSI 3
#define TFT_SCLK 46
#define TFT_MISO -1 

// Pins
const int servoPin = 15; 
const int buttonGoodPin = 11;
const int buttonBadPin = 47;
const int buzzerPin = 13;
// Built-in buzzer
//const int buzzerPin = 48;

// Declare the hardware objects
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
Servo myServo;

// Other variables
int buttonGoodState = 0;
int buttonBadState = 0;

void clearScreen() {
  // Clear the screen and switch font color
  tft.setCursor(60, 100);
  tft.setTextSize(3);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
}


void setup() {
  ////////////////////////////////
  // Configure the serial comms //
  ////////////////////////////////

  // Send startup message to computer
  Serial.begin(115200);
  Serial.println("Starting Access Control...");

  /////////////////////////
  // Configure the servo //
  /////////////////////////

  // Allow allocation of all hardware timers for ESP32 PWM
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  
  myServo.setPeriodHertz(50); // Standard 50Hz frequency for SG90

  // Attach the servo with custom pulse widths optimized for SG90 (500us to 2400us)
  myServo.attach(servoPin, 500, 2400); 

  // Lock door
  myServo.write(0);   // Move to 0 degrees

  ///////////////////////
  // Configure buttons //
  ///////////////////////
 
  pinMode(buttonGoodPin, INPUT);
  pinMode(buttonBadPin, INPUT);

  //////////////////////
  // Configure buzzer //
  //////////////////////
 
  ledcAttach(buzzerPin, 2000, 8);

  ///////////////////////////
  // Configure the display //
  ///////////////////////////

  // Initialize display
  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI);
  tft.init(240, 320);
  tft.setRotation(1); // 320x240 landscape
  tft.fillScreen(ST77XX_BLACK);
  
  // Show splash screen on display
  tft.setCursor(0, 80);
  tft.setTextColor(ST77XX_RED);
  tft.setTextSize(3);
  tft.println("    MARS BASE\n");
  tft.setTextColor(ST77XX_GREEN);
  tft.println("  Access Control");
  
  // Wait 5 seconds
  delay(5000);
  
  clearScreen();

  // Wait 1 seconds
  delay(1000);

  tft.setCursor(10, 100);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.print("Enter the code: ");

  // Send status message to computer
  Serial.println("Access Control initialized!");
}


// Loop code - executes over and over again
void loop() {
  
  // Wait 100 ms until the next read
  delay(100);

  // read the state of the buttons
  buttonGoodState = digitalRead(buttonGoodPin);
  buttonBadState = digitalRead(buttonBadPin);

  // check if the pushbutton is pressed. If it is, the buttonState is HIGH:
  if (buttonGoodState == HIGH) {
    delay(500);
    myServo.write(180); // Move to 180 degrees

    // Print access granted
    clearScreen();
    tft.setCursor(10, 100);
    tft.setTextColor(ST77XX_GREEN);
    tft.print("Access Granted");
    delay(5000);

    myServo.write(0);   // Move to 0 degrees

    // Reset the prompt
    clearScreen();
    tft.setCursor(10, 100);
    tft.print("Enter the code: ");
  }
  // check if the pushbutton is pressed. If it is, the buttonState is HIGH:
  else if (buttonBadState == HIGH) {
    // Print access denied
    clearScreen();
    tft.setCursor(10, 100);
    tft.setTextColor(ST77XX_RED);
    tft.print("Access Denied");

    ledcWriteTone(buzzerPin, 2000);
    delay(500);

    // Play sounds
    ledcWriteTone(buzzerPin, 1000);
    delay(500);
    ledcWriteTone(buzzerPin, 2000);
    delay(500);
    ledcWriteTone(buzzerPin, 1000);
    delay(500);
    ledcWriteTone(buzzerPin, 2000);
    delay(500);
    ledcWriteTone(buzzerPin, 1000);
    delay(500);
    ledcWriteTone(buzzerPin, 2000);
    delay(500);

    // Stop the sound completely for 1 second
    ledcWriteTone(buzzerPin, 0); 
    delay(1000);

    // Reset the prompt
    clearScreen();
    tft.setCursor(10, 100);
    tft.print("Enter the code: ");
  }
}




