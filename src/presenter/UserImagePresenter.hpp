#pragma once
#include <QObject>
#include <QString>
#include <QList>

#include "MainWindow.hpp"
#include "services/UserImageService.hpp"

class MainWindow;

class UserImagePresenter : public QObject {
		Q_OBJECT

public:
				explicit UserImagePresenter(MainWindow* view);


				void handleShowImages();
				void handleDeleteImage(const QString& imagePath);

public slots:
				void handleImagePreview(const QString& imagePath);

private:
				MainWindow* view;
};

