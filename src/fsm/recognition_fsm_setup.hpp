//recognition_fsm_setup.hpp
#pragma once
#include "recognition_fsm.hpp"
#include "recognition_states.hpp"

// 임계/시간 파라미터(필요시 설정/GUI에서 변경 가능하도록)
struct FsmParams {
		// 1) Detect 단계 (얼굴 존재 여부 히스테리시스)
		double detectEnter        = 0.80f;	// 얼굴 있다고 보기 시작			
		double detectExit		  = 0.65f;	// 얼굴 해제 조건(조금 낮게 ) -> 깜박임 방지
		int	   detectMinDwellMs = 100 + 150;	// 아주 짧은 깜박 방지(100ms~150ms)

		// 2) Recognition단계 (유사도 히스테리시스)
		double recogEnter		  = 0.80f;	// 인식 성공 "진입": 보수적으로 높임			
		double recogExit		  = 0.65f;	// 인식 성공 "유지/해제": 약간 낮춤
		int	   recogTimeoutMs	  = 3500;	// 한 번의 시도 제한 (3.5s 내 못 넘으면 실패 처리)
		bool   recogPass          = false;	// 

		// 3) 성공/실패 연출 및 쿨다운
		int successHoldMs		  = 800;	// 성공 오버레이 유지 
		int failCooldownMs		  = 1000;	// 실패 후 짧은 쿨다운(연속 시도 급증 방지)

		// 4) 문 열림 누적 정책
		int authThresh			  = 5;		// 5 프레임(or 5회) 연속/가까운 시퀀스 성공시 도어 오픈

		// 5) 연속 실패 잠금
		int lockoutFails		  = 5;		// 연속 실패 n회 시 
		int lockoutMs			  = 30000;	// 30초 쿨다운

		int reedDebounceMs        = 80;
		int waitCloseHoldMs       = 200;	// 닫힘 확인 안정화
		int recogFailHoldMs       = 300;    // 인식 실패 판정 대기
		int idleNoFaceMs		  = 800;	// 얼굴 사라지면 IDLE로
		int duplicateHoldMs       = 200;    //	중복 얼굴 표시 시간
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
						return c.facePresent && (c.detectScore >= P.detectEnter);
				},
				/*minDwellMs=*/100
		});


		// DETECTING -> IDLE: 얼굴 유실 히스테리시스(exit thresh)
		fsm.addTransition({
				"detect->idle",
				RecognitionState::DETECTING, RecognitionState::IDLE,
				[P](const FsmContext& c) {
						return (!c.facePresent);
				},
				P.detectMinDwellMs
		});

		// DETECTING -> RECOGNIZING: 검출 안정 + 인식 시작 조건 
		fsm.addTransition({ 
				"detect->recognizing",
				RecognitionState::DETECTING, RecognitionState::RECOGNIZING,
				[P] (const FsmContext& c) {
						return (c.detectScore >= P.detectEnter) && 
							   c.livenessOk &&
							   (c.recogConfidence >= P.recogEnter) &&
							   (!c.registerRequested); 
				},
				P.detectMinDwellMs
		});

		fsm.addTransition({
				"recognizing->detect",
				RecognitionState::RECOGNIZING, RecognitionState::DETECTING,
				[P] (const FsmContext& c) {
					return (c.detectScore < P.detectExit) || (c.recogConfidence < P.recogExit);
				},
				0
		});


		// RECOGNIZING -> AUTH_SUCCESS: 신뢰도 히스테리시스(enter) + 라이브니스 OK
		fsm.addTransition({
				"recognizing->success",
				RecognitionState::RECOGNIZING, RecognitionState::AUTH_SUCCESS,
				[P] (const FsmContext& c) {
						return (c.detectScore >= P.detectEnter) &&
								c.livenessOk && 
								//c.allowEntry && 
								(c.recogConfidence >= P.recogEnter) && 
								(c.authStreak >= 1) &&
								!c.doorOpened;		// 문이 이미 열려 있으면 재성공 금지
				},
				/*minDwellMs=*/P.successHoldMs
		});

		// AUTH_SUCCESS -> DOOR_OPEN: 성공 후 딜레이
		fsm.addTransition({
				"success->dooropen",
				RecognitionState::AUTH_SUCCESS, RecognitionState::DOOR_OPEN,
				[P] (const FsmContext& c) { 
						return (c.detectScore >= P.detectEnter) &&  
							    c.livenessOk &&  
								c.allowEntry && 
								//(c.authStreak >= P.authThresh) && 
								!c.doorOpened; 
				},
				/*minDwellMs=*/P.successHoldMs
		});

		// DOOR_OPEN -> WAIT_CLOSE : 리드센서가 열림 감지될 때
		fsm.addTransition({
				"dooropen->waitclose",
				RecognitionState::DOOR_OPEN, RecognitionState::WAIT_CLOSE,
				[] (const FsmContext& c) { return c.doorOpened; },
				P.reedDebounceMs
		});


		// WAIT_CLOSE -> IDLE : 문이 다시 닫히면 종료
		fsm.addTransition({
				"waitclose->idle",
				RecognitionState::WAIT_CLOSE, RecognitionState::IDLE,
				[](const FsmContext& c){ return !c.doorOpened; },
				/*minDwellMs=*/P.waitCloseHoldMs
		});

		// AUTH_SUCCESS -> IDLE: 등록 성공 후 IDLE로 복귀
		fsm.addTransition({
				"success->idle",
				RecognitionState::AUTH_SUCCESS, RecognitionState::IDLE,
				[P] (const FsmContext& c) {
						qDebug() << "[FSM] c.recgConfidence: " << c.recogConfidence;
						//return (c.recogConfidence > P.recogExit);
						return c.timeout || c.doorOpened;
				},
				/*mainDwellMs=*/P.successHoldMs
				
		});


		// DOOR_OPEN -> IDLE: 0.2초 체류후 IDLE로 복귀
		fsm.addTransition({
				"dooropen->idle",
				RecognitionState::DOOR_OPEN, RecognitionState::IDLE,
				[] (const FsmContext& c) { 
					return !c.doorOpened;
				},
				/*minDwellMs=*/P.successHoldMs
		});


		// RECOGNIZING -> AUTH_FAIL: 타임아웃 또는 신뢰도 하방 이탈 지속
		fsm.addTransition({
				"recognizing->fail",
				RecognitionState::RECOGNIZING, RecognitionState::AUTH_FAIL,
				[P] (const FsmContext& c) {
					if (c.facePresent && c.recogConfidence > P.recogExit) return false;	
					bool condNotMet = (c.detectScore >= P.detectEnter) && c.livenessOk && !c.allowEntry;
					bool hardFail   = (c.facePresent && (c.detectScore < P.detectEnter || !c.livenessOk));
					//return c.timeout || (!c.livenessOk) || (c.recogConfidence <= P.recogExit);
					return condNotMet || hardFail;
				},
				/*minDwellMs=*/P.recogFailHoldMs
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

		// LOCKED_OUT -> IDLE: 락아웃 시간 경과(간단히 dwell로 처리)
		fsm.addTransition({
				"lockout->idle",
				RecognitionState::LOCKED_OUT, RecognitionState::IDLE,
				[P] (const FsmContext& c) { return true/*c.timeout*/; },             
				/*minDwellMs=*/P.failCooldownMs
		});

		// RECOGNIZING -> IDLE : 얼굴이 사라지면 휴지 상태로
		fsm.addTransition({
				"recognizing->idle",
				RecognitionState::RECOGNIZING, RecognitionState::IDLE,
				[](const FsmContext& c){ return !c.facePresent; },
				/*minDwellMs=*/P.idleNoFaceMs
		});



		// RECOGNIZING -> DUPLICATE_FACE
		fsm.addTransition({
				"recognizing->duplicate",
				RecognitionState::RECOGNIZING, RecognitionState::DUPLICATE_FACE,
				[] (const FsmContext& c) { return c.isDuplicate; },
				/*minDwellMs=*/P.duplicateHoldMs
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


