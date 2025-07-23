#pragma once

#include <QObject>
#include <QString>

class DoorSensorService;
class MainWindow;

class DoorSensorPresenter : public QObject {
		Q_OBJECT

public:
				explicit DoorSensorPresenter(DoorSensorService* service, MainWindow* view, QObject* parent);
				~DoorSensorPresenter();

				//void doorSensorStart();
				
private:
				MainWindow* view;
				DoorSensorService* service;
				//QThread* thread;
};

