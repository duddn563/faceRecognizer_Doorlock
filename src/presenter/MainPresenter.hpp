#pragma once

#include <QThread>

#include "MainWindow.hpp"

#include "services/LogDtos.hpp"

#include "UserImagePresenter.hpp"
#include "DoorSensorPresenter.hpp"
#include "FaceSensorPresenter.hpp"
#include "FaceRecognitionPresenter.hpp"
#include "FaceRegisterPresenter.hpp"

#include "FaceSensorService.hpp"
#include "DoorSensorService.hpp"
#include "FaceRecognitionService.hpp"
#include "UserImageService.hpp"
#include "QSqliteService.hpp"

class MainPresenter : public QObject {
		Q_OBJECT


public:
		MainPresenter(MainWindow* view, QObject* parent = nullptr);
		~MainPresenter();

		void startAllServices();
		//QSqliteService* db_() const { return db_; }
        QSqliteService* db_;

public slots:
    // Auth
    void requestAuthPage(int page, int pageSize, const QString& userLike);
    // System
    void requestSystemPage(int page, int pageSize, int minLevel,
                           const QString& tagLike, const QString& sinceIso);

signals:
    void deliverAuthLogs(const QVector<AuthLog>& rows, int page, int total, int pageSize);
    void deliverSystemLogs(const QVector<SystemLog>& rows, int page, int total, int pageSize);


private:
		MainWindow* view;

		QThread* faceRecognitionThread;
		QThread* faceSensorThread;
		QThread* doorSensorThread;

		FaceRecognitionService* faceRecognitionService;
		FaceSensorService* faceSensorService;
		DoorSensorService* doorSensorService;
		UserImageService* userImageService;

		FaceRecognitionPresenter* faceRecognitionPresenter;
		FaceRegisterPresenter* faceRegisterPresenter;
		FaceSensorPresenter* faceSensorPresenter;
		DoorSensorPresenter* doorSensorPresenter;
		UserImagePresenter* userImagePresenter;


		void connectUIEvents();
		void onViewStateChanged();
        bool wired = false;     // 중복 연결 방지
};

