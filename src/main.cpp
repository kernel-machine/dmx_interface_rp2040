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
#include <EEPROM.h>
#include <Wire.h>
#include <hardware/flash.h>
#include <hardware/sync.h>

#define BUTTON_K1 10
#define BUTTON_K2 11
#define BUTTON_K3 12
#define BUTTON_K4 13

static const int buttonPins[4] = {BUTTON_K1, BUTTON_K2, BUTTON_K3, BUTTON_K4};
static const char *buttonNames[4] = {"M", "^", "v", "Ent"};
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

// Menu States
enum MenuState {
  STATE_NORMAL,
  STATE_MENU_MAIN,
  STATE_SET_REFRESH_RATE,
  STATE_SET_DISCONNECT_MODE,
  STATE_SET_SCREENSAVER_TIMEOUT,
  STATE_SET_FADE_TIME,
  STATE_MENU_CUES,       // Select CUE index (1 to 10)
  STATE_MENU_CUE_ACTION, // Select Play / Record / Back
  STATE_PLAYING_CUE,     // Playback mode screen
  STATE_CUE_FEEDBACK     // Action feedback screen (brief auto-dismiss message)
};

// Feedback Types
enum FeedbackType { FEEDBACK_RECORDED, FEEDBACK_DELETED };

// Disconnect Modes
enum DisconnectMode { MODE_BLACKOUT = 0, MODE_HOLD_LAST = 1 };

// Menu Items
enum MenuItem {
  ITEM_REFRESH_RATE,
  ITEM_DISCONNECT_MODE,
  ITEM_SCREENSAVER_TIMEOUT,
  ITEM_FADE_TIME,
  ITEM_CUES,
  ITEM_EXIT,
  ITEM_COUNT // Helper
};

static const char *menuItemNames[ITEM_COUNT] = {
    "DMX Refresh Rate", "On Disconnect", "Screensaver Time",
    "Fade Time",        "CUEs",          "Exit"};

// CUE Storage Constants
#define CUE_SIZE 1024 // 512 bytes Universe 1 + 512 bytes Universe 2
#define NUM_CUES 10
#define FLASH_CUES_OFFSET                                                      \
  0x80000 // 512 KB offset from start of flash (safe from sketch & filesystem
          // sectors)
#define FLASH_CUES_ADDR (XIP_BASE + FLASH_CUES_OFFSET)
#define EEPROM_CUE_RECORDED_START_ADDR 10

// State Variables
static MenuState currentMenuState = STATE_NORMAL;
static int mainMenuCursor = 0;
static uint8_t dmxFpsSetting = 40;  // Default DMX refresh rate
static uint8_t tempFpsSetting = 40; // Temporary value for setting edit
static uint8_t disconnectModeSetting =
    MODE_BLACKOUT; // Default disconnect behavior
static uint8_t tempDisconnectMode =
    MODE_BLACKOUT; // Temporary value for disconnect edit
static bool universeTimedOut[2] = {false,
                                   false}; // Track universe timeout state

static bool cueRecorded[NUM_CUES] = {false}; // Track if each CUE is saved
static int cueMenuCursor = 0; // Selector cursor for CUE list menu
static int cueActionCursor =
    0; // Selector cursor for CUE action menu (Play/Record/Back)
static int selectedCue = 0; // Currently selected CUE index (0..9)
static int playingCue = -1; // Currently playing CUE index (0..9), or -1 if live
static FeedbackType activeFeedback =
    FEEDBACK_RECORDED;                      // Current feedback to display
static unsigned long feedbackStartTime = 0; // Millis when feedback view started
static unsigned long lastUserInteractionTime =
    0; // Tracks last user interaction (button press)
static bool screenSaverActive = false; // Tracks if OLED screensaver is active

static uint8_t screensaverTimeoutSetting =
    60; // Screensaver timeout in seconds (0 = disabled)
static uint8_t tempScreensaverTimeout = 60; // Temporary value for setting edit
static uint8_t fadeTimeSetting = 3; // Fade time in seconds for CUE transitions
static uint8_t tempFadeTime = 3;    // Temporary value for fade time edit

// DMX state tracker and crossfade engine
static uint8_t currentDmxValues[2][512] = {{0}, {0}};
static bool isCrossFading = false;
static unsigned long crossFadeStartTime = 0;
static unsigned long crossFadeDuration = 0;
static uint8_t crossFadeStartBuffer[2][512] = {{0}, {0}};
static uint8_t crossFadeTargetBuffer[2][512] = {{0}, {0}};

#define EEPROM_MAGIC 0x5A
#define EEPROM_MAGIC_ADDR 0
#define EEPROM_FPS_ADDR 1
#define EEPROM_DISCONNECT_MODE_ADDR 2
#define EEPROM_SCREENSAVER_TIMEOUT_ADDR 3
#define EEPROM_FADE_TIME_ADDR 4

// EEPROM settings management
void loadSettings() {
  EEPROM.begin(256);
  uint8_t magic = EEPROM.read(EEPROM_MAGIC_ADDR);
  if (magic == EEPROM_MAGIC) {
    dmxFpsSetting = EEPROM.read(EEPROM_FPS_ADDR);
    if (dmxFpsSetting > 44) {
      dmxFpsSetting = 44;
    }
    disconnectModeSetting = EEPROM.read(EEPROM_DISCONNECT_MODE_ADDR);
    screensaverTimeoutSetting = EEPROM.read(EEPROM_SCREENSAVER_TIMEOUT_ADDR);
    if (screensaverTimeoutSetting > 240) {
      screensaverTimeoutSetting = 60; // Default if invalid
    }
    fadeTimeSetting = EEPROM.read(EEPROM_FADE_TIME_ADDR);
    if (fadeTimeSetting > 10) {
      fadeTimeSetting = 3; // Default if invalid (3 seconds)
    }
    for (int i = 0; i < NUM_CUES; i++) {
      cueRecorded[i] = (EEPROM.read(EEPROM_CUE_RECORDED_START_ADDR + i) == 1);
    }
    if (disconnectModeSetting > 11) {
      disconnectModeSetting = MODE_BLACKOUT; // Sanity check
    } else if (disconnectModeSetting >= 2) {
      int cueIdx = disconnectModeSetting - 2;
      if (!cueRecorded[cueIdx]) {
        disconnectModeSetting = MODE_BLACKOUT; // Fallback if CUE is empty
      }
    }
  } else {
    // Not initialized, save default values
    dmxFpsSetting = 40;
    disconnectModeSetting = MODE_BLACKOUT;
    screensaverTimeoutSetting = 60;
    fadeTimeSetting = 3;
    EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
    EEPROM.write(EEPROM_FPS_ADDR, dmxFpsSetting);
    EEPROM.write(EEPROM_DISCONNECT_MODE_ADDR, disconnectModeSetting);
    EEPROM.write(EEPROM_SCREENSAVER_TIMEOUT_ADDR, screensaverTimeoutSetting);
    EEPROM.write(EEPROM_FADE_TIME_ADDR, fadeTimeSetting);
    for (int i = 0; i < NUM_CUES; i++) {
      cueRecorded[i] = false;
      EEPROM.write(EEPROM_CUE_RECORDED_START_ADDR + i, 0);
    }
    EEPROM.commit();
  }
}

void saveSettings() {
  EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
  EEPROM.write(EEPROM_FPS_ADDR, dmxFpsSetting);
  EEPROM.write(EEPROM_DISCONNECT_MODE_ADDR, disconnectModeSetting);
  EEPROM.write(EEPROM_SCREENSAVER_TIMEOUT_ADDR, screensaverTimeoutSetting);
  EEPROM.write(EEPROM_FADE_TIME_ADDR, fadeTimeSetting);
  for (int i = 0; i < NUM_CUES; i++) {
    EEPROM.write(EEPROM_CUE_RECORDED_START_ADDR + i, cueRecorded[i] ? 1 : 0);
  }
  EEPROM.commit();
}

void recordCue(int cueIdx) {
  extern EnttecPro enttecPro;
  if (cueIdx < 0 || cueIdx >= NUM_CUES)
    return;

  // Allocate a temporary buffer for all 10 CUEs in RAM
  uint8_t *tempBuffer = (uint8_t *)malloc(NUM_CUES * CUE_SIZE);
  if (!tempBuffer)
    return;

  // Copy current flash CUE contents into RAM buffer
  const uint8_t *flash_cues_ptr = (const uint8_t *)FLASH_CUES_ADDR;
  memcpy(tempBuffer, flash_cues_ptr, NUM_CUES * CUE_SIZE);

  // Update the target CUE space with current live DMX values from enttecPro
  uint8_t *targetCue = tempBuffer + (cueIdx * CUE_SIZE);
  memcpy(targetCue, enttecPro.getDmxData(0), 512);
  memcpy(targetCue + 512, enttecPro.getDmxData(1), 512);

  // Disable interrupts and reprogram flash
  uint32_t ints = save_and_disable_interrupts();

  // Erase 3 sectors (12 KB) at offset FLASH_CUES_OFFSET
  flash_range_erase(FLASH_CUES_OFFSET, 3 * FLASH_SECTOR_SIZE);

  // Program 10,240 bytes at offset FLASH_CUES_OFFSET
  flash_range_program(FLASH_CUES_OFFSET, tempBuffer, NUM_CUES * CUE_SIZE);

  restore_interrupts(ints);

  free(tempBuffer);

  // Update EEPROM flag for this CUE
  cueRecorded[cueIdx] = true;
  saveSettings();
}

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
static unsigned long u1LedOnUntil = 0;
static unsigned long u2LedOnUntil = 0;
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

    // Draw Bun
    display.fillRoundRect(38, 6, 52, 22, 8, SSD1306_WHITE);
    // Draw Contrast Outline (separation between bun & sausage)
    display.fillRoundRect(40, 10, 48, 14, 6, SSD1306_BLACK);
    // Draw Sausage (extended on both sides)
    display.fillRoundRect(30, 12, 68, 10, 5, SSD1306_WHITE);
    // Draw Mustard squiggle
    for (int x = 38; x < 86; x += 8) {
      display.drawLine(x, 15, x + 4, 19, SSD1306_BLACK);
      display.drawLine(x + 4, 19, x + 8, 15, SSD1306_BLACK);
    }

    // Draw Brand Name
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(16, 32);
    display.print("KM Light");

    // Draw Subtitle
    display.setTextSize(1);
    display.setCursor(13, 52);
    display.print("DMX Pro Interface");

    display.display();
    delay(2000); // 2-second splash screen duration
  }

  // Initialize USB Serial (CDC) for communication with QLC+
  Serial.begin(115200);

  // Initialize the Enttec protocol handler
  enttecPro.begin();

  // Initialize DMX output (PIO + GPIO)
  dmxOutput.begin();

  // Load persistent settings and apply DMX refresh rate
  loadSettings();
  dmxOutput.setRefreshRate(dmxFpsSetting);

  // Initialize screensaver activity timer after the setup delay
  lastUserInteractionTime = millis();

  // Signal that we're ready — blink LED 3 times
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
}

// =============================================================================
// DMX Crossfader Engine
// =============================================================================
void startCrossFade(const uint8_t *targetU1, const uint8_t *targetU2,
                    unsigned long durationMs) {
  if (durationMs == 0) {
    if (targetU1) {
      dmxOutput.setData(0, targetU1, 512);
      memcpy(currentDmxValues[0], targetU1, 512);
    }
    if (targetU2) {
      dmxOutput.setData(1, targetU2, 512);
      memcpy(currentDmxValues[1], targetU2, 512);
    }
    isCrossFading = false;
    return;
  }

  if (targetU1) {
    memcpy(crossFadeStartBuffer[0], currentDmxValues[0], 512);
    memcpy(crossFadeTargetBuffer[0], targetU1, 512);
  } else {
    memcpy(crossFadeStartBuffer[0], currentDmxValues[0], 512);
    memcpy(crossFadeTargetBuffer[0], currentDmxValues[0], 512);
  }

  if (targetU2) {
    memcpy(crossFadeStartBuffer[1], currentDmxValues[1], 512);
    memcpy(crossFadeTargetBuffer[1], targetU2, 512);
  } else {
    memcpy(crossFadeStartBuffer[1], currentDmxValues[1], 512);
    memcpy(crossFadeTargetBuffer[1], currentDmxValues[1], 512);
  }

  crossFadeStartTime = millis();
  crossFadeDuration = durationMs;
  isCrossFading = true;
}

void updateCrossFade() {
  if (!isCrossFading)
    return;

  float progress = (float)(millis() - crossFadeStartTime) / crossFadeDuration;
  if (progress >= 1.0f) {
    progress = 1.0f;
    isCrossFading = false;
  }

  static uint8_t tempBuffer[512];
  for (int port = 0; port < 2; port++) {
    for (int ch = 0; ch < 512; ch++) {
      int valStart = crossFadeStartBuffer[port][ch];
      int valTarget = crossFadeTargetBuffer[port][ch];
      tempBuffer[ch] = (uint8_t)(valStart + (valTarget - valStart) * progress);
    }
    dmxOutput.setData(port, tempBuffer, 512);
    memcpy(currentDmxValues[port], tempBuffer, 512);
  }
}

// =============================================================================
// Helper to draw a menu item with selection highlight
// =============================================================================
void drawMenuItem(const char *label, bool selected, int y) {
  display.setCursor(0, y);
  if (selected) {
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.print(" > ");
    display.print(label);
    // Pad with spaces to clear the line in reverse video
    int len = strlen(label);
    for (int i = len; i < 17; i++) {
      display.print(" ");
    }
  } else {
    display.setTextColor(SSD1306_WHITE);
    display.print("   ");
    display.print(label);
  }
}

// =============================================================================
// Helper to draw a vertical scrollbar on the right edge of the OLED screen
// =============================================================================
void drawScrollBar(int currentItem, int totalItems, int itemsPerPage,
                   int yStart, int yEnd) {
  if (totalItems <= itemsPerPage)
    return;

  int scrollBarX = 124;
  int scrollBarWidth = 3;
  int height = yEnd - yStart;

  // Track limits (top and bottom horizontal lines)
  display.drawFastHLine(scrollBarX - 1, yStart, 5, SSD1306_WHITE);
  display.drawFastHLine(scrollBarX - 1, yEnd - 1, 5, SSD1306_WHITE);

  // Track vertical line
  display.drawFastVLine(scrollBarX + 1, yStart, height, SSD1306_WHITE);

  // Proportional handle height
  int handleHeight = (height * itemsPerPage) / totalItems;
  if (handleHeight < 6)
    handleHeight = 6;

  // Position mapped to currentItem
  int handleY =
      yStart + (currentItem * (height - handleHeight)) / (totalItems - 1);

  // Draw the handle
  display.fillRect(scrollBarX, handleY, scrollBarWidth, handleHeight,
                   SSD1306_WHITE);
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

  bool k1Pressed = false;
  bool k2Pressed = false;
  bool k3Pressed = false;
  bool k4Pressed = false;

  for (int i = 0; i < 4; i++) {
    bool currentVal = digitalRead(buttonPins[i]);
    if (currentVal != btnState[i]) {
      if (millis() - btnLastDebounce[i] > 50) {
        btnState[i] = currentVal;
        btnLastDebounce[i] = millis();
        if (currentVal == LOW) { // Pressed!
          lastPressedKey = buttonNames[i];
          if (i == 0)
            k1Pressed = true;
          else if (i == 1)
            k2Pressed = true;
          else if (i == 2)
            k3Pressed = true;
          else if (i == 3)
            k4Pressed = true;

          // Force immediate update of display for snappy feedback
          lastOledUpdate = 0;
        }
      }
    }
  }

  // Intercept button presses to manage screensaver
  if (k1Pressed || k2Pressed || k3Pressed || k4Pressed) {
    lastUserInteractionTime = millis();
    if (screenSaverActive) {
      screenSaverActive = false;
      display.ssd1306_command(SSD1306_DISPLAYON);
      // Force redraw immediately
      lastOledUpdate = 0;
      // Consume the presses to prevent triggering actions on wake up
      k1Pressed = false;
      k2Pressed = false;
      k3Pressed = false;
      k4Pressed = false;
    }
  }

  // Check if screensaver should turn on (based on screensaverTimeoutSetting in
  // seconds, 0 = disabled)
  if (screensaverTimeoutSetting > 0 && !screenSaverActive &&
      (millis() - lastUserInteractionTime >=
       (unsigned long)screensaverTimeoutSetting * 1000)) {
    screenSaverActive = true;
    display.ssd1306_command(SSD1306_DISPLAYOFF);
  }

  // --------------------------------------------------
  // Menu State Machine Logic
  // --------------------------------------------------
  if (currentMenuState == STATE_NORMAL) {
    if (k1Pressed) {
      currentMenuState = STATE_MENU_MAIN;
      mainMenuCursor = 0;
      lastOledUpdate = 0;
    }
  } else if (currentMenuState == STATE_MENU_MAIN) {
    if (k1Pressed) { // Back
      currentMenuState = STATE_NORMAL;
      lastOledUpdate = 0;
    } else if (k2Pressed) { // Scroll Up
      mainMenuCursor = (mainMenuCursor - 1 + ITEM_COUNT) % ITEM_COUNT;
      lastOledUpdate = 0;
    } else if (k3Pressed) { // Scroll Down
      mainMenuCursor = (mainMenuCursor + 1) % ITEM_COUNT;
      lastOledUpdate = 0;
    } else if (k4Pressed) { // Enter
      if (mainMenuCursor == ITEM_REFRESH_RATE) {
        currentMenuState = STATE_SET_REFRESH_RATE;
        tempFpsSetting = dmxFpsSetting;
      } else if (mainMenuCursor == ITEM_DISCONNECT_MODE) {
        currentMenuState = STATE_SET_DISCONNECT_MODE;
        tempDisconnectMode = disconnectModeSetting;
      } else if (mainMenuCursor == ITEM_SCREENSAVER_TIMEOUT) {
        currentMenuState = STATE_SET_SCREENSAVER_TIMEOUT;
        tempScreensaverTimeout = screensaverTimeoutSetting;
      } else if (mainMenuCursor == ITEM_FADE_TIME) {
        currentMenuState = STATE_SET_FADE_TIME;
        tempFadeTime = fadeTimeSetting;
      } else if (mainMenuCursor == ITEM_CUES) {
        currentMenuState = STATE_MENU_CUES;
        cueMenuCursor = 0;
      } else if (mainMenuCursor == ITEM_EXIT) {
        currentMenuState = STATE_NORMAL;
      }
      lastOledUpdate = 0;
    }
  } else if (currentMenuState == STATE_SET_REFRESH_RATE) {
    if (k1Pressed) { // Cancel
      currentMenuState = STATE_MENU_MAIN;
      lastOledUpdate = 0;
    } else if (k2Pressed) { // Increase
      if (tempFpsSetting == 0) {
        tempFpsSetting = 10;
      } else if (tempFpsSetting < 44) {
        tempFpsSetting++;
      }
      lastOledUpdate = 0;
    } else if (k3Pressed) { // Decrease
      if (tempFpsSetting == 10) {
        tempFpsSetting = 0; // Max
      } else if (tempFpsSetting > 10) {
        tempFpsSetting--;
      }
      lastOledUpdate = 0;
    } else if (k4Pressed) { // Save
      dmxFpsSetting = tempFpsSetting;
      saveSettings();
      dmxOutput.setRefreshRate(dmxFpsSetting);
      currentMenuState = STATE_MENU_MAIN;
      lastOledUpdate = 0;
    }
  } else if (currentMenuState == STATE_SET_DISCONNECT_MODE) {
    if (k1Pressed) { // Cancel
      currentMenuState = STATE_MENU_MAIN;
      lastOledUpdate = 0;
    } else if (k2Pressed) { // Scroll forward/next option
      uint8_t nextMode = tempDisconnectMode;
      while (true) {
        nextMode = (nextMode + 1) % 12; // 0..11
        if (nextMode == MODE_BLACKOUT || nextMode == MODE_HOLD_LAST) {
          break;
        }
        int cueIdx = nextMode - 2;
        if (cueRecorded[cueIdx]) {
          break;
        }
      }
      tempDisconnectMode = nextMode;
      lastOledUpdate = 0;
    } else if (k3Pressed) { // Scroll backward/prev option
      uint8_t prevMode = tempDisconnectMode;
      while (true) {
        prevMode = (prevMode - 1 + 12) % 12;
        if (prevMode == MODE_BLACKOUT || prevMode == MODE_HOLD_LAST) {
          break;
        }
        int cueIdx = prevMode - 2;
        if (cueRecorded[cueIdx]) {
          break;
        }
      }
      tempDisconnectMode = prevMode;
      lastOledUpdate = 0;
    } else if (k4Pressed) { // Save
      disconnectModeSetting = tempDisconnectMode;
      saveSettings();
      // Apply blackout or play CUE immediately if we are currently timed out
      for (int port = 0; port < 2; port++) {
        if (universeTimedOut[port]) {
          if (disconnectModeSetting == MODE_BLACKOUT) {
            uint8_t zeroData[DMX_CHANNELS_MAX] = {0};
            dmxOutput.setData(port, zeroData, DMX_CHANNELS_MAX);
          } else if (disconnectModeSetting >= 2 &&
                     disconnectModeSetting <= 11) {
            int cueIdx = disconnectModeSetting - 2;
            const uint8_t *flash_cues_ptr = (const uint8_t *)FLASH_CUES_ADDR;
            const uint8_t *cue_ptr =
                flash_cues_ptr + (cueIdx * CUE_SIZE) + (port * 512);
            dmxOutput.setData(port, cue_ptr, 512);
          }
        }
      }
      currentMenuState = STATE_MENU_MAIN;
      lastOledUpdate = 0;
    }
  } else if (currentMenuState == STATE_SET_SCREENSAVER_TIMEOUT) {
    if (k1Pressed) { // Cancel
      currentMenuState = STATE_MENU_MAIN;
      lastOledUpdate = 0;
    } else if (k2Pressed) { // Increase (Max 240s)
      if (tempScreensaverTimeout < 240) {
        tempScreensaverTimeout += 10;
      }
      lastOledUpdate = 0;
    } else if (k3Pressed) { // Decrease (Min 0)
      if (tempScreensaverTimeout >= 10) {
        tempScreensaverTimeout -= 10;
      }
      lastOledUpdate = 0;
    } else if (k4Pressed) { // Save
      screensaverTimeoutSetting = tempScreensaverTimeout;
      saveSettings();
      currentMenuState = STATE_MENU_MAIN;
      lastOledUpdate = 0;
    }
  } else if (currentMenuState == STATE_SET_FADE_TIME) {
    if (k1Pressed) { // Cancel
      currentMenuState = STATE_MENU_MAIN;
      lastOledUpdate = 0;
    } else if (k2Pressed) { // Increase (Max 10s)
      if (tempFadeTime < 10) {
        tempFadeTime++;
      }
      lastOledUpdate = 0;
    } else if (k3Pressed) { // Decrease (Min 0s)
      if (tempFadeTime > 0) {
        tempFadeTime--;
      }
      lastOledUpdate = 0;
    } else if (k4Pressed) { // Save
      fadeTimeSetting = tempFadeTime;
      saveSettings();
      currentMenuState = STATE_MENU_MAIN;
      lastOledUpdate = 0;
    }
  } else if (currentMenuState == STATE_MENU_CUES) {
    if (k1Pressed) { // Cancel/Back
      currentMenuState = STATE_MENU_MAIN;
      mainMenuCursor = ITEM_CUES;
      lastOledUpdate = 0;
    } else if (k2Pressed) { // Scroll Up
      cueMenuCursor = (cueMenuCursor - 1 + (NUM_CUES + 1)) % (NUM_CUES + 1);
      lastOledUpdate = 0;
    } else if (k3Pressed) { // Scroll Down
      cueMenuCursor = (cueMenuCursor + 1) % (NUM_CUES + 1);
      lastOledUpdate = 0;
    } else if (k4Pressed) {            // Enter
      if (cueMenuCursor == NUM_CUES) { // Back item
        currentMenuState = STATE_MENU_MAIN;
        mainMenuCursor = ITEM_CUES;
      } else {
        selectedCue = cueMenuCursor;
        currentMenuState = STATE_MENU_CUE_ACTION;
        cueActionCursor = 0;
      }
      lastOledUpdate = 0;
    }
  } else if (currentMenuState == STATE_MENU_CUE_ACTION) {
    if (k1Pressed) { // Cancel/Back
      currentMenuState = STATE_MENU_CUES;
      cueMenuCursor = selectedCue;
      lastOledUpdate = 0;
    } else if (k2Pressed) { // Scroll Up
      cueActionCursor = (cueActionCursor - 1 + 4) %
                        4; // 4 actions: Play, Record, Delete, Back
      lastOledUpdate = 0;
    } else if (k3Pressed) { // Scroll Down
      cueActionCursor = (cueActionCursor + 1) % 4;
      lastOledUpdate = 0;
    } else if (k4Pressed) {       // Enter
      if (cueActionCursor == 0) { // Play
        if (cueRecorded[selectedCue]) {
          playingCue = selectedCue;
          currentMenuState = STATE_PLAYING_CUE;
          // Start crossfade into the CUE values
          const uint8_t *flash_cues_ptr = (const uint8_t *)FLASH_CUES_ADDR;
          const uint8_t *cue_ptr = flash_cues_ptr + (playingCue * CUE_SIZE);
          startCrossFade(cue_ptr, cue_ptr + 512,
                         (unsigned long)fadeTimeSetting * 1000);
        }
      } else if (cueActionCursor == 1) { // Record
        recordCue(selectedCue);
        activeFeedback = FEEDBACK_RECORDED;
        feedbackStartTime = millis();
        currentMenuState = STATE_CUE_FEEDBACK;
      } else if (cueActionCursor == 2) { // Delete
        cueRecorded[selectedCue] = false;
        if (disconnectModeSetting == (selectedCue + 2)) {
          disconnectModeSetting = MODE_BLACKOUT;
        }
        saveSettings();
        activeFeedback = FEEDBACK_DELETED;
        feedbackStartTime = millis();
        currentMenuState = STATE_CUE_FEEDBACK;
      } else if (cueActionCursor == 3) { // Back
        currentMenuState = STATE_MENU_CUES;
        cueMenuCursor = selectedCue;
      }
      lastOledUpdate = 0;
    }
  } else if (currentMenuState == STATE_PLAYING_CUE) {
    if (k1Pressed || k4Pressed) { // Stop
      // Start crossfade out of the CUE back to live USB DMX
      const uint8_t *liveU1 = enttecPro.getDmxData(0);
      const uint8_t *liveU2 = enttecPro.getDmxData(1);
      startCrossFade(liveU1, liveU2, (unsigned long)fadeTimeSetting * 1000);

      playingCue = -1;
      currentMenuState = STATE_MENU_CUE_ACTION;
      lastOledUpdate = 0;
    }
  } else if (currentMenuState == STATE_CUE_FEEDBACK) {
    if (millis() - feedbackStartTime >= 1000) {
      currentMenuState = STATE_MENU_CUE_ACTION;
      cueActionCursor = 0;
      lastOledUpdate = 0;
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

  if (playingCue != -1) {
    // We are playing a CUE. Ignore/clear incoming live serial data.
    enttecPro.clearDmxFlag(0);
    enttecPro.clearDmxFlag(1);

    // Only write static values if we are NOT currently fading in!
    if (!isCrossFading) {
      const uint8_t *flash_cues_ptr = (const uint8_t *)FLASH_CUES_ADDR;
      const uint8_t *cue_ptr = flash_cues_ptr + (playingCue * CUE_SIZE);

      dmxOutput.setData(0, cue_ptr, 512);
      dmxOutput.setData(1, cue_ptr + 512, 512);
      memcpy(currentDmxValues[0], cue_ptr, 512);
      memcpy(currentDmxValues[1], cue_ptr + 512, 512);
    }
  } else {
    // Live mode: update from USB Serial
    // Check Port 1 (Universe 1)
    if (enttecPro.hasDmxData(0)) {
      bool valuesChanged = false;
      const uint8_t *newData = enttecPro.getDmxData(0);
      uint16_t len = enttecPro.getDmxLength(0);
      for (int i = 0; i < len; i++) {
        if (newData[i] != currentDmxValues[0][i]) {
          valuesChanged = true;
          break;
        }
      }

      if (!isCrossFading) {
        dmxOutput.setData(0, enttecPro.getDmxData(0),
                          enttecPro.getDmxLength(0));
        memcpy(currentDmxValues[0], enttecPro.getDmxData(0),
               enttecPro.getDmxLength(0));
      }

      if (universeTimedOut[0]) {
        universeTimedOut[0] = false;
        // Smoothly fade back to live!
        const uint8_t *liveU1 = enttecPro.getDmxData(0);
        const uint8_t *liveU2 = enttecPro.getDmxData(1);
        startCrossFade(liveU1, liveU2, (unsigned long)fadeTimeSetting * 1000);
      }

      enttecPro.clearDmxFlag(0);
      hasActivity = true;
      lastDmxTime[0] = millis();
      dmxPacketCount[0]++;
      if (valuesChanged) {
        u1LedOnUntil = millis() + 150;
      }
    }

    // Check Port 2 (Universe 2)
    if (enttecPro.hasDmxData(1)) {
      bool valuesChanged = false;
      const uint8_t *newData = enttecPro.getDmxData(1);
      uint16_t len = enttecPro.getDmxLength(1);
      for (int i = 0; i < len; i++) {
        if (newData[i] != currentDmxValues[1][i]) {
          valuesChanged = true;
          break;
        }
      }

      if (!isCrossFading) {
        dmxOutput.setData(1, enttecPro.getDmxData(1),
                          enttecPro.getDmxLength(1));
        memcpy(currentDmxValues[1], enttecPro.getDmxData(1),
               enttecPro.getDmxLength(1));
      }

      if (universeTimedOut[1]) {
        universeTimedOut[1] = false;
        // Smoothly fade back to live!
        const uint8_t *liveU1 = enttecPro.getDmxData(0);
        const uint8_t *liveU2 = enttecPro.getDmxData(1);
        startCrossFade(liveU1, liveU2, (unsigned long)fadeTimeSetting * 1000);
      }

      enttecPro.clearDmxFlag(1);
      hasActivity = true;
      lastDmxTime[1] = millis();
      dmxPacketCount[1]++;
      if (valuesChanged) {
        u2LedOnUntil = millis() + 150;
      }
    }

    if (hasActivity) {
      lastActivityTime = millis();
    }

    // Check for serial signal timeout / disconnection
    for (int port = 0; port < 2; port++) {
      if (lastDmxTime[port] > 0 && !universeTimedOut[port]) {
        if (millis() - lastDmxTime[port] > 2000) { // 2-second timeout
          universeTimedOut[port] = true;
          if (disconnectModeSetting == MODE_BLACKOUT) {
            uint8_t zeroData[512] = {0};
            startCrossFade(zeroData, zeroData,
                           (unsigned long)fadeTimeSetting * 1000);
          } else if (disconnectModeSetting >= 2 &&
                     disconnectModeSetting <= 11) {
            int cueIdx = disconnectModeSetting - 2;
            const uint8_t *flash_cues_ptr = (const uint8_t *)FLASH_CUES_ADDR;
            const uint8_t *cue_ptr = flash_cues_ptr + (cueIdx * CUE_SIZE);
            startCrossFade(cue_ptr, cue_ptr + 512,
                           (unsigned long)fadeTimeSetting * 1000);
          }
        }
      }
    }
  }

  // --------------------------------------------------
  // 3. Continuously transmit DMX frames at the set rate
  // --------------------------------------------------
  updateCrossFade();
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
  if (hasDisplay && !screenSaverActive &&
      (lastOledUpdate == 0 || (millis() - lastOledUpdate >= 100))) {
    lastOledUpdate = millis();
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    if (currentMenuState == STATE_NORMAL) {
      display.setCursor(0, 0);
      display.println("    KM LIGHT DMX");
      display.println("---------------------");

      // Universe 1 Status
      display.print("U1 (GP0): ");
      if (lastDmxTime[0] > 0 && (millis() - lastDmxTime[0] < 1000)) {
        display.println("Active");
      } else {
        display.println("Idle");
      }

      // Universe 2 Status
      display.print("U2 (GP4): ");
      if (lastDmxTime[1] > 0 && (millis() - lastDmxTime[1] < 1000)) {
        display.println("Active");
      } else {
        display.println("Idle");
      }

      // Draw Virtual LEDs (flash solid when values are actively changing)
      if (millis() < u1LedOnUntil) {
        display.fillCircle(115, 19, 3, SSD1306_WHITE);
      } else {
        display.drawCircle(115, 19, 3, SSD1306_WHITE);
      }

      if (millis() < u2LedOnUntil) {
        display.fillCircle(115, 27, 3, SSD1306_WHITE);
      } else {
        display.drawCircle(115, 27, 3, SSD1306_WHITE);
      }

      // DMX Rate Status
      display.print("Rate: ");
      if (dmxFpsSetting == 0) {
        display.print("Max");
      } else {
        display.print(dmxFpsSetting);
        display.print(" Hz");
      }
      display.print(" [");
      if (disconnectModeSetting == MODE_BLACKOUT) {
        display.print("BO");
      } else if (disconnectModeSetting == MODE_HOLD_LAST) {
        display.print("Last");
      } else {
        display.print("C");
        display.print(disconnectModeSetting - 1);
      }
      display.println("]");

    } else if (currentMenuState == STATE_MENU_MAIN) {
      display.setCursor(0, 0);
      display.println("=====================");
      display.println("      MAIN MENU      ");
      display.println("=====================");

      // Scrollable viewport of 3 items (since we have 6 items)
      int startIdx = mainMenuCursor - 1;
      if (startIdx < 0)
        startIdx = 0;
      if (startIdx > ITEM_COUNT - 3)
        startIdx = ITEM_COUNT - 3;

      for (int i = 0; i < 3; i++) {
        int idx = startIdx + i;
        bool selected = (mainMenuCursor == idx);
        int y = 26 + (i * 12);
        drawMenuItem(menuItemNames[idx], selected, y);
      }
      drawScrollBar(mainMenuCursor, ITEM_COUNT, 3, 26, 60);

    } else if (currentMenuState == STATE_SET_REFRESH_RATE) {
      display.setCursor(0, 0);
      display.println("=====================");
      display.println("  DMX REFRESH RATE   ");
      display.println("=====================");
      display.println();

      display.setTextSize(2);
      display.setCursor(20, 26);
      if (tempFpsSetting == 0) {
        display.println("  MAX");
      } else {
        display.print("  ");
        display.print(tempFpsSetting);
        display.println(" Hz");
      }

    } else if (currentMenuState == STATE_SET_DISCONNECT_MODE) {
      display.setCursor(0, 0);
      display.println("=====================");
      display.println("    ON DISCONNECT    ");
      display.println("=====================");
      display.println();

      display.setTextSize(2);
      display.setCursor(10, 26);
      if (tempDisconnectMode == MODE_BLACKOUT) {
        display.println("BLACKOUT");
      } else if (tempDisconnectMode == MODE_HOLD_LAST) {
        display.println("HOLD LAST");
      } else {
        display.print(" CUE ");
        display.println(tempDisconnectMode - 1);
      }

    } else if (currentMenuState == STATE_SET_SCREENSAVER_TIMEOUT) {
      display.setCursor(0, 0);
      display.println("=====================");
      display.println("  SCREENSAVER TIME   ");
      display.println("=====================");
      display.println();

      display.setTextSize(2);
      display.setCursor(10, 26);
      if (tempScreensaverTimeout == 0) {
        display.println("DISABLED");
      } else {
        display.print("  ");
        display.print(tempScreensaverTimeout);
        display.println(" Sec");
      }

    } else if (currentMenuState == STATE_SET_FADE_TIME) {
      display.setCursor(0, 0);
      display.println("=====================");
      display.println("   CUE FADE TIME     ");
      display.println("=====================");
      display.println();

      display.setTextSize(2);
      display.setCursor(20, 26);
      display.print("  ");
      display.print(tempFadeTime);
      display.println(" Sec");

    } else if (currentMenuState == STATE_MENU_CUES) {
      display.setCursor(0, 0);
      display.println("=====================");
      display.println("     SELECT CUE      ");
      display.println("=====================");

      // Scrollable viewport of 3 items (since we have 10 CUEs + Back = 11 items
      // total)
      int startIdx = cueMenuCursor - 1;
      if (startIdx < 0)
        startIdx = 0;
      if (startIdx > (NUM_CUES + 1) - 3)
        startIdx = (NUM_CUES + 1) - 3;

      for (int i = 0; i < 3; i++) {
        int idx = startIdx + i;
        bool selected = (cueMenuCursor == idx);
        int y = 26 + (i * 12);

        if (idx == NUM_CUES) {
          drawMenuItem("Back", selected, y);
        } else {
          char label[20];
          snprintf(label, sizeof(label), "CUE %d [%s]", idx + 1,
                   cueRecorded[idx] ? "Saved" : "Empty");
          drawMenuItem(label, selected, y);
        }
      }
      drawScrollBar(cueMenuCursor, NUM_CUES + 1, 3, 26, 60);

    } else if (currentMenuState == STATE_MENU_CUE_ACTION) {
      display.setCursor(0, 0);
      display.println("=====================");
      display.print("     CUE ");
      display.print(selectedCue + 1);
      display.println(" MENU");
      display.println("=====================");

      // Actions: Play, Record, Delete, Back
      drawMenuItem("Play", cueActionCursor == 0, 24);
      drawMenuItem("Record", cueActionCursor == 1, 34);
      drawMenuItem("Delete", cueActionCursor == 2, 44);
      drawMenuItem("Back", cueActionCursor == 3, 54);

    } else if (currentMenuState == STATE_PLAYING_CUE) {
      display.setCursor(0, 0);
      display.print("=====================");
      display.print("    PLAYING CUE ");
      display.print(playingCue + 1);
      display.println();
      display.println("=====================");
      display.println();

      display.setTextSize(2);
      display.setCursor(15, 26);
      display.println(" ACTIVE");

      display.setTextSize(1);
      display.setCursor(0, 52);
      display.println("Press M/Ent to Stop");

    } else if (currentMenuState == STATE_CUE_FEEDBACK) {
      display.setCursor(0, 0);
      display.println("=====================");
      display.print("      CUE ");
      display.print(selectedCue + 1);
      display.println(" INFO      ");
      display.println("=====================");
      display.println();

      display.setTextSize(2);
      display.setCursor(10, 26);
      if (activeFeedback == FEEDBACK_RECORDED) {
        display.println("RECORDED!");
      } else {
        display.println("DELETED!");
      }
    }

    display.display();
  }
}