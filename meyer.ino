#include <Wire.h>
#include "rgb_lcd.h"
#include "SoftwareSerial.h"

// --- Noise sampling & printing schedule ---
const unsigned long NOISE_WINDOW_MS    = 350;   // averaging window
const unsigned long PRINT_INTERVAL_MS  = NOISE_WINDOW_MS;  // print every x
const unsigned long SAMPLE_INTERVAL_MS = 1;     // sample every 2 ms (~50 samples per 100 ms)
const int noiseMax = 600;

// --- Rolling window accumulators ---
unsigned long lastSampleMs   = 0;
unsigned long windowStartMs  = 0;
unsigned long lastPrintMs    = 0;

unsigned long sampleSum      = 0;
unsigned int  sampleCount    = 0;
int           last100msAvg   = 0;

// --- Optional: keep LCD in NOISE color for a while without blocking ---
unsigned long noiseHoldUntil = 0;  // millis timestamp until which NOISE color is shown
const unsigned long NOISE_HOLD_MS = 4000;
// ----------------------------
// Hardware
// ----------------------------
const int PIN_LED2       = 6;   // status LED blue button
const int PIN_BUTTON2    = 7;   // call lie
const int PIN_LED        = 4;   // status LED red button
const int PIN_BUTTON     = 5;   // roll button (active low -> we invert read)
const int PIN_SOUND      = A2;  // optional sound sensor
const int PIN_KEYPAD_RX  = 2;   // from keypad TX
const int PIN_KEYPAD_TX  = 3;   // to keypad RX (unused but required by SoftwareSerial)

SoftwareSerial keypadSerial(PIN_KEYPAD_RX, PIN_KEYPAD_TX);
rgb_lcd lcd;

// ----------------------------
// Behavior settings
// ----------------------------
const uint16_t MESSAGE_DELAY_MS = 1000;   // generic short delay
const uint16_t REAL_ROLL_SHOW_MS = 1000;  // show the real roll for 1 second

// LCD colors
const uint8_t LCD_R_NEUTRAL = 150, LCD_G_NEUTRAL = 150, LCD_B_NEUTRAL = 150;
const uint8_t LCD_R_ACTIVE  = 200, LCD_G_ACTIVE  =  50, LCD_B_ACTIVE  =  50;
const uint8_t LCD_R_LOSE    = 255, LCD_G_LOSE    =   0, LCD_B_LOSE    =   0;
const uint8_t LCD_R_NOISE   = 254, LCD_G_NOISE   = 100, LCD_B_NOISE   =   2;

// ----------------------------
// Previous lie state (for comparison)
// We only compare LIES (not real rolls).
// Rank: higher is better. If current < previous => GAME OVER.
// ----------------------------
int    prevLieRank = -1;   // -1 means "no previous lie yet"
String prevLieText = "";   // what we show when passing to next player

// ----------------------------
// Forward declarations
// ----------------------------
void   wipeLine(uint8_t row);
String readTwoDiceDigitsFromKeypad();
void   evaluateMeyerRoll(int d1, int d2, int &rankOut, String &textOut);
void   evaluateMeyerLieFromDigits(char c1, char c2, int &rankOut, String &textOut);
bool   buttonPressedEdge();

// ----------------------------
// Setup
// ----------------------------
void setup() {
  Serial.begin(9600);
  keypadSerial.begin(9600);

  // --- ADD: let Python know the board is ready
  Serial.println("READY");

  pinMode(PIN_LED, OUTPUT);

  // If you do NOT have an external pull-up resistor on the button,
  // use INPUT_PULLUP (recommended). Otherwise keep INPUT.
  pinMode(PIN_BUTTON, INPUT); // <-- CHANGE from INPUT if you don't have a resistor

  
  // Initialize timers for noise logic
  unsigned long now = millis();
  windowStartMs = now;
  lastPrintMs   = now;
  lastSampleMs  = now;


  randomSeed(analogRead(A0));

  lcd.begin(16, 2);
  lcd.setRGB(LCD_R_NEUTRAL, LCD_G_NEUTRAL, LCD_B_NEUTRAL);
  lcd.setCursor(0, 0);
  lcd.print("Meyer ver. 1.0");
  lcd.setCursor(0, 1);
  lcd.print("Ready...");
}

// ----------------------------
// Main loop
// ----------------------------
void loop() {
  // Optional: heartbeat so you see something even when idle
  static uint32_t lastBeat = 0;
  if (millis() - lastBeat > 1000) {
    lastBeat = millis();
  }

  if (buttonPressedEdge()) {
    // --- ADD:
    Serial.println("BTN");   // rising edge detected

    lcd.setRGB(LCD_R_ACTIVE, LCD_G_ACTIVE, LCD_B_ACTIVE);
    digitalWrite(PIN_LED, HIGH);

    // 1) REAL ROLL
    int d1 = random(1, 7);
    int d2 = random(1, 7);

    int realRank = 0;
    String realText;
    evaluateMeyerRoll(d1, d2, realRank, realText);

    // --- ADD: Log the real roll (CSV: tag,d1,d2,rank,text)
    Serial.print("REAL,");
    Serial.print(d1); Serial.print(',');
    Serial.print(d2); Serial.print(',');
    Serial.print(realRank); Serial.print(',');
    Serial.println(realText);

    wipeLine(0); wipeLine(1);
    lcd.setCursor(0, 0);
    lcd.print(realText);
    delay(REAL_ROLL_SHOW_MS);

    // 2) PROMPT FOR LIE
    wipeLine(0); wipeLine(1);
    lcd.setCursor(0, 1);
    lcd.print("Lie: ");
    String lieDigits = readTwoDiceDigitsFromKeypad(); // blocks until two digits

    // --- ADD: raw lie digits
    Serial.print("LIE_DIGITS,");
    Serial.println(lieDigits);

    // 3) EVALUATE LIE
    int lieRank = 0;
    String lieText;
    evaluateMeyerLieFromDigits(lieDigits[0], lieDigits[1], lieRank, lieText);

    // --- ADD: evaluated lie (CSV: tag,digits,rank,text)
    Serial.print("LIE,");
    Serial.print(lieDigits); Serial.print(',');
    Serial.print(lieRank);   Serial.print(',');
    Serial.println(lieText);

    // Show lie on LCD
    wipeLine(0); wipeLine(1);
    lcd.setCursor(0, 0);
    lcd.print(lieText);

    // 4) COMPARE ONLY LIES
    bool gameOver = false;
    if (prevLieRank != -1) {
      if (lieRank < prevLieRank) {
        gameOver = true;
      }
    }

    if (gameOver) {
      // --- ADD:
      Serial.println("GAME_OVER");

      lcd.setRGB(LCD_R_LOSE, LCD_G_LOSE, LCD_B_LOSE);
      wipeLine(0); wipeLine(1);
      lcd.setCursor(0, 0); lcd.print("   GAME  OVER  ");
      lcd.setCursor(0, 1); lcd.print("   You lose!   ");
      delay(2000);

      prevLieRank = -1;
      prevLieText = "";
      wipeLine(0); wipeLine(1);
      lcd.setRGB(LCD_R_NEUTRAL, LCD_G_NEUTRAL, LCD_B_NEUTRAL);
    } else {
      prevLieRank = lieRank;
      prevLieText = lieText;

      // --- ADD: not game over, pass state forward
      Serial.print("PASS,");
      Serial.print(prevLieRank); Serial.print(',');
      Serial.println(prevLieText);

      lcd.setRGB(LCD_R_NEUTRAL, LCD_G_NEUTRAL, LCD_B_NEUTRAL);
    }

    digitalWrite(PIN_LED, LOW);
  }

  // Optional: sound sensor
// Optional: sound sensor
// ---------- Non-blocking noise sampling / averaging / printing ----------

unsigned long now = millis();

// 1) Sample periodically (every SAMPLE_INTERVAL_MS)
if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
  lastSampleMs = now;
  int s = analogRead(PIN_SOUND);  // 0..1023 on Uno
  sampleSum   += (unsigned long)s;
  sampleCount += 1;
}

// 2) Close the 100 ms window -> compute average
if (now - windowStartMs >= NOISE_WINDOW_MS) {
  windowStartMs = now;

  if (sampleCount > 0) {
    last100msAvg = (int)(sampleSum / sampleCount);
  } else {
    last100msAvg = 0;
  }

  // Reset accumulators for next window
  sampleSum   = 0;
  sampleCount = 0;

  // 3) Threshold check uses ONLY the 100 ms average
  if (last100msAvg > noiseMax) {
    // Hold NOISE color without blocking
    noiseHoldUntil = now + NOISE_HOLD_MS;
  }
}

// 4) Maintain LCD color without delays
if ((long)(noiseHoldUntil - now) > 0) {
  // still in hold window
  lcd.setRGB(LCD_R_NOISE, LCD_G_NOISE, LCD_B_NOISE);
} else {
  lcd.setRGB(LCD_R_NEUTRAL, LCD_G_NEUTRAL, LCD_B_NEUTRAL);
}

// 5) Print once per second (only the number)
if (now - lastPrintMs >= PRINT_INTERVAL_MS) {
  lastPrintMs = now;
  Serial.println(last100msAvg);  // <-- just the number (no "IDLE")
}

}
// ----------------------------
// Utilities
// ----------------------------

// Show a blank (16 spaces) on the given LCD row.
void wipeLine(uint8_t row) {
  lcd.setCursor(0, row);
  lcd.print("                ");
}

// Simple rising-edge detection for the (active-low) button
bool buttonPressedEdge() {
  static bool lastPressed = false;
  bool pressed = !digitalRead(PIN_BUTTON); // invert: active low
  bool edge = (pressed && !lastPressed);
  lastPressed = pressed;
  return edge;
}

// Block until exactly TWO valid dice digits are entered via keypad.
// Valid digits: '1'..'6' only (7/8/9/0 ignored), '*' clears, '#' ignored.
// Echo typed digits after "Lie: ".
String readTwoDiceDigitsFromKeypad() {
  String input = "";

  // Repaint prompt
  wipeLine(1);
  lcd.setCursor(0, 1);
  lcd.print("Lie: ");

  while (true) {
    if (keypadSerial.available()) {
      uint8_t code = keypadSerial.read();
      char c = 0;

      // Map keypad scan codes to characters
      switch (code) {
        case 0xE1: c = '1'; break;
        case 0xE2: c = '2'; break;
        case 0xE3: c = '3'; break;
        case 0xE4: c = '4'; break;
        case 0xE5: c = '5'; break;
        case 0xE6: c = '6'; break;
        case 0xE7: c = '7'; break; // ignored (not a die face)
        case 0xE8: c = '8'; break; // ignored
        case 0xE9: c = '9'; break; // ignored
        case 0xEB: c = '0'; break; // ignored
        case 0xEA: // '*': clear input
          input = "";
          wipeLine(1);
          lcd.setCursor(0, 1);
          lcd.print("Lie: ");
          continue;
        case 0xEC: // '#': ignore
        default:
          continue;
      }

      // Accept only '1'..'6' as valid dice digits
      if (c >= '1' && c <= '6') {
        if (input.length() < 2) {
          input += c;
          lcd.print(c);
        }
      }

      // Return once we have exactly two digits
      if (input.length() == 2) {
        delay(200); // small debounce/pause
        return input;
      }
    }
  }
}

// Evaluate a REAL roll (two dice) into a rank (higher is better) and display text.
// TRUE Meyer ranking with 32 as the lowest normal roll.
//
// Rank scheme (higher = better):
//   21 (Meyer)           -> 1000
//   31 (Little Meyer)    ->  900
//   Pairs (66..11)       ->  800 + pip
//   Normal (65..41..32)  ->   XY (e.g., 65 -> 65, 41 -> 41, 32 -> 32)
//
// Display text:
//   Meyer! / Lil Meyer! / Par X / XY
void evaluateMeyerRoll(int d1, int d2, int &rankOut, String &textOut) {
  // normalize to high-first for standard display/ranking of normal rolls
  int hi = (d1 > d2) ? d1 : d2;
  int lo = (d1 > d2) ? d2 : d1;

  // Special: Meyer (21) and Little Meyer (31) (either order counts)
  bool isMeyer      = ((d1 == 2 && d2 == 1) || (d1 == 1 && d2 == 2));
  bool isLittleMeyer= ((d1 == 3 && d2 == 1) || (d1 == 1 && d2 == 3));

  if (isMeyer) {
    rankOut = 1000;
    textOut = "Meyer!";
    return;
  }
  if (isLittleMeyer) {
    rankOut = 900;
    textOut = "Lil Meyer!";
    return;
  }

  // Pairs
  if (d1 == d2) {
    rankOut = 800 + d1;     // 66 -> 806, ..., 11 -> 801
    textOut = "Par " + String(d1);
    return;
  }

  // Normal roll
  int val = hi * 10 + lo;   // e.g., 65, 41, 32
  rankOut = val;            // since 65>64>...>41>32, and we've defined 32 as lowest
  textOut = String(val);    // show as "65", "41", "32"
}

// Evaluate a DECLARED LIE from two keypad digits.
// Accepts '21' or '12' as Meyer; '31' or '13' as Little Meyer.
// Otherwise normalizes digits to high-first for ranking/formatting.
void evaluateMeyerLieFromDigits(char c1, char c2, int &rankOut, String &textOut) {
  int a = (c1 - '0');
  int b = (c2 - '0');

  // Safety clamp to 1..6 (should already be true due to input filter)
  if (a < 1 || a > 6 || b < 1 || b > 6) {
    rankOut = -1;
    textOut = "--";
    return;
  }

  // Reuse the same logic as for real rolls
  evaluateMeyerRoll(a, b, rankOut, textOut);
}