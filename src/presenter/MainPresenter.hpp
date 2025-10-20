#pragma once

#include <QThread>

#include "MainWindow.hpp"

//#include "services/LogDtos.hpp"

#include "UserImagePresenter.hpp"
#include "FaceRecognitionPresenter.hpp"
#include "FaceRegisterPresenter.hpp"

#include "FaceRecognitionService.hpp"
#include "UserImageService.hpp"
#include "QSqliteService.hpp"

#include "ble/BleServer.hpp"
//#include "include/LogDtos.hpp"

class MainPresenter : public QObject {
		Q_OBJECT

public:
		MainPresenter(MainWindow* view, QObject* parent = nullptr);
		~MainPresenter();

		void startAllServices();

		void startBle();
		void stopBle();

		bool onSelectAuthLogs(int offset, int limit, const QString& tagLike, 
							  QVector<AuthLog>* outRows, int *outTotal);
		bool onSelectSystemLogs(int offset, int limit, int minLevel, const QString& tagLike, const QString& sinceIso,
                                QVector<SystemLog>* outRows, int* outTotal);
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
		QThread* bleThread;
		FaceRecognitionPresenter* faceRecognitionPresenter;
		UserImagePresenter* userImagePresenter;


		FaceRecognitionService* faceRecognitionService;
		UserImageService* userImageService;
		QSqliteService* db_;
		BleServer* bleServer;

		void connectUIEvents();
		void onViewStateChanged();

		void stopFaceEngine();
		void stopImageService();
};

