#pragma once

#include <QObject>
#include "gui/MainWindow.hpp"
#include "services/FaceRecognitionService.hpp"

class FaceRegisterPresenter : public QObject {
    Q_OBJECT

public:
    FaceRegisterPresenter(FaceRecognitionService* service, MainWindow* view, QObject* parent = nullptr);

public slots:
    void onRegisterFace();

private:
		void onRegisterFinished(bool success, const QString& message);
		void connectService();		
	
    FaceRecognitionService* service;
    MainWindow* view;
};

