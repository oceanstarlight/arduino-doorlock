// 보드 A (최소화) : PIR → 키패드 활성, 키패드·RFID 판정, LCD 표시, 보드 B 로 'O'(열기) / 'F'(실패) 전송
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <SoftwareSerial.h>

char keys[4][4] = {{'1','2','3','A'}, {'4','5','6','B'}, {'7','8','9','C'}, {'*','0','#','D'}};
byte rowPins[4] = {4, 3, 2, A0};   // R1~R4
byte colPins[4] = {5, 6, 7, 8};    // C1~C4
Keypad keypad(makeKeymap(keys), rowPins, colPins, 4, 4);

MFRC522 rfid(10, MFRC522::UNUSED_PIN);   // SS=D10, RST 는 3.3V 직결
LiquidCrystal_I2C lcd(0x27, 16, 2);
SoftwareSerial link(A1, A2);             // RX(안 씀), TX(A2) → 보드 B D4

byte myCard[4] = {0xDE, 0xAD, 0xBE, 0xEF};   // A3_rfid_test 로 확인한 내 카드 UID
String pw = "";
bool active = false;             // 키패드 활성 상태
unsigned long lastMotion = 0;    // 마지막 움직임·키 입력 시각

void prompt() {                  // 1줄 안내, 커서는 2줄 처음 (여기에 * 표시)
  lcd.clear();
  lcd.print(active ? "Enter PW" : "Wave to start");
  lcd.setCursor(0, 1);
}

void result(bool ok) {           // 보드 B 로 결과 전송 + LCD 표시
  link.write(ok ? 'O' : 'F');
  lcd.clear();
  lcd.print(ok ? "ACCESS GRANTED" : "ACCESS DENIED");
  delay(2000);
  pw = "";
  prompt();
}

void setup() {
  link.begin(9600);
  lcd.init();
  lcd.backlight();
  pinMode(9, INPUT);             // PIR OUT
  SPI.begin();
  rfid.PCD_Init();
  prompt();
}

void loop() {
  // PIR : 움직임이 있으면 활성, 10초 동안 움직임·입력이 없으면 비활성
  if (digitalRead(9) == HIGH) {
    lastMotion = millis();
    if (!active) { active = true; prompt(); }
  }
  if (active && millis() - lastMotion >= 10000) {
    active = false;
    pw = "";
    prompt();
  }

  // 키패드 : 활성일 때만 읽음
  if (active) {
    char k = keypad.getKey();
    if (k) {
      lastMotion = millis();
      if (k == '#')      result(pw == "1234");
      else if (k == '*') { pw = ""; prompt(); }
      else               { pw += k; lcd.print('*'); }
    }
  }

  // RFID : 항상 동작
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    result(memcmp(rfid.uid.uidByte, myCard, 4) == 0);
    rfid.PICC_HaltA();
  }
}
