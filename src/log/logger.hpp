// logger.h
#pragma once
#include <string>
#include <QString>
#include <QDebug>
#include <QtGlobal>

#define LOG_DIR		"/var/log/face_doorlock"
#define LOG_FILE	LOG_DIR "/log.txt" 

class Logger {
public:
    static void write(const std::string& message);

		static void writef(const char* format, ...);
};

namespace GlobalLogger {

inline void logMessage(QtMsgType type, const QString& functionName, const QString& message)
{
		QString fullMsg = QString("[%1] %2").arg(functionName, message);

		switch (type) {
			case QtDebugMsg:
					qDebug() << fullMsg;
					break;
			case QtInfoMsg:
					qInfo() << fullMsg;
					break;
			case QtCriticalMsg:
					qCritical() << fullMsg;
					break;
			case QtFatalMsg:
					qFatal("%s", fullMsg.toUtf8().constData());
					break;
		}
}

}		// namespace GlobalLogger

#define LOG_DEBUG(msg)		GlobalLogger::logMessage(QtDebugMsg, __FUNCTION__, msg)
#define LOG_INFO(msg)			GlobalLogger::logMessage(QtInfoMsg, __FUNCTION__, msg)
#define LOG_WARN(msg)			GlobalLogger::logMessage(QtWarningMsg, __FUNCTION__, msg)
#define LOG_CRITICAL(msg) GlobalLogger::logMessage(QtCriticalMsg, __FUNCTION__, msg)

