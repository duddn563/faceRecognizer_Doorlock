#ifndef BLESERVER_HPP
#define BLESERVER_HPP
#include <QCoreApplication>
#include <QtBluetooth/QLowEnergyAdvertisingData>
#include <QtBluetooth/QLowEnergyAdvertisingParameters>
#include <QtBluetooth/QLowEnergyCharacteristicData>
#include <QtBluetooth/QLowEnergyDescriptorData>
#include <QtBluetooth/QLowEnergyServiceData>
#include <QtBluetooth/QLowEnergyController>
#include <QtBluetooth/QBluetoothUuid>
#include <QDateTime>
#include <QTimer>
#include <QDebug>

#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSaveFile>
#include <QThread>
#include <QtGlobal>
#include <cmath>
#include <QProcess>

#include "AuthLogRepo.h"
#include "services/FaceRecognitionService.hpp"
#include "include/states.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>


// Utile
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

extern const QBluetoothUuid SERVICE_UUID;   
extern const QBluetoothUuid CHAR_CMD_UUID; 
extern const QBluetoothUuid CHAR_STATE_UUID;		
extern const QBluetoothUuid CHAR_NOTIFY_UUID;

class AuthLogRepo;

class BleServer : public QObject {
	Q_OBJECT
	public:
		explicit BleServer(QObject* parent = nullptr, FaceRecognitionService* recogServ = nullptr);
		void resetStackOnce(const char* hci);
		void setupGatt_();
		void startAdvertising_();


		~BleServer();

		// 스레드에서 호출할 시작/정지 API
		public slots:
			void run();					// QThread::started 연결
		void stop();				// 안전 종료

		// 구성
	public:
		void setInterfaceName(const QString& ifname) { hciName_ = ifname; }
		void setDbPath(const QString& p) { dbPath_ = p; }

signals:
		void ready();			// 광고까지 시작 완료
		void errorHappened(QString msg);
		void log(QString linie);

		void bleStateChanged(States::BleState s);

	private:
		//내부 핼퍼
		void teardown_();

		// QObject::connect connection
		void addConnect_();

		// characteristic 핸들러 분리
		void handleCommand_(const QString& s);

	private:
		FaceRecognitionService* service;
		void sendJsonLine(const QJsonObject& obj);
		void sendCmdResult(const QString& cmd, bool ok, const QString& msg = QString(), const QJsonObject& extra = {});
		int  sh(const QString& cmd);
		void sendFileOverBle(const QString& path, const QString& mime, const QString& name);

		QByteArray captureJpeg(int w, int h, int quality);
		QByteArray captureJpegOneShot(int w, int h, int quality); 

		void softRestartBle();
		void reset_ble_stack(const std::string& hci = "hci0");

		QJsonObject getInfoJson();
		QJsonObject getNetJson();
		QJsonObject getBtJson();

		// Utils
		QString readAll(const QString& path);
		QString runCmd(const QString& cmd);

		void run_cmds(const std::vector<std::string>& cmds, int delay_ms = 400);
		std::string run_cmd(const std::string& cmd, int *exit_code = nullptr);


	private:
		QScopedPointer<QLowEnergyController> g_peripheral_;
		QLowEnergyService*									 g_service_ = nullptr;
		std::unique_ptr<AuthLogRepo>				 g_auth_;

		// 상태
		bool started_ = false;

		QString hciName_ = QStringLiteral("hci0");
		QString dbPath_  = QStringLiteral("/root/trunk/faceRecognizer_Doorlock/assert/db/doorlock.db");
};
#endif // BLESERVER_HPP




