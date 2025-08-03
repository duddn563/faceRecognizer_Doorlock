#include "UserImagePresenter.hpp"
#include <QPointer>

UserImagePresenter::UserImagePresenter(UserImageService* service, MainWindow* view)
		: service(service), QObject(view), view(view) 
{
		qDebug() << "[UserImagePresenter] constructor.";
}

void UserImagePresenter::onShowImages()
{
		qDebug() << "[UserImagePresenter] handleShowImages called";
		QList<UserImage> images = UserImageService::getUserImages();

		if (images.isEmpty()) {
				view->showInfo("정보", "등록된 이미지가 없습니다.");
				return;
		}

		view->showUserImageGallery(images);
}

void UserImagePresenter::handleDeleteImage(const QString& imagePath)
{
		if (UserImageService::deleteImage(imagePath)) {
				QPointer<QDialog> dialogToClose = view->getGalleryDialog();
				if (dialogToClose && dialogToClose->isVisible()) {
						dialogToClose->close();
						QTimer::singleShot(150, this, &UserImagePresenter::onShowImages);
				} else {
						onShowImages();
				}

		} else {
				view->showError("삭제 실패", "이미지 삭제에 실패했습니다.");
		}
}

void UserImagePresenter::handleImagePreview(const QString& imagePath) 
{
		qDebug() << "[UserImagePresenter] handleImagePreview is called";
		if (!view) return;

		if (QFile::exists(imagePath)) {
				view->showImagePreview(imagePath);
		} else {
				view->showInfo("오류", "이미지 파일이 존재하지 않습니다.");
		}
}

void UserImagePresenter::onShowUserList()
{
		qDebug() << "[UserImagePresenter] onShowUserList is called";
		service->fetchUserList();
}

void UserImagePresenter::presentUserList(const QStringList& users)
{
		qDebug() << "[UserImagePresenter] presenterUserList is called";
		if (!view) {
				qDebug() << "[UserImagePresenter] view pointer is nullptr";
				return;
		}
	
		qDebug() << "[UserImagePresenter] view pointer:" << view;
		view->showUserList(users);
}
