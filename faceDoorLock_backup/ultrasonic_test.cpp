#include <wiringPi.h>
#include <iostream>
#include <unistd.h> // for usleep

#define TRIG 2  // wiringPi 2 (BCM 27)
#define ECHO 4  // wiringPi 4 (BCM 23)

double getDistance() {
    // TRIG LOW → HIGH → LOW
    digitalWrite(TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG, LOW);

    // ECHO가 HIGH 될 때까지 대기
    while (digitalRead(ECHO) == LOW);
    long startTime = micros();

    // ECHO가 LOW 될 때까지 대기
    while (digitalRead(ECHO) == HIGH);
    long endTime = micros();

    // 거리 계산 (속도 = 340m/s)
    double distance = (endTime - startTime) * 0.034 / 2.0;
    return distance;
}

int main() {
    if (wiringPiSetup() == -1) {
        std::cerr << "WiringPi 초기화 실패!" << std::endl;
        return 1;
    }

    pinMode(TRIG, OUTPUT);
    pinMode(ECHO, INPUT);

    digitalWrite(TRIG, LOW);
    delay(30); // 안정화 시간

    std::cout << "초음파 거리 측정 시작 (Ctrl+C로 종료)" << std::endl;

    while (true) {
        double dist = getDistance();
        std::cout << "거리: " << dist << " cm" << std::endl;
        delay(500); // 0.5초 대기
    }

    return 0;
}

