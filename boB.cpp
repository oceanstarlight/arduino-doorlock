// servo 필수 라이브러리
#include <Servo.h>
// mstimer 내부 인터럽트 라이브러리
#include <MsTimer2.h>

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

// LED 핀 번호 초록 D10번, 빨강 D12번임. 
// 한칸 띈 이유는 두개 따닥 붙이면 불편해서임.
// 굳이 D 핀 쓴 이유는 켜기+쓰기만 하니까 그럼.
const int RED_LED = 12;
const int GREEN_LED = 10;

void pushButton()
{
  // 지금 시간 - 직전 시간이 300 이상이면 직전 시간을 지금 시간으로 갱신
  // 마지막 버튼 누름 이후 300ms 동안 아무것도 없다면
  // 채터링 방지 == 튐 방지
  if(millis() - msSave >= 300) 
  {
    pushFlag = true;
    msSave = millis();
  }
}

// servo 동작 함수, angle은 목표 각도
void moveServo(int angle)
{
  servo.write(angle);
  // 서보 떨림 방지용 신호 켜는 함수
  servo.attach(ServoPin);
  delay(500);
  //신호 끄기
  servo.detach();
}


void Dooropen()
{
  // LED 단
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, HIGH);

  // Servo 구동 및 3초 대기
  moveServo(90);
  delay(3000);
}

void Doorlock()
{
  digitalWrite(RED_LED, HIGH);
  digitalWrite(GREEN_LED, LOW);

  moveServo(0);
}

void setup() {
  Serial.begin(9600);
  pinMode(InterButton, INPUT_PULLUP); // 초기 동작 안함

  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(InterButton), pushButton, FALLING);
  servo.attach(ServoPin); 
  servo.detach();

  Doorlock();
}

void loop() {
  if(pushFlag == true){
    pushFlag = false;
    Dooropen();
    // serial moniter에 출력 // 잘 작동하나 확인.
    Serial.println("nooleum");
    Doorlock();
  }
}
