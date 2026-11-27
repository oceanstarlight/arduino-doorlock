// 보드 B (최소화) : 보드 A 의 'O'(열기) / 'F'(실패) 수신, 실내 버튼(외부 인터럽트), MsTimer2(내부 인터럽트) 5초 자동 잠금 + LED
#include <Servo.h>
#include <MsTimer2.h>

// 보드 A 링크 : 하드웨어 Serial, RX(D0) ← 보드 A A2  (업로드할 땐 D0 선을 잠깐 뽑기)
Servo servo;                     // 신호 A0, 0° 잠금 / 90° 열림

volatile bool btnFlag = false, lockFlag = false;
volatile unsigned long lastBtn = 0;
volatile byte openTicks = 0, failTicks = 0;   // 100ms 단위 카운트다운

void isrButton() {               // 외부 인터럽트 (D2, FALLING) : 300ms 채터링 방지
  if (millis() - lastBtn >= 300) { btnFlag = true; lastBtn = millis(); }
}

void isrTick() {                 // 내부 인터럽트 (MsTimer2, 100ms)
  static byte blink = 0;
  blink++;
  if (openTicks > 0 && --openTicks == 0) lockFlag = true;
  if (failTicks > 0) failTicks--;
  digitalWrite(10, openTicks > 0);                              // 초록 : 열림
  digitalWrite(12, failTicks > 0 ? (blink & 1) : openTicks == 0); // 빨강 : 잠김 / 실패 깜빡임
}

void openDoor() {
  openTicks = 50;                // 5초
  servo.write(90);
}

void setup() {
  Serial.begin(9600);
  pinMode(2, INPUT_PULLUP);      // 실내 버튼 (D2 ↔ GND)
  pinMode(10, OUTPUT);
  pinMode(12, OUTPUT);
  servo.write(0);                // attach 전에 각도를 정해 켜질 때 튀지 않게
  servo.attach(A0);
  attachInterrupt(0, isrButton, FALLING);
  MsTimer2::set(100, isrTick);
  MsTimer2::start();
}

void loop() {
  if (btnFlag) { btnFlag = false; openDoor(); }
  if (lockFlag) { lockFlag = false; if (openTicks == 0) servo.write(0); }   // 그 사이 다시 열렸으면 잠그지 않음

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'O') openDoor();
    if (c == 'F') failTicks = 20;   // 2초
  }
}
