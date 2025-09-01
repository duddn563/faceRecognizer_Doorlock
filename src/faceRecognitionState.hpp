// FaceRecognitionState.hpp
#pragma once

#include <QObject>

enum class RecognitionState {
    IDLE = 0,
		DOOR_OPEN,				// 1
		DETECTING,				// 2
    RECOGNIZING,			// 3
    REGISTERING,			// 4
    DUPLICATE_FACE,		// 5
    AUTH_SUCCESS,			// 6
    AUTH_FAIL,				// 7
    LOCKED_OUT				// 8
};

Q_DECLARE_METATYPE(RecognitionState)
