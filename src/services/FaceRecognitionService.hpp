#ifndef FACERECOGNITIONSERVICE_H
#define FACERECOGNITIONSERVICE_H

#include <QObject>
#include <QThread>
#include <opencv2/opencv.hpp>
#include <opencv2/face.hpp>
#include <fstream>
#include <unistd.h>
#include <map>
#include <QMutexLocker>
#include <QMutex>

#include <QDebug>

#include "faceRecognitionState.hpp"
#include "AuthManager.hpp"
#include "util.hpp"

#define CAM_NUM												0

#define ROOT_PATH											"/root/trunk/faceRecognizer_Doorlock/"
#define CONF_PATH											ROOT_PATH "etc/"

#define ASSERT_PATH                   ROOT_PATH "assert/"
#define FACEDETECTOR                  ASSERT_PATH "detector/haarcascade_frontalface_default.xml"
#define EYESDETECTOR                  ASSERT_PATH "detector/haarcascade_eye.xml"
#define FACE_MODEL_FILE								ASSERT_PATH	"face_model.yml"
#define USER_FACES_DIR								ASSERT_PATH "face_images/"
#define USER_LABEL_FILE								ASSERT_PATH	"labels.txt"
#define OPEN_IMAGE										ASSERT_PATH	"images/open_image.PNG"

#define AUTH_SUCCESS									1
#define AUTH_FAILED										0

using namespace std;
using namespace cv;
using namespace cv::face;

class FaceRecognitionService : public QObject {
		Q_OBJECT

public:
				explicit FaceRecognitionService(QObject *parent = nullptr);
				void init();
				void stop();
				void procFrame();

				QString getUserName();
				void startRegistering(const QString& name);
				void resetUnlockFlag();

signals:
				void frameReady(const QImage& frame);
				void stateChanged(RecognitionState newState);

private:
				void openCamera();

				void loadDetector();
				void createLBPH();
				void loadModel();

				void registerExistingUser(); 
				void loadLabelMap();

				void drawTransparentBox(Mat& img, Rect rect, Scalar color, double alpha);
				void drawCornerBox(Mat& img, Rect rect, Scalar color, int thickness, int length);

				Mat alignAndNormalizeFace(const Mat& grayFrame, const Rect& faceRect);
				void saveLabelToFile(int label, const string& name); 
				void trainOrUpdateModel(const vector<Mat>& images, const vector<int>& labels);
				bool isDuplicateFace(const Mat& newFace);
				void saveCapturedFace(const Rect& face, const Mat& aligendFace, const Mat& frame);
				int	 getNextLabel();

				void setState(RecognitionState newState);
				int handleRecognition(Mat& frame, const Rect& face, const Mat& alignedFace, QString& labelText, Scalar& boxColor);
				void handleRegistration(Mat& frame, const Rect& face, const Mat& alignedFace, QString& labelText, Scalar& boxColor);
				void finalizeRegistration();

private:
				std::atomic<bool> isRunning = true;
				VideoCapture cap;

				CascadeClassifier faceDetector;
				CascadeClassifier eyesDetector;

				Ptr<LBPHFaceRecognizer> recognizer;

				RecognitionState currentState = RecognitionState::IDLE;
				
				map<int, vector<Mat>> storedFaces;
				map<int, string> labelMap;

				QMutex frameMutex;
				AuthManager authManager;

				int currentLabel = -1;
				int captureCount = 0;
				bool isRegistering = false;
				QString userName;
				bool hasAlreadyUnlocked = false;

};

#endif		// FACERECOGNITIONSERVICE_H
