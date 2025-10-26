#pragma once
#include <QObject>

namespace States {
	enum class BleState  { Idle, Scanning, Connected, Disconnected };
	enum class DoorState { Locked, Open };
}
enum class DetectedStatus { 
	FaceDetected,		// 정상 감지
	FaceNotDetected,	// 감지 안됨
	Recognized,			// 인식정상
	AuthSuccessed,		// 인증 성공
	AuthFailed,			// 인증 실패

	// Registration
	Registering,		// 등록
	PoseFront,			// 정면
	PoseYawLeft,		// 머리를 약간 왼쪽으로
	PoseYawRight,		// 머리를 약간 오른쪽으로
	PosePitchUp,		// 턱을 약갼 올리기(고개 들기)
	PosePitchDown,		// 턱을 약간 내리기(고개 숙이기)
	ForceAbortRegistration,	// 등록 타임아웃
	FailedRegistration,		// 등록 실패
	TimeoutRegistration,	// 등록 타임 아웃
	RetryRegistration,		// 등록 재시도	
	CompleteRegistration,	// 등록 완료
	DuplicateFace,			// 중복 얼굴
	

	
	// Quality out
	CenterOff,			// 화면 중앙에서 벗어남
	TooSmall,			// 얼굴 박스가 너무 작음/멀다
	TooBlurry,			// 흔들림/초점 불량
	TooDark,			// 조도 부족
	LowContrast			// 중앙부 대비 약함(얼굴 중앙이 흐릿)
};



enum class RecognitionState {
    IDLE = 0,
	DOOR_OPEN,				// 1
	WAIT_CLOSE,				// 2
	DETECTING,				// 3
    RECOGNIZING,			// 4
    REGISTERING,			// 5
    DUPLICATE_FACE,		    // 6
    AUTH_SUCCESS,			// 7
    AUTH_FAIL,				// 8
    LOCKED_OUT				// 9
};

Q_DECLARE_METATYPE(DetectedStatus)
Q_DECLARE_METATYPE(RecognitionState)
Q_DECLARE_METATYPE(States::BleState)
Q_DECLARE_METATYPE(States::DoorState)

