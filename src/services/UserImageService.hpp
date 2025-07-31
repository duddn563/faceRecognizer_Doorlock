#pragma once
#include <QString>
#include <QList>

class UserImagePresenter;

struct UserImage {
		QString filePath;
		QString userName;
};

class UserImageService {
		public:
				explicit UserImageService(UserImagePresenter* presenter);
				void setPresenter(UserImagePresenter* presenter);
				static QList<UserImage> getUserImages();
				static bool deleteImage(const QString& path);
				void fetchUserList();

		private:
				UserImagePresenter* presenter;
};
