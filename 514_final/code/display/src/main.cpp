#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEClient.h>

// ===================== Pins =====================
#define A1 2
#define A2 5
#define B1 3
#define B2 4
#define RED_PIN   9
#define GREEN_PIN 8
#define BLUE_PIN  20
#define BUTTON_PIN 21

// ===================== BLE =====================
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID "abcd1234-ab12-ab12-ab12-abcdef123456"

static boolean doConnect = false;
static boolean connected = false;
static BLERemoteCharacteristic* pRemoteChar;
static BLEAdvertisedDevice* myDevice;

// ===================== 传感器数据 =====================
volatile int moisturePct = 0;
volatile int lightPct    = 0;
volatile int healthScore = 0;

// ===================== 模式 =====================
int displayMode = 0; // 0=水分(蓝) 1=光照(红) 2=健康(紫)

// ===================== 电机 =====================
int stepIndex  = 0;
int currentPos = 0;
int targetPos  = 0;

unsigned long lastStepTime = 0;
const unsigned long stepInterval = 200;

const uint8_t seq[4][4] = {
  {1, 0, 1, 0},
  {0, 1, 1, 0},
  {0, 1, 0, 1},
  {1, 0, 0, 1}
};

int percentToSteps(int pct) {
  pct = constrain(pct, 0, 100);
  return map(pct, 0, 100, 350, 0);
}

void stepMotor(int dir) {
  stepIndex = (stepIndex + dir + 4) % 4;
  digitalWrite(A1, seq[stepIndex][0]);
  digitalWrite(A2, seq[stepIndex][1]);
  digitalWrite(B1, seq[stepIndex][2]);
  digitalWrite(B2, seq[stepIndex][3]);
  delay(8);
}

// 非阻塞走步，每200ms走一个chunk
void motorTick() {
  if (currentPos == targetPos) return;
  if (millis() - lastStepTime < stepInterval) return;
  lastStepTime = millis();

  int dir = (targetPos > currentPos) ? +1 : -1;
  int remaining = abs(targetPos - currentPos);
  int chunk = min(10, remaining);

  for (int i = 0; i < chunk; i++) {
    stepMotor(dir);
    currentPos += dir;
  }
}

void homeMotor() {
  Serial.println("Homing...");
  for (int j = 0; j < 50; j++) {
    for (int i = 0; i < 10; i++) stepMotor(-1);
    delay(500);
  }
  delay(300);
  for (int i = 0; i < 5; i++) stepMotor(+1);
  delay(200);
  currentPos = 0;
  targetPos  = 0;
  Serial.println("Home done");
}

// ===================== LED =====================
void setLED(bool r, bool g, bool b) {
  digitalWrite(RED_PIN,   r ? HIGH : LOW);
  digitalWrite(GREEN_PIN, g ? HIGH : LOW);
  digitalWrite(BLUE_PIN,  b ? HIGH : LOW);
}

void updateLED() {
  switch(displayMode) {
    case 0: setLED(0,0,1); break; // 蓝：水分
    case 1: setLED(1,0,0); break; // 红：光照
    case 2: setLED(1,0,1); break; // 紫：健康
  }
}

// ===================== 指针更新 =====================
void updatePointer() {
  int pct = 0;
  switch(displayMode) {
    case 0: pct = moisturePct;  break;
    case 1: pct = lightPct;     break;
    case 2: pct = healthScore;  break;
  }
  targetPos = percentToSteps(pct);
  Serial.print("Mode "); Serial.print(displayMode);
  Serial.print(" | pct="); Serial.print(pct);
  Serial.print(" | target="); Serial.println(targetPos);
}

// ===================== BLE回调 =====================
void notifyCallback(BLERemoteCharacteristic* pChar,
                    uint8_t* pData, size_t length, bool isNotify) {
  String data = String((char*)pData).substring(0, length);
  Serial.print("BLE received: "); Serial.println(data);

  int m = -1, l = -1, h = -1;
  if (sscanf(data.c_str(), "M:%d,L:%d,H:%d", &m, &l, &h) == 3) {
    moisturePct = m;
    lightPct    = l;
    healthScore = h;
    Serial.print("M="); Serial.print(m);
    Serial.print(" L="); Serial.print(l);
    Serial.print(" H="); Serial.println(h);
    updatePointer();
  }
}

class ClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    connected = true;
    Serial.println("BLE connected");
  }
  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("BLE disconnected");
  }
};

bool connectToServer() {
  BLEClient* pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new ClientCallbacks());
  pClient->connect(myDevice);

  BLERemoteService* pService = pClient->getService(SERVICE_UUID);
  if (!pService) {
    Serial.println("Service not found");
    return false;
  }

  pRemoteChar = pService->getCharacteristic(CHARACTERISTIC_UUID);
  if (!pRemoteChar) {
    Serial.println("Characteristic not found");
    return false;
  }

  if (pRemoteChar->canNotify()) {
    pRemoteChar->registerForNotify(notifyCallback);
  }

  return true;
}

class AdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.getName() == "PlantSensor") {
      Serial.println("Found PlantSensor!");
      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
    }
  }
};

// ===================== 按钮 =====================
int lastButtonState = HIGH;
unsigned long lastDebounce = 0;
const unsigned long debounceDelay = 50;

void checkButton() {
  int reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonState) {
    lastDebounce = millis();
  }

  if (millis() - lastDebounce > debounceDelay) {
    static int stableState = HIGH;
    if (reading != stableState) {
      stableState = reading;
      if (stableState == LOW) {
        displayMode = (displayMode + 1) % 3;
        Serial.print("Mode -> "); Serial.println(displayMode);
        updateLED();
        updatePointer();
      }
    }
  }

  lastButtonState = reading;
}

// ===================== Setup =====================
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(A1, OUTPUT);
  pinMode(A2, OUTPUT);
  pinMode(B1, OUTPUT);
  pinMode(B2, OUTPUT);

  pinMode(RED_PIN,   OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN,  OUTPUT);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  setLED(1,0,1); // 紫色：归零中

  homeMotor();
  delay(500);

  displayMode = 0;
  updateLED();   // 蓝色：水分模式
  updatePointer();

  // BLE扫描
  BLEDevice::init("PlantDisplay");
  BLEScan* pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());
  pScan->setActiveScan(true);
  pScan->start(10);
  Serial.println("BLE scanning...");
}

// ===================== Loop =====================
void loop() {
  checkButton();
  motorTick();

  if (doConnect) {
    if (connectToServer()) {
      Serial.println("Connected to PlantSensor");
    } else {
      Serial.println("Connect failed, retrying...");
    }
    doConnect = false;
  }

  // 断线重连
  if (!connected) {
    static unsigned long lastScan = 0;
    if (millis() - lastScan > 10000) {
      lastScan = millis();
      Serial.println("Rescanning...");
      BLEDevice::getScan()->start(5);
    }
  }
}