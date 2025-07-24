#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QDir>
#include <opencv2/opencv.hpp>
#include <QDebug>
#include <opencv2/face.hpp>
#include <fstream>
#include <unistd.h>
#include <map>
#include "faceRecognitionState.hpp"

//#include "util.hpp"

//#define CAM_NUM											0

class FaceRecognitionService;
class MainWindow;

class FaceRecognitionPresenter : public QObject {
		Q_OBJECT

public:
				explicit FaceRecognitionPresenter(FaceRecognitionService* service, MainWindow* view, QObject* parent);
				~FaceRecognitionPresenter();
				
private:
				MainWindow* view;
				FaceRecognitionService* service;
				//QThread* thread;

				RecognitionState currentDoorState = RecognitionState::IDLE;

				void onViewStateChanged(RecognitionState state);
};
