#include "hw/DoorlockController.hpp"


DoorlockController::DoorlockController() {}

bool DoorlockController::init() 
{
	if (wiringPiSetup() == -1) {
		qDebug() << "[init] Door lock controller wiringPi 초기화 실패!";
		isReady_ = false;
		return -1;
	}

	pinMode(RELAY_PIN, OUTPUT);
	isReady_ = true;

	qDebug() << "[init] door init Ok";
	return 1;
}

bool DoorlockController::setUnlocked(bool on) 
{
	if (on == true) {
		digitalWrite(RELAY_PIN, HIGH);
		qDebug() << "[setUnlocked] Door Unlocked!";
		return 1;
	}
	else if (on == false) {
		digitalWrite(RELAY_PIN, LOW);
		qDebug() << "[setUnlocked] Door Locked";
		return 1;
	}

	return 0;
}

bool DoorlockController::lock() { return setUnlocked(false); }
bool DoorlockController::unlock() { return setUnlocked(false); }

bool DoorlockController::isReady() { return isReady_; }

