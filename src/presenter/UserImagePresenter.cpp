#include "UserImagePresenter.hpp"

UserImagePresenter::UserImagePresenter(MainWindow* view)
		: QObject(view), view(view) 
{
		std::cout << "[UserImagePresenter] constructor." << std::endl;
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
		if (!view) return;

		if (QFile::exists(imagePath)) {
				view->showImagePreview(imagePath);
		} else {
				view->showInfo("오류", "이미지 파일이 존재하지 않습니다.");
		}
					
}


