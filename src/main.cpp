// =============================================================================
// Pico DMX Interface — Enttec DMX USB Pro Emulator (Dual Universe)
// =============================================================================
//
// This firmware makes a Raspberry Pi Pico act as an Enttec DMX USB Pro Mk2
// compatible interface for QLC+ and other lighting control software.
//
// Hardware:
//   - GP0 (Output 1 / Universe 1) → MAX485 (1) DI
//   - GP1                         → MAX485 (1) DE + RE̅ (tied together, HIGH =
//   TX)
//   - GP4 (Output 2 / Universe 2) → MAX485 (2) DI
//   - GP5                         → MAX485 (2) DE + RE̅ (tied together, HIGH =
//   TX)
//   - USB                         → PC running QLC+
//
// Protocol:
//   USB Serial (CDC) ←→ Enttec Pro Mk2 protocol ←→ DMX512 via PIO (Pico-DMX)
//
// =============================================================================

// SSD1206
// GND (Green) -> GND
// SDA (Yellow) -> GP6
// SCL (Orange) -> GP7
// VCC (Brown) -> 5V

// Buttons
// K1 (White) -> GP10
// K2 (Gray) -> GP11
// K3 (Purple) -> GP12
// K4 (Blue) -> GP13
// GND (Green) -> GND

#include "dmx_driver.h"
#include "enttec_pro.h"
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>

#define BUTTON_K1 10
#define BUTTON_K2 11
#define BUTTON_K3 12
#define BUTTON_K4 13

static const int buttonPins[4] = {BUTTON_K1, BUTTON_K2, BUTTON_K3, BUTTON_K4};
static const char *buttonNames[4] = {"K1", "K2", "K3", "K4"};
static String lastPressedKey = "None";

#define SDA_PIN 6
#define SCL_PIN 7
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET -1    // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS                                                         \
  0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, OLED_RESET);

// LED pin for status indication
#define LED_PIN LED_BUILTIN

// Global instances
EnttecPro enttecPro;
DmxDriver dmxOutput;

// Activity LED state
static unsigned long lastActivityTime = 0;
static bool ledState = false;

// OLED update timing and statistics
static unsigned long lastOledUpdate = 0;
static unsigned long lastDmxTime[2] = {0, 0};
static uint32_t dmxPacketCount[2] = {0, 0};
static bool hasDisplay = false;

// =============================================================================
// Setup
// =============================================================================

void setup() {
  // Initialize the built-in LED for status indication
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Initialize button pins as INPUT_PULLUP
  for (int i = 0; i < 4; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
  }

  // Initialize I2C with non-default pins (using Wire1 since GP6/GP7 are on
  // I2C1)
  Wire1.setSDA(SDA_PIN);
  Wire1.setSCL(SCL_PIN);
  Wire1.begin();

  // Initialize SSD1306 OLED display
  // Try SCREEN_ADDRESS (0x3D) first, then fallback to 0x3C
  if (display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    hasDisplay = true;
  } else if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    hasDisplay = true;
  }

  if (hasDisplay) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Pico DMX Pro Mk2");
    display.println("Ready!");
    display.display();
  }

  // Initialize USB Serial (CDC) for communication with QLC+
  Serial.begin(115200);

  // Initialize the Enttec protocol handler
  enttecPro.begin();

  // Initialize DMX output (PIO + GPIO)
  dmxOutput.begin();

  // Signal that we're ready — blink LED 3 times
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
}

// =============================================================================
// Main Loop
// =============================================================================

void loop() {
  // --------------------------------------------------
  // 0. Read and debounce buttons
  // --------------------------------------------------
  static bool btnState[4] = {HIGH, HIGH, HIGH, HIGH};
  static unsigned long btnLastDebounce[4] = {0, 0, 0, 0};

  for (int i = 0; i < 4; i++) {
    bool currentVal = digitalRead(buttonPins[i]);
    if (currentVal != btnState[i]) {
      if (millis() - btnLastDebounce[i] > 50) {
        btnState[i] = currentVal;
        btnLastDebounce[i] = millis();
        if (currentVal == LOW) { // Pressed!
          lastPressedKey = buttonNames[i];
          // Force immediate update of display for snappy feedback
          lastOledUpdate = 0;
        }
      }
    }
  }

  // --------------------------------------------------
  // 1. Read bytes from USB Serial and feed to parser
  // --------------------------------------------------
  while (Serial.available() > 0) {
    uint8_t byte = Serial.read();
    enttecPro.processByte(byte);
  }

  // --------------------------------------------------
  // 2. If new DMX data was received, update the outputs
  // --------------------------------------------------
  bool hasActivity = false;

  // Check Port 1 (Universe 1)
  if (enttecPro.hasDmxData(0)) {
    dmxOutput.setData(0, enttecPro.getDmxData(0), enttecPro.getDmxLength(0));
    enttecPro.clearDmxFlag(0);
    hasActivity = true;
    lastDmxTime[0] = millis();
    dmxPacketCount[0]++;
  }

  // Check Port 2 (Universe 2)
  if (enttecPro.hasDmxData(1)) {
    dmxOutput.setData(1, enttecPro.getDmxData(1), enttecPro.getDmxLength(1));
    enttecPro.clearDmxFlag(1);
    hasActivity = true;
    lastDmxTime[1] = millis();
    dmxPacketCount[1]++;
  }

  if (hasActivity) {
    lastActivityTime = millis();
  }

  // --------------------------------------------------
  // 3. Continuously transmit DMX frames at the set rate
  // --------------------------------------------------
  dmxOutput.update();

  // --------------------------------------------------
  // 4. Activity LED — blink when receiving data
  // --------------------------------------------------
  if (millis() - lastActivityTime < 100) {
    // Recent activity — LED on
    if (!ledState) {
      digitalWrite(LED_PIN, HIGH);
      ledState = true;
    }
  } else {
    // No recent activity — LED off
    if (ledState) {
      digitalWrite(LED_PIN, LOW);
      ledState = false;
    }
  }

  // --------------------------------------------------
  // 5. Update OLED display periodically (every 500ms) or on event
  // (lastOledUpdate == 0)
  // --------------------------------------------------
  if (hasDisplay &&
      (lastOledUpdate == 0 || (millis() - lastOledUpdate >= 500))) {
    lastOledUpdate = millis();
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.println("  Pico DMX Pro Mk2");
    display.println("---------------------");

    // Universe 1 Status
    display.print("U1 (GP0): ");
    if (lastDmxTime[0] > 0 && (millis() - lastDmxTime[0] < 1000)) {
      display.print("Active (");
      display.print(dmxPacketCount[0]);
      display.println(")");
    } else {
      display.println("Idle");
    }

    // Universe 2 Status
    display.print("U2 (GP4): ");
    if (lastDmxTime[1] > 0 && (millis() - lastDmxTime[1] < 1000)) {
      display.print("Active (");
      display.print(dmxPacketCount[1]);
      display.println(")");
    } else {
      display.println("Idle");
    }

    // Buttons live status
    display.print("Keys: ");
    for (int i = 0; i < 4; i++) {
      if (digitalRead(buttonPins[i]) == LOW) { // currently pressed
        display.print("[");
        display.print(buttonNames[i]);
        display.print("] ");
      } else {
        display.print(" ");
        display.print(buttonNames[i]);
        display.print("  ");
      }
    }
    display.println();

    // Last pressed key
    display.print("Last: ");
    display.println(lastPressedKey);

    display.display();
  }
}