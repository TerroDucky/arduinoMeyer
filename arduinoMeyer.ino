#include <Wire.h>
#include "rgb_lcd.h"
#include "SoftwareSerial.h"

const unsigned long NOISE_WINDOW_MS    = 350;   // averaging window
const unsigned long PRINT_INTERVAL_MS  = NOISE_WINDOW_MS*10;  // print every 10x
const unsigned long SAMPLE_INTERVAL_MS = 1;     // sample every x ms
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
const int PIN_BUTTON     = 5;   // roll button (active low -> we invert read)
const int PIN_SOUND      = A2;  // sound sensor
const int PIN_KEYPAD_RX  = 2;   // from keypad TX
const int PIN_KEYPAD_TX  = 3;   // to keypad RX (unused but required by SoftwareSerial)

SoftwareSerial keypadSerial(PIN_KEYPAD_RX, PIN_KEYPAD_TX);
rgb_lcd lcd;


// Behavior settings

const uint16_t MESSAGE_DELAY_MS = 1000;   // generic short delay
const uint16_t REAL_ROLL_SHOW_MS = 1000;  // show the real roll for 1 second

// LCD colours
const uint8_t LCD_R_NEUTRAL = 150, LCD_G_NEUTRAL = 150, LCD_B_NEUTRAL = 150;
const uint8_t LCD_R_ACTIVE  = 200, LCD_G_ACTIVE  =  50, LCD_B_ACTIVE  =  50;
const uint8_t LCD_R_LOSE    = 255, LCD_G_LOSE    =   0, LCD_B_LOSE    =   0;
const uint8_t LCD_R_NOISE   = 254, LCD_G_NOISE   = 100, LCD_B_NOISE   =   2;


int    prevLieRank = -1;   // -1 means "no previous lie yet"
String prevLieText = "";   // what we show when passing to next player

void   wipeLine(uint8_t row);
String readTwoDiceDigitsFromKeypad();
void   evaluateMeyerRoll(int d1, int d2, int &rankOut, String &textOut);
void   evaluateMeyerLieFromDigits(char c1, char c2, int &rankOut, String &textOut);
bool   buttonPressedEdge();

void setup() {
  Serial.begin(9600);
  keypadSerial.begin(9600);

  Serial.println("READY");
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUTTON, INPUT); 
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
  static uint32_t lastBeat = 0;
  if (millis() - lastBeat > 1000) {
    lastBeat = millis();
  }

  if (buttonPressedEdge()) {
    lcd.setRGB(LCD_R_ACTIVE, LCD_G_ACTIVE, LCD_B_ACTIVE);
    digitalWrite(PIN_LED, HIGH);

    int d1 = random(1, 7);
    int d2 = random(1, 7);

    int realRank = 0;
    String realText;
    evaluateMeyerRoll(d1, d2, realRank, realText);

    Serial.print("REAL,");
    Serial.print(d1); Serial.print(',');
    Serial.print(d2); Serial.print(',');
    Serial.print(realRank); Serial.print(',');
    Serial.println(realText);

    wipeLine(0); wipeLine(1);
    lcd.setCursor(0, 0);
    lcd.print(realText);
    delay(REAL_ROLL_SHOW_MS);

    wipeLine(0); wipeLine(1);
    lcd.setCursor(0, 1);
    lcd.print("Lie: ");
    String lieDigits = readTwoDiceDigitsFromKeypad(); 

    Serial.print("LIE_DIGITS,");
    Serial.println(lieDigits);

    int lieRank = 0;
    String lieText;
    evaluateMeyerLieFromDigits(lieDigits[0], lieDigits[1], lieRank, lieText);

    Serial.print("LIE,");
    Serial.print(lieDigits); Serial.print(',');
    Serial.print(lieRank);   Serial.print(',');
    Serial.println(lieText);

    wipeLine(0); wipeLine(1);
    lcd.setCursor(0, 0);
    lcd.print(lieText);

    bool gameOver = false;
    if (prevLieRank != -1) {
      if (lieRank < prevLieRank) {
        gameOver = true;
      }
    }

    if (gameOver) {
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

      Serial.print("PASS,");
      Serial.print(prevLieRank); Serial.print(',');
      Serial.println(prevLieText);

      lcd.setRGB(LCD_R_NEUTRAL, LCD_G_NEUTRAL, LCD_B_NEUTRAL);
    }

    digitalWrite(PIN_LED, LOW);
  }

unsigned long now = millis();

// Sample periodically (every SAMPLE_INTERVAL_MS)
if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
  lastSampleMs = now;
  int s = analogRead(PIN_SOUND);  
  sampleSum   += (unsigned long)s;
  sampleCount += 1;
}

// Close the 100 ms window -> compute average
if (now - windowStartMs >= NOISE_WINDOW_MS) {
  windowStartMs = now;

  if (sampleCount > 0) {
    last100msAvg = (int)(sampleSum / sampleCount);
  } else {
    last100msAvg = 0;
  }

  sampleSum   = 0;
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

void wipeLine(uint8_t row) {
  lcd.setCursor(0, row);
  lcd.print("                ");
}

bool buttonPressedEdge() {
  static bool lastPressed = false;
  bool pressed = !digitalRead(PIN_BUTTON); 
  bool edge = (pressed && !lastPressed);
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
        delay(200); // small pause
        return input;
      }
    }
  }
}

void evaluateMeyerRoll(int d1, int d2, int &rankOut, String &textOut) {
  int hi = (d1 > d2) ? d1 : d2;
  int lo = (d1 > d2) ? d2 : d1;

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

  if (d1 == d2) {
    rankOut = 800 + d1;    
    textOut = "Par " + String(d1);
    return;
  }

  int val = hi * 10 + lo;   
  rankOut = val;            
  textOut = String(val);   
}

void evaluateMeyerLieFromDigits(char c1, char c2, int &rankOut, String &textOut) {
  int a = (c1 - '0');
  int b = (c2 - '0');

  if (a < 1 || a > 6 || b < 1 || b > 6) {
    rankOut = -1;
    textOut = "--";
    return;
  }

  evaluateMeyerRoll(a, b, rankOut, textOut);
}