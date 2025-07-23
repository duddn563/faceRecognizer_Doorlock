#include "UserImagePresenter.hpp"

UserImagePresenter::UserImagePresenter(MainWindow* view)
		: QObject(view), view(view) 
{
		std::cout << "[UserImagePresenter] constructor." << std::endl;
		//connect(view, &MainWindow::showUserImagesRequested, this, &UserImagePresenter::handleShowImages);

	/*
		connect(view->showUserImages, &QPushButton::clicked, this, [=]() {
					qDebug() << "사용자 이미지 버튼 클릭됨";
					emit showUserImagesRequested();
		});
	*/

		//connect(this, &MainWindow::imageClicked, this, &UserImagePresenter::handleImagePreview);
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

