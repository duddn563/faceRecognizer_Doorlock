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
				explicit UserImagePresenter(UserImageService* service, MainWindow* view);


				void onShowImages();
				void handleDeleteImage(const QString& imagePath);
				void onShowUserList();
				void presentUserList(const QStringList& users);

public slots:
				void handleImagePreview(const QString& imagePath);

private:
				MainWindow* view;
				UserImageService* service;
};

