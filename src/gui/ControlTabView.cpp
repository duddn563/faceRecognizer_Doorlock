// ===========================
// File: src/gui/ControlTabView.cpp
// ===========================
#include "ControlTabView.hpp"
#include <QGridLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QProcess>

static QString hhmmss(qint64 secs) {
    qint64 h = secs / 3600;
    qint64 m = (secs % 3600) / 60;
    qint64 s = secs % 60;
    return QString::asprintf("%02lld:%02lld:%02lld", h, m, s);
}

QLabel* ControlTabView::v(const QString& text) {
    auto* L = new QLabel(text);
    L->setObjectName("value");
    return L;
}

ControlTabView::ControlTabView(QWidget* parent) : QWidget(parent) {
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(12,12,12,12);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(12);

    // 좌측 컬럼(상태 카드들)
    auto* sysBox = buildSystemBox();
    auto* netBox = buildNetworkBox();
    auto* camBox = buildCameraBox();
    auto* faceBox = buildFaceBox();
    auto* sensorBox = buildSensorBox();
    auto* secBox = buildSecurityBox();

    // 우측 컬럼(액션 + 로그)
    auto* actBox = buildActionsBox();
    auto* logBox = buildLogsBox();

    // 배치: 2열 그리드
    grid->addWidget(sysBox,   0,0);
    grid->addWidget(netBox,   1,0);
    grid->addWidget(camBox,   2,0);
    grid->addWidget(faceBox,  3,0);
    grid->addWidget(sensorBox,4,0);
    grid->addWidget(secBox,   5,0);

    grid->addWidget(actBox,   0,1,2,1); // Actions는 약간 크게
    grid->addWidget(logBox,   2,1,4,1);

    setLayout(grid);
}

QGroupBox* ControlTabView::buildSystemBox(){
    auto* box = new QGroupBox("시스템");
    auto* form = new QFormLayout(box);
    lbModel_ = v(); lbAppVer_ = v(); lbUptime_ = v();
    lbCpuTemp_ = v(); lbCpu_ = v(); lbMem_ = v(); lbDisk_ = v();
    form->addRow("모델", lbModel_);
    form->addRow("앱 버전", lbAppVer_);
    form->addRow("업타임", lbUptime_);
    form->addRow("CPU 온도", lbCpuTemp_);
    form->addRow("CPU 사용률", lbCpu_);
    form->addRow("메모리", lbMem_);
    form->addRow("디스크", lbDisk_);
    return box;
}

QGroupBox* ControlTabView::buildNetworkBox(){
    auto* box = new QGroupBox("네트워크");
    auto* form = new QFormLayout(box);
    lbIfname_=v(); lbIPv4_=v(); lbIPv6_=v(); lbMac_=v(); lbSsid_=v(); lbRssi_=v();
    form->addRow("IF", lbIfname_);
    form->addRow("IPv4", lbIPv4_);
    form->addRow("IPv6", lbIPv6_);
    form->addRow("MAC", lbMac_);
    form->addRow("SSID", lbSsid_);
    form->addRow("RSSI", lbRssi_);
    return box;
}

QGroupBox* ControlTabView::buildCameraBox(){
    auto* box = new QGroupBox("카메라");
    auto* form = new QFormLayout(box);
    lbRes_=v(); lbFps_=v(); lbLastFrame_=v(); lbExposure_=v();
    form->addRow("해상도", lbRes_);
    form->addRow("FPS", lbFps_);
    form->addRow("마지막 프레임", lbLastFrame_);
    form->addRow("노출", lbExposure_);
    return box;
}

QGroupBox* ControlTabView::buildFaceBox(){
    auto* box = new QGroupBox("얼굴 인식 엔진");
    auto* form = new QFormLayout(box);
    lbEngine_=v(); lbModelVer_=v(); lbUsers_=v(); lbEmbeds_=v(); lbThresh_=v(); lbLastTrain_=v(); lbRetrainNeed_=v();
    form->addRow("엔진", lbEngine_);
    form->addRow("모델", lbModelVer_);
    form->addRow("사용자 수", lbUsers_);
    form->addRow("임베딩 수", lbEmbeds_);
    form->addRow("임계값", lbThresh_);
    form->addRow("마지막 학습", lbLastTrain_);
    form->addRow("재학습 필요", lbRetrainNeed_);
    return box;
}

QGroupBox* ControlTabView::buildSensorBox(){
    auto* box = new QGroupBox("센서/도어락");
    auto* form = new QFormLayout(box);
    lbDoor_=v(); lbDist_=v(); lbLock_=v();
    form->addRow("도어 스위치", lbDoor_);
    form->addRow("초음파(cm)", lbDist_);
    form->addRow("도어락", lbLock_);
    return box;
}

QGroupBox* ControlTabView::buildSecurityBox(){
    auto* box = new QGroupBox("보안 상태");
    auto* form = new QFormLayout(box);
    lbSucc_=v(); lbFail_=v(); lbLockout_=v();
    form->addRow("성공(최근)", lbSucc_);
    form->addRow("실패(최근)", lbFail_);
    form->addRow("락아웃 잔여", lbLockout_);
    return box;
}

QGroupBox* ControlTabView::buildActionsBox(){
    auto* box = new QGroupBox("Actions");
    auto* lay = new QVBoxLayout(box);

    auto* row1 = new QHBoxLayout();
    auto* btnRefresh = new QPushButton("새로고침");
    auto* btnRestartCam = new QPushButton("카메라 재시작");
    row1->addWidget(btnRefresh);
    row1->addWidget(btnRestartCam);

    auto* row2 = new QHBoxLayout();
    auto* btnUnlock = new QPushButton("도어 열기");
    auto* btnLock = new QPushButton("도어 잠그기");
    row2->addWidget(btnUnlock);
    row2->addWidget(btnLock);

    auto* row3 = new QHBoxLayout();
    auto* btnRetrain = new QPushButton("인식기 재학습");
    auto* btnExportLogs = new QPushButton("로그 내보내기");
    row3->addWidget(btnRetrain);
    row3->addWidget(btnExportLogs);

    lbActionMsg_ = new QLabel("Ready");
    lbActionMsg_->setWordWrap(true);

    lay->addLayout(row1);
    lay->addLayout(row2);
    lay->addLayout(row3);
    lay->addWidget(lbActionMsg_);
    lay->addStretch();

    // 시그널 연결 → Presenter가 외부에서 connect할 수도 있지만, View에서 직접 방출
    connect(btnRefresh,   &QPushButton::clicked, this, &ControlTabView::refreshRequested);
    connect(btnRestartCam,&QPushButton::clicked, this, &ControlTabView::restartCameraRequested);
    connect(btnUnlock,    &QPushButton::clicked, this, &ControlTabView::unlockRequested);
    connect(btnLock,      &QPushButton::clicked, this, &ControlTabView::lockRequested);
    connect(btnRetrain,   &QPushButton::clicked, this, &ControlTabView::retrainRequested);
    connect(btnExportLogs,&QPushButton::clicked, this, &ControlTabView::exportLogsRequested);

    return box;
}

QGroupBox* ControlTabView::buildLogsBox(){
    auto* box = new QGroupBox("최근 로그 (5)");
    auto* lay = new QVBoxLayout(box);

    tblLogs_ = new QTableWidget(0, 2, box);
    QStringList headers; headers << "시간" << "메시지";
    tblLogs_->setHorizontalHeaderLabels(headers);
    tblLogs_->horizontalHeader()->setStretchLastSection(true);
    tblLogs_->verticalHeader()->setVisible(false);
    tblLogs_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tblLogs_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tblLogs_->setSelectionMode(QAbstractItemView::SingleSelection);
    tblLogs_->setShowGrid(false);

    lay->addWidget(tblLogs_);
    return box;
}

void ControlTabView::showSystemInfo(const QString& model,
                                    const QString& appVer,
                                    qint64 uptimeSec,
                                    double cpuTempC,
                                    double cpuUsagePct,
                                    double memUsedPct,
                                    double diskUsedPct) {
    lbModel_->setText(model);
    lbAppVer_->setText(appVer);
    lbUptime_->setText(hhmmss(uptimeSec));
    lbCpuTemp_->setText(celsius(cpuTempC));
    lbCpu_->setText(pct(cpuUsagePct));
    lbMem_->setText(pct(memUsedPct));
    lbDisk_->setText(pct(diskUsedPct));
}

void ControlTabView::showNetworkInfo(const QString& ifname,
                                     const QString& ipv4,
                                     const QString& ipv6,
                                     const QString& mac,
                                     const QString& ssid,
                                     int rssi) {
    lbIfname_->setText(ifname);
    lbIPv4_->setText(ipv4);
    lbIPv6_->setText(ipv6);
    lbMac_->setText(mac);
    lbSsid_->setText(ssid);
    lbRssi_->setText(QString::number(rssi));
}

void ControlTabView::showCameraInfo(const QString& resolution,
                                    double fps,
                                    const QDateTime& lastFrameAt,
                                    const QString& exposureMode) {
    lbRes_->setText(resolution);
    lbFps_->setText(QString::number(fps, 'f', 1));
    lbLastFrame_->setText(lastFrameAt.isValid() ? lastFrameAt.toString("HH:mm:ss") : "-");
    lbExposure_->setText(exposureMode);
}

void ControlTabView::showFaceEngineInfo(const QString& engineType,
                                        const QString& modelVer,
                                        int userCount,
                                        int embeddingCount,
                                        double threshold,
                                        const QDateTime& lastTrainedAt,
                                        bool retrainNeeded) {
    lbEngine_->setText(engineType);
    lbModelVer_->setText(modelVer);
    lbUsers_->setText(QString::number(userCount));
    lbEmbeds_->setText(QString::number(embeddingCount));
    lbThresh_->setText(QString::number(threshold, 'f', 2));
    lbLastTrain_->setText(lastTrainedAt.isValid() ? lastTrainedAt.toString("yyyy-MM-dd HH:mm:ss") : "-");
    lbRetrainNeed_->setText(retrainNeeded ? "예" : "아니오");
}

void ControlTabView::showSensorInfo(bool doorOpened,
                                    double distanceCm,
                                    const QString& lockState) {
    lbDoor_->setText(doorOpened ? "열림" : "닫힘");
    lbDist_->setText(QString::number(distanceCm, 'f', 1));
    lbLock_->setText(lockState);
}

void ControlTabView::showSecurityInfo(int recentSuccess,
                                      int recentFail,
                                      int lockoutRemainSec) {
    lbSucc_->setText(QString::number(recentSuccess));
    lbFail_->setText(QString::number(recentFail));
    if (lockoutRemainSec > 0) lbLockout_->setText(hhmmss(lockoutRemainSec));
    else lbLockout_->setText("0");
}

void ControlTabView::showRecentLogs(const QList<QPair<QDateTime, QString>>& items) {
    tblLogs_->setRowCount(items.size());
    for (int i=0; i<items.size(); ++i) {
        const auto& it = items[i];
        auto* c0 = new QTableWidgetItem(it.first.toString("HH:mm:ss"));
        auto* c1 = new QTableWidgetItem(it.second);
        tblLogs_->setItem(i, 0, c0);
        tblLogs_->setItem(i, 1, c1);
    }
    // 최신이 맨 위: 소트
    tblLogs_->sortItems(0, Qt::DescendingOrder);
}

void ControlTabView::showActionResult(const QString& action, bool ok, const QString& message) {
    lbActionMsg_->setText(QString("[%1] %2: %3").arg(action, ok?"성공":"실패", message));
}


