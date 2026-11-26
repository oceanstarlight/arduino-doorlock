#include <Servo.h>

Servo servo;

// InterButton 버튼 누르면 받는 핀 번호
// ServoPin 서보모터 동작 핀
// pushFlag, LockFlag 동작 상황?
// volatile ISR/loop 같이 써서 붙음

bool pushFlag = false, lockFlag = false;
const int InterButton = 2;
const int ServoPin = 7;

void pushButton(){
  pushFlag = true;
}

void setup() {
  pinMode(InterButton, INPUT_PULLUP); // 초기 동작 안함
  attachInterrupt(digitalPinToInterrupt(2), pushButton, FALLING);
  servo.write(0); // 초기 각도 0
  servo.attach(ServoPin); 
}

void loop() {
  if(pushFlag){
    servo.write(90);
    delay(3000);
  } else {
    servo.write(0);
  }
}
