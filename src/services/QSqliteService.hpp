#pragma once
/*
#include <iostream>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QBuffer>
#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QSqlDriver>


class QSqliteService : public QObject
{
    Q_OBJECT
public:
        static QSqliteService& instance() {
            static QSqliteService instance;
            return instance;
        }

        QSqliteService(const QSqliteService&) = delete;             // 복사 생성자 삭제, 다른 객체가 싱글톤 객체를 복사하지 못하도록 하여 싱글톤의 고유성 보장
        QSqliteService& operator=(const QSqliteService&) = delete;  // 복사 대입 연산자 삭제, 객체의 복사를 악아 싱글톤 보장
        ~QSqliteService();

        // Open DB
        bool openDatabase();

        // Close DB
        void closeDatabase();

        // Insert DB
        bool insertAuthLog(const QString& userName, const QString& message, const QDateTime& timestamp, const QByteArray& image = QByteArray());

private:
        explicit QSqliteService(QObject* parent = nullptr);

        bool initializeDatabase();
};
*/
#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QVector>
#include <QString>
#include "services/LogDtos.hpp"


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
};

