#ifndef FACERECOGNITIONSERVICE_H
#define FACERECOGNITIONSERVICE_H

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QElapsedTimer>
#include <opencv2/opencv.hpp>
#include <opencv2/face.hpp>
#include <fstream>
#include <unistd.h>
#include <map>
#include <QMutexLocker>
#include <QMutex>
#include <QAtomicInt>

#include <QDebug>

#include "faceRecognitionState.hpp"
#include "fsm/recognition_fsm.hpp"
#include "fsm/recognition_fsm_setup.hpp"
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

#define AUTH_SUCCESSED								1
#define AUTH_FAILED										0

using namespace std;
using namespace cv;
using namespace cv::face;

class FaceRecognitionPresenter;


typedef struct RecogResult_t {
		double confidence;			// 얼굴인식 신뢰도
		int		 result;			  // 인식 결과
} recogResult_t;

class FaceRecognitionService : public QObject {
		Q_OBJECT

public:
				explicit FaceRecognitionService(QObject *parent = nullptr, FaceRecognitionPresenter* presenter = nullptr);
				void setPresenter(FaceRecognitionPresenter* presenter);
				void init();
				void stop();
				void procFrame();

				QString getUserName();
				void startRegistering(const QString& name);
				void resetUnlockFlag();
				void fetchReset();

				void setState(RecognitionState newState) {
						if (currentState == newState) return;
						currentState = newState;
						emit stateChanged(newState);
				}
				RecognitionState getState() const { return currentState; }
signals:
				void frameReady(const QImage& frame);
				//void stateChanged(RecognitionState newState);
				void registerFinished(bool success, const QString& message);

				void stateChanged(RecognitionState s);

public slots:
				void setDetectScore(double v);
				void setRecogConfidence(double v);
				void setDuplicate(bool v);
				void setRegisterRequested(bool v);
				void setLivenessOk(bool v);
				void setDoorOpened(bool v);
				void incFailCount();
				void resetFailCount();

private slots:
				void onTick();

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

				//void setState(RecognitionState newState);
				recogResult_t  handleRecognition(Mat& frame, const Rect& face, const Mat& alignedFace, QString& labelText, Scalar& boxColor);
				void handleRegistration(Mat& frame, const Rect& face, const Mat& alignedFace, QString& labelText, Scalar& boxColor);
				void finalizeRegistration();

				bool computeTimeout(const FsmContext& c);

private:
				FaceRecognitionPresenter* presenter;
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
				QAtomicInt isRegisteringAtomic;
				QString userName;
				bool hasAlreadyUnlocked = false;

				RecognitionFsm fsm_{this};
				FsmParams params_;
				QTimer tick_;
				QElapsedTimer monotonic_;
				QElapsedTimer stateTimer_;
				RecognitionState prevState_ = RecognitionState::IDLE;

				double detectScore_ = 0.0;
				double recogConf_		= 0.0;
				bool	 isDup_				= false;
				bool	 regReq_			= false;
				bool	 livenessOk_	= false;
				bool	 doorOpened_  = false;
				int		 failCount_   = 0;
				bool	 facePresent_ = false;
};

#endif		// FACERECOGNITIONSERVICE_H
