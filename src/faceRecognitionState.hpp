// FaceRecognitionState.hpp
#pragma once

#include <QObject>

enum class RecognitionState {
    IDLE,
    DETECTING_PERSON,
    RECOGNIZING_FACE,
    REGISTERING,
		DUPLICATEDFACE,
    UNLOCKED
};

Q_DECLARE_METATYPE(RecognitionState)
