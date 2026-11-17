/*
 * ================================================================
 *  보드 B : 문 작동부 (Arduino Uno)
 * ================================================================
 *  담당 기능
 *   - 서보(SG90)로 잠금/해제
 *   - HC-06 블루투스 인증 + HC-SR04 초음파로 1m 이내 접근 시 열림
 *   - 실내 열림 버튼 (외부 인터럽트 INT0, D2, FALLING)
 *   - MsTimer2 100ms 틱 (내부 인터럽트) : 자동 잠금 / LED 깜빡임 / 타임아웃
 *   - 보드 A와 SoftwareSerial(UART) 통신
 *
 *  ※ 업로드 전 반드시 D0 에 꽂힌 선(HC-06 TXD)을 뽑을 것!
 *    (HC-06 이 D0 을 잡고 있으면 업로드가 실패함. 보드 손상은 없음)
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
// D0 (RX) <- HC-06 TXD         (하드웨어 시리얼 = 블루투스)
// D1 (TX) -> 1kΩ -> HC-06 RXD, RXD -> 2kΩ -> GND (분압 약 3.33V, HC-06 RX는 3.3V 레벨!)
const uint8_t PIN_BUTTON  = 2;   // 실내 열림 버튼 (INT0)
const uint8_t PIN_LINK_RX = 4;   // <- 보드 A 의 A2 (TX)
const uint8_t PIN_LINK_TX = 5;   // -> 보드 A 의 A1 (RX)
const uint8_t PIN_TRIG    = 6;   // HC-SR04 Trig
const uint8_t PIN_ECHO    = 7;   // HC-SR04 Echo
const uint8_t PIN_SERVO   = A0;  // SG90 신호선(주황). Servo 는 아날로그 핀도 디지털처럼 사용 가능
const uint8_t PIN_LED_G   = 10;  // 초록 : 문 열림
const uint8_t PIN_LED_Y   = 11;  // 노랑 : 블루투스 인증됨, 접근 대기 중 (깜빡임)
const uint8_t PIN_LED_R   = 12;  // 빨강 : 잠김(켜짐) / 인증 실패(빠른 깜빡임)
// LED 저항 : 3개 모두 220Ω

// ---------------- 설정값 ----------------
const char     BT_PASSWORD[]  = "BT1234"; // 폰에서 보낼 인증 코드
const int      LOCK_ANGLE     = 0;        // 잠금 각도 (실제 걸쇠에 맞게 조정)
const int      UNLOCK_ANGLE   = 90;       // 해제 각도
const uint8_t  OPEN_TICKS     = 50;       // 열림 유지 시간 50 x 100ms = 5초
const uint16_t ARM_TICKS      = 300;      // BT 인증 후 접근 대기 300 x 100ms = 30초
const uint8_t  FAIL_TICKS     = 20;       // 실패 시 빨강 깜빡임 2초
const uint8_t  SERVO_OFF_TICK = 8;        // 이동 후 0.8초 뒤 서보 detach (떨림 방지)
const int      NEAR_CM        = 100;      // 1m
const uint8_t  NEAR_COUNT_OK  = 3;        // 연속 3번 1m 이내여야 열림 (오작동 방지)
const uint8_t  BT_MAX_FAIL    = 3;        // BT 비밀번호 연속 실패 허용 횟수
const unsigned long BT_LOCKOUT_MS = 30000; // 초과 시 30초 BT 입력 차단

SoftwareSerial link(PIN_LINK_RX, PIN_LINK_TX);
Servo lockServo;

// ---------------- ISR 과 공유하는 변수 (volatile 필수) ----------------
volatile bool          buttonFlag    = false;  // 버튼 눌림 알림
volatile unsigned long buttonPreTime = 0;      // 채터링 방지용 직전 시각
volatile uint8_t       openTicks     = 0;      // >0 이면 열림 상태, 0 되면 자동 잠금
volatile uint16_t      armTicks      = 0;      // >0 이면 BT 인증 후 접근 대기 중
volatile uint8_t       failTicks     = 0;      // >0 이면 빨강 빠른 깜빡임
volatile uint8_t       servoOffTicks = 0;
volatile bool          autoLockFlag  = false;
volatile bool          armEndFlag    = false;
volatile bool          servoOffFlag  = false;

// ================================================================
//  외부 인터럽트 : 실내 버튼 (FALLING = 누르는 순간 1 -> 0)
//  수업 예제처럼 millis() 차이로 채터링을 막음.
//  ISR 안에서는 Serial/서보 같은 무거운 일은 하지 않고 플래그만 세움.
// ================================================================
void isrButton() {
  unsigned long now = millis();   // ISR 안에서도 값 읽기는 가능
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
  if (armTicks > 0      && --armTicks == 0)      armEndFlag   = true;
  if (servoOffTicks > 0 && --servoOffTicks == 0) servoOffFlag = true;
  if (failTicks > 0) failTicks--;

  bool isOpen = (openTicks > 0);
  digitalWrite(PIN_LED_G, isOpen);
  digitalWrite(PIN_LED_Y, (armTicks > 0) && (blink & 0x04));   // 약 0.8초 주기 깜빡임
  if (failTicks > 0) digitalWrite(PIN_LED_R, blink & 0x01);    // 0.2초 주기 빠른 깜빡임
  else               digitalWrite(PIN_LED_R, !isOpen);         // 잠김이면 켜짐
}

// ---------------- 보드 A 로 상태 전송 ----------------
// 형식: "ST:<상태>\n"  예) ST:OPEN:BT, ST:LOCK, ST:ARM
void sendStatus(const char *msg) {
  link.print(F("ST:"));
  link.println(msg);
  Serial.println(msg);   // 폰(블루투스 터미널)에도 보임
}

// ---------------- 서보 이동 ----------------
// write() 를 attach() 보다 먼저 해야 첫 펄스부터 목표 각도로 나감
// (반대로 하면 attach 순간 기본값 90도로 튀었다가 돌아옴)
void moveServo(int angle) {
  lockServo.write(angle);
  lockServo.attach(PIN_SERVO);
  servoOffFlag  = false;           // 이전 detach 예약 취소
  servoOffTicks = SERVO_OFF_TICK;  // 일정 시간 뒤 detach
}

// 상태 전송을 서보 이동보다 먼저 함 :
// SoftwareSerial 송신 중엔 인터럽트가 잠깐 막혀 서보 펄스가 흔들리기 때문
void openDoor(const char *by) {
  char buf[16];
  snprintf(buf, sizeof(buf), "OPEN:%s", by);
  sendStatus(buf);

  noInterrupts();                  // 16비트 변수(armTicks)는 원자적으로 써야 함
  openTicks = OPEN_TICKS;
  armTicks  = 0;
  interrupts();
  moveServo(UNLOCK_ANGLE);
}

void lockDoor() {
  sendStatus("LOCK");
  openTicks = 0;
  moveServo(LOCK_ANGLE);
}

void signalFail(const char *msg) {
  failTicks = FAIL_TICKS;
  sendStatus(msg);
}

// ---------------- 초음파 거리 측정 (cm, 실패 시 -1) ----------------
int readDistanceCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  unsigned long dur = pulseIn(PIN_ECHO, HIGH, 25000UL); // 최대 약 4m, 타임아웃 25ms
  if (dur == 0) return -1;
  return (int)(dur / 58);
}

// ---------------- 한 줄 읽기 도우미 ----------------
// '\r' 또는 '\n' 이 올 때까지 버퍼에 모았다가 true 반환.
// (폰 앱 줄바꿈 설정이 CR / LF / CR+LF 무엇이든 동작하도록, 빈 줄은 무시)
bool readLine(Stream &s, char *buf, uint8_t size, uint8_t &len) {
  while (s.available()) {
    char c = s.read();
    if (c == '\r' || c == '\n') {
      if (len == 0) continue;
      buf[len] = '\0';
      len = 0;
      return true;
    }
    if (len < size - 1) buf[len++] = c;
  }
  return false;
}

// ================================================================
void setup() {
  Serial.begin(9600);          // HC-06 기본 속도 9600
  link.begin(9600);            // 보드 A 와 통신

  pinMode(PIN_BUTTON, INPUT_PULLUP);   // 버튼은 D2 <-> GND 연결, 누르면 LOW
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_Y, OUTPUT);
  pinMode(PIN_LED_R, OUTPUT);

  moveServo(LOCK_ANGLE);       // 전원 켜지면 잠금 상태로 시작

  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), isrButton, FALLING);
  MsTimer2::set(100, isrTick); // 100ms 주기
  MsTimer2::start();

  Serial.println(F("DoorLock B ready. Send password."));
  sendStatus("LOCK");
}

void loop() {
  static char    btBuf[20],   linkBuf[20];
  static uint8_t btLen = 0,   linkLen = 0;
  static unsigned long lastSonar = 0;
  static uint8_t nearCount = 0;
  static uint8_t btFail = 0;
  static unsigned long btLockUntil = 0;   // 0 이 아니면 그 시각까지 BT 입력 무시

  // ---- 1) 실내 버튼 (외부 인터럽트가 세운 플래그 처리) ----
  if (buttonFlag) {
    buttonFlag = false;
    openDoor("BTN");
  }

  // ---- 2) 타이머 인터럽트가 세운 플래그 처리 ----
  // 플래그가 선 직후 문이 다시 열렸을 수 있으니 현재 값을 한 번 더 확인
  if (autoLockFlag) { autoLockFlag = false; if (openTicks == 0) lockDoor(); }
  if (armEndFlag)   { armEndFlag = false;  sendStatus("ARMEND"); }
  if (servoOffFlag) { servoOffFlag = false; if (servoOffTicks == 0) lockServo.detach(); }

  // ---- 3) 블루투스(폰) 수신 ----
  if (btLockUntil != 0 && (long)(millis() - btLockUntil) >= 0) btLockUntil = 0;
  if (readLine(Serial, btBuf, sizeof(btBuf), btLen)) {
    if (btLockUntil != 0) {
      Serial.println(F("LOCKED OUT. wait."));   // 차단 중: 무시
    } else if (strcmp(btBuf, BT_PASSWORD) == 0) {
      btFail = 0;
      noInterrupts(); armTicks = ARM_TICKS; interrupts();
      nearCount = 0;
      sendStatus("ARM");            // 폰 화면: ARM → 1m 안으로 다가오면 열림
    } else if (strcmp(btBuf, "LOCK") == 0) {
      lockDoor();
    } else {
      signalFail("BTFAIL");
      if (++btFail >= BT_MAX_FAIL) {          // 무작위 대입 방지
        btFail = 0;
        btLockUntil = millis() + BT_LOCKOUT_MS;
        if (btLockUntil == 0) btLockUntil = 1;
      }
    }
  }

  // ---- 4) 보드 A 수신 (키패드 / RFID 결과) ----
  if (readLine(link, linkBuf, sizeof(linkBuf), linkLen)) {
    if      (strcmp(linkBuf, "OPEN:KEY")  == 0) openDoor("KEY");
    else if (strcmp(linkBuf, "OPEN:RFID") == 0) openDoor("RFID");
    else if (strncmp(linkBuf, "FAIL", 4)  == 0) failTicks = FAIL_TICKS; // A가 LCD에 이미 표시함
  }

  // ---- 5) BT 인증 상태에서만 초음파로 1m 접근 확인 (100ms 간격) ----
  noInterrupts(); bool armed = (armTicks > 0); interrupts();
  if (armed && millis() - lastSonar >= 100) {
    lastSonar = millis();
    int d = readDistanceCm();
    if (d > 2 && d <= NEAR_CM) nearCount++;
    else                       nearCount = 0;
    if (nearCount >= NEAR_COUNT_OK) {
      nearCount = 0;
      openDoor("BT");
    }
  }
}
