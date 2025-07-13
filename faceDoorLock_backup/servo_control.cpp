#include <wiringPi.h>
#include <iostream>
using namespace std;

#define SERVO_PIN 1 // GPIO18 (WiringPi 1)

// 아두이노 스타일의 map 함수 정의
long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void setAngle(int angle) {
    int pwmValue = map(angle, 0, 180, 5, 25); // 예: 0도 -> 5, 180도 -> 25
    pwmWrite(SERVO_PIN, pwmValue);
    delay(500); // 서보가 움직일 시간
}

int main() {
    wiringPiSetup();
    pinMode(SERVO_PIN, PWM_OUTPUT);

    pwmSetMode(PWM_MODE_MS);
    pwmSetRange(2000);      // PWM 주기
    pwmSetClock(192);       // PWM 클럭

    setAngle(0);
    delay(1000);
    setAngle(90);
    delay(1000);
    setAngle(180);
    delay(1000);

    return 0;
}

