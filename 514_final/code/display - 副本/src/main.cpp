#include <Arduino.h>

#define A1 2
#define A2 5
#define B1 3
#define B2 4

#define RED_PIN   9
#define GREEN_PIN 8
#define BLUE_PIN  20

int stepIndex = 0;
int currentPos = 0;

const uint8_t seq[4][4] = {
  {1, 0, 1, 0},
  {0, 1, 1, 0},
  {0, 1, 0, 1},
  {1, 0, 0, 1}
};

// 0刻度在顺时针端(350步)，5刻度在逆时针端(0步)
// 总共350步=180度，每档70步=36度
const int LEVEL_POS[6] = {
  350,  // 0
  280,  // 1
  210,  // 2
  140,  // 3
  70,   // 4
  0     // 5
};

void setLED(bool r, bool g, bool b) {
  digitalWrite(RED_PIN,   r ? HIGH : LOW);
  digitalWrite(GREEN_PIN, g ? HIGH : LOW);
  digitalWrite(BLUE_PIN,  b ? HIGH : LOW);
}

void stepMotor(int dir) {
  stepIndex = (stepIndex + dir + 4) % 4;
  digitalWrite(A1, seq[stepIndex][0]);
  digitalWrite(A2, seq[stepIndex][1]);
  digitalWrite(B1, seq[stepIndex][2]);
  digitalWrite(B2, seq[stepIndex][3]);
  delay(8);
}

void moveToPosition(int target) {
  while (currentPos != target) {
    int dir = (target > currentPos) ? +1 : -1;
    int remaining = abs(target - currentPos);
    int chunk = min(10, remaining);

    for (int i = 0; i < chunk; i++) {
      stepMotor(dir);
      currentPos += dir;
    }
    delay(200);
  }
  Serial.print("到达位置: ");
  Serial.println(currentPos);
}

void homeMotor() {
  Serial.println("Homing: 逆时针撞限位...");
  setLED(true, true, false);

  // 分段撞限位
  for (int j = 0; j < 50; j++) {
    for (int i = 0; i < 10; i++) {
      stepMotor(-1);
    }
    delay(200);
  }

  delay(300);

  Serial.println("回退5步...");
  for (int i = 0; i < 5; i++) {
    stepMotor(+1);
  }
  delay(200);

  currentPos = 0;
  Serial.println("归零完成，当前在5刻度位置(0步)");
}

void goToLevel(int level) {
  if (level < 0) level = 0;
  if (level > 5) level = 5;

  int target = LEVEL_POS[level];
  Serial.print("前往刻度 ");
  Serial.print(level);
  Serial.print(" -> ");
  Serial.print(target);
  Serial.println("步");

  moveToPosition(target);

  switch(level) {
    case 0: setLED(0,0,0); break;
    case 1: setLED(1,0,0); break;
    case 2: setLED(0,1,0); break;
    case 3: setLED(0,0,1); break;
    case 4: setLED(1,1,0); break;
    case 5: setLED(0,1,1); break;
  }
}

int testLevel = 0;
unsigned long holdStart = 0;
const unsigned long holdTime = 3000;

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(A1, OUTPUT);
  pinMode(A2, OUTPUT);
  pinMode(B1, OUTPUT);
  pinMode(B2, OUTPUT);

  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);

  setLED(true, false, true);

  homeMotor();
  delay(500);

  // 归零后先走到0刻度
  goToLevel(0);
  holdStart = millis();
}

void loop() {
  if (millis() - holdStart >= holdTime) {
    testLevel++;
    if (testLevel > 5) testLevel = 0;

    goToLevel(testLevel);
    holdStart = millis();
  }
}