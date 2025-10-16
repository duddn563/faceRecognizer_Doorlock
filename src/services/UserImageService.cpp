#include "UserImageService.hpp"
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QCoreApplication>

#include "include/common_path.hpp"
#include "presenter/UserImagePresenter.hpp"

UserImageService::UserImageService(UserImagePresenter* presenter)
	: presenter(presenter){ }

void UserImageService::setPresenter(UserImagePresenter* p) { presenter = p; }

QList<UserImage> UserImageService::getUserImages() 
{
		qDebug() << "[UserImageService] Initialization User image service";
		QList<UserImage> list;
		QDir dir(USER_FACES_DIR);
		QStringList files = dir.entryList(QStringList() << "*.png" << "*.jpg", QDir::Files);

		for (const QString& file : files) {
				UserImage img;
				img.filePath = dir.filePath(file);
				img.userName = file.section('_', 2, 2);
				list.append(img);
		}

		return list;
}

void UserImageService::fetchUserList()
{
		if (!presenter) {
				qDebug() << "[UserImageService] presenter is nullptr";
				return;
		}

		const QString filePath = QDir(QCoreApplication::applicationDirPath()).filePath(EMBEDDING_JSON_PATH) + EMBEDDING_JSON;

		QFile file(filePath);
		QStringList users;

		if (!file.exists()) {
				qWarning() << "[UserImageService]" << filePath << " not found";
				presenter->presentUserList(users);
				return;
		}

		if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
				qDebug() << "[UserImageService] failed to open" << filePath << ":" <<  file.errorString();
				presenter->presentUserList(users);
				return; 
		}

		const QByteArray raw = file.readAll();
		file.close();

		QJsonParseError perr;
		const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
		if (perr.error != QJsonParseError::NoError) {
				qWarning() << "[UserImageService] JSON parse error:" << perr.errorString();
				presenter->presentUserList(users);
				return;
		}

		auto appendUser = [&](int id, const QString& name) {
			users.append(QStringLiteral("%1: %2").arg(id).arg(name));
			qDebug() << "[fetchUserList] user:" << id << name;
		};

		if (doc.isObject()) {
			const QJsonObject root = doc.object();
			const QJsonArray items = root.value(QStringLiteral("items")).toArray();
			for (const QJsonValue& v : items) {
				const QJsonObject o = v.toObject();
				const int id = o.value(QStringLiteral("id")).toInt(-1);
				const QString name = o.value(QStringLiteral("name")).toString(QStringLiteral("Unknown"));
				if (id >= 0) appendUser(id, name);
			}
		}
		else if (doc.isArray()) {
			const QJsonArray arr = doc.array();
			for (const QJsonValue& v : arr) {
				const QJsonObject o = v.toObject();
				const int id = o.value("id").toInt(-1);
				const QString name = o.value("name").toString(QStringLiteral("Unknown"));
				const int dim = o.value("embedding").toArray().size();

				users.append(QString("%1: %2 (%3D)").arg(id).arg(name).arg(dim));
				qDebug() << "[UserImageService] user: " << id << name << "dim=" << dim;
			}
		}
		else {
			qWarning() << "[fetchUser] unsupported JSON root";
		}
			
		presenter->presentUserList(users);		
}

bool UserImageService::deleteImage(const QString& path)
{
		return QFile::remove(path);
}
