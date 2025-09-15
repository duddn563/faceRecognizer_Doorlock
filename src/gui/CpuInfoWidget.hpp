#pragma once
#include <QWidget>
#include <QLabel>
#include <QTreeWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QTimer>
#include <QMap>

class CpuInfoWidget : public QWidget {
    Q_OBJECT
public:
    explicit CpuInfoWidget(QWidget* parent = nullptr);

public slots:
    void refresh();
    void setAutoRefresh(bool on);

private:
    // 상단 요약
    QLabel* lblModel_    = nullptr;
    QLabel* lblCores_    = nullptr;
    QLabel* lblLoadAvg_  = nullptr;
    QLabel* lblTemp_     = nullptr;
    QLabel* lblGovernor_ = nullptr;

    // 코어 상세
    QTreeWidget* tree_   = nullptr;

    // 컨트롤
    QPushButton* btnRefresh_ = nullptr;
    QCheckBox*   chkAuto_    = nullptr;
    QTimer       timer_;

    // helpers
    static QString readFirstLine(const QString& path);
    static QString readAll(const QString& path);
    static QString runCmd(const QString& cmd, const QStringList& args = {}, int msec = 1000);
    static QString humanMHz(qint64 khz);
    static QString humanTempMilliC(qint64 milliC);
    static QStringList listCpuIds(); // ["cpu0","cpu1",...]

    // 통계: /proc/stat 기반 사용률(%)
    struct CpuTimes { quint64 user=0,nice=0,system=0,idle=0,iowait=0,irq=0,softirq=0,steal=0,guest=0,guest_nice=0; };
    static QMap<QString, CpuTimes> readProcStat(); // key: "cpu", "cpu0", ...
    QMap<QString, CpuTimes> prev_;                 // 이전 샘플
    static double usagePercent(const CpuTimes& prev, const CpuTimes& cur);

    // 채우기
    void fillSummary();
    void fillPerCore();
};

