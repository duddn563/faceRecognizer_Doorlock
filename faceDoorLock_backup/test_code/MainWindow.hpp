// -------------------- gui/MainWindow.hpp --------------------
#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP
#include <opencv2/opencv.hpp>
#include <opencv2/objdetect.hpp>  // 얼굴 인식을 위한 헤더
#include <opencv2/face.hpp>
#include <opencv2/imgproc.hpp>

#include "faceRecognizerWorker.hpp"
#include "FD_ultrasonic.hpp"
#include "DD_ultrasonic.hpp"
#include "logger.hpp"
#include "util.hpp"

#include <QMainWindow>
#include <QTimer>
#include <QPushButton>
#include <QDir>
#include <QDebug>
#include <QMessageBox>
#include <QListWidget>
#include <QInputDialog>
#include <QStringList>
#include <QFile>

#include <wiringPi.h>

#include <fstream>
#include <filesystem>
#include <cmath>
#include <map>

#define CAM_NUM											  -1

#define	CONF_PATH											"/usr/local/etc/face_doorlock/"						
#define	HAARCASCADE_FRONTALFACE				CONF_PATH "haarcascades/haarcascade_frontalface_default.xml"
#define HAARCASCADE_EYE								CONF_PATH "haarcascades/haarcascade_eye.xml"
#define USER_LABEL_FILE								CONF_PATH "labels.txt"	
#define USER_FACES_DIR								CONF_PATH	"data"

#define IMAGES_PATH										"/root/trunk/faceRecognizer_Doorlock/ui/images/"							
#define COME_IMAGE_PATH								IMAGES_PATH	"come_image.PNG"
#define OPEN_IMAGE_PATH							IMAGES_PATH "open_image.PNG"

#define BTN_STYLE										\
    "QPushButton {"									\
    "  background-color: #5DADE2;"	\
    "  color: white;"								\
    "  border-radius: 8px;"					\
    "  padding: 8px;"								\
    "  font-size: 14;"							\
		"	 min-width: 40px;"						\
		"	 min-height: 40px;"						\
		"	 max-width: 100px;"						\
    "}"															\
    "QPushButton:hover {"						\
    "  background-color: #3498DB;"	\
    "}"					
#define STATUS_BAR_STYLE	"color: #00aaff; font: 14pt 'Segoe UI';"
#define	CAM_LABEL_STYLE		"background-color: #1e1e1e; border: 2px solid #00aaff; border-radius: 10px;"

using namespace std;
using namespace cv;

namespace fs = std::filesystem;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

static constexpr int WINDOW_MIN_WIDTH		= 640;
static constexpr int WINDOW_MIN_HEIGHT	= 480; 

typedef struct UserData {
		int			label;
		QString name;
		cv::Mat grayFaceImage;
		cv::Mat colorFaceImage; 
} UserData;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void updateFrame();
    void onRegisterFace();
		void onClearUsers();
		void showUserList();
		void onExitProgram();
		void onShowUserImages();
	

private:
    Ui::MainWindow    *ui = nullptr;
		QLabel *overlayLabel = nullptr;
    QTimer				    *timer;
    VideoCapture      cap;
    CascadeClassifier faceCascade, eyeCascade;

		bool isRegistering = false;
		int	captureCount   = 0;  
		int currentLabel   = 0;
		QString	userName;
		map<int, string>	 labelMap;

		bool isFaceDetecting = false;
		bool isDoorDetecting = false;

		cv::Ptr<cv::face::LBPHFaceRecognizer> faceRecognizer;

		QThread *workerThread;
		FaceRecognizerWorker *worker;

		void setupFaceRecognitionWorker();

		void openCamera();
	
		void setupUi();
		void setupFdUltrasonic();
		void setupDdUltrasonic();
		void setupQTimer();

		void showOpenImage();
		void showComeImage();

		void createLBPH();
		void loadFaceCascade();
		void loadEyesCascade();

		void registerExistingUser();

		int getNextLabel();
		void saveLabelToFile(int label, const string &name);
		void loadLabelMap();
		bool isDuplicateFace(const Mat& newFace);
		Mat alignAndNormalizeFace(const Mat& grayFrame, const Rect& faceRect);
		bool isUserNameExists(const QString& name);
		void clearAllUserData();

		void setupCameraLabel();
		void setupOverlayLabel();
		void applyStyles();
		void connectSignals();
		QList<QPushButton*> buttonList() const;

		void showErrorMessage(const QString& title, const QString& message);
};

#endif // MAINWINDOW_HPP
