#ifndef DD_ULTRASONICWORKER_H
#define DD_ULTRASONICWORKER_H

#include <wiringPi.h>
#include <chrono>
#include <thread>
#include <iostream>

#include <QObject>
#include <QThread>

#define SIG_PIN 0

class DD_UltrasonicWorker : public QObject {
    Q_OBJECT
public:
    explicit DD_UltrasonicWorker(QObject *parent = nullptr);
    ~DD_UltrasonicWorker();

signals:
    void doorDetected();      // 문이 가까이 오면 emit
    void doorLeft();          // 문이 멀어지면 emit

public slots:
    void DD_process();             // 쓰레드 루프
};

#endif // DD_ULTRASONICEWORKER_H

