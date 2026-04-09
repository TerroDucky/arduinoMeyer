#include <Wire.h>
#include "rgb_lcd.h"
#include "SoftwareSerial.h"

const unsigned long NOISE_WINDOW_MS    = 350;
const unsigned long PRINT_INTERVAL_MS  = NOISE_WINDOW_MS * 10;
const unsigned long SAMPLE_INTERVAL_MS = 1;
const int noiseMax = 600;

// Rolling window accumulators
unsigned long lastSampleMs   = 0;
unsigned long windowStartMs  = 0;
unsigned long lastPrintMs    = 0;

unsigned long sampleSum      = 0;
unsigned int  sampleCount    = 0;
int           last100msAvg   = 0;

unsigned long noiseHoldUntil = 0;
const unsigned long NOISE_HOLD_MS = 4000;

const int PIN_LED2       = 6;   // status LED blue button
const int PIN_BUTTON2    = 7;   // call lie
const int PIN_LED        = 4;   // status LED red button
const int PIN_BUTTON     = 5;   // roll button (active low -> inverted read)
const int PIN_SOUND      = A2;
const int PIN_KEYPAD_RX  = 2;
const int PIN_KEYPAD_TX  = 3;

SoftwareSerial keypadSerial(PIN_KEYPAD_RX, PIN_KEYPAD_TX);
rgb_lcd lcd;

// Behavior settings
const uint16_t MESSAGE_DELAY_MS = 1000;
const uint16_t REAL_ROLL_SHOW_MS = 1000;

// LCD colours
const uint8_t LCD_R_NEUTRAL = 150, LCD_G_NEUTRAL = 150, LCD_B_NEUTRAL = 150;
const uint8_t LCD_R_ACTIVE  = 200, LCD_G_ACTIVE  =  50, LCD_B_ACTIVE  =  50;
const uint8_t LCD_R_LOSE    = 255, LCD_G_LOSE    =   0, LCD_B_LOSE    =   0;
const uint8_t LCD_R_NOISE   = 254, LCD_G_NOISE   = 100, LCD_B_NOISE   =   2;

// Previous lie state
int    prevLieRank = -1;
String prevLieText = "";

// ✅ NEW: previous real roll (hidden)
int    prevRealRank = -1;
String prevRealText = "";

// Prototypes
void   wipeLine(uint8_t row);
String readTwoDiceDigitsFromKeypad();
void   evaluateMeyerRoll(int d1, int d2, int &rankOut, String &textOut);
void   evaluateMeyerLieFromDigits(char c1, char c2, int &rankOut, String &textOut);
bool   buttonPressedEdge();
bool   button2PressedEdge();

void setup() {
  Serial.begin(9600);
  keypadSerial.begin(9600);

  Serial.println("READY");

  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_LED2, OUTPUT);
  pinMode(PIN_BUTTON, INPUT);
  pinMode(PIN_BUTTON2, INPUT);

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

void loop() {

  // -------- CALL LIE BUTTON --------
  if (button2PressedEdge()) {
    if (prevLieRank != -1 && prevRealRank != -1) {

      digitalWrite(PIN_LED2, HIGH);

      bool lied = (prevRealRank < prevLieRank);

      wipeLine(0);
      wipeLine(1);

      if (lied) {
        lcd.setRGB(LCD_R_LOSE, LCD_G_LOSE, LCD_B_LOSE);
        lcd.setCursor(0, 0);
        lcd.print("      LIE!      ");
      } else {
        lcd.setRGB(0, 200, 0);
        lcd.setCursor(0, 0);
        lcd.print("    NOT LIE     ");
      }

      delay(2000);

      // Reset round
      prevLieRank  = -1;
      prevLieText  = "";
      prevRealRank = -1;
      prevRealText = "";

      wipeLine(0);
      wipeLine(1);
      lcd.setRGB(LCD_R_NEUTRAL, LCD_G_NEUTRAL, LCD_B_NEUTRAL);

      digitalWrite(PIN_LED2, LOW);
    }
  }

  // -------- ROLL BUTTON --------
  if (buttonPressedEdge()) {
    lcd.setRGB(LCD_R_ACTIVE, LCD_G_ACTIVE, LCD_B_ACTIVE);
    digitalWrite(PIN_LED, HIGH);

    int d1 = random(1, 7);
    int d2 = random(1, 7);

    int realRank = 0;
    String realText;
    evaluateMeyerRoll(d1, d2, realRank, realText);

    // ✅ store real roll
    prevRealRank = realRank;
    prevRealText = realText;

    wipeLine(0); wipeLine(1);
    lcd.setCursor(0, 0);
    lcd.print(realText);
    delay(REAL_ROLL_SHOW_MS);

    wipeLine(0); wipeLine(1);
    lcd.setCursor(0, 1);
    lcd.print("Lie: ");
    String lieDigits = readTwoDiceDigitsFromKeypad();

    int lieRank = 0;
    String lieText;
    evaluateMeyerLieFromDigits(lieDigits[0], lieDigits[1], lieRank, lieText);

    wipeLine(0); wipeLine(1);
    lcd.setCursor(0, 0);
    lcd.print(lieText);

    bool gameOver = false;
    if (prevLieRank != -1 && lieRank < prevLieRank) {
      gameOver = true;
    }

    if (gameOver) {
      lcd.setRGB(LCD_R_LOSE, LCD_G_LOSE, LCD_B_LOSE);
      wipeLine(0); wipeLine(1);
      lcd.setCursor(0, 0);
      lcd.print("   GAME  OVER  ");
      lcd.setCursor(0, 1);
      lcd.print("   You lose!   ");
      delay(2000);

      prevLieRank  = -1;
      prevLieText  = "";
      prevRealRank = -1;
      prevRealText = "";

      wipeLine(0); wipeLine(1);
      lcd.setRGB(LCD_R_NEUTRAL, LCD_G_NEUTRAL, LCD_B_NEUTRAL);
    } else {
      prevLieRank = lieRank;
      prevLieText = lieText;
      lcd.setRGB(LCD_R_NEUTRAL, LCD_G_NEUTRAL, LCD_B_NEUTRAL);
    }

    digitalWrite(PIN_LED, LOW);
  }

  // -------- NOISE SENSOR --------
  unsigned long now = millis();

  if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = now;
    int s = analogRead(PIN_SOUND);
    sampleSum += s;
    sampleCount++;
  }

  if (now - windowStartMs >= NOISE_WINDOW_MS) {
    windowStartMs = now;
    last100msAvg = (sampleCount > 0) ? sampleSum / sampleCount : 0;
    sampleSum = 0;
    sampleCount = 0;

    if (last100msAvg > noiseMax) {
      noiseHoldUntil = now + NOISE_HOLD_MS;
    }
  }

  if ((long)(noiseHoldUntil - now) > 0) {
    lcd.setRGB(LCD_R_NOISE, LCD_G_NOISE, LCD_B_NOISE);
  } else {
    lcd.setRGB(LCD_R_NEUTRAL, LCD_G_NEUTRAL, LCD_B_NEUTRAL);
  }

  if (now - lastPrintMs >= PRINT_INTERVAL_MS) {
    lastPrintMs = now;
    Serial.println(last100msAvg);
  }
}

// -------- HELPERS --------

void wipeLine(uint8_t row) {
  lcd.setCursor(0, row);
  lcd.print("                ");
}

bool buttonPressedEdge() {
  static bool lastPressed = false;
  bool pressed = !digitalRead(PIN_BUTTON);
  bool edge = pressed && !lastPressed;
  lastPressed = pressed;
  return edge;
}

bool button2PressedEdge() {
  static bool lastPressed = false;
  bool pressed = digitalRead(PIN_BUTTON2);
  bool edge = pressed && !lastPressed;
  lastPressed = pressed;
  return edge;
}

String readTwoDiceDigitsFromKeypad() {
  String input = "";

  wipeLine(1);
  lcd.setCursor(0, 1);
  lcd.print("Lie: ");

  while (true) {
    if (keypadSerial.available()) {
      uint8_t code = keypadSerial.read();
      char c = 0;

      switch (code) {
        case 0xE1: c = '1'; break;
        case 0xE2: c = '2'; break;
        case 0xE3: c = '3'; break;
        case 0xE4: c = '4'; break;
        case 0xE5: c = '5'; break;
        case 0xE6: c = '6'; break;
        case 0xEA:
          input = "";
          wipeLine(1);
          lcd.setCursor(0, 1);
          lcd.print("Lie: ");
          continue;
        default:
          continue;
      }

      if (input.length() < 2) {
        input += c;
        lcd.print(c);
      }

      if (input.length() == 2) {
        delay(200);
        return input;
      }
    }
  }
}

void evaluateMeyerRoll(int d1, int d2, int &rankOut, String &textOut) {
  int hi = max(d1, d2);
  int lo = min(d1, d2);

  if ((hi == 2 && lo == 1)) {
    rankOut = 1000;
    textOut = "Meyer!";
  } else if ((hi == 3 && lo == 1)) {
    rankOut = 900;
    textOut = "Lil Meyer!";
  } else if (d1 == d2) {
    rankOut = 800 + d1;
    textOut = "Par " + String(d1);
  } else {
    rankOut = hi * 10 + lo;
    textOut = String(rankOut);
  }
}

void evaluateMeyerLieFromDigits(char c1, char c2, int &rankOut, String &textOut) {
  int a = c1 - '0';
  int b = c2 - '0';
  evaluateMeyerRoll(a, b, rankOut, textOut);
}
