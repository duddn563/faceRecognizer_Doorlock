#pragma once
#include <QObject>
#include <QThread>

#include "SystemLogTypes.hpp"

namespace syslog_detail { class SystemLogWriter; }

class SystemLogger final : public QObject {
    Q_OBJECT
public:
    static SystemLogger& instance();
    static void init();      // 앱 시작시 1회
    static void shutdown();  // 선택

    // 어디서든 한 줄로 호출
    static void debug(const QString& tag, const QString& msg, const QString& extra = {});
    static void info (const QString& tag, const QString& msg, const QString& extra = {});
    static void warn (const QString& tag, const QString& msg, const QString& extra = {});
    static void error(const QString& tag, const QString& msg, const QString& extra = {});
    static void critical(const QString& tag, const QString& msg, const QString& extra = {});

signals:
    void appendRequested(const SystemLogEntry& e); // 워커에게 보냄

private:
	QThread* th = nullptr;
	syslog_detail::SystemLogWriter* wr = nullptr;
   

    explicit SystemLogger(QObject* parent=nullptr);
    ~SystemLogger() override;
};

