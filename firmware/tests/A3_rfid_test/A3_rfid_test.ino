/*
 * [보드 A 테스트 3] RC522 RFID 단독 테스트 (시리얼 모니터 9600)
 * 연결 : SDA(SS)=D10, SCK=D13, MOSI=D11, MISO=D12, RST=D9  (SS·SCK·MOSI·RST 는 1k+2k 분압)
 *        VCC = 3.3V (5V 절대 금지), GND
 *
 * 1) 시작하면 RC522 칩 버전(VersionReg)을 읽어서 배선 상태를 판정함
 *    0x91 / 0x92 / 0x88 / 0x12 / 0xB2 등 → SPI 통신 OK (정품/호환칩)
 *    0x00 또는 0xFF                      → 통신 안 됨 (배선·분압·전원 확인)
 * 2) 카드/태그를 대면 UID 를 출력 → board_A_input.ino 의 ALLOWED_UID 에 적기
 *
 * 문제 해결 (0x00 / 0xFF 가 나올 때)
 *  - VCC 가 3.3V 핀에 꽂혀 있는지, GND 연결됐는지
 *  - MISO(D12) 는 분압 없이 직결인지 (분압을 넣으면 신호가 약해져서 안 읽힘)
 *  - 분압 저항 방향: 아두이노 핀 → 1k → [RC522 핀] → 2k → GND
 *  - SDA 를 I2C 의 SDA(A4) 에 꽂지 않았는지 (RC522 의 SDA 는 SS 신호 = D10)
 */
#include <SPI.h>
#include <MFRC522.h>

MFRC522 rfid(10, 9);   // SS, RST

void setup() {
  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();
  delay(50);

  byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.print(F("RC522 version: 0x"));
  Serial.println(v, HEX);
  if (v == 0x00 || v == 0xFF) {
    Serial.println(F("FAIL: no response. Check wiring (3.3V, GND, D10~D13, D9, dividers)."));
  } else {
    Serial.println(F("OK: RC522 responds. Tap a card."));
  }
}

void loop() {
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial())   return;

  // 코드에 바로 붙여넣을 수 있는 형태로 출력: {0xDE, 0xAD, 0xBE, 0xEF},
  Serial.print(F("UID ("));
  Serial.print(rfid.uid.size);
  Serial.print(F(" bytes): {"));
  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.print(F("0x"));
    if (rfid.uid.uidByte[i] < 0x10) Serial.print('0');
    Serial.print(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) Serial.print(F(", "));
  }
  Serial.println(F("},"));
  if (rfid.uid.size != 4) {
    Serial.println(F("  (4-byte UID only is supported by board_A_input)"));
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  delay(500);
}
