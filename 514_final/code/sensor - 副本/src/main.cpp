#include <Arduino.h>
#include <Wire.h>

// ===================== Pins =====================
// BH1750
const int I2C_SDA = 6;
const int I2C_SCL = 7;

// RGB LED (common cathode)
const int RED_PIN   = 9;
const int GREEN_PIN = 8;
const int BLUE_PIN  = 20;

// Moisture sensor
const int MOISTURE_PIN = 2;   // A0 / GPIO2

// BH1750 address
uint8_t BH1750_ADDR = 0x23;

// ===================== LED =====================
void setLED(bool r, bool g, bool b) {
  digitalWrite(RED_PIN,   r ? HIGH : LOW);
  digitalWrite(GREEN_PIN, g ? HIGH : LOW);
  digitalWrite(BLUE_PIN,  b ? HIGH : LOW);
}

void ledOff() {
  setLED(false, false, false);
}

void startupAnimation() {
  setLED(true, false, false);
  delay(250);
  setLED(false, true, false);
  delay(250);
  setLED(false, false, true);
  delay(250);
  setLED(true, true, false);
  delay(250);
  setLED(false, true, true);
  delay(250);
  setLED(true, false, true);
  delay(250);
  ledOff();
}

// ===================== BH1750 =====================
bool bh1750WriteCmd(uint8_t cmd) {
  Wire.beginTransmission(BH1750_ADDR);
  Wire.write(cmd);
  return (Wire.endTransmission() == 0);
}

bool bh1750Begin() {
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(50);

  if (!bh1750WriteCmd(0x01)) return false;  // power on
  delay(10);

  if (!bh1750WriteCmd(0x07)) return false;  // reset
  delay(10);

  if (!bh1750WriteCmd(0x10)) return false;  // continuous high res mode
  delay(180);

  return true;
}

float readBH1750Lux() {
  Wire.requestFrom(BH1750_ADDR, (uint8_t)2);

  if (Wire.available() == 2) {
    uint16_t raw = (Wire.read() << 8) | Wire.read();
    return raw / 1.2f;
  }

  return -1.0f;
}

bool tryBH1750Addresses() {
  BH1750_ADDR = 0x23;
  if (bh1750Begin()) {
    Serial.println("BH1750 found at 0x23");
    return true;
  }

  BH1750_ADDR = 0x5C;
  if (bh1750Begin()) {
    Serial.println("BH1750 found at 0x5C");
    return true;
  }

  return false;
}

// ===================== Moisture =====================
int readMoistureRaw() {
  return analogRead(MOISTURE_PIN);
}

// ===================== Globals =====================
bool bh1750OK = false;
unsigned long lastReadTime = 0;

// ===================== Setup =====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Sensor device test start");

  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  ledOff();

  startupAnimation();

  pinMode(MOISTURE_PIN, INPUT);

  bh1750OK = tryBH1750Addresses();
  if (!bh1750OK) {
    Serial.println("BH1750 init FAILED");
  }
}

// ===================== Loop =====================
void loop() {
  if (millis() - lastReadTime >= 1000) {
    lastReadTime = millis();

    float lux = -1.0f;
    if (bh1750OK) {
      lux = readBH1750Lux();
    }

    int moistureRaw = readMoistureRaw();

    Serial.print("Light (lux): ");
    Serial.print(lux);
    Serial.print(" | Moisture raw: ");
    Serial.println(moistureRaw);

    // ===== LED feedback for testing =====
    // 红：BH1750有问题
    // 蓝：光照高
    // 绿：湿度高
    // 紫：两者都偏高
    // 黄：都偏低 / 中间状态

    if (lux < 0 && bh1750OK) {
      static bool blink = false;
      blink = !blink;
      setLED(blink, false, false);
      return;
    }

    bool lightHigh = (lux > 300.0f);
    bool moistureHigh = (moistureRaw > 1500);

    if (lightHigh && moistureHigh) {
      setLED(true, false, true);      // purple
    } else if (lightHigh) {
      setLED(false, false, true);     // blue
    } else if (moistureHigh) {
      setLED(false, true, false);     // green
    } else {
      setLED(true, true, false);      // yellow
    }
  }
}