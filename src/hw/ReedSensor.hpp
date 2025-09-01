// ReedSensor.hpp
#pragma once
#include <gpiod.h>
#include <chrono>
#include <QDebug>

class ReedSensor {
public:
    // reedActiveHigh: 자석 감지 시 HIGH이면 true, LOW이면 false (테스트로 결정)
    ReedSensor(const char* chip, unsigned line, bool reedActiveHigh)
      : chipname_(chip), line_(line), activeHigh_(reedActiveHigh) {}

    bool init(){
				qDebug() << "[HW] Reed init OK";
        //chip_ = gpiod_chip_open_by_name(chipname_);
				chip_ = open_chip_name_or_path(chipname_);
        if(!chip_) return false;
        io_ = gpiod_chip_get_line(chip_, line_);
        if(!io_) return false;
        // 입력 요청 (풀업/풀다운은 모듈에 따라 다름. 필요시 외부저항/모듈설정 사용)
        return gpiod_line_request_input(io_, "reed") == 0;
    }
    void deinit(){
        if(io_){ gpiod_line_release(io_); io_=nullptr; }
        if(chip_){ gpiod_chip_close(chip_); chip_=nullptr; }
    }
    // 자석 감지(문 닫힘) 여부
    bool isClosed() const {
        int v = gpiod_line_get_value(io_);
        if(v < 0) return false;
        return activeHigh_ ? (v==1) : (v==0);
    }

		static gpiod_chip* open_chip_name_or_path(const char* s) {
				if (!s) return nullptr;
				if (s[0] == '/') return gpiod_chip_open(s);      // 경로
				return gpiod_chip_open_by_name(s);               // 이름
		}

private:
    const char* chipname_;
    unsigned line_;
    bool activeHigh_;
    gpiod_chip* chip_ = nullptr;
    gpiod_line* io_   = nullptr;
};

