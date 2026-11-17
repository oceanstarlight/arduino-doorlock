/*
 * [보드 A 테스트 3] RC522 RFID 단독 테스트 (시리얼 모니터 9600)
 * 연결 : SDA(SS)=D10, SCK=D13, MOSI=D11, MISO=D12, RST=3.3V 에 직결
 *        (분압 약 3.33V : SCK·MOSI 는 핀 → 2kΩ → [RC522] → 2kΩ+2kΩ → GND, SDA 는 10kΩ 3개로 같은 구조)
 *        VCC = 3.3V (5V 절대 금지), GND
 *
 * 1) 시작하면 SPI 통신을 3단계로 판정함
 *    (a) 칩 버전(VersionReg)을 10번 읽어서 값이 매번 같은지   → 다르면 접촉 불량
 *    (b) 레지스터에 0x55, 0xAA 를 써 보고 그대로 읽히는지     → 안 되면 MISO/MOSI 문제
 *    (c) 버전 값이 알려진 값인지 (0x91/0x92 정품, 0x88/0x12/0xB2 호환칩) → 모르는 값이면 경고만
 *    0x00 또는 0xFF → 통신 안 됨 (배선·분압·전원 확인)
 * 2) 카드/태그를 대면 UID 를 출력 → board_A_input.ino 의 ALLOWED_UID 에 적기
 *
 * 문제 해결 (0x00 / 0xFF 가 나올 때)
 *  - VCC 가 3.3V 핀에 꽂혀 있는지, GND 연결됐는지
 *  - MISO(D12) 는 분압 없이 직결인지 (분압을 넣으면 신호가 약해져서 안 읽힘)
 *  - 분압 저항 방향: 아두이노 핀 → 위 저항 1개 → [RC522 핀] → 아래 저항 2개 직렬 → GND (뒤집으면 약 1.7V 라서 안 읽힘)
 *  - SCK·MOSI 를 10kΩ 으로 분압하면 SPI 가 너무 느려져서 안 됨 (SDA 만 10kΩ 가능)
 *  - RST 가 3.3V 레일에 꽂혀 있는지 (GND 나 빈 칸이면 칩이 리셋 상태로 멈춤)
 *  - SDA 를 I2C 의 SDA(A4) 에 꽂지 않았는지 (RC522 의 SDA 는 SS 신호 = D10)
 */
#include <SPI.h>
#include <MFRC522.h>

MFRC522 rfid(10, MFRC522::UNUSED_PIN);   // SS, RST(3.3V 직결이라 사용 안 함)

void setup() {
  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();
  delay(50);

  // (a) 버전 10번 읽기 : 값이 흔들리면 접촉 불량
  byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  bool stable = true;
  for (byte i = 0; i < 10; i++) {
    if (rfid.PCD_ReadRegister(MFRC522::VersionReg) != v) stable = false;
    delay(2);
  }
  Serial.print(F("RC522 version: 0x"));
  Serial.println(v, HEX);

  // (b) 쓰고 다시 읽기 : MISO 가 빠져 있으면 두 패턴을 다 맞출 수 없음
  rfid.PCD_WriteRegister(MFRC522::TReloadRegL, 0x55);
  bool rw = (rfid.PCD_ReadRegister(MFRC522::TReloadRegL) == 0x55);
  rfid.PCD_WriteRegister(MFRC522::TReloadRegL, 0xAA);
  rw = rw && (rfid.PCD_ReadRegister(MFRC522::TReloadRegL) == 0xAA);
  rfid.PCD_Init();                           // 바꾼 레지스터 원래대로 되돌리기

  bool known = (v == 0x91 || v == 0x92 || v == 0x88 || v == 0x12 || v == 0xB2);

  if (v == 0x00 || v == 0xFF) {
    Serial.println(F("FAIL: no response. Check 3.3V, GND, RST->3.3V, D10~D13, dividers."));
  } else if (!stable) {
    Serial.println(F("FAIL: unstable reads. Reseat divider resistors and MISO(D12) wire."));
  } else if (!rw) {
    Serial.println(F("FAIL: write/read-back failed. Check MOSI(D11), MISO(D12), SCK(D13)."));
  } else if (!known) {
    Serial.println(F("WARN: SPI OK but unknown chip version. Try a card anyway."));
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
