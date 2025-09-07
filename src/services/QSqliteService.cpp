#include "QSqliteService.hpp"
#include "services/SqlCommon.hpp"

#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QDebug>

using namespace SqlCommon;

// 무상태: 호출 시점에 동일 커넥션 확보
static QSqlDatabase ensureOpenConnectionForThisThread() {
    const QString name = SqlCommon::connectionNameForCurrentThread();
    QSqlDatabase db;

    if (!QSqlDatabase::contains(name)) {
        db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(SqlCommon::dbFilePath());
    } else {
        db = QSqlDatabase::database(name);
        if (db.databaseName().isEmpty())
            db.setDatabaseName(SqlCommon::dbFilePath());
    }
    if (!db.isOpen()) db.open();
    return db;
}

bool QSqliteService::initializeDatabase()
{
    QSqlDatabase db = ensureOpenConnectionForThisThread();
    if (!db.isOpen()) {
        qCritical() << "[SQL] Open failed:" << db.lastError().text()
                    << " path=" << db.databaseName();
        return false;
    }

    {   // 신뢰성 옵션
        QSqlQuery pragma(db);
        pragma.exec("PRAGMA journal_mode=WAL;");
        pragma.exec("PRAGMA synchronous=NORMAL;");
    }

    QSqlQuery q(db);

    // 인증로그
    if (!q.exec(
        "CREATE TABLE IF NOT EXISTS auth_logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "user_name TEXT NOT NULL, "
        "message   TEXT NOT NULL, "
        "timestamp TEXT NOT NULL, "
        "image     BLOB)"
    )) {
        qCritical() << "Failed to create auth_logs:" << q.lastError().text();
        return false;
    }

    // 시스템로그
    if (!q.exec(
        "CREATE TABLE IF NOT EXISTS system_logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "level INTEGER NOT NULL, "
        "tag TEXT, "
        "message TEXT NOT NULL, "
        "timestamp TEXT NOT NULL, "
        "extra TEXT)"
    )) {
        qCritical() << "Failed to create system_logs:" << q.lastError().text();
        return false;
    }

    // 인덱스
    q.exec("CREATE INDEX IF NOT EXISTS idx_auth_ts   ON auth_logs(timestamp)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_auth_user ON auth_logs(user_name)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_sys_ts    ON system_logs(timestamp)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_sys_level ON system_logs(level)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_sys_tag   ON system_logs(tag)");

    qDebug() << "[SQL] Database opened & schema ready. path=" << db.databaseName()
             << " driver=" << db.driverName();
    return true;
}

bool QSqliteService::insertAuthLog(const QString& userName,
                                   const QString& message,
                                   const QDateTime& timestamp,
                                   const QByteArray& image)
{
    QSqlDatabase db = ensureOpenConnectionForThisThread();
    if (!db.isOpen()) {
        qCritical() << "[SQL] DB open failed:" << db.lastError().text();
        return false;
    }

    const QString nameSafe =  userName.isNull() ? QString("") : userName;
    const QString msgSafe  =  message.isNull() ? QString("") : message;
    const QString timeSafe = timestamp.isValid() 
                    ? timestamp.toString(Qt::ISODateWithMs)
                    : QDateTime::currentDateTime().toString(Qt::ISODateWithMs);

    QSqlQuery q(db);
    q.prepare("INSERT INTO auth_logs (user_name, message, timestamp, image) "
              "VALUES (?, ?, ?, ?)");
    q.addBindValue(nameSafe);
    q.addBindValue(msgSafe);
    q.addBindValue(timeSafe);
    q.addBindValue(image);

    if (!q.exec()) {
        qCritical() << "Insert auth log failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool QSqliteService::insertSystemLog(int level, const QString& tag, const QString& message,
                                     const QDateTime& timestamp, const QString& extra)
{
    QSqlDatabase db = ensureOpenConnectionForThisThread();
    if (!db.isOpen()) {
        qCritical() << "[SQL] DB open failed:" << db.lastError().text();
        return false;
    }

    QSqlQuery q(db);
    q.prepare("INSERT INTO system_logs (level, tag, message, timestamp, extra) "
              "VALUES (?, ?, ?, ?, ?)");
    q.addBindValue(level);
    q.addBindValue(tag);
    q.addBindValue(message);
    q.addBindValue(timestamp.toString(Qt::ISODateWithMs));
    q.addBindValue(extra);

    if (!q.exec()) {
        qCritical() << "Insert system log failed:" << q.lastError().text();
        return false;
    }
    return true;
}

// ---------- 조회 (선택) ----------
bool QSqliteService::selectAuthLogs(int offset, int limit,
                                    const QString& userLike,
                                    QVector<AuthLog>* outRows,
                                    int* outTotal)
{
	qDebug() << __func__;
    QSqlDatabase db = ensureOpenConnectionForThisThread();
    if (!db.isOpen()) return false;

    // total
    {
        QSqlQuery qc(db);
        if (userLike.isEmpty()) {
            qc.prepare("SELECT COUNT(*) FROM auth_logs");
        } else {
            qc.prepare("SELECT COUNT(*) FROM auth_logs WHERE user_name LIKE ?");
            qc.addBindValue("%" + userLike + "%");
        }
        if (!qc.exec() || !qc.next()) return false;
        if (outTotal) *outTotal = qc.value(0).toInt();
    }

    QSqlQuery q(db);
    if (userLike.isEmpty()) {
        q.prepare("SELECT id, user_name, message, timestamp, image "
                  "FROM auth_logs ORDER BY id DESC LIMIT ? OFFSET ?");
    } else {
        q.prepare("SELECT id, user_name, message, timestamp, image "
                  "FROM auth_logs WHERE user_name LIKE ? "
                  "ORDER BY id DESC LIMIT ? OFFSET ?");
        q.addBindValue("%" + userLike + "%");
    }
    q.addBindValue(limit);
    q.addBindValue(offset);

    if (!q.exec()) {
		qDebug() << "[QSQL] sql execute failed!";
		return false;
	}

    if (outRows) {
        outRows->clear();
        while (q.next()) {
            AuthLog r;
            r.id        = q.value(0).toInt();
            r.userName  = q.value(1).toString();
            r.message   = q.value(2).toString();
            r.timestamp = QDateTime::fromString(q.value(3).toString(), Qt::ISODateWithMs);
            r.imageBlob = q.value(4).toByteArray();
            outRows->push_back(r);
        }
    }
    return true;
}

bool QSqliteService::selectSystemLogs(int offset, int limit,
                                      int minLevel, const QString& tagLike, const QString& sinceIso,
                                      QVector<SystemLog>* outRows, int* outTotal)
{
    QSqlDatabase db = ensureOpenConnectionForThisThread();
    if (!db.isOpen()) return false;

    QString where = "WHERE level >= ?";
    QList<QVariant> bc; bc << minLevel;
    QList<QVariant> br; br << minLevel;

    if (!tagLike.isEmpty()) { where += " AND tag LIKE ?";      bc << ("%"+tagLike+"%"); br << ("%"+tagLike+"%"); }
    if (!sinceIso.isEmpty()){ where += " AND timestamp >= ?";  bc << sinceIso;          br << sinceIso; }

    // total
    QSqlQuery qc(db);
    qc.prepare("SELECT COUNT(*) FROM system_logs " + where);
    for (auto& v : bc) qc.addBindValue(v);
    if (!qc.exec() || !qc.next()) return false;
    if (outTotal) *outTotal = qc.value(0).toInt();

    // rows
    QSqlQuery q(db);
    q.prepare("SELECT id, level, tag, message, timestamp, extra "
              "FROM system_logs " + where + " ORDER BY id DESC LIMIT ? OFFSET ?");
    for (auto& v : br) q.addBindValue(v);
    q.addBindValue(limit);
    q.addBindValue(offset);

    if (!q.exec()) return false;

    if (outRows) {
        outRows->clear();
        while (q.next()) {
            SystemLog r;
            r.id        = q.value(0).toInt();
            r.level     = q.value(1).toInt();
            r.tag       = q.value(2).toString();
            r.message   = q.value(3).toString();
            r.timestamp = QDateTime::fromString(q.value(4).toString(), Qt::ISODateWithMs);
            r.extra     = q.value(5).toString();
            outRows->push_back(r);
        }
    }
    return true;
}


