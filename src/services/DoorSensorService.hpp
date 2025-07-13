#ifndef DOORSENSORSERVICE_H
#define DOORSENSORSERVICE_H

#include <wiringPi.h>
#include <chrono>
#include <thread>
#include <iostream>

#include <QObject>
#include <QThread>

#define SIG_PIN 0

class DoorSensorService : public QObject {
    Q_OBJECT
public:
				DoorSensorService(QObject* parent = nullptr);
				void stop();

public slots:
				void run();

signals:
				void doorClosed();
				void doorOpened();

private:
				std::atomic<bool> isRunning = true;
};

#endif // DOORSENSORSERVICE_H

