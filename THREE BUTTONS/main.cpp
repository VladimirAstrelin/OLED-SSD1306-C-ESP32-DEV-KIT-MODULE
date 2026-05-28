/**
 * @file    main.cpp
 * @brief   ESP32 template: SSD1306 OLED + 3 debounced buttons (non-blocking)
 *
 * @author  VLAD
 * @date    2026
 * @version 1.0.0
 *
 * @hardware
 *   Board   : ESP32 Dev Module
 *   Display : SSD1306 128×64 OLED (I2C)
 *   Buttons : 3× tactile, active-LOW (INPUT_PULLUP)
 *   LED     : Built-in LED on GPIO2
 *
 * @wiring
 *   SSD1306 SDA  → GPIO21
 *   SSD1306 SCL  → GPIO22
 *   SSD1306 VCC  → 3.3V
 *   SSD1306 GND  → GND
 *   BTN1 one leg → GPIO4,  other leg → GND
 *   BTN2 one leg → GPIO16, other leg → GND
 *   BTN3 one leg → GPIO17, other leg → GND
 *
 * @features
 *   - Edge detection  — fires only on the falling edge (press moment), not on hold
 *   - Non-blocking    — no delay() anywhere; timing via millis()
 *   - Debounce        — stable-state algorithm with configurable timeout
 *   - Static strings  — const char* throughout; no heap allocation
 *   - Error handling  — display init failure halts with LED blink pattern
 *   - Scalable        — button count derived via sizeof(); add rows to buttons[]
 *
 * @dependencies
 *   - Adafruit GFX Library
 *   - Adafruit SSD1306
 *   - Wire (built-in)
 *
 * @license MIT
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================================
// CONFIGURATION — edit this section to adapt the template
// ============================================================================

// --- Display ---
#define SCREEN_WIDTH    128     ///< OLED width, pixels
#define SCREEN_HEIGHT   64      ///< OLED height, pixels
#define OLED_RESET      -1      ///< Reset pin; -1 = share Arduino reset / none
#define SCREEN_ADDRESS  0x3C    ///< I2C address: 0x3C (common) or 0x3D

// --- I2C pins (ESP32 Dev Module defaults) ---
#define I2C_SDA  21
#define I2C_SCL  22

// --- GPIO assignments ---
constexpr uint8_t BTN1    = 4;   ///< Button 1
constexpr uint8_t BTN2    = 16;  ///< Button 2
constexpr uint8_t BTN3    = 17;  ///< Button 3
constexpr uint8_t LED_PIN = 2;   ///< On-board LED (active HIGH on most ESP32 boards)

// --- Timing (ms) ---
constexpr unsigned long DEBOUNCE_MS      = 50;  ///< Debounce window
constexpr unsigned long LED_BLINK_MS     = 50;  ///< LED flash duration on button press
constexpr unsigned long ERROR_BLINK_MS   = 200; ///< LED blink half-period on fatal error

// ============================================================================
// BUTTON — data structure + array
// ============================================================================

/**
 * @brief Holds all state needed for edge-detecting, debounced button polling.
 *
 * Two-state debounce model:
 *   lastReading  — raw pin value, may still be bouncing
 *   stableState  — value that has been stable for ≥ DEBOUNCE_MS
 *
 * A press event is generated exactly once: when stableState transitions
 * HIGH → LOW (active-LOW wiring).
 */
struct Button {
    uint8_t       pin;              ///< GPIO number
    bool          lastReading;      ///< Last sampled pin level (may be noisy)
    bool          stableState;      ///< Confirmed stable level after debounce
    unsigned long lastDebounceTime; ///< Timestamp of the last level change (ms)
    const char*   serialMsg;        ///< String sent to Serial on press
    const char*   displayMsg;       ///< String shown on OLED on press
};

/**
 * @brief Button definitions.
 *
 * Initial state HIGH = released (INPUT_PULLUP).
 * To add a button: append a row here — BUTTON_COUNT updates automatically.
 */
Button buttons[] = {
    { BTN1, HIGH, HIGH, 0, "BTN1_PRESSED", "BTN 1" },
    { BTN2, HIGH, HIGH, 0, "BTN2_PRESSED", "BTN 2" },
    { BTN3, HIGH, HIGH, 0, "BTN3_PRESSED", "BTN 3" },
};

/// Number of buttons; computed at compile time — no magic numbers.
constexpr uint8_t BUTTON_COUNT = sizeof(buttons) / sizeof(buttons[0]);

// ============================================================================
// LED STATE — non-blocking flash
// ============================================================================

bool          ledActive    = false; ///< True while the LED is on and waiting to turn off
unsigned long ledStartTime = 0;     ///< millis() snapshot when the LED was turned on

// ============================================================================
// DISPLAY OBJECT
// ============================================================================

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

void displayText(const char* text);
bool isButtonPressed(Button& btn);
void flashLed();
void updateLed();

// ============================================================================
// displayText()
// ============================================================================

/**
 * @brief Clears the OLED buffer and renders a single text string.
 *
 * Uses const char* to avoid heap allocation (no String objects).
 * Text size 2 → each character is 12×16 px; fits ~10 chars per line.
 *
 * @param text  Null-terminated string to display.
 */
void displayText(const char* text) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(text);
    display.display();  // Push buffer to hardware
}

// ============================================================================
// isButtonPressed()
// ============================================================================

/**
 * @brief Polls a button and returns true exactly once per physical press.
 *
 * Algorithm (stable-state debounce + falling-edge detection):
 *  1. Sample the pin.
 *  2. If the sample differs from lastReading → restart the debounce timer.
 *  3. If the sample has been stable for ≥ DEBOUNCE_MS:
 *       - Update stableState.
 *       - Return true only on a HIGH→LOW transition (press, not release).
 *
 * Calling this every loop iteration is sufficient; no interrupt needed.
 *
 * @param btn  Reference to the Button struct to poll.
 * @return     true on the frame the button is first confirmed pressed.
 */
bool isButtonPressed(Button& btn) {
    bool currentReading = digitalRead(btn.pin);

    // Step 1 — detect any level change and restart the debounce timer
    if (currentReading != btn.lastReading) {
        btn.lastDebounceTime = millis();
        btn.lastReading      = currentReading;
    }

    // Step 2 — wait for the signal to settle
    if ((millis() - btn.lastDebounceTime) > DEBOUNCE_MS) {

        // Step 3 — stable transition detected
        if (currentReading != btn.stableState) {
            btn.stableState = currentReading;

            // Return true only on press (HIGH→LOW); ignore release (LOW→HIGH)
            if (btn.stableState == LOW) {
                return true;
            }
        }
    }

    return false;
}

// ============================================================================
// flashLed() / updateLed()
// ============================================================================

/**
 * @brief Starts a non-blocking LED flash.
 *
 * Records the start time and sets the active flag.
 * updateLed() must be called every loop iteration to complete the flash.
 */
void flashLed() {
    digitalWrite(LED_PIN, HIGH);
    ledStartTime = millis();
    ledActive    = true;
}

/**
 * @brief Turns off the LED when its flash duration has elapsed.
 *
 * Must be called unconditionally inside loop().
 * Has no effect if no flash is pending.
 */
void updateLed() {
    if (ledActive && (millis() - ledStartTime) >= LED_BLINK_MS) {
        digitalWrite(LED_PIN, LOW);
        ledActive = false;
    }
}

// ============================================================================
// setup()
// ============================================================================

void setup() {
    // --- Serial (debug output) ---
    Serial.begin(115200);
    delay(1000);  // Allow the Serial monitor to open before first prints

    Serial.println("========================================");
    Serial.println("  ESP32 Button Template — Starting up");
    Serial.println("========================================");

    // --- I2C bus ---
    Wire.begin(I2C_SDA, I2C_SCL);
    Serial.printf("[I2C]  SDA=GPIO%d  SCL=GPIO%d\n", I2C_SDA, I2C_SCL);

    // --- SSD1306 display ---
    // On failure: print diagnostics and halt with a rapid LED blink pattern.
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println("[FATAL] SSD1306 not found — check wiring & I2C address!");
        Serial.println("        SDA=GPIO21  SCL=GPIO22  VCC=3.3V  GND=GND");

        pinMode(LED_PIN, OUTPUT);
        while (true) {
            digitalWrite(LED_PIN, HIGH); delay(ERROR_BLINK_MS);
            digitalWrite(LED_PIN, LOW);  delay(ERROR_BLINK_MS);
            Serial.print('.');
        }
    }

    Serial.println("[OLED] SSD1306 initialized  OK");
    displayText("READY");

    // --- Buttons (INPUT_PULLUP → released = HIGH, pressed = LOW) ---
    for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        pinMode(buttons[i].pin, INPUT_PULLUP);
        Serial.printf("[BTN%d] GPIO%d configured\n", i + 1, buttons[i].pin);
    }

    // --- On-board LED ---
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    Serial.println("[LED]  GPIO2 configured");

    Serial.println("========================================");
    Serial.println("  System ready — waiting for input...");
    Serial.println("========================================");
}

// ============================================================================
// loop()
// ============================================================================

/**
 * @brief Main loop — fully non-blocking.
 *
 * Execution order each iteration:
 *   1. Update LED state (turn off after flash duration).
 *   2. Poll all buttons; on press → Serial + display + LED flash.
 */
void loop() {
    // 1. LED housekeeping — must run every iteration
    updateLed();

    // 2. Poll buttons
    for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        if (isButtonPressed(buttons[i])) {
            Serial.println(buttons[i].serialMsg);   // Debug output
            displayText(buttons[i].displayMsg);      // Update OLED
            flashLed();                              // Non-blocking LED flash
        }
    }
}


