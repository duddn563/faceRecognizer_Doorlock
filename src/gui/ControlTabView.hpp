// ===========================
// File: src/gui/ControlTabView.hpp
// ===========================
#pragma once

#include <QWidget>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QDateTime>

// GUI 전용 View. Presenter는 나중에 연결.
// - 상태 표시용 public slots 제공 (필요한 카드별로 분리)
// - 액션 버튼 클릭 시 시그널 방출

class ControlTabView : public QWidget {
    Q_OBJECT
public:
    explicit ControlTabView(QWidget* parent=nullptr);

signals:
    // 우측 Actions 영역 버튼 시그널 (Presenter가 connect)
    void refreshRequested();
    void restartCameraRequested();
    void unlockRequested();
    void lockRequested();
    void retrainRequested();
    void exportLogsRequested();

public slots:
    // A. 시스템
    void showSystemInfo(const QString& model,
                        const QString& appVer,
                        qint64 uptimeSec,
                        double cpuTempC,
                        double cpuUsagePct,
                        double memUsedPct,
                        double diskUsedPct);

    // B. 네트워크
    void showNetworkInfo(const QString& ifname,
                         const QString& ipv4,
                         const QString& ipv6,
                         const QString& mac,
                         const QString& ssid,
                         int rssi);

    // C. 카메라
    void showCameraInfo(const QString& resolution,
                        double fps,
                        const QDateTime& lastFrameAt,
                        const QString& exposureMode);

    // D. 얼굴 인식 엔진
    void showFaceEngineInfo(const QString& engineType,
                            const QString& modelVer,
                            int userCount,
                            int embeddingCount,
                            double threshold,
                            const QDateTime& lastTrainedAt,
                            bool retrainNeeded);

    // E. 센서/액추에이터
    void showSensorInfo(bool doorOpened,
                        double distanceCm,
                        const QString& lockState);

    // F. 보안/통계
    void showSecurityInfo(int recentSuccess,
                          int recentFail,
                          int lockoutRemainSec);

    // G. 로그 요약 (가장 최근이 맨 위)
    void showRecentLogs(const QList<QPair<QDateTime, QString>>& items);

    // 액션 결과 토스트/라벨
    void showActionResult(const QString& action, bool ok, const QString& message);

private:
    // 그룹 빌더
    QGroupBox* buildSystemBox();
    QGroupBox* buildNetworkBox();
    QGroupBox* buildCameraBox();
    QGroupBox* buildFaceBox();
    QGroupBox* buildSensorBox();
    QGroupBox* buildSecurityBox();
    QGroupBox* buildActionsBox();
    QGroupBox* buildLogsBox();

    // 라벨 헬퍼
    static QLabel* v(const QString& text="-");
    static QString pct(double x) { return QString::number(x, 'f', 1) + "%"; }
    static QString celsius(double c) { return QString::number(c, 'f', 1) + "°C"; }

    // 상태 라벨 보관
    // System
    QLabel *lbModel_, *lbAppVer_, *lbUptime_, *lbCpuTemp_, *lbCpu_, *lbMem_, *lbDisk_;
    // Network
    QLabel *lbIfname_, *lbIPv4_, *lbIPv6_, *lbMac_, *lbSsid_, *lbRssi_;
    // Camera
    QLabel *lbRes_, *lbFps_, *lbLastFrame_, *lbExposure_;
    // Face
    QLabel *lbEngine_, *lbModelVer_, *lbUsers_, *lbEmbeds_, *lbThresh_, *lbLastTrain_, *lbRetrainNeed_;
    // Sensor
    QLabel *lbDoor_, *lbDist_, *lbLock_;
    // Security
    QLabel *lbSucc_, *lbFail_, *lbLockout_;

    // 액션 결과
    QLabel *lbActionMsg_{};

    // 로그 테이블 (시간, 메시지)
    QTableWidget* tblLogs_{};
};

