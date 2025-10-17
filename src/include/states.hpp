#pragma once
#include <QObject>

namespace States {
	enum class BleState  { Idle, Scanning, Connected, Disconnected };
	enum class DoorState { Locked, Open };
}

Q_DECLARE_METATYPE(States::BleState)
Q_DECLARE_METATYPE(States::DoorState)

