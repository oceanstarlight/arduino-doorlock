/*
 * ================================================================
 *  보드 A : 입력부 (Arduino Uno)
 * ================================================================
 *  담당 기능
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
 *  ※ LCD 에 아무것도 안 보이면: (1) 뒷면 파란 가변저항으로 명암 조절
 *                              (2) i2c_scanner 로 주소 확인 후 LCD_ADDR 수정
 * ================================================================
 */

#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <SoftwareSerial.h>

// ---------------- 핀 배치 ----------------
// 키패드 : R1~R4 = D8, D7, D6, D5 / C1~C4 = D4, D3, D2, A0
// RFID   : SDA(SS)=D10, SCK=D13, MOSI=D11, MISO=D12, RST=3.3V 직결, 3.3V, GND
//          (분압 → 약 3.33V : SCK·MOSI 는 2kΩ / 2kΩ+2kΩ, SDA 는 10kΩ / 10kΩ+10kΩ)
// LCD    : SDA=A4, SCL=A5, VCC=5V, GND
// 링크   : A1(RX) <- 보드B D5 / A2(TX) -> 보드B D4
const uint8_t PIN_RFID_SS  = 10;
const uint8_t PIN_LINK_RX  = A1;
const uint8_t PIN_LINK_TX  = A2;
const uint8_t LCD_ADDR     = 0x27;   // 안 되면 0x3F 로 바꿔보기

// ---------------- 테스트 모드 ----------------
// 1 이면 보드 B 없이 보드 A 만 테스트 가능 :
//  - 보드 B 로 보내는 메시지를 시리얼 모니터에 "[A->B] OPEN:KEY" 처럼 출력
//  - 시리얼 모니터에서 "ST:OPEN:BT" 같은 줄을 보내면 보드 B 가 보낸 것처럼 처리
//  - 시리얼 모니터 설정: 속도 9600, 오른쪽 아래 줄 끝 "New Line"(또는 "Both NL & CR") 권장
//    ("No line ending" 이어도 입력 후 0.2초 지나면 한 줄로 처리함)
// 보드 B 와 연결해서 최종 시연할 땐 0 으로 바꿔도 되고, 1 로 둬도 동작엔 문제 없음
#define TEST_MODE 1

// ---------------- 설정값 ----------------
const char PASSWORD[]   = "1234";        // 키패드 비밀번호
const uint8_t MAX_INPUT = 8;             // 최대 입력 자릿수
const uint8_t MAX_FAIL  = 3;             // 연속 실패 허용 횟수
const unsigned long LOCKOUT_MS = 30000;  // 초과 시 30초 입력 차단
const unsigned long MSG_MS     = 3000;   // 안내 메시지 표시 시간

// 등록된 RFID UID (처음엔 모르는 카드를 찍어서 LCD/시리얼에 뜬 UID 를 여기에 적기)
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
byte rowPins[ROWS] = {8, 7, 6, 5};
byte colPins[COLS] = {4, 3, 2, A0};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
// RST 핀을 3.3V 에 바로 연결했으므로 리셋 핀은 '사용 안 함'으로 지정 → 라이브러리가 소프트 리셋 사용
// (MFRC522 라이브러리 1.4.x 이상 필요. UNUSED_PIN 에서 오류 나면 라이브러리 업데이트)
MFRC522 rfid(PIN_RFID_SS, MFRC522::UNUSED_PIN);
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
SoftwareSerial link(PIN_LINK_RX, PIN_LINK_TX);

// ---------------- 상태 변수 ----------------
char    input[MAX_INPUT + 1];
uint8_t inputLen = 0;
uint8_t failCount = 0;
unsigned long lockoutUntil = 0;   // 0 이 아니면 그 시각까지 입력 차단
unsigned long msgUntil = 0;       // 0 이 아니면 그 시각에 기본 화면으로 복귀
const char *doorState = "LOCKED";

// 한 줄 수신 버퍼 (구조체는 함수보다 위에 있어야 Arduino IDE 자동 원형 생성에서 오류가 안 남)
struct LineBuf { char buf[20]; uint8_t len; unsigned long lastRx; };

// ================================================================
//  LCD 표시
// ================================================================
// 기본 화면 : 1줄 = 문 상태, 2줄 = 입력한 비밀번호(* 로 가림)
void showIdle() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Door:"));
  lcd.print(doorState);
  lcd.setCursor(0, 1);
  lcd.print(F("PW:"));
  for (uint8_t i = 0; i < inputLen; i++) lcd.print('*');
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
//  인증 결과 처리
// ================================================================
// 보드 B 로 한 줄 전송 ("OPEN:KEY" 등). 테스트 모드면 시리얼 모니터에도 출력
void sendLink(const __FlashStringHelper *head, const char *method) {
  link.print(head);
  link.println(method);
#if TEST_MODE
  Serial.print(F("[A->B] "));
  Serial.print(head);
  Serial.println(method);
#endif
}

void grant(const char *method) {          // method : "KEY" / "RFID"
  failCount = 0;
  sendLink(F("OPEN:"), method);           // 보드 B 에 열림 명령
  showMsg(F("ACCESS GRANTED"), method);
}

void deny(const char *method) {
  failCount++;
  sendLink(F("FAIL:"), method);           // 보드 B 는 빨강 LED 깜빡임
  if (failCount >= MAX_FAIL) {
    lockoutUntil = millis() + LOCKOUT_MS;
    failCount = 0;
    showMsg(F("TOO MANY FAILS"), "Wait 30 sec");
  } else {
    char buf[17];
    snprintf(buf, sizeof(buf), "%s  (%u/%u)", method, failCount, MAX_FAIL);
    showMsg(F("ACCESS DENIED"), buf);
  }
}

bool isLockedOut() {
  if (lockoutUntil == 0) return false;
  if ((long)(millis() - lockoutUntil) >= 0) {   // 오버플로에 안전한 비교
    lockoutUntil = 0;
    // 차단 중에 온 보드 B 메시지가 아직 표시 중이면, 그 메시지 시간이 끝날 때 기본 화면으로 감
    if (msgUntil == 0 || (long)(millis() - msgUntil) >= 0) showIdle();
    return false;
  }
  return true;
}

// ================================================================
//  키패드
// ================================================================
void handleKey(char k) {
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

  // UID 를 "DE AD BE EF" 형태 문자열로 만들기 (등록용)
  char uidStr[17] = "";
  for (byte i = 0; i < rfid.uid.size && i < 5; i++) {
    char hex[4];
    snprintf(hex, sizeof(hex), "%02X ", rfid.uid.uidByte[i]);
    strcat(uidStr, hex);
  }
  Serial.print(F("RFID UID: "));
  Serial.println(uidStr);

  bool ok = false;
  if (rfid.uid.size == 4) {               // 일반 MIFARE 카드는 4바이트 UID
    for (uint8_t n = 0; n < UID_COUNT && !ok; n++) {
      ok = (memcmp(rfid.uid.uidByte, ALLOWED_UID[n], 4) == 0);
    }
  }

  if (ok) {
    grant("RFID");
  } else {
    deny("RFID");
  }
  if (!ok && lockoutUntil == 0) {         // 차단 메시지가 떠 있을 땐 덮어쓰지 않음
    lcd.setCursor(0, 1);                  // 모르는 카드면 UID 를 보여줘서 등록하기 쉽게
    lcd.print(F("                "));
    lcd.setCursor(0, 1);
    lcd.print(uidStr);
  }

  rfid.PICC_HaltA();                      // 같은 카드 연속 인식 방지
  rfid.PCD_StopCrypto1();
}

// ================================================================
//  보드 B 에서 오는 상태 메시지 ("ST:OPEN:BT" 등)
// ================================================================
void handleStatus(const char *buf) {
  if (strncmp(buf, "ST:", 3) != 0) {
#if TEST_MODE
    Serial.print(F("[TEST] ignored (must start with ST:): "));
    Serial.println(buf);
#endif
    return;
  }
  const char *st = buf + 3;

  if (strncmp(st, "OPEN", 4) == 0) {
    doorState = "OPEN";
    const char *by = (st[4] == ':') ? st + 5 : "";
    if      (strcmp(by, "BT")  == 0) showMsg(F("OPEN by PHONE"), "Bluetooth+1m");
    else if (strcmp(by, "BTN") == 0) showMsg(F("OPEN from"), "INSIDE button");
    else if (msgUntil == 0)          showIdle();  // KEY/RFID 는 이미 GRANTED 표시 중
  } else if (strcmp(st, "LOCK") == 0) {
    doorState = "LOCKED";
    if (msgUntil == 0) showIdle();
  } else if (strcmp(st, "ARM") == 0) {
    showMsg(F("PHONE VERIFIED"), "Come within 1m");
  } else if (strcmp(st, "ARMEND") == 0) {
    showMsg(F("PHONE TIMEOUT"), "Send PW again");
  } else if (strcmp(st, "BTFAIL") == 0) {
    showMsg(F("PHONE PW WRONG"), "");
  }
#if TEST_MODE
  else {
    Serial.print(F("[TEST] unknown status: "));
    Serial.println(buf);
  }
#endif
}

// 스트림에서 한 줄('\n' 또는 '\r' 로 끝)이 완성되면 handleStatus 로 넘김
// 링크용 / 시리얼 모니터용 버퍼를 따로 둬서 서로 섞이지 않게 함
// 완성된 한 줄을 처리. echo=true 면 받은 줄을 시리얼 모니터에 다시 보여줌 (테스트용)
void finishLine(LineBuf &lb, bool echo) {
  lb.buf[lb.len] = '\0';
  lb.len = 0;
  if (echo) {
    Serial.print(F("[B->A] "));
    Serial.println(lb.buf);
  }
  handleStatus(lb.buf);
}

void pollLines(Stream &s, LineBuf &lb, bool echo) {
  while (s.available()) {
    char c = s.read();
    lb.lastRx = millis();
    if (c == '\r' || c == '\n') {
      if (lb.len == 0) continue;          // 빈 줄 무시 (CR+LF 대응)
      finishLine(lb, echo);
      continue;
    }
    if (lb.len < sizeof(lb.buf) - 1) lb.buf[lb.len++] = c;
  }
}

LineBuf linkLine = {"", 0, 0};
#if TEST_MODE
LineBuf monLine  = {"", 0, 0};            // 시리얼 모니터 입력 (보드 B 흉내)
#endif

void handleLink() {
  pollLines(link, linkLine, false);
#if TEST_MODE
  pollLines(Serial, monLine, true);
  // 시리얼 모니터가 "No line ending" 이어도 0.2초 동안 입력이 없으면 한 줄로 처리
  if (monLine.len > 0 && millis() - monLine.lastRx > 200) finishLine(monLine, true);
#endif
}

// ================================================================
void setup() {
  Serial.begin(9600);      // USB 디버그용 (RFID UID 확인)
  link.begin(9600);        // 보드 B 와 통신 (양쪽 속도 같아야 함)

  lcd.init();
  lcd.backlight();

  SPI.begin();
  rfid.PCD_Init();
  Serial.println(F("DoorLock A ready."));
#if TEST_MODE
  Serial.println(F("[TEST] Serial Monitor: 9600 baud, line ending = New Line"));
  Serial.println(F("[TEST] type ST:OPEN:BT / ST:LOCK / ST:ARM / ST:ARMEND / ST:BTFAIL / ST:OPEN:BTN"));
#endif

  showMsg(F("SMART DOORLOCK"), "Starting...");
}

void loop() {
  handleLink();

  // 안내 메시지 시간이 끝나면: 입력 차단 중이면 차단 화면 복구, 아니면 기본 화면
  // (차단 중에 보드 B 상태 메시지가 와서 차단 화면을 덮어썼을 때를 대비)
  if (msgUntil != 0 && (long)(millis() - msgUntil) >= 0) {
    if (isLockedOut()) {
      showMsg(F("TOO MANY FAILS"), "Wait 30 sec");
      msgUntil = lockoutUntil;            // 차단이 끝날 때까지 유지
    } else if (msgUntil != 0) {           // isLockedOut() 이 방금 기본 화면을 그렸으면 생략 (깜빡임 방지)
      showIdle();
    }
  }

  if (isLockedOut()) {
    keypad.getKey();       // 차단 중 눌린 키는 버림
    return;
  }

  char k = keypad.getKey();
  if (k) handleKey(k);

  handleRfid();
}
