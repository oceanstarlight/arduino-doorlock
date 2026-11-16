/*
 * [보드 A 테스트 4] 링크 핀(A1/A2) 루프백 테스트 (시리얼 모니터 9600)
 * 보드 B 없이 링크용 핀 A2(TX) → A1(RX) 이 제대로 연결되는지 확인.
 *
 * 연결 : 보드 A 의 A2 와 A1 사이를 1kΩ 저항으로 연결 (저항이 없으면 점퍼선도 가능)
 *        ※ 이 테스트 동안엔 보드 B 로 가는 링크선을 빼둘 것
 *        ※ 저항을 쓰는 이유: 실수로 보드 B 가 꽂혀 있으면 A2(LOW) 와 B 의 TX(HIGH) 가
 *          맞붙어 큰 전류가 흐를 수 있는데, 1kΩ 이 있으면 5mA 로 제한돼서 안전함
 *
 * 왜 핀 레벨로 확인하나?
 *   SoftwareSerial 은 송신하는 동안 인터럽트를 막기 때문에, 같은 보드에서 보낸 바이트를
 *   자기 RX 로 동시에 받을 수 없음. 그래서 A2 를 LOW/HIGH 로 바꿔 보고 A1 이 따라오는지만 봄.
 *   실제 메시지 형식은 board_A_input 의 TEST_MODE 로 확인.
 *
 * 기대 결과 : 1초마다 "PASS" 출력
 * "FAIL (low=0 ...)" → 저항(또는 점퍼)이 안 꽂혔거나 다른 핀에 꽂힘
 *   (A1 은 내부 풀업이 켜져 있어서 선이 빠지면 항상 HIGH 로 읽힘 → low 판정에서 실패)
 */

const uint8_t PIN_TX = A2;   // board_A_input 의 링크 TX
const uint8_t PIN_RX = A1;   // board_A_input 의 링크 RX

void setup() {
  Serial.begin(9600);
  pinMode(PIN_TX, OUTPUT);
  pinMode(PIN_RX, INPUT_PULLUP);   // 선이 빠지면 HIGH 로 떠서 FAIL 을 확실히 잡음
  digitalWrite(PIN_TX, HIGH);      // UART 대기 상태는 HIGH
  Serial.println(F("Link pin loopback: connect A2 -> A1 via 1k resistor (or jumper). Unplug board B link."));
}

void loop() {
  digitalWrite(PIN_TX, LOW);
  delayMicroseconds(50);
  bool lowOk = (digitalRead(PIN_RX) == LOW);

  digitalWrite(PIN_TX, HIGH);
  delayMicroseconds(50);
  bool highOk = (digitalRead(PIN_RX) == HIGH);

  if (lowOk && highOk) {
    Serial.println(F("PASS: A2 -> A1 OK"));
  } else {
    Serial.print(F("FAIL (low="));
    Serial.print(lowOk);
    Serial.print(F(", high="));
    Serial.print(highOk);
    Serial.println(F(") check 1k resistor/jumper A2 -> A1"));
  }
  delay(1000);
}
