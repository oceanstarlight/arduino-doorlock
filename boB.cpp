#include <Servo.h>

Servo servo;

// InterButton  버튼 누르면 받는 핀 번호
// ServoPin   서보모터 동작 핀
// pushFlag, LockFlag   동작 상황?
// volatile ISR/loop  같이 써서 붙음
// msSave   직전 시간 계산 (채터링)
// digitalPinToInterrupt  핀번호 -> 인터럽트 번호로 변환 // 우노 D2 = int0

volatile bool pushFlag = false, lockFlag = false;
const int InterButton = 2;
const int ServoPin = 7;
volatile unsigned long msSave = 0;

void pushButton(){
  // 지금 시간 - 직전 시간이 300 이상이면 직전 시간을 지금 시간으로 갱신
  // 마지막 버튼 누름 이후 300ms 동안 아무것도 없다면
  // 채터링 방지 == 튐 방지
  if(millis() - msSave >= 300) 
  {
    pushFlag = true;
    msSave = millis();
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(InterButton, INPUT_PULLUP); // 초기 동작 안함
  attachInterrupt(digitalPinToInterrupt(InterButton), pushButton, FALLING);
  servo.write(0); // 초기 각도 0
  servo.attach(ServoPin); 
}

void loop() {
  if(pushFlag == true){
    pushFlag = false;
    servo.write(90);
    delay(3000);
    servo.write(0);
    
    // serial moniter에 출력 // 잘 작동하나 확인.
    Serial.println("nooleum");
  }
}
