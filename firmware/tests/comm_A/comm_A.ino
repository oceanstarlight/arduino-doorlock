/*
 * comm_A : 보드 A 통신 단독 테스트 (하드웨어 Serial, D0/D1, 9600bps)
 *
 * 배선
 *   A D1(TX) -> B D0(RX)
 *   A D0(RX) <- B D1(TX)
 *   GND <-> GND (공통)
 *   테스트 버튼 : D2 <-> GND (인증 성공 -> "OPEN:KEY")
 *                 D3 <-> GND (인증 실패 -> "FAIL:KEY")
 *   D13 내장 LED : 보드 B 가 알려준 문 상태 (켜짐 = 열림)
 *
 * 주의 : 업로드할 땐 D0/D1 선을 뽑을 것
 * 단독 테스트 : 선 뽑고 시리얼 모니터(9600, Newline)에서 보드 B 역할
 *   - 버튼 누르면 모니터에 OPEN:KEY / FAIL:KEY 출력
 *   - 모니터에 ST:OPEN:KEY 입력 -> D13 켜짐, ST:LOCK 입력 -> D13 꺼짐
 */

const int PIN_BTN_OK   = 2;
const int PIN_BTN_FAIL = 3;
const int PIN_LED_DOOR = LED_BUILTIN;   // D13

// ---------------- 송신 : A -> B ----------------
void sendOpen(const char *method) {     // method : "KEY" / "RFID"
  Serial.print(F("OPEN:"));
  Serial.println(method);               // println 이 끝에 "\r\n" 을 붙임
}

void sendFail(const char *method) {
  Serial.print(F("FAIL:"));
  Serial.println(method);
}

// ---------------- 수신 : B -> A ----------------
// 글자를 모으다가 '\r' 또는 '\n' 이 오면 한 줄 완성 -> true
bool readLine(char *buf, uint8_t size) {
  static uint8_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (len == 0) continue;           // "\r\n" 의 두 번째 글자 등 빈 줄 무시
      buf[len] = '\0';
      len = 0;
      return true;
    }
    if (len < size - 1) buf[len++] = c; // 버퍼 넘치면 나머지 버림
  }
  return false;
}

// "ST:OPEN:KEY" / "ST:OPEN:RFID" / "ST:OPEN:BTN" / "ST:LOCK"
void handleStatus(const char *line) {
  if      (strncmp(line, "ST:OPEN", 7) == 0) digitalWrite(PIN_LED_DOOR, HIGH);
  else if (strcmp(line, "ST:LOCK") == 0)     digitalWrite(PIN_LED_DOOR, LOW);
  // 모르는 줄은 무시
}

// ---------------- 테스트 버튼 ----------------
// 눌리는 순간 한 번만 true (50ms 이내 변화는 채터링으로 보고 무시)
bool justPressed(int pin, bool &last, unsigned long &tLast) {
  bool now = (digitalRead(pin) == LOW);
  if (now != last && millis() - tLast >= 50) {
    last  = now;
    tLast = millis();
    return now;
  }
  return false;
}

void setup() {
  Serial.begin(9600);                   // 보드 B 와 속도 같아야 함
  pinMode(PIN_BTN_OK, INPUT_PULLUP);
  pinMode(PIN_BTN_FAIL, INPUT_PULLUP);
  pinMode(PIN_LED_DOOR, OUTPUT);
  // 디버그용 Serial.print 넣지 말 것 : 전부 보드 B 로 감
}

void loop() {
  static char buf[20];
  if (readLine(buf, sizeof(buf))) handleStatus(buf);

  static bool okLast = false, failLast = false;
  static unsigned long okT = 0, failT = 0;
  if (justPressed(PIN_BTN_OK,   okLast,   okT))   sendOpen("KEY");
  if (justPressed(PIN_BTN_FAIL, failLast, failT)) sendFail("KEY");
}
