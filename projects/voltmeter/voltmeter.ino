/*
 * Voltmeter for the BYUI ESP32 Dev Board
 * 
 * Hardware: 
 * - ST7789 240x320 TFT Display (rotated to 320x240)
 */

// Software Libraries
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// Display pins
#define TFT_CS   0
#define TFT_RST  1
#define TFT_DC   45
#define TFT_MOSI 3
#define TFT_SCLK 46
#define TFT_MISO -1 // unused

// Declare the display
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// Setup code - executes once
void setup() {

  // Send startup message to computer
  Serial.begin(115200);
  Serial.println("Starting the Voltmeter...");
  
  // Initialize display
  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI);
  tft.init(240, 320);
  tft.setRotation(1); // 320x240 landscape
  tft.fillScreen(ST77XX_BLACK);
  
  // Show splash screen on display
  tft.setCursor(60, 100);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(3);
  tft.println("Voltmeter");
  
  // Wait 5 seconds
  delay(5000);
  
  // Clear the screen and switch font color
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);

  // Send status message to computer
  Serial.println("Voltmeter initialized!");
}

// Loop code - executes over and over again
void loop() {
  
  // Take a measurement on pin 13
  float sensorValue = analogRead(13);  // sensor reading (ranges from 0 to 4095)

  // Rescale the measurement for display
  float voltValue = sensorValue / 4095 * 3.3; // convert to voltage (0V to 3.3V)
  int meterValue = map(sensorValue, 0, 4095, 0, 100); // scale to 0-100%

  // Print the voltage on the display
  tft.setCursor(10, 50);
  tft.print("Voltage: ");
  tft.print(voltValue);
  tft.print("V");

  // Draw a green bar as a voltage gauge
  tft.fillRect(10, 100, (meterValue*2), 20, ST77XX_GREEN);
  tft.fillRect(10 + (meterValue*2), 100, 200 - (meterValue*2), 20, ST77XX_BLACK); // clear rest of old bar

  // Wait 100 ms until the next read
  delay(100);
}

