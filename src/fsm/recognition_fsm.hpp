#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>
#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>
#include <utility>
#include "faceRecognitionState.hpp"

struct FsmContext {
		// FaceRecognitionService가 채워줄 읽기 전용 스냅샷들
		double detectScore = 0.0;				// 얼굴 검출 강도/스코어
		double recogConfidence = 0.0;		// 인식 신뢰도(확률)
		bool isDuplicate = false;				// 중복 사용자 탐지
		bool registerRequested = false;	// 등록 요청(버튼/메뉴)
		bool livenessOk = true;					// 라이브니스 결과
		bool doorOpened = false;					// 리드 수위치 등 문 열림
		int failCount = 0;							// 연속 실패 횟수
		bool facePresent = false;				// 얼굴 존재 여부
		bool timeout = false;						// 상태 타임아웃 여부
		qint64 nowMs = 0;								// 단조 시간(ms)
};


class IFsmState {
public:
		virtual ~IFsmState() = default;
		virtual void onEnter(const FsmContext&) {}
		virtual void onUpdate(const FsmContext&) {}
		virtual void onExit(const FsmContext&) {}
};

// 히스테리시스 게이트: enter/exit 임계+ N-of-M 프레임 확증
class HysteresisGate {
public:
		HysteresisGate(double enterThresh, double exitThresh, int confirmFrames = 3, int window = 3)
				: enter_(enterThresh), exit_(exitThresh), need_(confirmFrames), win_(window) {}

		// 입력값 x에 대해 상태(true/false) 유지/전환 걸졍
		bool feed(double x) {
			// 창에 누적
			buf_.push_back(x);
			if ((int)buf_.size() > win_) buf_.erase(buf_.begin());


			// 최근 창에서 조건 충족 카운터
			int ok = 0;
			for (double v : buf_) {
					if (!state_) {
							if (v >= enter_) ok++;
					} else {
							if (v <= exit_) ok++;
					}
			}
			
			if (!state_) {
					if (ok >= need_) state_ = true;
			} else {
					if (ok >= need_) state_ = false;
			}

			return state_;
	}

	void reset(bool s = false) { state_ = s; buf_.clear(); }

private:
	double enter_, exit_;
	int need_, win_;
	bool state_ = false;
	std::vector<double> buf_;
};

struct Transition {
		const char* name = "unnamed";			// 전환 식별용 이름
		RecognitionState from;
		RecognitionState to;
		std::function<bool(const FsmContext&)> guard;		// 전환 조건
		int minDwellMs = 50;			// 상태 유지 최소 시간(히스테리시스)
};

class RecognitionFsm : public QObject {
		Q_OBJECT
public:
			explicit RecognitionFsm(QObject* parent = nullptr);

			void addState(RecognitionState s, std::unique_ptr<IFsmState> st); 
			void addTransition(const Transition& t); 
			void start(RecognitionState initial);
			void stop();
			void updateContext(const FsmContext& c); 
			RecognitionState current() const { return current_; }

signals: 
			void stateChanged(RecognitionState);

private slots:
			void tick(); 

private:
		QTimer timer_;
		FsmContext ctx_;
		RecognitionState current_ = RecognitionState::IDLE;
		QElapsedTimer enterTime_;
		std::unordered_map<RecognitionState, std::unique_ptr<IFsmState>> states_;
		std::vector<Transition> trans_;
};
