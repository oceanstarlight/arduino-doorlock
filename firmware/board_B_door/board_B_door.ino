/*
 * ================================================================
 *  보드 B : 문 작동부 (Arduino Uno)
 * ================================================================
 *  담당 기능
 *   - 서보(SG90)로 잠금/해제
 *   - 실내 열림 버튼 (외부 인터럽트 INT0, D2, FALLING)
 *   - MsTimer2 100ms 틱 (내부 인터럽트) : 5초 자동 잠금 / LED 표시
 *   - 보드 A와 SoftwareSerial(UART) 통신 : 열림 명령 수신, 문 상태 전송
 *
 *  필요한 라이브러리 : MsTimer2 (라이브러리 매니저에서 설치)
 *                      Servo, SoftwareSerial 은 기본 내장
 *
 *  타이머 사용 현황 (충돌 주의)
 *   - Timer0 : millis(), delay()
 *   - Timer1 : Servo 라이브러리  → D9, D10 PWM(analogWrite) 사용 불가 (digitalWrite 는 OK)
 *   - Timer2 : MsTimer2          → tone() 사용 불가 (부저는 능동 부저만)
 * ================================================================
 */

#include <SoftwareSerial.h>
#include <Servo.h>
#include <MsTimer2.h>

// ---------------- 핀 배치 ----------------
const unsigned int PIN_BUTTON  = 2;   // 실내 열림 버튼 (INT0)
const unsigned int PIN_LINK_RX = 4;   // <- 보드 A 의 A2 (TX)
const unsigned int PIN_LINK_TX = 5;   // -> 보드 A 의 A1 (RX)
const unsigned int PIN_SERVO   = A0;  // SG90 신호선(주황). Servo 는 아날로그 핀도 디지털처럼 사용 가능
const unsigned int PIN_LED_G   = 10;  // 초록 : 문 열림
const unsigned int PIN_LED_R   = 12;  // 빨강 : 잠김(켜짐) / 인증 실패(빠른 깜빡임)

// ---------------- 설정값 ----------------
const int     LOCK_ANGLE     = 0;    // 잠금 각도 (실제 걸쇠에 맞게 조정)
const int     UNLOCK_ANGLE   = 90;   // 해제 각도
const unsigned int OPEN_TICKS     = 50;   // 열림 유지 시간 50 x 100ms = 5초
const unsigned int FAIL_TICKS     = 20;   // 실패 시 빨강 깜빡임 2초
const unsigned int SERVO_OFF_TICK = 8;    // 이동 후 0.8초 뒤 서보 신호 끄기 (떨림·전류 감소)

SoftwareSerial link(PIN_LINK_RX, PIN_LINK_TX);
Servo lockServo;

// ---------------- ISR 과 공유하는 변수 (volatile 필수) ----------------
volatile bool          buttonFlag    = false;  // 버튼 눌림 알림
volatile unsigned long buttonPreTime = 0;      // 채터링 방지용 직전 시각
volatile uint8_t       openTicks     = 0;      // >0 이면 열림 상태, 0 되면 자동 잠금
volatile uint8_t       failTicks     = 0;      // >0 이면 빨강 빠른 깜빡임
volatile uint8_t       servoOffTicks = 0;
volatile bool          autoLockFlag  = false;
volatile bool          servoOffFlag  = false;

// ================================================================
//  외부 인터럽트 : 실내 버튼 (FALLING = 누르는 순간 1 -> 0)
//  수업 예제처럼 millis() 차이로 채터링을 막음. ISR 에서는 플래그만 세움.
// ================================================================
void isrButton() {
  unsigned long now = millis();
  if (now - buttonPreTime >= 300) {
    buttonFlag = true;
    buttonPreTime = now;
  }
}

// ================================================================
//  내부(타이머) 인터럽트 : MsTimer2, 100ms 마다 호출
//  카운트다운 + LED 표시를 담당
// ================================================================
void isrTick() {
  static uint8_t blink = 0;
  blink++;

  if (openTicks > 0     && --openTicks == 0)     autoLockFlag = true;
  if (servoOffTicks > 0 && --servoOffTicks == 0) servoOffFlag = true;
  if (failTicks > 0) failTicks--;

  bool isOpen = (openTicks > 0);
  digitalWrite(PIN_LED_G, isOpen);
  if (failTicks > 0) digitalWrite(PIN_LED_R, blink & 0x01);   // 0.2초 주기 빠른 깜빡임
  else               digitalWrite(PIN_LED_R, !isOpen);        // 잠김이면 켜짐
}

// ---------------- 서보 이동 ----------------
// write() 를 attach() 보다 먼저 해야 첫 펄스부터 목표 각도로 나감 (켜질 때 튀는 것 방지)
void moveServo(int angle) {
  lockServo.write(angle);
  lockServo.attach(PIN_SERVO);
  servoOffFlag  = false;           // 이전 detach 예약 취소
  servoOffTicks = SERVO_OFF_TICK;  // 일정 시간 뒤 detach
}

// 보드 A 로 문 상태를 먼저 보낸 뒤 서보 이동 (SoftwareSerial 송신 중엔 서보 펄스가 흔들리기 때문)
void openDoor(const char *by) {    // by : "KEY" / "RFID" / "BTN"
  link.print(F("ST:OPEN:"));
  link.println(by);
  openTicks = OPEN_TICKS;
  moveServo(UNLOCK_ANGLE);
}

void lockDoor() {
  link.println(F("ST:LOCK"));
  moveServo(LOCK_ANGLE);
}

// ================================================================
void setup() {
  link.begin(9600);            // 보드 A 와 통신 (양쪽 속도 같아야 함)

  pinMode(PIN_BUTTON, INPUT_PULLUP);   // 버튼은 D2 <-> GND 연결, 누르면 LOW
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_R, OUTPUT);

  lockDoor();                  // 전원 켜지면 잠금 상태로 시작

  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), isrButton, FALLING);
  MsTimer2::set(100, isrTick); // 100ms 주기
  MsTimer2::start();
}

void loop() {
  static char    buf[20];
  static uint8_t len = 0;

  // ---- 1) 실내 버튼 (외부 인터럽트가 세운 플래그 처리) ----
  if (buttonFlag) {
    buttonFlag = false;
    openDoor("BTN");
  }

  // ---- 2) 타이머 인터럽트가 세운 플래그 처리 ----
  // 플래그가 선 직후 문이 다시 열렸을 수 있으니 현재 값을 한 번 더 확인
  if (autoLockFlag) { autoLockFlag = false; if (openTicks == 0) lockDoor(); }
  if (servoOffFlag) { servoOffFlag = false; if (servoOffTicks == 0) lockServo.detach(); }

  // ---- 3) 보드 A 명령 수신 : '\r' 또는 '\n' 까지 모아서 한 줄로 처리 ----
  while (link.available()) {
    char c = link.read();
    if (c == '\r' || c == '\n') {
      if (len == 0) continue;            // 빈 줄 무시 (println 의 \r\n 대응)
      buf[len] = '\0';
      len = 0;
      if      (strcmp(buf, "OPEN:KEY")  == 0) openDoor("KEY");
      else if (strcmp(buf, "OPEN:RFID") == 0) openDoor("RFID");
      else if (strcmp(buf, "FAIL", 4)  == 0) failTicks = FAIL_TICKS;  // 빨강 깜빡임
      continue;
    }
    if (len < sizeof(buf) - 1) buf[len++] = c;
  }
}
