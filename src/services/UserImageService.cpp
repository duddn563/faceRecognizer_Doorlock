#include "UserImageService.hpp"
#include <QDir>
#include <QFile>

#define USER_FACES_DIR                "/root/trunk/faceRecognizer_Doorlock/assert/face_images/"

QList<UserImage> UserImageService::getUserImages() 
{
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

bool UserImageService::deleteImage(const QString& path)
{
		return QFile::remove(path);
}
