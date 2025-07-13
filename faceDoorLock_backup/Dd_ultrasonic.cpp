#include "Dd_ultrasonic.hpp"


DD_UltrasonicWorker::DD_UltrasonicWorker(QObject *parent) : QObject(parent) {
    wiringPiSetup();

		// Initialize SIG pin
		pinMode(SIG_PIN, OUTPUT);
		digitalWrite(SIG_PIN, LOW);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
}

DD_UltrasonicWorker::~DD_UltrasonicWorker() {}

void DD_UltrasonicWorker::DD_process() {
		bool doorPreviouslyDetected = false;

    while (true) {
				// 1. SIG 핀을 출력으로 설정하고 TRIG 신호 전송
        pinMode(SIG_PIN, OUTPUT);
        digitalWrite(SIG_PIN, LOW);
        delayMicroseconds(2);
        digitalWrite(SIG_PIN, HIGH);
        delayMicroseconds(10);
        digitalWrite(SIG_PIN, LOW);


        // 2. SIG 핀을 입력으로 전환하여 ECHO 받기
        pinMode(SIG_PIN, INPUT);

        while (digitalRead(SIG_PIN) == LOW);
        long startTime = micros();

        while (digitalRead(SIG_PIN) == HIGH);
        long endTime = micros();

        double dist = (endTime - startTime) * 0.034 / 2.0;

				//std::cout << "[" << __func__ << "] 거리 측정: " << dist << " cm" << std::endl;

				// 상태 변경 여부 감지
				if (dist < 2.0 && !doorPreviouslyDetected) {
						emit doorDetected();
						doorPreviouslyDetected = true;
				} else if (dist >= 2.0 && doorPreviouslyDetected) {
						emit doorLeft();
						doorPreviouslyDetected = false;
				}

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

