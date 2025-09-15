#pragma once

#include <QTimer>
#include <QPointer>
#include <QObject>
#include "gui/MainWindow.hpp"
#include "services/FaceRecognitionService.hpp"

class FaceRegisterPresenter : public QObject {
    Q_OBJECT

public:
    FaceRegisterPresenter(FaceRecognitionService* service, MainWindow* view, QObject* parent = nullptr);
    Q_INVOKABLE void onRegisterFace();

		void presentRegistration(bool success, const QString& message);

signals:
		void registrationStarted();
		void registrationResult(bool ok, const QString& message);

private slots:
		void onRegistrationCompleted(bool ok, const QString& msg);

private:
		//void connectService();		

		QPointer<FaceRecognitionService> service;
		MainWindow* view{nullptr};
		QTimer m_registerTimer;								// 위치독
		bool m_registerInProgress = false;		// 디바운스

};

