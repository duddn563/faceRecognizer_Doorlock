// DoorlockController.hpp
#pragma once
//#include <gpiod.h>
#include <wiringPi.h>
#include <iostream>
#include <unistd.h>
#include <QDebug>

#define RELAY_PIN 7  // wiringPi 기준 GPIO2 → wiringPi 8번 (물리핀 3)

class DoorlockController {
public:
	DoorlockController();
	//~DoorlockController();
	
	bool init();
	bool setUnlocked(bool on); 		// true=열림(ON), false=잠금(OFF)
	bool lock();
	bool unlock();
	bool isReady();

private:
	bool isReady_;
};


