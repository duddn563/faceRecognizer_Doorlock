#pragma once
#include <QString>
#include <QList>

struct UserImage {
		QString filePath;
		QString userName;
};

class UserImageService {
		public:
				static QList<UserImage> getUserImages();
				static bool deleteImage(const QString& path);
};
