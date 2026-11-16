# Arduino 스마트 도어락 (기말 과제)

Arduino Uno(ATmega328P) 2대를 UART로 연결한 도어락.

| 보드 | 역할 | 모듈 |
|---|---|---|
| A (입력부) | 비밀번호 / RFID 판정, LCD 표시 | 4x4 키패드, RC522, 1602 LCD(I2C) |
| B (문 작동부) | 문 열고 닫기, 폰 인증 + 1m 판정 | SG90 서보, HC-06, HC-SR04, LED 3개, 실내 버튼 |

- 외부 인터럽트: 보드 B `D2`(INT0), 실내 버튼, FALLING, millis()로 채터링 방지
- 내부 인터럽트: 보드 B `MsTimer2` 100ms 틱, 자동 잠금 / 폰 인증 만료 / LED 깜빡임

## 열리는 방법
1. 키패드 `1234#`
2. 등록된 RFID 카드 (`ALLOWED_UID`에 UID 등록)
3. 폰(Serial Bluetooth Terminal)으로 `BT1234` 전송 → 30초 안에 초음파 1m 이내 접근
4. 실내 버튼

열리고 5초 뒤 자동으로 잠김.

## 파일
- `docs/wiring.html` : 배선도, 안전 점검표, 조립 순서 (브라우저로 열기)
- `firmware/board_A_input/` : 보드 A 스케치
- `firmware/board_B_door/` : 보드 B 스케치
- `firmware/tools/i2c_scanner/` : LCD I2C 주소 확인용

## 라이브러리
Keypad, MFRC522, LiquidCrystal I2C, MsTimer2 (+ 내장 Servo, SoftwareSerial)

## 주의
- RC522는 **3.3V**. 신호선 4개는 1k+2k 분압.
- 서보는 **외부 5V**. GND만 공통.
- 보드 B 업로드 전 **D0의 HC-06 TXD 선을 뽑기**.
