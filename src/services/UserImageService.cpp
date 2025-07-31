#include "UserImageService.hpp"
#include <QDir>
#include <QFile>
#include <QDebug>
#include "presenter/UserImagePresenter.hpp"

#define USER_FACES_DIR                "/root/trunk/faceRecognizer_Doorlock/assert/face_images/"
#define USER_LABEL_FILE								"/root/trunk/faceRecognizer_Doorlock/assert/labels.txt"

UserImageService::UserImageService(UserImagePresenter* presenter)
	: presenter(presenter)
{ 
		//qDebug() << "[UserImageService] Constructor called, presenter ptr = " << presenter;	

}

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
		qDebug() << "[UserImageService] fetchUserList is called";
		if (!presenter) {
				qDebug() << "[UserImageService] presenter is nullptr";
				return;
		}

		QString filePath = QString::fromStdString(USER_LABEL_FILE);

		QFile file(filePath);
		QStringList users;

		if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
				qDebug() << "[UserImageService] Label.txt is not exist";
				return; 
		}
		else {
				while (!file.atEnd()) {
						QByteArray line = file.readLine();
						QString str(line);
						QStringList parts = str.trimmed().split(' ');
						if (parts.size() >= 2) {
								qDebug() << "[UserImageService] parts[0]:" << parts[0] << ", parts[1]:" << parts[1];
								users.append(parts[0] + ": " + parts[1]);
						}
				}
		}

		//qDebug() << "[UserImageService] presenter pointer:" << presenter;

		presenter->presentUserList(users);		
}

bool UserImageService::deleteImage(const QString& path)
{
		return QFile::remove(path);
}
