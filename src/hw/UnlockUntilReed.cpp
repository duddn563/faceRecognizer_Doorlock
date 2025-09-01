// UnlockUntilReed.cpp
#include "UnlockUntilReed.hpp"
#include <chrono>
#include <thread>

using namespace std::chrono;

void UnlockUntilReed::start(){
    if (running_.exchange(true)) return;

    th_ = std::thread([this](){
        const auto tStart = steady_clock::now();
        // 0) 즉시 문을 '열림 유지'
        door_->setUnlocked(true);

        // 1단계: 사용자가 문을 '여는지' 감시
        bool proceed = waitOpenPhase(); // false면 열지 않고 openTimeout 초과 → 바로 잠금 종료
        if (!proceed) {
            door_->setUnlocked(false);
            running_.store(false);
            return;
        }

        // 2단계: 한 번이라도 열렸다면, 이제 '닫힘' 감지 대기
        waitClosePhase();

        // 종료: 잠금
        door_->setUnlocked(false);
        running_.store(false);
    });

    th_.detach();
}

void UnlockUntilReed::stop(){
    if (!running_.exchange(false)) return;
    door_->setUnlocked(false);
}

// 1단계: '문 열림 감지' 대기
bool UnlockUntilReed::waitOpenPhase(){
    // 현재 상태 파악: 닫힘이면(자석 감지) -> 열림을 기다림
    // 연속 디바운스 사용
    int openHits = 0;
		bool sawClosed =  false;			// 닫힘을 한 번이라도 봤는가
    const auto t0 = steady_clock::now();

    while (running_.load()) {
        bool isClosed = reed_->isClosed();

				/*
        if (!isClosed) { // 문이 '열렸다'로 해석
            if (++openHits >= opt_.hits) {
                // 열림 확정 → 다음 Phase로 진행
                return true;
            }
        } else {
            openHits = 0;
        }
				*/

				if (isClosed) {
						sawClosed = true;
						openHits = 0;
				}
				else if (sawClosed) {
						if (++openHits >= opt_.hits) {
								return true;
						}
				}
				else {
							// 닫힘을 본 적이 없으면 '열림'카운터하지 않음(floating방어)
							openHits = 0;
				}

        // '문을 열지 않은 채' openTimeout 경과 시 종료
        auto now = steady_clock::now();
        if (duration_cast<milliseconds>(now - t0).count() >= opt_.openTimeoutMs) {
						qDebug() << "[HW] Door openTimeout expired -> lock";

            // 열지 않아 종료
            return false;
        }

        // 전체 안전 타임아웃도 가드 (openTimeout이 더 짧게 설정되는 게 일반적)
        if (duration_cast<milliseconds>(now - t0).count() >= opt_.maxUnlockMs) {
						qDebug() << "[HW] Door maxUnlockout expired -> lock";
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(opt_.pollMs));
    }
    return false; // 중단됨
}

// 2단계: '문 닫힘 감지' 대기
void UnlockUntilReed::waitClosePhase(){
    int closeHits = 0;
    const auto t1 = steady_clock::now();

    while (running_.load()) {
        bool isClosed = reed_->isClosed();

        if (isClosed) { // 자석 감지 → 닫힘
            if (++closeHits >= opt_.hits) {
                // 닫힘 확정 → 종료
                return;
            }
        } else {
            closeHits = 0;
        }

        auto now = steady_clock::now();
        if (duration_cast<milliseconds>(now - t1).count() >= opt_.maxUnlockMs) {
            // 안전 타임아웃 → 종료
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(opt_.pollMs));
    }
}

