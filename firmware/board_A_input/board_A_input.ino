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
// RFID   : SDA(SS)=D10, SCK=D13, MOSI=D11, MISO=D12, RST=D9, 3.3V, GND
// LCD    : SDA=A4, SCL=A5, VCC=5V, GND
// 링크   : A1(RX) <- 보드B D5 / A2(TX) -> 보드B D4
const uint8_t PIN_RFID_SS  = 10;
const uint8_t PIN_RFID_RST = 9;
const uint8_t PIN_LINK_RX  = A1;
const uint8_t PIN_LINK_TX  = A2;
const uint8_t LCD_ADDR     = 0x27;   // 안 되면 0x3F 로 바꿔보기

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
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
SoftwareSerial link(PIN_LINK_RX, PIN_LINK_TX);

// ---------------- 상태 변수 ----------------
char    input[MAX_INPUT + 1];
uint8_t inputLen = 0;
uint8_t failCount = 0;
unsigned long lockoutUntil = 0;   // 0 이 아니면 그 시각까지 입력 차단
unsigned long msgUntil = 0;       // 0 이 아니면 그 시각에 기본 화면으로 복귀
const char *doorState = "LOCKED";

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
void grant(const char *method) {          // method : "KEY" / "RFID"
  failCount = 0;
  link.print(F("OPEN:"));
  link.println(method);                   // 보드 B 에 열림 명령
  showMsg(F("ACCESS GRANTED"), method);
}

void deny(const char *method) {
  failCount++;
  link.print(F("FAIL:"));
  link.println(method);                   // 보드 B 는 빨강 LED 깜빡임
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
    showIdle();
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
void handleLink() {
  static char buf[20];
  static uint8_t len = 0;
  while (link.available()) {
    char c = link.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (len < sizeof(buf) - 1) buf[len++] = c;
      continue;
    }
    buf[len] = '\0';
    len = 0;
    if (strncmp(buf, "ST:", 3) != 0) continue;
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
  }
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

  showMsg(F("SMART DOORLOCK"), "Starting...");
}

void loop() {
  handleLink();

  // 안내 메시지 시간이 끝나면 기본 화면으로
  if (msgUntil != 0 && (long)(millis() - msgUntil) >= 0 && !isLockedOut()) {
    showIdle();
  }

  if (isLockedOut()) {
    keypad.getKey();       // 차단 중 눌린 키는 버림
    return;
  }

  char k = keypad.getKey();
  if (k) handleKey(k);

  handleRfid();
}
