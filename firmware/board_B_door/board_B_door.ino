/*
 * ================================================================
 *  보드 B : 문 작동부 (Arduino Uno)
 * ================================================================
 *  담당 기능
 *   - 서보(SG90)로 잠금/해제
 *   - 실내 열림 버튼 (외부 인터럽트 INT0, D2, FALLING)
 *   - MsTimer2 100ms 틱 (내부 인터럽트) : 5초 자동 잠금 / LED 표시
 *   - 보드 A와 하드웨어 Serial(D0/D1) 통신 : 열림 명령 수신, 문 상태 전송
 *   - HC-06 블루투스(SoftwareSerial D4/D5) : 휴대폰 앱으로 열기/잠금/상태 확인
 *
 *  필요한 라이브러리 : MsTimer2 (라이브러리 매니저에서 설치)
 *                      Servo, SoftwareSerial 은 기본 내장
 *
 *  ⚠️ 업로드할 때는 D0 선(보드 A 의 A2 에서 오는 선)을 잠깐 뽑아야 한다.
 *     D0 는 USB 업로드에도 쓰는 핀이라, 보드 A 가 연결돼 있으면 업로드가 실패한다.
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
// D0 (RX) <- 보드 A 의 A2 (TX) / D1 (TX) -> 보드 A 의 A1 (RX)  : 하드웨어 Serial
const unsigned int PIN_BUTTON = 2;   // 실내 열림 버튼 (INT0)
const unsigned int PIN_BT_RX  = 4;   // <- HC-06 TXD (직결)
const unsigned int PIN_BT_TX  = 5;   // -> HC-06 RXD (1kΩ/2kΩ 분압, HC-06 RXD 는 3.3V)
const unsigned int PIN_SERVO  = A0;  // SG90 신호선(주황). Servo 는 아날로그 핀도 디지털처럼 사용 가능
const unsigned int PIN_LED_G  = 10;  // 초록 : 문 열림
const unsigned int PIN_LED_R  = 12;  // 빨강 : 잠김(켜짐) / 인증 실패(빠른 깜빡임)

// ---------------- 설정값 ----------------
const char    BT_PASSWORD[]  = "2580"; // 앱에서 보내는 비밀번호 (HC-06 페어링 PIN 과는 별개)
const int     LOCK_ANGLE     = 0;      // 잠금 각도 (실제 걸쇠에 맞게 조정)
const int     UNLOCK_ANGLE   = 90;     // 해제 각도
const unsigned int OPEN_TICKS     = 50;   // 열림 유지 시간 50 x 100ms = 5초
const unsigned int FAIL_TICKS     = 20;   // 실패 시 빨강 깜빡임 2초
const unsigned int SERVO_OFF_TICK = 8;    // 이동 후 0.8초 뒤 서보 신호 끄기 (떨림·전류 감소)

#define linkA Serial                  // 보드 A 링크 (하드웨어 Serial, D0/D1)
SoftwareSerial bt(PIN_BT_RX, PIN_BT_TX);
Servo lockServo;

// ---------------- ISR 과 공유하는 변수 (volatile 필수) ----------------
volatile bool          buttonFlag    = false;  // 버튼 눌림 알림
volatile unsigned long buttonPreTime = 0;      // 채터링 방지용 직전 시각
volatile uint8_t       openTicks     = 0;      // >0 이면 열림 상태, 0 되면 자동 잠금
volatile uint8_t       failTicks     = 0;      // >0 이면 빨강 빠른 깜빡임
volatile uint8_t       servoOffTicks = 0;
volatile bool          autoLockFlag  = false;
volatile bool          servoOffFlag  = false;

const char *openedBy = "";                     // 마지막으로 연 방법 (앱의 상태 요청 응답용)

// ================================================================
//  외부 인터럽트 : 실내 버튼 (FALLING = 누르는 순간 1 -> 0)
//  millis() 차이로 채터링을 막음. ISR 에서는 플래그만 세움.
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

// ================================================================
//  상태 보내기 : "ST:<st>" 또는 "ST:<st>:<by>"
// ================================================================
void printStatus(Print &out, const char *st, const char *by) {
  out.print(F("ST:"));
  out.print(st);
  if (by && by[0]) {
    out.print(':');
    out.print(by);
  }
  out.println();
}

// toA = false 면 앱(블루투스)에만 보냄 (FAIL 은 보드 A 가 쓰지 않음)
void report(const char *st, const char *by, bool toA) {
  if (toA) printStatus(linkA, st, by);
  printStatus(bt, st, by);
}

// ---------------- 서보 이동 ----------------
// write() 를 attach() 보다 먼저 해야 첫 펄스부터 목표 각도로 나감 (켜질 때 튀는 것 방지)
void moveServo(int angle) {
  lockServo.write(angle);
  lockServo.attach(PIN_SERVO);
  servoOffFlag  = false;           // 이전 detach 예약 취소
  servoOffTicks = SERVO_OFF_TICK;  // 일정 시간 뒤 detach
}

// 상태를 먼저 보낸 뒤 서보 이동 (SoftwareSerial 송신 중엔 인터럽트가 막혀 서보 펄스가 흔들리기 때문)
void openDoor(const char *by) {    // by : "KEY" / "RFID" / "BTN" / "BT"
  openedBy = by;
  report("OPEN", by, true);
  openTicks = OPEN_TICKS;
  moveServo(UNLOCK_ANGLE);
}

void lockDoor() {
  openTicks    = 0;                // 앱의 즉시 잠금도 이 함수로 처리
  autoLockFlag = false;
  report("LOCK", "", true);
  moveServo(LOCK_ANGLE);
}

void fail(const char *by) {        // by : "KEY" / "RFID" / "BT"
  failTicks = FAIL_TICKS;          // 빨강 깜빡임
  report("FAIL", by, false);
}

// ================================================================
//  한 줄 받기 : '\r' 또는 '\n' 이 오면 완성 (빈 줄 무시, println 의 \r\n 대응)
// ================================================================
struct LineBuf {
  char    buf[24];
  uint8_t len;
};

// 한 줄이 완성되면 true, 내용은 lb.buf 에 있음
bool readLine(Stream &in, LineBuf &lb) {
  while (in.available()) {
    char c = in.read();
    if (c == '\r' || c == '\n') {
      if (lb.len == 0) continue;
      lb.buf[lb.len] = '\0';
      lb.len = 0;
      return true;
    }
    if (lb.len < sizeof(lb.buf) - 1) lb.buf[lb.len++] = c;
  }
  return false;
}

// ---- 보드 A 명령 : OPEN:KEY / OPEN:RFID / FAIL:KEY / FAIL:RFID ----
void handleBoardA(const char *cmd) {
  if      (strcmp(cmd, "OPEN:KEY")  == 0) openDoor("KEY");
  else if (strcmp(cmd, "OPEN:RFID") == 0) openDoor("RFID");
  else if (strncmp(cmd, "FAIL:", 5) == 0) fail(cmd + 5);
}

// ---- 앱 명령 : OPEN:<비번> / LOCK:<비번> / ST? ----
void handleApp(const char *cmd) {
  if (strcmp(cmd, "ST?") == 0) {                       // 현재 상태 요청
    if (openTicks > 0) report("OPEN", openedBy, false);
    else               report("LOCK", "", false);
    return;
  }

  bool isOpen = (strncmp(cmd, "OPEN:", 5) == 0);
  bool isLock = (strncmp(cmd, "LOCK:", 5) == 0);
  if (!isOpen && !isLock) return;                      // 모르는 명령은 무시

  if (strcmp(cmd + 5, BT_PASSWORD) != 0) {             // 비밀번호 틀림
    fail("BT");
    return;
  }
  if (isOpen) openDoor("BT");
  else        lockDoor();
}

// ================================================================
void setup() {
  linkA.begin(9600);           // 보드 A 와 통신 (양쪽 속도 같아야 함)
  bt.begin(9600);              // HC-06 기본 속도

  pinMode(PIN_BUTTON, INPUT_PULLUP);   // 버튼은 D2 <-> GND 연결, 누르면 LOW
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_R, OUTPUT);

  lockDoor();                  // 전원 켜지면 잠금 상태로 시작

  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), isrButton, FALLING);
  MsTimer2::set(100, isrTick); // 100ms 주기
  MsTimer2::start();
}

void loop() {
  static LineBuf lineA  = {"", 0};
  static LineBuf lineBt = {"", 0};

  // ---- 1) 실내 버튼 (외부 인터럽트가 세운 플래그 처리) ----
  if (buttonFlag) {
    buttonFlag = false;
    openDoor("BTN");
  }

  // ---- 2) 타이머 인터럽트가 세운 플래그 처리 ----
  // 플래그가 선 직후 문이 다시 열렸을 수 있으니 현재 값을 한 번 더 확인
  if (autoLockFlag) { autoLockFlag = false; if (openTicks == 0) lockDoor(); }
  if (servoOffFlag) { servoOffFlag = false; if (servoOffTicks == 0) lockServo.detach(); }

  // ---- 3) 보드 A / 앱 명령 수신 ----
  if (readLine(linkA, lineA))  handleBoardA(lineA.buf);
  if (readLine(bt, lineBt))    handleApp(lineBt.buf);
}
