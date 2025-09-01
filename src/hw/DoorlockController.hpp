// DoorlockController.hpp
#pragma once
#include <gpiod.h>
#include <QDebug>

class DoorlockController {
public:
    DoorlockController(const char* chip, unsigned line, bool activeHigh)
      : chipname_(chip), line_(line), activeHigh_(activeHigh) {}
    bool init(){
				qDebug() << "[HW] Door init OK";
				chip_ = open_chip_name_or_path(chipname_);

        if(!chip_) return false;
        io_ = gpiod_chip_get_line(chip_, line_);
        if(!io_) return false;
        int def = activeHigh_ ? 0 : 1; // 기본 잠금(OFF)
        return gpiod_line_request_output(io_, "doorlock", def) == 0;
    }
    void deinit(){
        if(io_){ gpiod_line_release(io_); io_=nullptr; }
        if(chip_){ gpiod_chip_close(chip_); chip_=nullptr; }
    }
    bool setUnlocked(bool on){ // true=열림(ON), false=잠금(OFF)
        int v = on ? (activeHigh_?1:0) : (activeHigh_?0:1);
        return gpiod_line_set_value(io_, v) == 0;
    }
    bool lock()   { return setUnlocked(false); }
    bool unlock() { return setUnlocked(true);  }

		static gpiod_chip* open_chip_name_or_path(const char* s) {
				if (!s) return nullptr;
				if (s[0] == '/') return gpiod_chip_open(s);      // 경로
				return gpiod_chip_open_by_name(s);               // 이름
		}

    ~DoorlockController(){ deinit(); }
private:
    const char* chipname_;
    unsigned line_;
    bool activeHigh_;
    gpiod_chip* chip_ = nullptr;
    gpiod_line* io_   = nullptr;
};

