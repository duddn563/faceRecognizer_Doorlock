#include "SystemLogger.hpp"
#include <QThread>
#include <QDebug>
#include "services/QSqliteService.hpp"
#include "log/SystemLogTypes.hpp"

namespace syslog_detail{
class SystemLogWriter : public QObject {
    Q_OBJECT
public slots:
    void append(const SystemLogEntry& e) {
        QSqliteService svc;
        svc.insertSystemLog(
            static_cast<int>(e.level),
            e.tag,
            e.message,
            e.ts.isValid() ? e.ts : QDateTime::currentDateTime(),
            e.extra
        );
    }
};		
} // namespace

SystemLogger& SystemLogger::instance() {
    static SystemLogger inst;
    return inst;
}

SystemLogger::SystemLogger(QObject* p) : QObject(p) {}

SystemLogger::~SystemLogger() {}

void SystemLogger::init() 
{
    static bool inited = false;
    if (inited) return;

    qRegisterMetaType<SystemLogEntry>("SystemLogEntry");


	auto& inst = instance();
	inst.th = new QThread;
	inst.wr = new syslog_detail::SystemLogWriter;
	inst.wr->moveToThread(inst.th);

    QObject::connect(&inst, &SystemLogger::appendRequested,
                     inst.wr, &syslog_detail::SystemLogWriter::append, Qt::QueuedConnection);
    QObject::connect(inst.th, &QThread::finished, inst.wr, &QObject::deleteLater);
    inst.th->start();
    inited = true;
}

void SystemLogger::shutdown() {
	auto& inst = instance();
 	if (!inst.th) return;

	inst.th->quit();
    if (!inst.th->wait(3000)) {
        inst.th->terminate();
        inst.th->wait();
    }

    inst.th->deleteLater();
    inst.th = nullptr;
	inst.wr = nullptr;
}

static void post(SysLogLevel lv, const QString& tag, const QString& msg, const QString& extra) {
    SystemLogEntry e{lv, tag, msg, QDateTime::currentDateTime(), extra};
    emit SystemLogger::instance().appendRequested(e);
}
void SystemLogger::debug(const QString& tag, const QString& msg, const QString& extra){ post(SysLogLevel::Debug, tag, msg, extra); }
void SystemLogger::info (const QString& tag, const QString& msg, const QString& extra){ post(SysLogLevel::Info , tag, msg, extra); }
void SystemLogger::warn (const QString& tag, const QString& msg, const QString& extra){ post(SysLogLevel::Warn , tag, msg, extra); }
void SystemLogger::error(const QString& tag, const QString& msg, const QString& extra){ post(SysLogLevel::Error, tag, msg, extra); }
void SystemLogger::critical(const QString& tag, const QString& msg, const QString& extra){ post(SysLogLevel::Critical, tag, msg, extra); }

#include "SystemLogger.moc"

