/*
 * I2C 주소 스캐너 (보드 A 에 LCD 만 연결하고 실행)
 * 시리얼 모니터(9600)에 LCD 주소가 나옴. 보통 0x27 또는 0x3F.
 * 나온 주소를 board_A_input.ino 의 LCD_ADDR 에 적으면 됨.
 */
#include <Wire.h>

void setup() {
  Serial.begin(9600);
  Wire.begin();
  Serial.println(F("I2C scan..."));
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {     // 0 = 응답(ACK) 있음
      Serial.print(F("Found: 0x"));
      if (addr < 16) Serial.print('0');
      Serial.println(addr, HEX);
      found++;
    }
  }
  if (found == 0) Serial.println(F("Nothing found. Check SDA=A4, SCL=A5, VCC, GND."));
}

void loop() {}
