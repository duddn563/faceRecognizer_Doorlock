#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QDir>
#include <QDebug>
#include <QImage>
#include <QPixmap>
//#include <opencv2/opencv.hpp>
//#include <opencv2/face.hpp>
#include <QPointer>
#include <fstream>
#include <unistd.h>
#include <map>
#include "faceRecognitionState.hpp"

//#include "util.hpp"

//#define CAM_NUM											0

class FaceRecognitionService;
class FaceRegisterPresenter;
class MainWindow;

class FaceRecognitionPresenter : public QObject {
		Q_OBJECT

public:
				explicit FaceRecognitionPresenter(FaceRecognitionService* service, MainWindow* view, QObject* parent);
				~FaceRecognitionPresenter();

				void onRegisterFace();

				void onReset();
				void presentReset();

				void onCamRestart();
				void presentCamRestart();

				QTimer* throttleTimer_;
				QImage pendingFrame_;
				QImage lastFrame_;
				
private:
				MainWindow* view;
				QPointer<FaceRecognitionService> service;
				QPointer<FaceRegisterPresenter> faceRegisterPresenter;

				RecognitionState currentDoorState = RecognitionState::IDLE;

				void onViewStateChanged(RecognitionState state);
				//void handleStateChanged(RecognitionState state);

};
