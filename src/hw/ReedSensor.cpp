#include "ReedSensor.hpp"

#ifdef GPIOD
// reedActiveHigh: 자석 감지 시 HIGH이면 true, LOW이면 false (테스트로 결정)
ReadSensor::ReedSensor(const char* chip, unsigned line, bool reedActiveHigh)
	: chipname_(chip), line_(line), activeHigh_(reedActiveHigh) {}

bool ReedSensor::init()
{
	qDebug() << "[init] Reed init OK";
	chip_ = open_chip_name_or_path(chipname_);
	if(!chip_) return false;
	qDebug() << "[Reed] " << chipname_ << ", " << line_; 
	io_ = gpiod_chip_get_line(chip_, line_);
	if(!io_) return false;
	// 입력 요청 (풀업/풀다운은 모듈에 따라 다름. 필요시 외부저항/모듈설정 사용)
	return gpiod_line_request_input(io_, "reed") == 0;
}
void deinit()
{
	if(io_){ gpiod_line_release(io_); io_=nullptr; }
	if(chip_){ gpiod_chip_close(chip_); chip_=nullptr; }
}
// 자석 감지(문 닫힘) 여부
bool isClosed() const 
{
	int v = gpiod_line_get_value(io_);
	if(v < 0) return false;
	qDebug() << "[Reed] " << chipname_ << ", " << line_; 
	return activeHigh_ ? (v==1) : (v==0);
}

static inline gpiod_chip* open_chip_name_or_path(const char* s) {
	if (!s) return nullptr;
	if (s[0] == '/') return gpiod_chip_open(s);      // 경로
	return gpiod_chip_open_by_name(s);               // 이름
}

#else
ReedSensor::ReedSensor() {}

bool ReedSensor::init() 
{
	if (wiringPiSetup() == -1) {
		qDebug() << "[init] Reed Sensor wiringPi setup failed!";
		return -1;
	}

	pinMode(REED_PIN, INPUT); // 입력모드로 설정
	pinMode(REED_PIN, PUD_UP);
	//qDebug() << "[HW] Reed init OK";
	return 1;
}

bool ReedSensor::isClosed() const 
{
	int st = digitalRead(REED_PIN);
	//qDebug() << "[isClosed] status=" << st;
	if (st == LOW) 
		return true; 
	else 
		return false;
}
#endif

