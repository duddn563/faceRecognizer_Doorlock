#ifndef FD_ULTRASONICWORKER_H 
#define	FD_ULTRASONICWORKER_H 

#include <wiringPi.h>
#include <chrono>
#include <thread>
#include <iostream>

#include <QObject>
#include <QThread>

#define FD_TRIG_PIN 2
#define FD_ECHO_PIN 4

class FD_UltrasonicWorker : public QObject {
    Q_OBJECT
public:
    explicit FD_UltrasonicWorker(QObject *parent = nullptr);
    ~FD_UltrasonicWorker();

signals:
    void personDetected();      // 사람이 가까이 오면 emit
    void personLeft();          // 사람이 멀어지면 emit

public slots:
    void FD_process();             // 쓰레드 루프
};

#endif // FD_UITRASONICWORKER_H

