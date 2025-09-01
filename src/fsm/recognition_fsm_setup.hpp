//recognition_fsm_setup.hpp
#pragma once
#include "recognition_fsm.hpp"
#include "recognition_states.hpp"

// 임계/시간 파라미터(필요시 설정/GUI에서 변경 가능하도록)
struct FsmParams {
		double detectEnter = 0.65;			
		double detectExit	 = 0.35;
		int		 detectMinDwellMs = 200;

		double recogEnter = 0.80;
		double recogExit	= 0.50;
		int		 recogTimeoutMs = 5000;

		int successHoldMs = 800;
		int failCooldownMs = 1000;

		int authThresh = 5;					// 얼굴 인증 임계값(어떤 조건을 만족하기 위해 넘아야 하는 경계치)

		int lockoutFails = 5;
		int lockoutMs = 30'000;
};

inline void setupRecognitionFsm(RecognitionFsm& fsm, const FsmParams& P)
{
		fsm.addState(RecognitionState::IDLE,					std::make_unique<IdleState>());
		fsm.addState(RecognitionState::DETECTING,			std::make_unique<DetectingState>(P.detectEnter, P.detectExit));
		fsm.addState(RecognitionState::RECOGNIZING,		std::make_unique<RecognizingState>(P.recogEnter, P.recogExit));
		fsm.addState(RecognitionState::REGISTERING,		std::make_unique<RegisteringState>());
		fsm.addState(RecognitionState::DUPLICATE_FACE,std::make_unique<DuplicateFaceState>());
		fsm.addState(RecognitionState::AUTH_SUCCESS,	std::make_unique<AuthSuccessState>());
		fsm.addState(RecognitionState::AUTH_FAIL,			std::make_unique<AuthFailState>());
		fsm.addState(RecognitionState::DOOR_OPEN,			std::make_unique<DoorOpenState>());
		fsm.addState(RecognitionState::LOCKED_OUT,		std::make_unique<LockedOutState>());


		// 전환 정의 (트리거/가드)
		auto nowMs = [](const FsmContext& c) { return c.nowMs; };

		// IDLE -> DETECTING: 얼굴 존재 히스테리시스(enter thresh) + 최소 체류
		fsm.addTransition({
				"idle->detect",
				RecognitionState::IDLE, RecognitionState::DETECTING,
				[P](const FsmContext& c) {
						return c.facePresent && c.detectScore >= P.detectEnter;
				},
				/*minDwellMs=*/100
		});


		// DETECTING -> IDLE: 얼굴 유실 히스테리시스(exit thresh)
		fsm.addTransition({
				"detect->idle",
				RecognitionState::DETECTING, RecognitionState::IDLE,
				[P](const FsmContext& c) {
						return (!c.facePresent) || (c.detectScore <= P.detectExit); // ||(!c.registerRequested);
				},
				P.detectMinDwellMs
		});


		// DETECTING -> RECOGNIZING: 검출 안정 + 인식 시작 조건
		fsm.addTransition({
				"detect->recognizing",
				RecognitionState::DETECTING, RecognitionState::RECOGNIZING,
				[P] (const FsmContext& c) {
						return c.facePresent && c.detectScore >= P.detectEnter * 0.95; //&& (!c.registerRequested); // 약간 관용
				},
				P.detectMinDwellMs
		});

		// RECOGNIZING -> AUTH_SUCCESS: 신뢰도 히스테리시스(enter) + 라이브니스 OK
		fsm.addTransition({
				"recognizing->success",
				RecognitionState::RECOGNIZING, RecognitionState::AUTH_SUCCESS,
				[P] (const FsmContext& c) {
						return c.livenessOk && (c.recogConfidence >= P.recogEnter); //&& (!c.registerRequested);
				},
				/*minDwellMs=*/150
		});

		// RECOGNIZING -> AUTH_FAIL: 타임아웃 또는 신뢰도 하방 이탈 지속
		fsm.addTransition({
				"recognizing->fail",
				RecognitionState::RECOGNIZING, RecognitionState::AUTH_FAIL,
				[P] (const FsmContext& c) {
						return c.timeout || (!c.livenessOk) || (c.recogConfidence <= P.recogExit && !c.facePresent);
				},
				/*minDwellMs=*/200
		});

		// RECOGNIZING -> DUPLICATE_FACE
		fsm.addTransition({
				"recognizing->duplicate",
				RecognitionState::RECOGNIZING, RecognitionState::DUPLICATE_FACE,
				[] (const FsmContext& c) { return c.isDuplicate; },
				/*minDwellMs=*/100
		});

		// AUTH_SUCCESS -> DOOR_OPEN: 성공 후 딜레이
		fsm.addTransition({
				"success->dooropen",
				RecognitionState::AUTH_SUCCESS, RecognitionState::DOOR_OPEN,
				[P] (const FsmContext& c) { 
						qDebug() << "[FSM] c.authStreak:" << c.authStreak;
						return c.detectScore >= 0.8 && c.livenessOk && (c.recogConfidence >= P.recogEnter) && (c.authStreak >= P.authThresh) && c.allowEntry; 
				},
				/*minDwellMs=*/150
		});


		// DOOR_OPEN -> IDLE: 0.2초 체류후 IDLE로 복귀
		fsm.addTransition({
				"dooropen->idle",
				RecognitionState::DOOR_OPEN, RecognitionState::IDLE,
				[] (const FsmContext& c) { return !c.doorOpened; },
				/*minDwellMs=*/200
		});



		// AUTH_FAIL -> LOCKED_OUT: 실패 누적
		fsm.addTransition({
				"fail->lockout",
				RecognitionState::AUTH_FAIL, RecognitionState::LOCKED_OUT,
				[P] (const FsmContext& c) { return c.failCount >= P.lockoutFails; },
				/*minDwellMs=*/50
		});

		// AUTH_FAIL -> IDLE: 쿨다운 후 재시도
		fsm.addTransition({
				"fail->idle",
				RecognitionState::AUTH_FAIL, RecognitionState::IDLE,
				[P] (const FsmContext& c) { return true; },
				P.failCooldownMs
		});

		// LOCKED_OUT -> IDLE: 락아웃 시간 경과
		fsm.addTransition({
				"lockout->idle",
				RecognitionState::LOCKED_OUT, RecognitionState::IDLE,
				[P] (const FsmContext& c) { return c.timeout; },
				/*minDwellMs=*/0
		});

		// IDLE <-> REGISTERING: UI에서 등록 시작/종료
		fsm.addTransition({
				"idle->register",
				RecognitionState::IDLE, RecognitionState::REGISTERING,
				[] (const FsmContext& c) { return c.registerRequested; },
				/*minDwellMs=*/0
		});
		fsm.addTransition({
				"register->idle",
				RecognitionState::REGISTERING, RecognitionState::IDLE,
				[] (const FsmContext& c) { return !c.registerRequested; },
				/*minDwellMs=*/0
		});
}


