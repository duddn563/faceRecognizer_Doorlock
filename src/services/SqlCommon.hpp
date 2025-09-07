#pragma once
#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <QThread>

namespace SqlCommon {
inline const char* KBaseConnName = "doorlock";

    inline QString dbFilePath() 
    {
        const QString dir = "/root/trunk/faceRecognizer_Doorlock/assert/db";
        QDir().mkpath(dir);
        return dir + "/doorlock.db";
    }

    inline QString connectionNameForCurrentThread() 
    {
        return QString("%1_%2").arg(KBaseConnName)
                              .arg((qulonglong)QThread::currentThreadId());
    }
} // namespace SqlCommon

