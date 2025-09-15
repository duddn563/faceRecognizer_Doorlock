#pragma once
#include <QWidget>
#include <QLabel>
#include <QFormLayout>
#include <QPushButton>

class BasicInfoWidget : public QWidget {
    Q_OBJECT
public:
    explicit BasicInfoWidget(QWidget* parent = nullptr);

public slots:
    void refresh();

private:
    // 라벨 핸들(값 뿌릴 자리)
    QLabel* lblHostname_      = nullptr;
    QLabel* lblOs_            = nullptr;
    QLabel* lblKernel_        = nullptr;
    QLabel* lblArch_          = nullptr;
    QLabel* lblQt_            = nullptr;
    QLabel* lblDeviceModel_   = nullptr;
    QLabel* lblCpuCores_      = nullptr;
    QLabel* lblUptime_        = nullptr;
    QLabel* lblBootTime_      = nullptr;
    QLabel* lblMem_           = nullptr;
    QLabel* lblDiskRoot_      = nullptr;

    // 헬퍼
    static QString readFirstLine(const QString& path);
    static QString readAll(const QString& path);
    static QString humanBytes(qulonglong bytes);
    static QString humanDurationSeconds(qulonglong secs);
};

