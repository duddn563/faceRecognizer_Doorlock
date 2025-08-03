#include "FaceRecognitionService.hpp"
#include "MainWindow.hpp"
#include <QImage>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <filesystem>
#include <chrono>
#include <ctime>

#include "presenter/FaceRecognitionPresenter.hpp"

// #define DEBUG 

namespace fs = std::filesystem;

FaceRecognitionService::FaceRecognitionService(QObject* parent, FaceRecognitionPresenter* presenter) : QObject(parent), presenter(presenter)
{
		std::cout << "Face Recognition Service create!!" << std::endl;
		init();
}

void FaceRecognitionService::setPresenter(FaceRecognitionPresenter* _presenter)
{
		presenter = _presenter;
}

void FaceRecognitionService::init()
{
		std::cout << "Face Recognition Service initiallize!!" << std::endl;
		openCamera();

		loadDetector();
		createLBPH();
		loadModel();

		loadLabelMap();
		registerExistingUser();

		QTimer *timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, &FaceRecognitionService::procFrame);
		timer->start(30);
}

void FaceRecognitionService::stop()
{
		std::cout << "Face Recognition Service stop" << std::endl;
		if (!isRunning) return;

		isRunning = false;

		if (cap.isOpened()) {
				cap.release();
		}

		currentState = RecognitionState::IDLE;
		userName.clear();

		this->moveToThread(QCoreApplication::instance()->thread());
}

void FaceRecognitionService::openCamera()
{
		qDebug() << "[FaceRecognitionService] openCamera is called";
		try {
				cap.open(CAM_NUM);
				if (!cap.isOpened()) {
						std::cout << "Failed to camera open!!" << std::endl;
				}
				std::cout << "Camera be opend!!" << std::endl;
		} catch(const cv::Exception& e) {
				std::cout << "OpenCV exception: " << e.what() << std::endl;
		}
}

void FaceRecognitionService::loadDetector()
{
		qDebug() << "[FaceRecognitionService] loadDetector is called";
		try {
				faceDetector.load(FACEDETECTOR);
				eyesDetector.load(EYESDETECTOR);
		}
		catch (Exception &e) {
				qDebug() << "[FaceRecognitionService] Failed to load detector (" << e.what() << ")";				
		}
}

void FaceRecognitionService::createLBPH()
{
		try {
				recognizer = face::LBPHFaceRecognizer::create();
				qDebug() << "[FaceRecognitionService] LBPHFaceRecognizer is created.";

		} catch (Exception& e) {
				std::cout << "Failed to create recognizer!!" << std::endl;
				return;
		}
}

void FaceRecognitionService::loadModel()
{
		if (!recognizer) 
				recognizer = face::LBPHFaceRecognizer::create();

		try {
				if (fs::exists(FACE_MODEL_FILE)) {
						recognizer->read(FACE_MODEL_FILE);
						qDebug() << "[FaceRecognitionService] Recognizer is loaded.";
				}
				else {
						cout << "No existing model found. Will create a new model." << endl;
				}
		} catch (cv::Exception& e) {
				cout << "[" << __func__ << "] Exception Message: " << e.what() << endl;
		}
}

void FaceRecognitionService::registerExistingUser()
{
		bool rc = 0;
		size_t firstUnderscore, secondUnderscore, thirdUnderscore;
		string name;

		rc = fs::is_directory(USER_FACES_DIR);

		if (rc) {
				for (const auto& entry : fs::directory_iterator(USER_FACES_DIR)) {
						string fname = entry.path().filename().string();				// format: face_3_GS_1.png
						if (!fname.empty()) {
								firstUnderscore	= fname.find('_');
								secondUnderscore = fname.find('_', firstUnderscore + 1); 



								if (firstUnderscore == string::npos || secondUnderscore == string::npos) {
										std::cout << "[" << __func__ <<  "] " << fname << "is invalid file name format!!" << std::endl;
										continue;
								}

								int		label = std::stoi(fname.substr(firstUnderscore + 1, secondUnderscore - firstUnderscore - 1));

#ifdef DEBUG
								thirdUnderscore  = fname.find('_', secondUnderscore + 1);

								if (thirdUnderscore == string::npos) {
										std::cout << "[" << __func__ <<  "] " << fname << "is invalid file name format!!" << std::endl;
										continue;
								}
								string name = fname.substr(secondUnderscore + 1, thirdUnderscore - secondUnderscore - 1);

								printf ("[%s] name: %s, label: %d\n", __func__, name.c_str(), label);
#endif

								Mat gray = imread(entry.path().string(), IMREAD_GRAYSCALE);
								cv::resize(gray, gray, Size(200, 200));

								storedFaces[label].push_back(gray);

						}
				}

				qDebug() << "[FaceRecognitionService] Existing user is stored.";
		}
		else {
				fs::create_directory(USER_FACES_DIR);
				std::cout << "User face directory does not exist or is not a directory." << std::endl;
		}

}

void FaceRecognitionService::loadLabelMap()
{
		int lbl;
		string name;
		ifstream ifs(USER_LABEL_FILE);
		int rc = 0;

		rc = Utile::checkFileExist(USER_LABEL_FILE, R_OK + W_OK);
		if (rc == -1) {
				Utile::createFile(USER_LABEL_FILE, R_OK + W_OK);
		}

		if (ifs.is_open()) {
				while (ifs >> lbl >> name) {
						labelMap[lbl] = name;
#ifdef DEBUG
						printf ("[%s] label: %d, name: %s\n", __func__, lbl, name.c_str());
#endif
				}
		}

		qDebug() << "[FaceRecognitionService] LabelMap file is loaded";

}

Mat FaceRecognitionService::alignAndNormalizeFace(const Mat& grayFrame, const Rect& faceRect)
{
		Mat faceROI = grayFrame(faceRect).clone();

    // 눈 검출
    vector<Rect> eyes;
    eyesDetector.detectMultiScale(faceROI, eyes, 1.1, 10, 0, Size(20, 20));

    if (eyes.size() < 2) return {};  // 눈이 2개 미만이면 실패

    // 눈 2개를 좌/우로 정렬
    Point eye1 = (eyes[0].x < eyes[1].x) ? eyes[0].tl() : eyes[1].tl();
    Point eye2 = (eyes[0].x < eyes[1].x) ? eyes[1].tl() : eyes[0].tl();

    // 눈 중심 계산
    eye1 += Point(eyes[0].width/2, eyes[0].height/2);
    eye2 += Point(eyes[1].width/2, eyes[1].height/2);

    // 각도 계산
    double dy = eye2.y - eye1.y;
    double dx = eye2.x - eye1.x;
    double angle = atan2(dy, dx) * 180.0 / CV_PI;

    // 얼굴 중앙 기준 회전
    Point2f center(faceROI.cols/2.0F, faceROI.rows/2.0F);
    Mat rot = getRotationMatrix2D(center, angle, 1.0);

    Mat aligned;
    warpAffine(faceROI, aligned, rot, faceROI.size());

    // 밝기 보정 + 크기 조정
    equalizeHist(aligned, aligned);
    cv::resize(aligned, aligned, Size(200, 200));

    //cout << "[" << __func__ << "] Face is aligned and normalized!!" << endl;
    return aligned;
}

void FaceRecognitionService::trainOrUpdateModel(const vector<Mat>& images, const vector<int>& labels)
{
    if (!recognizer) {
        recognizer = face::LBPHFaceRecognizer::create();
    }

    if (fs::exists(FACE_MODEL_FILE)) {
        recognizer->update(images, labels);
        LOG_INFO("Update existing face recognition model.");
    }
    else {
        recognizer->train(images, labels);
        LOG_INFO("Trained new face recognition model.");
    }

    recognizer->save(FACE_MODEL_FILE);
    qDebug() << "✅ 모델이 저장되었습니다: " << FACE_MODEL_FILE;
}


void FaceRecognitionService::startRegistering(const QString& name)
{
		qDebug() << "[FaceRecognitionService] Registration has begun";
		userName = name;
    currentLabel = getNextLabel();
    captureCount = 0;
		isRegisteringAtomic.storeRelaxed(1);
    setState(RecognitionState::REGISTERING);
}

QString FaceRecognitionService::getUserName() 
{
		return userName; 
}

void FaceRecognitionService::setState(RecognitionState newState) 
{
		if (currentState == RecognitionState::UNLOCKED) {
				return;
		}

		switch(newState) {
        case RecognitionState::IDLE:
            emit stateChanged(newState);
						qDebug() << "[FaceRecognitionService] The status changes to IDLE";
            break;

        case RecognitionState::UNLOCKED:
            emit stateChanged(newState);
						qDebug() << "[FaceRecognitionService] The status changes to UNLOCKED";
            break;

				case RecognitionState::DUPLICATEDFACE:
						emit stateChanged(newState);
						qDebug() << "[FaceRecognitionService] The status changes to DUPLICATEDFACE";
						break;

				case RecognitionState::REGISTERING:
						emit stateChanged(newState);
						qDebug() << "[FaceRecognitionService] The status changes to REGISTERING";
						break;

        default:
						break;
		}
}

int FaceRecognitionService::handleRecognition(Mat& frame, const Rect& face, const Mat& aligendFace, QString& labelText, Scalar& boxColor)
{
		qDebug() << "[FaceRecognitionService] handleRecognition() is called";
		int predictedLabel = -1;
		double confident = 999.0;
		int authFlag = -1;

		if (recognizer->empty()) {
				qDebug() << "[FaceRecognitionService] Model is not trained. Unpredictable.";
				return AUTH_FAILED;
		}

		if (!storedFaces.empty()) {
				qDebug() << "[FaceRecognitionService] Model is not empty";
				recognizer->predict(aligendFace, predictedLabel, confident);
		}

		if (confident < 60.0 && labelMap.count(predictedLabel)) {
				labelText = QString::fromStdString(labelMap[predictedLabel]);
				boxColor = Scalar(0, 255, 0);
				authFlag = AUTH_SUCCESS;
		} 
		else {
				labelText = "Unknown";
				boxColor = Scalar(0, 0, 255);
				authFlag = AUTH_FAILED;
		}

		drawTransparentBox(frame, face, boxColor, 0.3);
    drawCornerBox(frame, face, boxColor, 2, 25);
    putText(frame, labelText.toStdString(), Point(face.x, face.y - 10),
            FONT_HERSHEY_DUPLEX, 1.0, boxColor, 2);
		cout << "[" << __func__ << "] auth flag: " << authFlag << endl;

		return authFlag;
}

void FaceRecognitionService::handleRegistration(Mat& frame, const Rect& face, const Mat& alignedFace, QString& labelText, Scalar& boxColor)
{
		qDebug() << "[FaceRecognitionService] handleRegistration is called";
		Mat colorFace = frame.clone();
		labelText = "Registering...";
		boxColor = Scalar(255, 0, 0);

		drawTransparentBox(frame, face, boxColor, 0.3);
    drawCornerBox(frame, face, boxColor, 2, 25);
    putText(frame, labelText.toStdString(), Point(face.x, face.y - 10),
            FONT_HERSHEY_DUPLEX, 1.0, boxColor, 2);


		qDebug() << "[FaceRecognitionService] Duplicate face start!";
		if (captureCount == 0 && isDuplicateFace(alignedFace)) {
				isRegisteringAtomic.storeRelaxed(0);
				setState(RecognitionState::DUPLICATEDFACE);
				emit registerFinished(false, "중복된 얼굴입니다.");
				return;
		}

		
		qDebug() << "[FaceRecognitionService] captureCount: " << captureCount;
		if (captureCount < 20) {
				saveCapturedFace(face, alignedFace, colorFace); 

				captureCount++;

				if (captureCount >= 20) {
						finalizeRegistration();
				}
		}
}

void FaceRecognitionService::saveCapturedFace(const Rect& face, const Mat& alignedFace, const Mat& frame) 
{
		if (!fs::exists(USER_FACES_DIR))
        fs::create_directory(USER_FACES_DIR);

    string filename = string(USER_FACES_DIR) + "face_" + to_string(currentLabel) + "_" +
                      userName.toStdString() + "_" + to_string(captureCount + 1) + ".png";

    if (!imwrite(filename, frame)) {
        qDebug() << "이미지 저장 실패: " << QString::fromStdString(filename);
    }

    storedFaces[currentLabel].push_back(alignedFace);
    labelMap[currentLabel] = userName.toStdString();

		qDebug() << "[FaceRecognitionService]" << currentLabel << "The face image has been saved and loaded";
}

void FaceRecognitionService::finalizeRegistration()
{
		saveLabelToFile(currentLabel, userName.toStdString());

		vector<Mat> images;
    vector<int> labels;
    for (const auto& entry : storedFaces) {
        for (const auto& img : entry.second) {
            images.push_back(img);
            labels.push_back(entry.first);
        }
    }

    trainOrUpdateModel(images, labels);

		loadModel();
    loadLabelMap();

		isRegisteringAtomic.storeRelaxed(0);
		setState(RecognitionState::IDLE);
		emit registerFinished(true, "등록 성공");

		qDebug() << "[FaceRecognitionService]" << userName << "registration is complete.";
}

void FaceRecognitionService::saveLabelToFile(int label, const string& name)
{
   ofstream ofs (USER_LABEL_FILE, ios::app);
   if (ofs.is_open()) ofs << label << " " << name << endl;

	 qDebug() << "[FaceRecognitionService] The user been saved to the label map.";
}


int FaceRecognitionService::getNextLabel()
{
    ifstream ifs(USER_LABEL_FILE);

    if (!access(USER_LABEL_FILE, F_OK) == 0) {
        ifstream file(USER_LABEL_FILE);
        file.close();
    }

    int maxLabel = 0;

    if (ifs.is_open()) {
        int lbl;
        string name;
        while (ifs >> lbl >> name) {
            if (lbl > maxLabel) maxLabel = lbl;
        }
    }

    return maxLabel + 1;
}

bool FaceRecognitionService::isDuplicateFace(const Mat& newFace)
{
		qDebug() << "[FaceRecognitionService] isDuplicateFace is called";
    if (!recognizer || newFace.empty()) {
        std::cerr << "[오류] 얼굴 인식기가 초기화되지 않았거나 입력 이미지가 비어 있습니다." << std::endl;
        return false;

    }

    int predictedLabel = -1;
    double confidence = 0.0;

    cv::Mat gray;
    if (newFace.channels() == 3)
        cv::cvtColor(newFace, gray, cv::COLOR_RGB2GRAY);
    else
        gray = newFace;

    if (!storedFaces.empty() && !labelMap.empty()) {
				qDebug() << "before predict";
				
        recognizer->predict(gray, predictedLabel, confidence);

				qDebug() << "after predict";
    }

    std::cout << "[" << __func__ << "] 예측된 라벨: " << predictedLabel << ", 신뢰도: " << confidence << std::endl;

    // 신뢰도 기준으로 중복 여부 판단
    const double DUPLICATE_THRESHOLD = 60.0; // 작을수록 엄격 (OpenCV LBPH 기준 50~100 적절)

		int rc = -1;

    return (confidence < DUPLICATE_THRESHOLD) && (confidence) && (predictedLabel != -1);
}

void FaceRecognitionService::resetUnlockFlag()
{
		hasAlreadyUnlocked = false;
}

void FaceRecognitionService::fetchReset()
{
		QMutexLocker locker(&frameMutex);
		qDebug() << "[FaceRecognitionService] fetchReset() called";

		QDir dir(USER_FACES_DIR);
		dir.removeRecursively();

		QFile::remove(FACE_MODEL_FILE);
		QFile::remove(USER_LABEL_FILE);

		labelMap.clear();
		currentLabel = 0;

		authManager.resetAuth();
		hasAlreadyUnlocked = false;	

		recognizer = face::LBPHFaceRecognizer::create();
		recognizer->save(FACE_MODEL_FILE);

		presenter->presentReset();
}

void FaceRecognitionService::drawTransparentBox(Mat& img, Rect rect, Scalar color, double alpha = 0.4)
{
    Mat overlay;
    img.copyTo(overlay);
    rectangle(overlay, rect, color, FILLED);
    addWeighted(overlay, alpha, img, 1 - alpha, 0, img);
}

void FaceRecognitionService::drawCornerBox(Mat& img, Rect rect, Scalar color, int thickness = 2, int length = 20)
{
    int x = rect.x, y = rect.y, w = rect.width, h = rect.height;

    line(img, {x, y}, {x + length, y}, color, thickness);
    line(img, {x, y}, {x, y + length}, color, thickness);

    line(img, {x + w, y}, {x + w - length, y}, color, thickness);
    line(img, {x + w, y}, {x + w, y + length}, color, thickness);

    line(img, {x, y + h}, {x + length, y + h}, color, thickness);
    line(img, {x, y + h}, {x, y + h - length}, color, thickness);

    line(img, {x + w, y + h}, {x + w - length, y + h}, color, thickness);
    line(img, {x + w, y + h}, {x + w, y + h - length}, color, thickness);
}

void FaceRecognitionService::procFrame()
{
		int authResult = AUTH_FAILED;
		Mat frame, frameCopy;
		{
				QMutexLocker locker(&frameMutex);
				if (!cap.read(frame) || frame.empty()) return;
		}

		frameCopy = frame.clone();
		Mat gray;
		cvtColor(frameCopy, gray, cv::COLOR_RGB2GRAY);	
		equalizeHist(gray, gray);

		vector<Rect> faces;
		faceDetector.detectMultiScale(gray, faces, 1.1, 5, CASCADE_SCALE_IMAGE, Size(100, 100));

		bool recognized = false;

		for (const auto& face : faces) 
		{
				Mat alignedFace = alignAndNormalizeFace(gray, face);
				if (alignedFace.empty()) continue;

				QString labelText;
				Scalar boxColor;

				if (isRegisteringAtomic.loadRelaxed() == 0) {
						authResult = handleRecognition(frameCopy, face, alignedFace, labelText, boxColor);
						if (authResult == AUTH_SUCCESS) {
								if (!hasAlreadyUnlocked) {
										recognized = true;
										authManager.handleAuthSuccess();
					
										if (authManager.shouldAllowEntry()) {
												setState(RecognitionState::UNLOCKED);
												qDebug() << "[FaceRecognitionService] Authenticate 3 time success -> Door open!";
												authManager.resetAuth();
												hasAlreadyUnlocked = true;
										}	
								}
						}
				}
				else {
						if(isRegisteringAtomic.testAndSetRelaxed(1, 1)) {
								qDebug() << "[FaceRecognitionService] before call handleRegistration()";
								handleRegistration(frameCopy, face, alignedFace, labelText, boxColor);
						}
				}
		}
		
		cvtColor(frameCopy, frameCopy, cv::COLOR_BGR2RGB);
    QImage qimg(frameCopy.data, frameCopy.cols, frameCopy.rows, frameCopy.step, QImage::Format_RGB888);
		emit frameReady(qimg.copy());
}
