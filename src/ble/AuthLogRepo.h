#pragma once

#include <QtCore>
#include <QtSql>
#include <QJsonObject>
#include <QJsonArray>

struct AuthLogRow {
    int     id = 0;
    QString userName;
    QString message;
    QString timestamp;   // ISO8601 텍스트
    bool    hasImage = false;

    QJsonObject toJson() const {
        QJsonObject o;
        o["id"]       = id;
        o["user"]     = userName;
        o["message"]  = message;
        o["ts"]       = timestamp;
        o["hasImage"] = hasImage;
        return o;
    }
};

class AuthLogRepo {
public:
    explicit AuthLogRepo(QString dbPath,
                         QString connectionName = "authdb_readonly")
        : dbPath_(std::move(dbPath)), connName_(std::move(connectionName)) {}

    bool open() {
        if (QSqlDatabase::contains(connName_)) {
            db_ = QSqlDatabase::database(connName_);
            return db_.isOpen();
        }
        db_ = QSqlDatabase::addDatabase("QSQLITE", connName_);
        db_.setDatabaseName(dbPath_);

        // 읽기 전용으로 열기 (Qt 5에서 지원)
        db_.setConnectOptions("QSQLITE_OPEN_READONLY=1");

        if (!db_.open()) {
            qCritical() << "[AuthLogRepo] open fail:" << db_.lastError().text()
                        << " path=" << dbPath_;
            return false;
        }
        return true;
    }

    bool isOpen() const { return db_.isOpen(); }
		// AuthLogRepo 안에 추가
		QByteArray fetchImageBlob(int id) {
			if (!db_.isOpen()) return {};
			QSqlQuery q(db_);
			q.prepare("SELECT image FROM auth_logs WHERE id=?");
			q.addBindValue(id);
			if (!q.exec() || !q.next()) return {};
			return q.value(0).toByteArray();
		}


    // 최근 N개 (sinceIso가 있으면 그 이후만)
    QList<AuthLogRow> fetchRecent(int limit = 50, const QString& sinceIso = QString()) {
        QList<AuthLogRow> out;
        if (!db_.isOpen()) return out;

        QSqlQuery q(db_);
        if (sinceIso.isEmpty()) {
            q.prepare("SELECT id, user_name, message, timestamp, "
                      "CASE WHEN image IS NULL OR length(image)=0 THEN 0 ELSE 1 END AS has_img "
                      "FROM auth_logs "
                      "ORDER BY timestamp DESC "
                      "LIMIT ?");
            q.addBindValue(qMax(1, limit));
        } else {
            q.prepare("SELECT id, user_name, message, timestamp, "
                      "CASE WHEN image IS NULL OR length(image)=0 THEN 0 ELSE 1 END AS has_img "
                      "FROM auth_logs "
                      "WHERE timestamp >= ? "
                      "ORDER BY timestamp DESC "
                      "LIMIT ?");
            q.addBindValue(sinceIso);
            q.addBindValue(qMax(1, limit));
        }

        if (!q.exec()) {
            qWarning() << "[AuthLogRepo] select fail:" << q.lastError().text();
            return out;
        }
        while (q.next()) {
            AuthLogRow r;
            r.id        = q.value(0).toInt();
            r.userName  = q.value(1).toString();
            r.message   = q.value(2).toString();
            r.timestamp = q.value(3).toString();
            r.hasImage  = (q.value(4).toInt() == 1);
            out.push_back(r);
        }
        return out;
    }

    // 특정 id의 이미지(BLOB) 로드 (없으면 빈 QByteArray 반환)
    QByteArray loadImageBytes(int id) {
        if (!db_.isOpen()) return {};
        QSqlQuery q(db_);
        q.prepare("SELECT image FROM auth_logs WHERE id=?");
        q.addBindValue(id);
        if (!q.exec() || !q.next()) return {};
        return q.value(0).toByteArray();
    }

    // JSON 패키징 헬퍼
    static QJsonObject toAuthJson(const QList<AuthLogRow>& rows) {
        QJsonArray arr;
        for (const auto& r : rows) arr.push_back(r.toJson());
        QJsonObject j; j["type"]="auth"; j["count"]=arr.size(); j["entries"]=arr;
        return j;
    }

private:
    QString dbPath_;
    QString connName_;
    QSqlDatabase db_;
};

