// ------------------------------
void resetPassword() {

    inputLength = 0;

    inputPassword[0] = '\0';

}

// ------------------------------
// LCD 대기 화면
// ------------------------------
void showHome() {

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("DOOR LOCK SYSTEM");

    lcd.setCursor(0, 1);
    lcd.print("Enter PW / Scan");

}

// ------------------------------
// 인증 성공
// ------------------------------
void accessGranted(const char* method) {

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("ACCESS GRANTED");

    lcd.setCursor(0, 1);
    lcd.print(method);

    doorOpen = true;
    openTime = millis();

    resetPassword();

}

// ------------------------------
// 인증 실패
// ------------------------------
void accessDenied(const char* method) {

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("ACCESS DENIED");

    lcd.setCursor(0, 1);
    lcd.print(method);

    resetPassword();

    delay(1500);

    showHome();

}

// ------------------------------
// RFID 카드 확인
// ------------------------------
void checkRFID() {

    if (!rfid.PICC_IsNewCardPresent()) {
        return;
    }

    if (!rfid.PICC_ReadCardSerial()) {
        return;
    }

    Serial.print("Card UID: ");

    for (byte i = 0; i < rfid.uid.size; i++) {

        Serial.print(rfid.uid.uidByte[i], HEX);
        Serial.print(" ");

    }

    Serial.println();

    if (checkUID()) {

        accessGranted("RFID SUCCESS");

    }
    else {

        accessDenied("RFID FAILED");

    }

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();

}

// ------------------------------
// 키패드 확인
// ------------------------------
void checkKeypad() {

    char key = keypad.getKey();

    if (key == NO_KEY) {
        return;
    }

    // * : 비밀번호 초기화
    if (key == '*') {

        resetPassword();

        lcd.setCursor(0, 1);
        lcd.print("                ");

        lcd.setCursor(0, 1);
        lcd.print("PW: ");

        return;
    }

    // # : 비밀번호 확인
    if (key == '#') {

        inputPassword[inputLength] = '\0';

        if (strcmp(inputPassword, password) == 0) {

            accessGranted("KEY SUCCESS");

        }
        else {

            accessDenied("KEY FAILED");

        }

        return;
    }

    // 숫자만 입력
    if (key >= '0' && key <= '9') {

        if (inputLength < 4) {

            inputPassword[inputLength] = key;
            inputLength++;

            inputPassword[inputLength] = '\0';

            lcd.setCursor(0, 1);
            lcd.print("PW: ");

            for (byte i = 0; i < inputLength; i++) {
                lcd.print("*");
            }

        }
    }

}

// ------------------------------
// 자동 잠금
// ------------------------------
void autoLock() {

    if (doorOpen && millis() - openTime >= openDuration) {

        doorOpen = false;

        lcd.clear();

        lcd.setCursor(0, 0);
        lcd.print("DOOR LOCKED");

        delay(1500);

        showHome();

    }

}

// ------------------------------
// 초기 설정
// ------------------------------
void setup() {

    Serial.begin(9600);

    SPI.begin();
    rfid.PCD_Init();

    lcd.init();
    lcd.backlight();

    resetPassword();

    lcd.setCursor(0, 0);
    lcd.print("DOOR LOCK SYSTEM");

    lcd.setCursor(0, 1);
    lcd.print("Initializing...");

    delay(2000);

    showHome();

}

// ------------------------------
// 메인 반복문
// ------------------------------
void loop() {

    checkRFID();

    if (!doorOpen) {
        checkKeypad();
    }

    autoLock();

}