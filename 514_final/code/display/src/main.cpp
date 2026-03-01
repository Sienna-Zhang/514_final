#include <Arduino.h>
#include <NimBLEDevice.h>

// -------- Stepper pins (from your schematic: GPIO1..GPIO4) --------
static const int PIN_STP1 = 1; // GPIO1_A0_D0
static const int PIN_STP2 = 2; // GPIO2_A1_D1
static const int PIN_STP3 = 3; // GPIO3_A2_D2
static const int PIN_STP4 = 4; // GPIO4_A3_D3

static const int PIN_LED  = 10; // change if your LED is different

// X27/X25 gauge stepper typical range ~315 degrees.
// Many Switec gauge steppers use 945 steps for full sweep (315° * 3 steps/deg).
static const int GAUGE_MIN_STEP = 0;
static const int GAUGE_MAX_STEP = 945;

// -------- BLE UUIDs (must match sensor) --------
static const char* TARGET_NAME = "PlantSensor";
static const char* SVC_UUID  = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char* CH_UUID   = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";

static NimBLEAdvertisedDevice* advDevice = nullptr;
static NimBLEClient* client = nullptr;
static NimBLERemoteCharacteristic* remoteChar = nullptr;

static volatile uint8_t g_score = 0;
static int currentStep = 0;
static int targetStep = 0;

// 4-wire half-step sequence (8 states) – common for gauge steppers
static const uint8_t seq[8][4] = {
  {1,0,0,0},
  {1,1,0,0},
  {0,1,0,0},
  {0,1,1,0},
  {0,0,1,0},
  {0,0,1,1},
  {0,0,0,1},
  {1,0,0,1}
};
static int seqIndex = 0;

static void writeCoils(const uint8_t s[4]) {
  digitalWrite(PIN_STP1, s[0]);
  digitalWrite(PIN_STP2, s[1]);
  digitalWrite(PIN_STP3, s[2]);
  digitalWrite(PIN_STP4, s[3]);
}

static void stepOnce(int dir) {
  // dir: +1 forward, -1 backward
  seqIndex = (seqIndex + (dir > 0 ? 1 : 7)) & 7;
  writeCoils(seq[seqIndex]);
  currentStep += (dir > 0 ? 1 : -1);
}

static int scoreToStep(uint8_t score) {
  long s = map((long)score, 0, 100, GAUGE_MIN_STEP, GAUGE_MAX_STEP);
  if (s < GAUGE_MIN_STEP) s = GAUGE_MIN_STEP;
  if (s > GAUGE_MAX_STEP) s = GAUGE_MAX_STEP;
  return (int)s;
}

static void onNotify(NimBLERemoteCharacteristic* c, uint8_t* data, size_t len, bool isNotify) {
  if (len < 1) return;
  g_score = data[0];
  targetStep = scoreToStep(g_score);
  digitalWrite(PIN_LED, (g_score < 30) ? HIGH : LOW);
}

// Find the sensor by name + service UUID
class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(NimBLEAdvertisedDevice* d) override {
    if (d->haveName() && d->getName() == TARGET_NAME && d->isAdvertisingService(NimBLEUUID(SVC_UUID))) {
      advDevice = d;
      NimBLEDevice::getScan()->stop();
    }
  }
};

static bool connectToSensor() {
  if (!advDevice) return false;

  client = NimBLEDevice::createClient();
  if (!client->connect(advDevice)) {
    NimBLEDevice::deleteClient(client);
    client = nullptr;
    return false;
  }

  NimBLERemoteService* svc = client->getService(SVC_UUID);
  if (!svc) return false;

  remoteChar = svc->getCharacteristic(CH_UUID);
  if (!remoteChar) return false;

  if (remoteChar->canNotify()) {
    if (!remoteChar->subscribe(true, onNotify)) return false;
  } else {
    return false;
  }
  return true;
}

static void startScan() {
  advDevice = nullptr;

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(new ScanCallbacks(), false);
  scan->setInterval(45);
  scan->setWindow(15);
  scan->setActiveScan(true);
  scan->start(5, false); // scan up to 5s
}

static void gaugeHome() {
  // crude homing: back off to min by stepping "too much" backward.
  // If you have a mechanical stop (common on gauge steppers), this works.
  for (int i = 0; i < (GAUGE_MAX_STEP + 200); i++) {
    stepOnce(-1);
    delayMicroseconds(2500);
  }
  currentStep = GAUGE_MIN_STEP;
  targetStep = GAUGE_MIN_STEP;
}

void setup() {
  pinMode(PIN_STP1, OUTPUT);
  pinMode(PIN_STP2, OUTPUT);
  pinMode(PIN_STP3, OUTPUT);
  pinMode(PIN_STP4, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  writeCoils(seq[0]);

  // Home needle at boot
  gaugeHome();

  NimBLEDevice::init("PlantDisplay");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  // Scan + connect
  startScan();
  connectToSensor();
}

void loop() {
  // Reconnect if disconnected
  if (!client || !client->isConnected() || !remoteChar) {
    if (client) {
      client->disconnect();
      NimBLEDevice::deleteClient(client);
      client = nullptr;
      remoteChar = nullptr;
    }
    startScan();
    connectToSensor();
  }

  // Move needle smoothly toward targetStep
  int diff = targetStep - currentStep;
  if (diff != 0) {
    int dir = (diff > 0) ? +1 : -1;
    stepOnce(dir);
    // speed: increase delay if your motor misses steps
    delayMicroseconds(2500);
  } else {
    // reduce heat / power when holding still (optional)
    // writeCoils((uint8_t[4]){0,0,0,0});
    delay(10);
  }
}