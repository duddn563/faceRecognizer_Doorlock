#pragma once
#include <QObject>

namespace States {
	enum class BleState  { Idle, Scanning, Connected, Disconnected };
	enum class DoorState { Locked, Open };
}


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

Q_DECLARE_METATYPE(RecognitionState)
Q_DECLARE_METATYPE(States::BleState)
Q_DECLARE_METATYPE(States::DoorState)

