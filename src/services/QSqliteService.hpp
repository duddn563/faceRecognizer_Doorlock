#pragma once
#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QVector>
#include <QString>
#include <QMutex>
#include "include/LogDtos.hpp"


class QSqliteService {
public:
    bool initializeDatabase();

    // 기존 명세 유지
    bool insertAuthLog(const QString& userName,
                       const QString& message,
                       const QDateTime& timestamp,
                       const QByteArray& image);

    // 추가: 시스템로그 입력
    bool insertSystemLog(int level, const QString& tag, const QString& message,
                         const QDateTime& timestamp, const QString& extra = QString());

    // 선택: 조회(페이징/필터) — 필요시 사용
    bool selectAuthLogs(int offset, int limit,
                        const QString& userLike,
                        QVector<AuthLog>* outRows,
                        int* outTotal);

    bool selectSystemLogs(int offset, int limit,
                          int minLevel, const QString& tagLike, const QString& sinceIso,
                          QVector<SystemLog>* outRows,
                          int* outTotal);

	bool deleteAuthLogs();
	bool deleteSysLogs();
	bool deleteAllLogs();

private:
	QMutex dbMutex;
};

