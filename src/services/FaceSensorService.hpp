#ifndef FACESENSORSERVICE_H 
#define	FACESENSORSERVICE_H 

#include <wiringPi.h>
#include <chrono>
#include <thread>
#include <iostream>

#include <QObject>
#include <QThread>

#define TRIG_PIN 4
#define ECHO_PIN 5			

class FaceSensorService : public QObject {
    Q_OBJECT
public:
				explicit FaceSensorService(QObject *parent = nullptr);
				void stop();

public slots:
				void run();
			

signals:
				void personDetected();      // 사람이 가까이 오면 emit
				void personLeft();          // 사람이 멀어지면 emit

private:
				std::atomic<bool> isRunning = true;
};

#endif // FACESENSORSERVICE_H 

