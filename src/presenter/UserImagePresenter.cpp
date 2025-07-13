#include "UserImagePresenter.hpp"

UserImagePresenter::UserImagePresenter(MainWindow* view)
		: QObject(view), view(view) 
{
		connect(view, &MainWindow::showUserImagesRequested, this, &UserImagePresenter::handleShowImages);
}

void UserImagePresenter::handleShowImages()
{
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
				handleShowImages();
		} else {
				view->showError("삭제 실패", "이미지 삭제에 실패했습니다.");
		}
}

void UserImagePresenter::handleImagePreview(const QString& imagePath) 
{
		view->showImagePreview(imagePath);
}

