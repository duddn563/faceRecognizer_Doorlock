#ifndef ULTRASONICSENSOR_H 
#define	ULTRASONICSENSOR_H

#include <wiringPi.h>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <atomic>

#include <QThread>

#define TRIG_PIN 4
#define ECHO_PIN 5

class UltrasonicSensor {
	public:
		UltrasonicSensor();
		~UltrasonicSensor();

		void start();
		void stop();
		float latestDist() const;

	private:
		void main_loop();

		std::thread th_;
		std::atomic<bool> isRunning{false};

		std::atomic<float> latestDist_{-1.0f};
};

#endif // ULTRASONICSENSOR_H 

