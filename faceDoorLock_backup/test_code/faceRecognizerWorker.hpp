#ifndef FACERECOGNIZERWORKER_HPP
#define FACERECOGNIZERWORKER_HPP

#include <QObject>
#include <opencv2/opencv.hpp>
#include <opencv2/face.hpp>
#include <map>
#include <vector>

#define CAM_NUM											  -1

#define	CONF_PATH											"/usr/local/etc/face_doorlock/"						
#define	HAARCASCADE_FRONTALFACE				CONF_PATH "haarcascades/haarcascade_frontalface_default.xml"
#define HAARCASCADE_EYE								CONF_PATH "haarcascades/haarcascade_eye.xml"
#define USER_LABEL_FILE								CONF_PATH "labels.txt"	
#define USER_FACES_DIR								CONF_PATH	"data"

#define IMAGES_PATH										"/root/trunk/faceRecognizer_Doorlock/ui/images/"							

using namespace std;
using namespace cv;

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

		Ptr<face::LBPHFaceRecognizer> faceRecognizer;

		CascadeClassifier faceCascade;

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

#endif // FACERECOGNIZERWORKER_HPP
