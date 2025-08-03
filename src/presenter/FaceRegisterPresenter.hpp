#pragma once

#include <QObject>
#include "gui/MainWindow.hpp"
#include "services/FaceRecognitionService.hpp"

class FaceRegisterPresenter : public QObject {
    Q_OBJECT

public:
    FaceRegisterPresenter(FaceRecognitionService* service, MainWindow* view, QObject* parent = nullptr);

		void presentRegistration(bool success, const QString& message);

public slots:
    void onRegisterFace();

signals:
		void registrationResult(bool success, const QString& message);
		void registrationStarted();

private:
		void connectService();		
	
    FaceRecognitionService* service;
    MainWindow* view;
};

