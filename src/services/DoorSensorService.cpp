#include "DoorSensorService.hpp"
#include <wiringPi.h>
#include <chrono>
#include <thread>

// #define DEBUG

DoorSensorService::DoorSensorService(QObject *parent) : QObject(parent) 
{
    wiringPiSetup();

		// Initialize SIG pin
		pinMode(SIG_PIN, OUTPUT);
		digitalWrite(SIG_PIN, LOW);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
}

void DoorSensorService::run()
{
		bool doorPreviouslyDetected = false;

		while (isRunning) {
				QThread::msleep(100);

				pinMode(SIG_PIN, OUTPUT);
				digitalWrite(SIG_PIN, LOW);
				delayMicroseconds(2);
				digitalWrite(SIG_PIN, HIGH);
				delayMicroseconds(10);
				digitalWrite(SIG_PIN, LOW);

				pinMode(SIG_PIN, INPUT);

				while (digitalRead(SIG_PIN) == LOW);
				long startTime = micros();

				while (digitalRead(SIG_PIN) == HIGH);
				long endTime = micros();

				double dist = (endTime - startTime) * 0.034 / 2.0;

#ifdef DEBUG
				std::cout << "Door sensor Service dist: " << dist << std::endl;
#endif

				if (dist < 2.0 && !doorPreviouslyDetected) {
						emit doorClosed();
						doorPreviouslyDetected = true;
				} else if (dist >= 2.0 && doorPreviouslyDetected) {
						emit doorOpened();
						doorPreviouslyDetected = false;
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}
}

void DoorSensorService::stop() {
		isRunning = false;
}

