#pragma once

#include <QObject>
#include "gui/MainWindow.hpp"
#include "services/FaceRecognitionService.hpp"

class FaceRegisterPresenter : public QObject {
    Q_OBJECT

public:
    FaceRegisterPresenter(FaceRecognitionService* service, MainWindow* view, QObject* parent = nullptr);

private slots:
    void onRegisterFace();

private:
    FaceRecognitionService* service;
    MainWindow* view;
};

