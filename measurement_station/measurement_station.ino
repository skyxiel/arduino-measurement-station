// Arduino Electrical Measurement Station
//
// Measures voltage, resistance, light, temperature and a potentiometer,
// shows them on the LCD and streams CSV to the PC (115200 baud) for
// ../tools/serial_logger.py to log and plot.
//
// Wiring (see README.md for the full table):
//   LCD 1602:  RS -> D7, E -> D8, D4 -> D9, D5 -> D10, D6 -> D11, D7 -> D12
//              RW -> GND, VSS -> GND, VDD -> 5V, A -> 220R -> 5V, K -> GND
//              V0 (contrast) -> 1k -> GND  (or the wiper of a spare pot)
//   Button:    D2 -> button -> GND        (short press = next page, hold 1 s = reset min/max)
//   A0 voltage probe:  probe+ -> 10k -> A0 -> 10k -> GND   (measures 0..10 V, probe- to GND)
//   A1 resistance:     5V -> Rx (unknown) -> A1 -> 10k -> GND
//   A2 light:          5V -> photoresistor -> A2 -> 10k -> GND
//   A3 temperature:    5V -> 10k NTC thermistor -> A3 -> 10k -> GND
//   A4 potentiometer:  ends to 5V and GND, wiper -> A4
//
// Serial commands (newline terminated):
//   rate <ms>     sample period, 50..10000 ms
//   cal <volts>   calibrate the voltage channel against a multimeter reading
//   bg <volts>    set the internal 1.1 V bandgap value (improves every reading)
//   reset         reset min/max        help   list commands

#include <LiquidCrystal.h>
#include <EEPROM.h>

// ---------- Pins ----------
const int PIN_BUTTON = 2;
const int PIN_VOLT   = A0;
const int PIN_RES    = A1;
const int PIN_LIGHT  = A2;
const int PIN_TEMP   = A3;
const int PIN_POT    = A4;

LiquidCrystal lcd(7, 8, 9, 10, 11, 12);

// ---------- Circuit constants (measure your resistors for better accuracy) ----------
const float R_TOP      = 10000.0;  // voltage divider, probe side
const float R_BOTTOM   = 10000.0;  // voltage divider, ground side
const float R_REF      = 10000.0;  // known resistor under Rx
const float R_THERM    = 10000.0;  // resistor under the thermistor
const float THERM_R25  = 10000.0;  // thermistor resistance at 25 C
const float THERM_BETA = 3950.0;   // thermistor beta value
const int   OVERSAMPLE = 16;       // ADC readings averaged per sample

// ---------- Calibration stored in EEPROM ----------
struct Calibration {
  uint16_t magic;
  float bandgap;   // real value of the ~1.1 V internal reference
  float voltGain;  // correction factor for the voltage channel
};
const uint16_t CAL_MAGIC = 0xCA11;
Calibration cal = {CAL_MAGIC, 1.1, 1.0};

// ---------- State ----------
struct Readings {
  float vcc, volts, ohms, lightPct, tempC, potPct;
};
Readings r;
float vMin = 1e9, vMax = -1e9;

unsigned long samplePeriod = 250;
unsigned long lastSample = 0;
int page = 0;
const int NUM_PAGES = 3;

bool lastButton = HIGH;
unsigned long buttonDownAt = 0;
bool holdHandled = false;

String rxLine;

// Measure the UNO's real supply voltage using the internal 1.1 V bandgap.
// USB "5 V" is often 4.7-5.1 V, so this makes absolute voltage readings much better.
float readVcc() {
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);  // AVcc ref, measure 1.1 V bandgap
  delay(2);                                              // let the reference settle
  long sum = 0;
  for (int i = 0; i < OVERSAMPLE; i++) {
    ADCSRA |= _BV(ADSC);
    while (bit_is_set(ADCSRA, ADSC)) {}
    sum += ADC;
  }
  float adc = sum / (float)OVERSAMPLE;
  return cal.bandgap * 1023.0 / adc;
}

// Averaged analogRead. The first reading after switching channel is thrown away.
float readAdc(int pin) {
  analogRead(pin);
  long sum = 0;
  for (int i = 0; i < OVERSAMPLE; i++) sum += analogRead(pin);
  return sum / (float)OVERSAMPLE;
}

// Resistance of the upper leg of a divider whose lower leg is rLower.
// Ratiometric, so it doesn't depend on the supply voltage. Returns NAN if open circuit.
float upperResistance(float adc, float rLower) {
  if (adc < 2.0) return NAN;             // nothing connected
  if (adc > 1021.0) return 0.0;          // effectively a short
  return rLower * (1023.0 - adc) / adc;
}

void sampleAll() {
  r.vcc = readVcc();

  float a = readAdc(PIN_VOLT);
  r.volts = a / 1023.0 * r.vcc * (R_TOP + R_BOTTOM) / R_BOTTOM * cal.voltGain;

  r.ohms = upperResistance(readAdc(PIN_RES), R_REF);

  r.lightPct = readAdc(PIN_LIGHT) / 1023.0 * 100.0;

  float rt = upperResistance(readAdc(PIN_TEMP), R_THERM);
  if (isnan(rt) || rt <= 0) {
    r.tempC = NAN;
  } else {
    // Beta equation: 1/T = 1/T25 + ln(R/R25)/B
    float invT = 1.0 / 298.15 + log(rt / THERM_R25) / THERM_BETA;
    r.tempC = 1.0 / invT - 273.15;
  }

  r.potPct = readAdc(PIN_POT) / 1023.0 * 100.0;

  if (r.volts < vMin) vMin = r.volts;
  if (r.volts > vMax) vMax = r.volts;
}

void printFloatOrNan(float v, int decimals) {
  if (isnan(v)) Serial.print(F("nan"));
  else Serial.print(v, decimals);
}

void sendCsv() {
  Serial.print(millis());             Serial.print(',');
  Serial.print(r.vcc, 3);             Serial.print(',');
  Serial.print(r.volts, 3);           Serial.print(',');
  printFloatOrNan(r.ohms, 1);         Serial.print(',');
  Serial.print(r.lightPct, 1);        Serial.print(',');
  printFloatOrNan(r.tempC, 2);        Serial.print(',');
  Serial.println(r.potPct, 1);
}

void sendHeader() {
  Serial.println(F("t_ms,vcc_V,voltage_V,resistance_ohm,light_pct,temp_C,pot_pct"));
}

// Formats a resistance like "4.70k" or "220R" into buf (at least 16 chars)
void formatOhms(float ohms, char *buf) {
  if (isnan(ohms))           strcpy(buf, "OPEN");
  else if (ohms >= 1e6)      { dtostrf(ohms / 1e6, 4, 2, buf); strcat(buf, "M"); }
  else if (ohms >= 1e3)      { dtostrf(ohms / 1e3, 4, 2, buf); strcat(buf, "k"); }
  else                       { dtostrf(ohms, 4, 0, buf);       strcat(buf, "R"); }
}

// Prints one LCD line padded to 16 characters (avoids flicker from lcd.clear())
void lcdLine(int row, const char *text) {
  char padded[17];
  snprintf(padded, sizeof(padded), "%-16s", text);
  lcd.setCursor(0, row);
  lcd.print(padded);
}

void updateLcd() {
  char a[16], b[16], line[40];
  switch (page) {
    case 0:
      dtostrf(r.volts, 5, 2, a);
      dtostrf(r.vcc, 4, 2, b);
      snprintf(line, sizeof(line), "V%sV Vcc%s", a, b);
      lcdLine(0, line);
      dtostrf(vMin, 4, 2, a);
      dtostrf(vMax, 4, 2, b);
      snprintf(line, sizeof(line), "mn%s mx%s", a, b);
      lcdLine(1, line);
      break;
    case 1:
      formatOhms(r.ohms, a);
      snprintf(line, sizeof(line), "R = %s", a);
      lcdLine(0, line);
      dtostrf(r.lightPct, 3, 0, a);
      snprintf(line, sizeof(line), "Light %s%%", a);
      lcdLine(1, line);
      break;
    case 2:
      if (isnan(r.tempC)) strcpy(a, " --.-");
      else dtostrf(r.tempC, 5, 1, a);
      snprintf(line, sizeof(line), "Temp %s%cC", a, (char)223);  // 223 = degree symbol on HD44780
      lcdLine(0, line);
      dtostrf(r.potPct, 3, 0, a);
      snprintf(line, sizeof(line), "Pot  %s%%", a);
      lcdLine(1, line);
      break;
  }
}

void resetMinMax() {
  vMin = r.volts;
  vMax = r.volts;
  Serial.println(F("# min/max reset"));
}

void handleButton() {
  bool b = digitalRead(PIN_BUTTON);
  unsigned long now = millis();
  if (b == LOW && lastButton == HIGH) {
    buttonDownAt = now;
    holdHandled = false;
  }
  if (b == LOW && !holdHandled && now - buttonDownAt > 1000) {
    resetMinMax();
    holdHandled = true;
  }
  if (b == HIGH && lastButton == LOW && !holdHandled && now - buttonDownAt > 30) {
    page = (page + 1) % NUM_PAGES;  // short press
    updateLcd();
  }
  lastButton = b;
}

void saveCal() {
  EEPROM.put(0, cal);
}

void handleCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.startsWith("rate ")) {
    long ms = cmd.substring(5).toInt();
    samplePeriod = constrain(ms, 50, 10000);
    Serial.print(F("# sample period = ")); Serial.print(samplePeriod); Serial.println(F(" ms"));
  } else if (cmd.startsWith("cal ")) {
    float real = cmd.substring(4).toFloat();
    float measured = r.volts / cal.voltGain;  // reading without the old correction
    if (real > 0.1 && measured > 0.1) {
      cal.voltGain = real / measured;
      saveCal();
      Serial.print(F("# voltage gain = ")); Serial.println(cal.voltGain, 4);
    } else {
      Serial.println(F("# need a voltage > 0.1 V on the probe to calibrate"));
    }
  } else if (cmd.startsWith("bg ")) {
    float v = cmd.substring(3).toFloat();
    if (v > 1.0 && v < 1.2) {
      cal.bandgap = v;
      saveCal();
      Serial.print(F("# bandgap = ")); Serial.println(cal.bandgap, 4);
    } else {
      Serial.println(F("# bandgap must be between 1.0 and 1.2 V"));
    }
  } else if (cmd == "reset") {
    resetMinMax();
  } else if (cmd == "help") {
    Serial.println(F("# rate <ms> | cal <volts> | bg <volts> | reset | help"));
    Serial.println(F("# To find bg: measure the 5V pin with a meter, then bg = 1.1 * meter / reported vcc"));
  } else if (cmd.length() > 0) {
    Serial.print(F("# unknown command: ")); Serial.println(cmd);
  }
  sendHeader();  // lets the logger resync after messages
}

void readSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (rxLine.length()) handleCommand(rxLine);
      rxLine = "";
    } else if (rxLine.length() < 32) {
      rxLine += c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  lcd.begin(16, 2);
  lcdLine(0, "Measurement");
  lcdLine(1, "Station");

  Calibration stored;
  EEPROM.get(0, stored);
  if (stored.magic == CAL_MAGIC) cal = stored;

  Serial.println(F("# Electrical Measurement Station - type 'help' for commands"));
  Serial.print(F("# bandgap=")); Serial.print(cal.bandgap, 4);
  Serial.print(F(" voltGain=")); Serial.println(cal.voltGain, 4);
  sendHeader();
  delay(800);
}

void loop() {
  handleButton();
  readSerial();

  unsigned long now = millis();
  if (now - lastSample >= samplePeriod) {
    lastSample = now;
    sampleAll();
    sendCsv();
    updateLcd();
  }
}
