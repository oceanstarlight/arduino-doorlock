/*
 * [보드 A 테스트 2] 키패드 단독 테스트 (시리얼 모니터 9600)
 * 연결 : R1~R4 = D8, D7, D6, D5 / C1~C4 = D4, D3, D2, A0  (board_A_input 과 같은 핀)
 *
 * 키를 누르면 시리얼 모니터에 "key: 5 (row 2, col 2)" 처럼 출력됨.
 * 16개 키를 순서대로 눌러서 표와 맞는지 확인해.
 *
 * 문제 해결
 *  - 아무 키도 안 나옴           → 키패드 커넥터 방향(앞/뒤) 뒤집혀 꽂혔는지 확인
 *  - 줄(행)이나 칸(열)이 통째로 안 됨 → 그 행/열 핀 선 확인
 *  - 다른 숫자가 나옴             → 핀 순서가 다름. 아래 rowPins / colPins 순서 바꾸기
 *    (예: 1 누르면 D 가 나오면 행·열 순서가 둘 다 거꾸로 → 배열을 뒤집기)
 *  - 행과 열이 바뀐 것 같음        → rowPins 와 colPins 를 서로 맞바꾸기
 *  여기서 맞춘 순서를 board_A_input.ino 에도 똑같이 적기.
 */
#include <Keypad.h>

const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'},
};
byte rowPins[ROWS] = {8, 7, 6, 5};
byte colPins[COLS] = {4, 3, 2, A0};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

void setup() {
  Serial.begin(9600);
  Serial.println(F("Keypad test. Press keys 1,2,3,A,4,5,...,D in order."));
}

void loop() {
  char k = keypad.getKey();
  if (!k) return;
  // 눌린 키가 표의 몇 행 몇 열인지 찾아서 같이 출력 (배선 확인용)
  for (byte r = 0; r < ROWS; r++)
    for (byte c = 0; c < COLS; c++)
      if (keys[r][c] == k) {
        Serial.print(F("key: "));
        Serial.print(k);
        Serial.print(F("  (row "));
        Serial.print(r + 1);
        Serial.print(F(", col "));
        Serial.print(c + 1);
        Serial.println(F(")"));
      }
}
