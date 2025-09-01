// UnlockUntilReed.hpp
#pragma once
#include <atomic>
#include <thread>
#include "DoorlockController.hpp"
#include "ReedSensor.hpp"

class UnlockUntilReed {
public:
    	  struct Opt {
         int pollMs;							// 폴링 간격
         int hits;								// close/open 디바운스 연속 히트 
         int openTimeoutMs;				// 열림 유지 후 '문을 여는 데' 혀옹 되는 최대 시간
         int maxUnlockMs;					// 전체 안전 타임 아웃(열림 유지 시작 ~ 종료)
         // 명시적 기본 생성자
         constexpr Opt(int p=50, int h=6, int openMs=5000, int maxMs=10000)
             : pollMs(p), hits(h), openTimeoutMs(openMs), maxUnlockMs(maxMs) {}
     };

			UnlockUntilReed(DoorlockController* door, ReedSensor* reed, Opt opt = Opt())
      : door_(door), reed_(reed), opt_(opt) {}


    // 시작: 문을 연 상태로 유지. (이미 동작 중이면 무시)
    void start();
    // 강제 중단: 즉시 잠금
    void stop();
    bool running() const { return running_.load(); }

private:
    enum class Phase { WaitOpen, WaitClose }; // 1단계: 열기 대기, 2단계: 닫기 대기
    bool waitOpenPhase();   // true=다음 phase 진행, false=바로 종료(잠금)
    void waitClosePhase();  // 닫힘 감지 또는 타임아웃 시 종료(잠금)

    DoorlockController* door_;
    ReedSensor* reed_;
    Opt opt_;
    std::atomic<bool> running_{false};
    std::thread th_;
};

