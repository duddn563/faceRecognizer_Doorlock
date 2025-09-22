// ReedSensor.hpp
#pragma once
#include <wiringPi.h>
#include <gpiod.h>
#include <chrono>
#include <QDebug>

//#define GPIOD
#define REED_PIN 	13

#ifdef GPIOD
class ReedSensor {
public:
    // reedActiveHigh: 자석 감지 시 HIGH이면 true, LOW이면 false (테스트로 결정)
    ReedSensor(const char* chip, unsigned line, bool reedActiveHigh);

    bool init();
    void deinit();
    bool isClosed() const;

private:
    const char* chipname_;
    unsigned line_;
    bool activeHigh_;
    gpiod_chip* chip_ = nullptr;
    gpiod_line* io_   = nullptr;
};
#else
class ReedSensor {
public:
    ReedSensor();
    bool init();
    bool isClosed() const ;
};
#endif

