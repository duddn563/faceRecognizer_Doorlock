// FaceRecognitionState.hpp
#pragma once

#include <QObject>

enum class RecognitionState {
    IDLE = 0,
		DOOR_OPEN,
		DETECTING,
    RECOGNIZING,
    REGISTERING,
    DUPLICATE_FACE,
    AUTH_SUCCESS,
    AUTH_FAIL,
    LOCKED_OUT
};

Q_DECLARE_METATYPE(RecognitionState)
