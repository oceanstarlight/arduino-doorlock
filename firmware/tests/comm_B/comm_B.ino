/*
 * comm_B : 보드 B 통신 단독 테스트 (하드웨어 Serial, D0/D1, 9600bps)
 *
 * 배선
 *   B D0(RX) <- A D1(TX)
 *   B D1(TX) -> A D0(RX)
 *   GND <-> GND (공통)
 *   실내 버튼 : D2 <-> GND
 *   초록 LED : D10 (열림) / 빨강 LED : D12 (잠김 = 켜짐, 인증 실패 = 빠른 깜빡임)
 *   서보 대신 초록 LED 로 열림 확인
 *
 * 주의 : 업로드할 땐 D0/D1 선을 뽑을 것
 * 단독 테스트 : 선 뽑고 시리얼 모니터(9600, Newline)에서 보드 A 역할
 *   - OPEN:KEY 입력 -> 초록 켜짐, 모니터에 ST:OPEN:KEY, 3초 뒤 ST:LOCK
 *   - FAIL:KEY 입력 -> 빨강 1초간 깜빡임
 *   - 버튼 누름     -> 모니터에 ST:OPEN:BTN
 */

const int PIN_BUTTON = 2;
const int PIN_LED_G  = 10;
const int PIN_LED_R  = 12;

const unsigned long OPEN_MS = 3000;     // 열림 유지 시간
const unsigned long FAIL_MS = 1000;     // 실패 깜빡임 시간

bool isOpen = false;
bool failing = false;
unsigned long openAt = 0, failAt = 0;

// ---------------- 송신 : B -> A ----------------
void openDoor(const char *by) {         // by : "KEY" / "RFID" / "BTN"
  Serial.print(F("ST:OPEN:"));
  Serial.println(by);
  isOpen = true;
  openAt = millis();                    // 열려 있을 때 또 열리면 시간 연장
}

void lockDoor() {
  Serial.println(F("ST:LOCK"));
  isOpen = false;
}

// ---------------- 수신 : A -> B ----------------
bool readLine(char *buf, uint8_t size) {
  static uint8_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (len == 0) continue;
      buf[len] = '\0';
      len = 0;
      return true;
    }
    if (len < size - 1) buf[len++] = c;
  }
  return false;
}

// "OPEN:KEY" / "OPEN:RFID" / "FAIL:KEY" / "FAIL:RFID"
void handleCommand(const char *line) {
  if (strcmp(line, "OPEN:KEY") == 0 || strcmp(line, "OPEN:RFID") == 0) {
    openDoor(line + 5);                 // "OPEN:" 뒤 글자 = 인증 방법
  } else if (strncmp(line, "FAIL:", 5) == 0) {
    failing = true;
    failAt  = millis();
  }
  // 모르는 줄은 무시
}

bool justPressed(int pin, bool &last, unsigned long &tLast) {
  bool now = (digitalRead(pin) == LOW);
  if (now != last && millis() - tLast >= 50) {
    last  = now;
    tLast = millis();
    return now;
  }
  return false;
}

void updateLeds() {
  digitalWrite(PIN_LED_G, isOpen);
  if (failing) digitalWrite(PIN_LED_R, (millis() / 100) % 2);   // 0.2초 주기 깜빡임
  else         digitalWrite(PIN_LED_R, !isOpen);
}

void setup() {
  Serial.begin(9600);                   // 보드 A 와 속도 같아야 함
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_R, OUTPUT);
  lockDoor();                           // 부팅 시 잠금 + "ST:LOCK" 알림
}

void loop() {
  static char buf[20];
  if (readLine(buf, sizeof(buf))) handleCommand(buf);

  static bool btnLast = false;
  static unsigned long btnT = 0;
  if (justPressed(PIN_BUTTON, btnLast, btnT)) openDoor("BTN");

  if (isOpen  && millis() - openAt >= OPEN_MS) lockDoor();      // 자동 잠금
  if (failing && millis() - failAt >= FAIL_MS) failing = false;

  updateLeds();
}
