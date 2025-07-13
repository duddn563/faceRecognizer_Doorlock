#pragma once

#include <QObject>
#include <QString>

class FaceSensorService;
class MainWindow;

class FaceSensorPresenter : public QObject {
		Q_OBJECT

public:
				explicit FaceSensorPresenter(FaceSensorService* service, MainWindow* view, QObject* parent);
				~FaceSensorPresenter();

				void faceSensorStart();

private:
				MainWindow* view;
				FaceSensorService* service;
				QThread* thread;
};



