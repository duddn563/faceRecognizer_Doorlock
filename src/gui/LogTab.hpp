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

	void DelAuthLogs();
	void DelSysLogs();
	void DelAllLogs();

private:
    QPushButton* btnAuthLog = nullptr;
    QPushButton* btnSysLog = nullptr;
	QPushButton* btnDelAuth = nullptr;
	QPushButton* btnDelSys  = nullptr;
	QPushButton* btnDelAll  = nullptr;
};

