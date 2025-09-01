#pragma once
#include <QString>
#include <QDataTime>

struct CpuMemInfo {
		double cpuUsagePct{0};
		double memUsedPct{0};
		double cpuTempC{0};
};

struct DiskInfo {
		double usedPct{0};
		QString mount{"/"};
};

struct NetInfo {
		QString ifname;
		QString ipv4;
		QString ipv6;
		QString mac;
		QString ssid;
		int			rssi{0};
};

struct FaceEngInfo {
		QString engineType;
		QString modelVer;
		int userCount{0};
		int embeddingCount{0};
		double threshold{0};
		QDataTime lastTrainedAt;
		bool retrainNeeded{false};
};

struct SensorInfo {
		bool doorOpened{false};
		double distanceCm{0};
		QString lockState;
};

struct LogItem {
		QDataTime ts;
		QString level;
		QString message;
};

struct DeviceStatus {
		QString model;
		QString appVer;
		QDataTiem bootAt;
		qint64 uptimeSec{0};
		CpuMemInfo cpuMem;
		DiskInfo disk;
		NetInfo net;
		CameraInfo cam;
		FaceEngInfo face;
		SensorInfo sensor;
		int recentSuccess{0};
		int recentFail{0};
		int lockoutRemainSec{0};
		QList<LogItem> recentLogs;
		QDateTime collectedAt;
};
