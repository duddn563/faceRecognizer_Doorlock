#pragma once
#include <QWidget>

class QPushButton;

class DevInfoTab : public QWidget {
    Q_OBJECT
public:
    explicit DevInfoTab(QWidget* parent = nullptr);

signals:
    void showDevInfo();
    void showSysInfo();

private:
    QPushButton* btnDevInfo = nullptr;
    QPushButton* btnSysInfo = nullptr;
};

