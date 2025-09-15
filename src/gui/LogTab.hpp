#pragma once
#include <QWidget>

class QPushButton;

class LogTab : public QWidget {
    Q_OBJECT
public:
    explicit LogTab(QWidget* parent = nullptr);

signals:
    void showAuthLogs();
    void showSysLogs();

private:
    QPushButton* btnAuthLog = nullptr;
    QPushButton* btnSysLog = nullptr;
};

