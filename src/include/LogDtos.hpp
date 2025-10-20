#pragma once
#include <QString>
#include <QDateTime>
#include <QByteArray>

// 인증 로그 DTO
struct AuthLog {
    int id{};
    QString userName;
    QString message;
    QDateTime timestamp;
    QByteArray imageBlob;
};

// 시스템 로그 DTO
struct SystemLog {
    int id{};
    int level{};        // 0~4
    QString tag;
    QString message;
    QDateTime timestamp;
    QString extra;
};

