#pragma once

#include <QObject>
#include <QTimer>
#include <QImage>
#include <QDir>
#include <opencv2/opencv.hpp>
#include <opencv2/face.hpp>
#include <fstream>
#include <unistd.h>
#include <map>

#include "faceRecognitionState.hpp"
#include "util.hpp"

#define CAM_NUM											  0

#define ROOT_PATH											"/root/trunk/faceRecognizer_Doorlock/"
#define	CONF_PATH											ROOT_PATH "etc/"						

#define ASSERT_PATH										ROOT_PATH "assert/"
#define	FACEDETECTOR									ASSERT_PATH "detector/haarcascade_frontalface_default.xml"
#define EYESDETECTOR									ASSERT_PATH "detector/haarcascade_eye.xml"
#define USER_FACES_DIR								ASSERT_PATH	"face_images/"
#define FACE_MODEL_FILE								ASSERT_PATH "face_model.yml"
#define USER_LABEL_FILE								ASSERT_PATH "labels.txt"	
#define IMAGES_PATH										ASSERT_PATH "images/"							

using namespace std;
using namespace cv;
using namespace cv::face;

typedef struct UserInfo_t	{
		int label;
		QString name;
		Mat grayFaceImage;
		Mat colorFaceImage;
} UserInfo;


class FaceRecognizerWorker : public QObject {
    Q_OBJECT
public:
    explicit FaceRecognizerWorker(QObject *parent = nullptr);
    ~FaceRecognizerWorker();

    void initialize();
    void stop();

		void setState(RecognitionState newState);
		void startRegistering(const QString& username);
		void updateFrame();
		QString getUserName() const; 

signals:
		void recognitionResult(const QString& result);
		void errorOccurred(const QString& error);
		void stateChanged(RecognitionState newState);
		void frameReady(const QImage& frame);
		void duplicatedFace(const QString& result);

private:
		void openCamera();
		void createLBPH();
		void loadDetector();	
		void loadModel();
		void loadLabelMap();
		void trainOrUpdateModel(const vector<Mat>& images, const vector<int>& labels);
		void registerExistingUser();
		void isUserNameExists(const QString& name);
		void saveLabelToFile(int label, const string& name);
		int  getNextLabel();


		void processFrame();
		void handleRecognition(const Rect& face, const Mat& aligned, const Mat& colorFace);
		void handleRegistration(const Rect&face, const Mat& aligned, const Mat& colorFace);

		bool isDuplicateFace(const Mat& newFace);
		Mat alignAndNormalizeFace(const Mat& grayFrame, const Rect& faceRect);

private:
		bool running;
		VideoCapture cap;

		CascadeClassifier faceDetector;
		CascadeClassifier eyesDetector;
		Ptr<LBPHFaceRecognizer> recognizer;

		RecognitionState currentState = RecognitionState::IDLE;

		map<int, string> labelMap;

		int currentLabel = -1;
		QString userName;
		int captureCount = 0;
		bool isRegistering = false;
		
};


/*
struct UserData {
		int label;
		string name;
		Mat grayFaceImage;
		Mat colorFaceImage;
};

class FaceRecognizerWorker : public QObject 
{
		Q_OBJECT
	
public:
		explicit FaceRecognizerWorker(QObject* parent = nullptr);
		~FaceRecognizerWorker();

		void startRegisterUser(const QString& userName);

		void processFrame(const Mat& frame);

		void loadModel();
		void saveModel();

signals:
		void registerProgress(int current, int max);

		void registerFinished(const QString& userName);

		void faceRecognized(const QString& userName, double confidence);

		void errorOccurred(const QString& message);

private:
		bool isDuplicateFace(const Mat& newFace);
		Mat alignAndNormalizeFace(const Mat& grayFrame, const Rect& faceRect);


		CascadeClassifier faceDetector;
		Ptr<face::LBPHFaceRecognizer> recognizer;

		map<int, vector<Mat>> storedFaces;
		vector<UserData> storedUsers;
		map<int, string> labelMap;

		int currentLabel;
		QString registeringUserName;
		int captureCount;
		const int maxCaptureCount = 20;

		bool registering;

		int getNextLabel();

		void saveLabelToFile;
		void loadLabelMap();

};
*/

