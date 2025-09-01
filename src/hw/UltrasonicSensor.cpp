#include "FaceSensorService.hpp"
#include <QDebug>


// #define DEBUG

UltrasonicSensor::(QObject *parent) : QObject(parent) 
{
    wiringPiSetup();

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    digitalWrite(TRIG_PIN, LOW);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
}


void FaceSensorService::run() {
		bool personPreviouslyDetected = false;

    while (isRunning) {
				QThread::msleep(100);
				
			  // 초음파 신호 전송
        digitalWrite(TRIG_PIN, LOW);
        delayMicroseconds(2);
        digitalWrite(TRIG_PIN, HIGH);
        delayMicroseconds(10);
        digitalWrite(TRIG_PIN, LOW);

        while (digitalRead(ECHO_PIN) == LOW);
        long startTime = micros();

        while (digitalRead(ECHO_PIN) == HIGH);
        long endTime = micros();

        double dist = (endTime - startTime) * 0.034 / 2.0;

#ifdef DEBUG
				std::cout << "Face Sensor dist:" <<  dist << " cm" << std::endl;
#endif

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

void FaceSensorService::stop()
{
		isRunning = false;
}
