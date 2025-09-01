#pragma once
#include <QObject>
#include <QString>
#include <QList>

#include "MainWindow.hpp"
#include "services/UserImageService.hpp"

class MainWindow;
class UserImageService;

class UserImagePresenter : public QObject {
		Q_OBJECT

public:
				// === Constructor ===	
				explicit UserImagePresenter(UserImageService* service, MainWindow* view);

				// === MainWindow에서 호출하는 메서드 ===
								void presentUserList(const QStringList& users);

public slots:
				void handleImagePreview(const QString& imagePath);
				void onShowImages();
				void handleDeleteImage(const QString& imagePath);
				void onShowUserList();

private:
				MainWindow* view;
				UserImageService* service;
};

