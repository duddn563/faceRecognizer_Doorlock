#pragma once
#include <QWidget>
#include <QLabel>
#include <QTreeWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QTimer>

class MemInfoWidget : public QWidget {
    Q_OBJECT
public:
    explicit MemInfoWidget(QWidget* parent = nullptr);

public slots:
    void refresh();
    void setAutoRefresh(bool on);

private:
    QLabel* lblRamSummary_ = nullptr;
    QLabel* lblSwapSummary_= nullptr;
    QLabel* lblDiskSummary_= nullptr;

    QTreeWidget* tree_ = nullptr;

    QPushButton* btnRefresh_ = nullptr;
    QCheckBox*   chkAuto_    = nullptr;
    QTimer       timer_;

    // helpers
    static QString readAll(const QString& path);
    static QString humanBytes(qulonglong bytes);
    static qulonglong parseLineKB(const QString& text, const QString& key);
};

