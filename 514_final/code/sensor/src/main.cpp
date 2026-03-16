#include <Arduino.h>
#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ===================== Pins =====================
const int I2C_SDA = 6;
const int I2C_SCL = 7;
const int RED_PIN   = 9;
const int GREEN_PIN = 8;
const int BLUE_PIN  = 20;
const int MOISTURE_PIN = 2;

// ===================== BLE =====================
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID "abcd1234-ab12-ab12-ab12-abcdef123456"

BLECharacteristic *pCharacteristic;
bool deviceConnected = false;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("BLE connected");
  }
  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("BLE disconnected");
    BLEDevice::startAdvertising();
  }
};

// ===================== LED =====================
void setLED(bool r, bool g, bool b) {
  digitalWrite(RED_PIN,   r ? HIGH : LOW);
  digitalWrite(GREEN_PIN, g ? HIGH : LOW);
  digitalWrite(BLUE_PIN,  b ? HIGH : LOW);
}

void startupAnimation() {
  setLED(1,0,0); delay(200);
  setLED(0,1,0); delay(200);
  setLED(0,0,1); delay(200);
  setLED(1,1,0); delay(200);
  setLED(0,1,1); delay(200);
  setLED(1,0,1); delay(200);
  setLED(0,0,0);
}

// ===================== BH1750 =====================
uint8_t BH1750_ADDR = 0x23;

bool bh1750WriteCmd(uint8_t cmd) {
  Wire.beginTransmission(BH1750_ADDR);
  Wire.write(cmd);
  return (Wire.endTransmission() == 0);
}

bool bh1750Begin() {
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(50);
  if (!bh1750WriteCmd(0x01)) return false;
  delay(10);
  if (!bh1750WriteCmd(0x07)) return false;
  delay(10);
  if (!bh1750WriteCmd(0x10)) return false;
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
  if (bh1750Begin()) { Serial.println("BH1750 at 0x23"); return true; }
  BH1750_ADDR = 0x5C;
  if (bh1750Begin()) { Serial.println("BH1750 at 0x5C"); return true; }
  return false;
}

// ===================== 滑动平均滤波 =====================
#define FILTER_SIZE 10
int moistureBuffer[FILTER_SIZE] = {0};
float luxBuffer[FILTER_SIZE] = {0};
int filterIndex = 0;
bool filterFull = false;

void updateFilter(int moistureRaw, float lux) {
  moistureBuffer[filterIndex] = moistureRaw;
  luxBuffer[filterIndex] = lux;
  filterIndex = (filterIndex + 1) % FILTER_SIZE;
  if (filterIndex == 0) filterFull = true;
}

int getFilteredMoisture() {
  int count = filterFull ? FILTER_SIZE : filterIndex;
  if (count == 0) return 0;
  long sum = 0;
  for (int i = 0; i < count; i++) sum += moistureBuffer[i];
  return sum / count;
}

float getFilteredLux() {
  int count = filterFull ? FILTER_SIZE : filterIndex;
  if (count == 0) return 0;
  float sum = 0;
  for (int i = 0; i < count; i++) sum += luxBuffer[i];
  return sum / count;
}

// ===================== 数值转换 =====================
int moistureToPercent(int raw) {
  int pct = map(raw, 3450, 2200, 0, 100);
  return constrain(pct, 0, 100);
}

int luxToPercent(float lux) {
  return constrain((int)(lux / 10.0f), 0, 100);
}

int calcHealthScore(int moisturePct, int lightPct) {
  int mScore;
  if (moisturePct >= 60 && moisturePct <= 80) {
    mScore = 100;
  } else if (moisturePct < 60) {
    mScore = map(moisturePct, 0, 60, 0, 100);
  } else {
    mScore = map(moisturePct, 80, 100, 100, 0);
  }

  int lScore;
  if (lightPct >= 40 && lightPct <= 60) {
    lScore = 100;
  } else if (lightPct < 40) {
    lScore = map(lightPct, 0, 40, 0, 100);
  } else {
    lScore = map(lightPct, 60, 100, 100, 0);
  }

  return (mScore + lScore) / 2;
}

// ===================== Globals =====================
bool bh1750OK = false;
unsigned long lastReadTime = 0;
unsigned long lastSendTime = 0;

// ===================== Setup =====================
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  setLED(0,0,0);

  startupAnimation();
  setLED(0, 1, 0); // 绿色常亮表示正常运行

  pinMode(MOISTURE_PIN, INPUT);

  bh1750OK = tryBH1750Addresses();
  if (!bh1750OK) Serial.println("BH1750 FAILED");

  // BLE初始化
  BLEDevice::init("PlantSensor");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();

  Serial.println("BLE advertising started");
}

// ===================== Loop =====================
void loop() {
  // 每500ms读一次传感器更新滤波器
  if (millis() - lastReadTime >= 500) {
    lastReadTime = millis();

    float lux = bh1750OK ? readBH1750Lux() : 0;
    int moistureRaw = analogRead(MOISTURE_PIN);
    updateFilter(moistureRaw, lux);

    Serial.print("Lux: "); Serial.print(lux);
    Serial.print(" | Moisture raw: "); Serial.println(moistureRaw);
  }

  // 每2秒发送一次BLE数据
  if (millis() - lastSendTime >= 2000 && deviceConnected) {
    lastSendTime = millis();

    int moistureRaw = getFilteredMoisture();
    float lux = getFilteredLux();

    int moisturePct = moistureToPercent(moistureRaw);
    int lightPct = luxToPercent(lux);
    int healthScore = calcHealthScore(moisturePct, lightPct);

    char buf[32];
    snprintf(buf, sizeof(buf), "M:%d,L:%d,H:%d", moisturePct, lightPct, healthScore);
    pCharacteristic->setValue(buf);
    pCharacteristic->notify();

    Serial.print("Sent: "); Serial.println(buf);
  }
}