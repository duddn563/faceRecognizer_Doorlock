#pragma once

#include <QThread>

#include "MainWindow.hpp"

#include "services/LogDtos.hpp"

#include "UserImagePresenter.hpp"
#include "FaceRecognitionPresenter.hpp"
#include "FaceRegisterPresenter.hpp"

#include "FaceRecognitionService.hpp"
#include "UserImageService.hpp"
#include "QSqliteService.hpp"

#include "capture/FrameCapture.hpp"
#include "ble/BleServer.hpp"
#include "include/capture/LatestFrameMailbox.hpp"

class MainPresenter : public QObject {
		Q_OBJECT

public:
		MainPresenter(MainWindow* view, QObject* parent = nullptr);
		~MainPresenter();

		void startAllServices();
    QSqliteService* db_;

		void startBle();
		void stopBle();

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
		QThread* doorSensorThread;
		QThread* bleThread;

		FaceRecognitionService* faceRecognitionService;
		UserImageService* userImageService;
		BleServer* bleServer;

		FaceRecognitionPresenter* faceRecognitionPresenter;
		FaceRegisterPresenter* faceRegisterPresenter;
		UserImagePresenter* userImagePresenter;

		std::unique_ptr<BleServer> ble_;
		std::unique_ptr<FrameCapture> capture_;
		LatestFrameMailbox mailbox_;	


		void connectUIEvents();
		void onViewStateChanged();
    bool wired = false;     // 중복 연결 방지
};

