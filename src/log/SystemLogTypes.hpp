#pragma once
#include <QString>
#include <QDateTime>
#include <QMetaType>

enum class SysLogLevel { Debug=0, Info=1, Warn=2, Error=3, Critical=4 };

struct SystemLogEntry {
    SysLogLevel level;
    QString tag;        // 예: "FRS", "FSM", "HW", "UI"
    QString message;
    QDateTime ts;
    QString extra;
};

Q_DECLARE_METATYPE(SystemLogEntry)

