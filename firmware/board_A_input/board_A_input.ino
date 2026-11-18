/*
 * ================================================================
 *  보드 A : 입력부 (Arduino Uno)
 * ================================================================
 *  담당 기능
 *   - PIR 모션 센서 : 사람이 감지되면 10초 동안 키패드 입력 활성화
 *   - 4x4 키패드 비밀번호 입력  ('*' = 지우기, '#' = 확인)
 *   - RC522 RFID 카드/태그 인식
 *   - I2C 1602 LCD 에 입력/상태 표시
 *   - 보드 B 와 SoftwareSerial(UART) 통신 : 인증 결과 전송, 문 상태 수신
 *
 *  필요한 라이브러리 (라이브러리 매니저에서 설치)
 *   - Keypad            (Mark Stanley, Alexander Brevig)
 *   - MFRC522           (GithubCommunity / miguelbalboa)
 *   - LiquidCrystal I2C (Frank de Brabander)
 *
 *  ※ RC522 는 반드시 3.3V 에 연결! 5V 에 꽂으면 모듈이 망가질 수 있음.
 * ================================================================
 */

#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <SoftwareSerial.h>

// ---------------- 핀 배치 ----------------
// 키패드 : 핀 위(1번)부터 C4 C3 C2 C1 R1 R2 R3 R4 → D8 D7 D6 D5 D4 D3 D2 A0 에 차례대로
// RFID   : SDA(SS)=D10, SCK=D13, MOSI=D11, MISO=D12, RST=3.3V 직결, 3.3V, GND
//          (SDA·SCK·MOSI 는 핀 → 1kΩ → [RC522] → 2kΩ → GND 분압 → 약 3.33V)
// LCD    : SDA=A4, SCL=A5, VCC=5V, GND
// PIR    : OUT=D9, VCC=5V, GND
// 링크   : A1(RX) <- 보드B D5 / A2(TX) -> 보드B D4
const uint8_t PIN_RFID_SS = 10;
const uint8_t PIN_PIR     = 9;
const uint8_t PIN_LINK_RX = A1;
const uint8_t PIN_LINK_TX = A2;
const uint8_t LCD_ADDR    = 0x27;   // 안 되면 0x3F 로 바꿔보기

// ---------------- 설정값 ----------------
const char PASSWORD[]   = "1234";        // 키패드 비밀번호
const uint8_t MAX_INPUT = 8;             // 최대 입력 자릿수
const unsigned long MSG_MS    = 3000;    // 안내 메시지 표시 시간
const unsigned long ACTIVE_MS = 10000;   // 마지막 움직임(또는 키 입력) 뒤 키패드 활성 시간

// 등록된 RFID UID (A3_rfid_test 로 카드를 찍어서 나온 값을 여기에 적기)
const uint8_t UID_COUNT = 2;
const uint8_t ALLOWED_UID[UID_COUNT][4] = {
  {0xDE, 0xAD, 0xBE, 0xEF},   // 예시 1 → 내 카드 UID 로 교체
  {0x12, 0x34, 0x56, 0x78},   // 예시 2 → 내 태그 UID 로 교체
};

// ---------------- 키패드 ----------------
const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'},
};
byte rowPins[ROWS] = {4, 3, 2, A0};   // R1, R2, R3, R4
byte colPins[COLS] = {5, 6, 7, 8};    // C1, C2, C3, C4

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
// RST 핀을 3.3V 에 바로 연결했으므로 리셋 핀은 '사용 안 함'으로 지정 → 라이브러리가 소프트 리셋 사용
MFRC522 rfid(PIN_RFID_SS, MFRC522::UNUSED_PIN);
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
SoftwareSerial link(PIN_LINK_RX, PIN_LINK_TX);

// ---------------- 상태 변수 ----------------
char    input[MAX_INPUT + 1];
uint8_t inputLen = 0;
unsigned long msgUntil = 0;       // 0 이 아니면 그 시각에 기본 화면으로 복귀
bool keypadActive = false;        // 모션 감지 후 키패드 입력 가능 상태
unsigned long lastMotion = 0;     // 마지막으로 움직임(또는 키 입력)이 있던 시각
const char *doorState = "LOCKED";

// ================================================================
//  LCD 표시
// ================================================================
// 기본 화면 : 1줄 = 문 상태, 2줄 = 키패드 활성이면 입력한 비밀번호(* 로 가림), 아니면 안내
void showIdle() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Door:"));
  lcd.print(doorState);
  lcd.setCursor(0, 1);
  if (keypadActive) {
    lcd.print(F("PW:"));
    for (uint8_t i = 0; i < inputLen; i++) lcd.print('*');
  } else {
    lcd.print(F("Wave to start"));
  }
  msgUntil = 0;
}

// 잠깐 띄우는 안내 메시지 (MSG_MS 뒤 기본 화면으로 돌아감)
void showMsg(const __FlashStringHelper *l1, const char *l2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(l1);
  lcd.setCursor(0, 1);
  lcd.print(l2);
  msgUntil = millis() + MSG_MS;
}

// ================================================================
//  인증 결과 처리 : 보드 B 로 "OPEN:KEY" / "FAIL:RFID" 같은 한 줄을 보냄
// ================================================================
void grant(const char *method) {          // method : "KEY" / "RFID"
  link.print(F("OPEN:"));
  link.println(method);                   // 보드 B 가 문을 엶
  showMsg(F("ACCESS GRANTED"), method);
}

void deny(const char *method) {
  link.print(F("FAIL:"));
  link.println(method);                   // 보드 B 는 빨강 LED 깜빡임
  showMsg(F("ACCESS DENIED"), method);
}

// ================================================================
//  PIR 모션 센서 : 감지되면 키패드 활성화, ACTIVE_MS 동안 움직임·입력이 없으면 비활성화
// ================================================================
void updateMotion() {
  if (digitalRead(PIN_PIR) == HIGH) {
    lastMotion = millis();
    if (!keypadActive) {
      keypadActive = true;
      if (msgUntil == 0) showIdle();
    }
  } else if (keypadActive && millis() - lastMotion >= ACTIVE_MS) {
    keypadActive = false;
    inputLen = 0;                         // 입력하다 만 비밀번호는 지움
    if (msgUntil == 0) showIdle();
  }
}

// ================================================================
//  키패드
// ================================================================
void handleKey(char k) {
  lastMotion = millis();                  // 입력하는 동안은 꺼지지 않게
  if (k == '*') {                         // 전체 지우기
    inputLen = 0;
  } else if (k == '#') {                  // 확인
    input[inputLen] = '\0';
    bool ok = (strcmp(input, PASSWORD) == 0);
    inputLen = 0;
    if (ok) grant("KEY");
    else    deny("KEY");
    return;
  } else if (k >= '0' && k <= '9') {      // 숫자만 입력 (A~D 는 무시)
    if (inputLen < MAX_INPUT) input[inputLen++] = k;
  }
  showIdle();
}

// ================================================================
//  RFID
// ================================================================
void handleRfid() {
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial())   return;

  bool ok = false;
  if (rfid.uid.size == 4) {               // 일반 MIFARE 카드는 4바이트 UID
    for (uint8_t n = 0; n < UID_COUNT && !ok; n++) {
      ok = (memcmp(rfid.uid.uidByte, ALLOWED_UID[n], 4) == 0);
    }
  }
  if (ok) grant("RFID");
  else    deny("RFID");

  rfid.PICC_HaltA();                      // 같은 카드 연속 인식 방지
  rfid.PCD_StopCrypto1();
}

// ================================================================
//  보드 B 에서 오는 상태 메시지 ("ST:OPEN:BTN", "ST:LOCK" 등)
// ================================================================
void handleStatus(const char *buf) {
  if (strncmp(buf, "ST:", 3) != 0) return;
  const char *st = buf + 3;

  if (strncmp(st, "OPEN", 4) == 0) {
    doorState = "OPEN";
    const char *by = (st[4] == ':') ? st + 5 : "";
    if      (strcmp(by, "BTN") == 0) showMsg(F("OPEN from"), "INSIDE button");
    else if (msgUntil == 0)          showIdle();  // KEY/RFID 는 이미 GRANTED 표시 중
  } else if (strcmp(st, "LOCK") == 0) {
    doorState = "LOCKED";
    if (msgUntil == 0) showIdle();
  }
}

// 보드 B 에서 오는 글자를 모아 '\r' 또는 '\n' 이 오면 한 줄로 처리 (빈 줄은 무시)
void handleLink() {
  static char buf[20];
  static uint8_t len = 0;
  while (link.available()) {
    char c = link.read();
    if (c == '\r' || c == '\n') {
      if (len == 0) continue;
      buf[len] = '\0';
      len = 0;
      handleStatus(buf);
      continue;
    }
    if (len < sizeof(buf) - 1) buf[len++] = c;
  }
}

// ================================================================
void setup() {
  Serial.begin(9600);      // USB 시리얼 모니터 (동작 확인용)
  link.begin(9600);        // 보드 B 와 통신 (양쪽 속도 같아야 함)

  lcd.init();
  lcd.backlight();

  pinMode(PIN_PIR, INPUT);

  SPI.begin();
  rfid.PCD_Init();
  Serial.println(F("DoorLock A ready."));

  showMsg(F("SMART DOORLOCK"), "Starting...");
}

void loop() {
  handleLink();

  // 안내 메시지 시간이 끝나면 기본 화면으로
  if (msgUntil != 0 && (long)(millis() - msgUntil) >= 0) showIdle();

  updateMotion();

  char k = keypad.getKey();               // 비활성 중에 눌린 키는 읽고 버림
  if (k && keypadActive) handleKey(k);

  handleRfid();
}
