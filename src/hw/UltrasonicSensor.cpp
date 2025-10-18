#include "UltrasonicSensor.hpp"
#include <QDebug>

// #define DEBUG

UltrasonicSensor::UltrasonicSensor()  { qDebug() << "[UltrasonicSensor] ctor"; }

UltrasonicSensor::~UltrasonicSensor() {
	stop();
}


void UltrasonicSensor::start()
{
	bool expected = false;
	if (!isRunning.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
		qDebug() << "[ultra] start() ignored: already running";
		return;
	}

	th_ = std::thread([this]() { 
			qDebug() << "[ultra] thread enter";
			isRunning.store(true, std::memory_order_release);
			try {
				this->main_loop();
			}
			catch (const std::exception& e) {
				qWarning() << "[ultra] excaption:" << e.what();
			}
			catch (...) {
				qWarning() << "[ultra] unknown exception";

			}

			isRunning.store(false, std::memory_order_release);
			qDebug() << "[ultra] thread exit";
	});
}

void UltrasonicSensor::stop()
{
	bool wasRunning = isRunning.exchange(false);
	if (wasRunning && th_.joinable()) {
		th_.join();
	}
}

float UltrasonicSensor::latestDist() const 
{
	return latestDist_.load(std::memory_order_acquire);
}
void UltrasonicSensor::main_loop()
{
	wiringPiSetup();

	pinMode(TRIG_PIN, OUTPUT);
	pinMode(ECHO_PIN, INPUT);
	pullUpDnControl(ECHO_PIN, PUD_DOWN);
	digitalWrite(TRIG_PIN, LOW);
	delay(50);

	auto waitState = [](int pin, int level, unsigned timeout_us)->bool {
        unsigned deadline = micros() + timeout_us;
        while (digitalRead(pin) != level) {
            if ((int)(micros() - deadline) >= 0) return false; // timeout
        }
        return true;
    };

	qDebug() << "[init] Ultrasonic sensor init OK (Running? " << isRunning.load() << ")";
	while (isRunning.load(std::memory_order_acquire)) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // 1) 트리거 전, ECHO가 LOW로 안정될 때까지 (timeout)
        if (!waitState(ECHO_PIN, LOW, 200000)) {
            latestDist_.store(-1.0f, std::memory_order_release);
            continue;
        }

        // 2) 트리거 10us
        digitalWrite(TRIG_PIN, LOW);  delayMicroseconds(2);
        digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
        digitalWrite(TRIG_PIN, LOW);

        // 3) 상승/하강 각각 timeout
        if (!waitState(ECHO_PIN, HIGH, 200000)) {
            latestDist_.store(-1.0f, std::memory_order_release);
            continue;
        }
        unsigned start = micros();

        if (!waitState(ECHO_PIN, LOW, 200000)) {
            latestDist_.store(-1.0f, std::memory_order_release);
            continue;
        }
        unsigned end = micros();

        // 4) 거리 계산(+ 유효 범위 클램프)
        unsigned elapsed = end - start;              // wrap-safe
        float dist = (elapsed * 0.0343f) / 2.0f;     // cm
        if (dist < 2.0f || dist > 400.0f) {          // HC-SR04 대략 범위
            latestDist_.store(-1.0f, std::memory_order_release);
            continue;
        }
		//qDebug() << "[ultrasonicSensor] dist:" << dist;	
        latestDist_.store(dist, std::memory_order_release);
    }
}


