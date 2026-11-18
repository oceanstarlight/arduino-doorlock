/*
 * [보드 A 테스트 5] PIR 모션 센서 단독 테스트 (시리얼 모니터 9600)
 * 연결 : PIR OUT = D9, VCC = 5V(아래쪽 + 레일), GND = − 레일
 *        핀 이름(VCC·OUT·GND)은 돔(흰 렌즈)을 빼면 기판에 적혀 있으니 확인하고 꽂기
 *
 * 기대 결과 : 손을 흔들면 "MOTION", 가만히 있으면 몇 초 뒤 "-" 출력
 *
 * 문제 해결
 *  - 전원 넣고 처음 30~60초는 센서가 안정화되는 시간이라 마구 반응해도 정상
 *  - 계속 MOTION 만 나옴 → 센서 앞에서 비켜서 1분 기다리기, 뒷면 노란 가변저항(감도) 줄이기
 *  - 반응이 늦게 꺼짐 → 뒷면 다른 가변저항(유지 시간)을 반시계로 끝까지 (약 3초)
 *  - 아무 반응 없음 → VCC 가 5V 인지 (3.3V 로는 동작 안 함), OUT 이 D9 인지
 */

const uint8_t PIN_PIR = 9;

void setup() {
  Serial.begin(9600);
  pinMode(PIN_PIR, INPUT);
  Serial.println(F("PIR test. Wave your hand."));
}

void loop() {
  static int last = -1;
  int now = digitalRead(PIN_PIR);
  if (now != last) {               // 바뀔 때만 출력
    Serial.println(now == HIGH ? F("MOTION") : F("-"));
    last = now;
  }
}
