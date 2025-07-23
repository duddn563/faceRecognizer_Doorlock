#pragma once

#include <QThread>

#include "MainWindow.hpp"

#include "UserImagePresenter.hpp"
#include "DoorSensorPresenter.hpp"
#include "FaceSensorPresenter.hpp"
#include "FaceRecognitionPresenter.hpp"

#include "FaceSensorService.hpp"
#include "DoorSensorService.hpp"
#include "FaceRecognitionService.hpp"

class MainPresenter : public QObject {
		Q_OBJECT


public:
		MainPresenter(MainWindow* view);
		~MainPresenter();

		void startAllServices();

private:
		MainWindow* view;

		QThread* faceRecognitionThread;
		QThread* faceSensorThread;
		QThread* doorSensorThread;

		FaceRecognitionService* faceRecognitionService;
		FaceSensorService* faceSensorService;
		DoorSensorService* doorSensorService;

		FaceRecognitionPresenter* faceRecognitionPresenter;
		FaceSensorPresenter* faceSensorPresenter;
		DoorSensorPresenter* doorSensorPresenter;
		UserImagePresenter* userImagePresenter;

		void connectUIEvents();
		void onViewStateChanged();
};

