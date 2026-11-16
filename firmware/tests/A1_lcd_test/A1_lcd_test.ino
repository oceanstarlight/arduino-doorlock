/*
 * [보드 A 테스트 1] LCD 단독 테스트
 * 연결 : LCD GND-GND, VCC-5V, SDA-A4, SCL-A5  (다른 모듈은 빼도 되고 꽂혀 있어도 됨)
 * 먼저 tools/i2c_scanner 로 주소를 확인하고 아래 LCD_ADDR 에 적기.
 *
 * 기대 결과
 *  - 1줄: "LCD TEST OK"   2줄: 0 부터 1초마다 올라가는 숫자
 *  - 백라이트가 5초마다 1번 깜빡임
 * 문제 해결
 *  - 백라이트만 켜지고 글자 없음 → 뒷면 파란 가변저항 돌리기 (명암)
 *  - 네모 칸만 보임           → 주소(LCD_ADDR) 확인, 또는 명암 조절
 *  - 백라이트도 안 켜짐       → VCC/GND, 뒷면 백라이트 점퍼 확인
 */
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

const uint8_t LCD_ADDR = 0x27;   // 스캐너 결과로 바꾸기 (0x3F 등)
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

void setup() {
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print(F("LCD TEST OK"));
  Serial.println(F("LCD test started"));
}

void loop() {
  static unsigned long last = 0;
  static unsigned int count = 0;
  if (millis() - last >= 1000) {
    last = millis();
    lcd.setCursor(0, 1);
    lcd.print(F("count: "));
    lcd.print(count);
    lcd.print(F("    "));          // 이전 숫자 잔상 지우기
    if (count % 5 == 4) {          // 5초마다 백라이트 깜빡
      lcd.noBacklight();
      delay(200);
      lcd.backlight();
    }
    count++;
  }
}
