#include <Servo.h>

Servo servo;

const int InterButton = 2;
const int ServoPin = 7;

void setup() {
  pinMode(InterButton, LOW); // 초기 동작 안함
  servo.attach(ServoPin); 
  servo.write(0); // 초기 각도 0
}

void loop() {
  int buttonState = digitalRead(InterButton);

  if(buttonState == HIGH){
    servo.write(90);
  }
  else {
    servo.write(0);
  }

  delay(30);
}