/*
 * [보드 A 테스트 1] LCD 단독 테스트
 * 연결 : LCD GND-GND, VCC-5V, SDA-A4, SCL-A5  (다른 모듈은 빼도 되고 꽂혀 있어도 됨)
 * 먼저 tools/i2c_scanner 로 주소를 확인하고 아래 LCD_ADDR 에 적기.
 *
 * 기대 결과
 *  - 1줄: "LCD TEST OK"   2줄: 0 부터 1초마다 올라가는 숫자
 *  - 백라이트가 5초마다 1번 깜빡임
 * 문제 해결  (먼저 확인: 백라이트가 5초마다 깜빡이나?)
 *   ※ 전원만 들어가도 백라이트는 켜지기 때문에, "켜져 있다" 만으로는 통신 확인이 안 됨
 *  - 깜빡임 O + 글자 없음/네모 칸 → I2C 통신은 정상. 뒷면 파란 가변저항(명암)만 돌리기
 *  - 깜빡임 X (계속 켜져만 있음)   → I2C 안 됨: LCD_ADDR(스캐너 결과), SDA=A4 / SCL=A5 배선 확인
 *  - 백라이트도 안 켜짐            → VCC/GND, 뒷면 백라이트 점퍼(LED 핀 두 개) 확인
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
