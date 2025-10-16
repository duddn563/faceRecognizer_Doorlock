// FaceRecognitionState.hpp
#pragma once

#include <QObject>

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
