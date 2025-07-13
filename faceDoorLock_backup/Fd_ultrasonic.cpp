#include "Fd_ultrasonic.hpp"


FD_UltrasonicWorker::FD_UltrasonicWorker(QObject *parent) : QObject(parent) {
    wiringPiSetup();

    pinMode(FD_TRIG_PIN, OUTPUT);
    pinMode(FD_ECHO_PIN, INPUT);
    digitalWrite(FD_TRIG_PIN, LOW);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
}

FD_UltrasonicWorker::~FD_UltrasonicWorker() {}

void FD_UltrasonicWorker::FD_process() {
		bool personPreviouslyDetected = false;

    while (true) {
			  // 초음파 신호 전송
        digitalWrite(FD_TRIG_PIN, LOW);
        delayMicroseconds(2);
        digitalWrite(FD_TRIG_PIN, HIGH);
        delayMicroseconds(10);
        digitalWrite(FD_TRIG_PIN, LOW);

        while (digitalRead(FD_ECHO_PIN) == LOW);
        long startTime = micros();

        while (digitalRead(FD_ECHO_PIN) == HIGH);
        long endTime = micros();

        double dist = (endTime - startTime) * 0.034 / 2.0;

				//std::cout << "[" << __func__  << "]  거리 측정:" << dist << " cm" << std::endl;

				// 상태 변경 여부 감지
				if (dist < 50.0 && !personPreviouslyDetected) {
						emit personDetected();
						personPreviouslyDetected = true;
				} else if (dist >= 50.0 && personPreviouslyDetected) {
						emit personLeft();
						personPreviouslyDetected = false;
				}

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

