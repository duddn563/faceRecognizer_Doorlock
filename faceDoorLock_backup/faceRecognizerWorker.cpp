#include "faceRecognizerWorker.hpp"
#include "logger.hpp"
#include <QFile>
#include <QTextStream>
#include <filesystem>
#include <QTimer>
#include <QMessageBox>

namespace fs = std::filesystem;

vector<UserInfo> storedUsers;
map<int, vector<Mat>> storedFaces;

void drawTransparentBox(Mat& img, Rect rect, Scalar color, double alpha = 0.4)
{
		Mat overlay;
		img.copyTo(overlay);
		rectangle(overlay, rect, color, FILLED);
		addWeighted(overlay, alpha, img, 1 - alpha, 0, img);
}

void drawCornerBox(Mat& img, Rect rect, Scalar color, int thickness = 2, int length = 20)
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

FaceRecognizerWorker::FaceRecognizerWorker(QObject* parent)
		: QObject(parent), running(false) {}

FaceRecognizerWorker::~FaceRecognizerWorker() 
{
		stop();
		if (cap.isOpened()) cap.release();
}


void FaceRecognizerWorker::initialize()
{
		loadDetector();
		createLBPH();

		registerExistingUser();
		loadLabelMap();
		loadModel();

		openCamera();
		running = true;

		QTimer *timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, &FaceRecognizerWorker::processFrame);
		timer->start(30);

}

void FaceRecognizerWorker::openCamera()
{
		try {
				cap.open(CAM_NUM);
				if (!cap.isOpened()) {
						emit errorOccurred("Camera open failed!!");
						Logger::writef("[%s] Camera open was failed(cam: %d)\n", __func__, CAM_NUM);
						return;
				}
				Logger::writef("[%s] Camera open was successfully(cam: %d)\n", __func__, CAM_NUM);
		} catch (const cv::Exception& e) {
				LOG_CRITICAL(QString("OpenCV exception: %1").arg(e.what()));
		}
}

void FaceRecognizerWorker::loadDetector() {
    faceDetector.load(FACEDETECTOR);
    eyesDetector.load(EYESDETECTOR);
}


void FaceRecognizerWorker::createLBPH()
{
		try {
				recognizer = face::LBPHFaceRecognizer::create();
		} 
		catch (Exception& e) {
				emit errorOccurred("Failed to create an LBPHFaceRecognizer.");
				return;
		}
}

void FaceRecognizerWorker::startRegistering(const QString& name)
{
		userName = name;
		currentLabel = getNextLabel();
		captureCount = 0;
		isRegistering = true;
		setState(RecognitionState::REGISTERING);
}

void FaceRecognizerWorker::setState(RecognitionState newState) {
    if (currentState != newState) {
        currentState = newState;
        emit stateChanged(currentState);
    }
}

QString FaceRecognizerWorker::getUserName() const
{
		return userName;
}

void FaceRecognizerWorker::registerExistingUser() 
{
		vector<Mat> images;
		vector<int> labels;

		//UserInfo newUser;

		if (!fs::exists(USER_FACES_DIR)) {
				fs::create_directory(USER_FACES_DIR);
		}

		bool rc = fs::is_directory(USER_FACES_DIR);

		printf ("[%s] ----------- User Information -----------\n", __func__); 
		if (rc) {
				for (const auto& entry : fs::directory_iterator(USER_FACES_DIR)) {
						string fname = entry.path().filename().string();
						if (!fname.empty()) {
								size_t firstUnderscore  = fname.find('_');
								size_t secondUnderscore = fname.find('_', firstUnderscore + 1);
								size_t thirdUnderscore  = fname.find('_', secondUnderscore + 1);

								if (firstUnderscore == string::npos || 
										secondUnderscore == string::npos ||
										thirdUnderscore == string::npos) {
										
										LOG_WARN("Invalid filename format");
										continue;	
								}

								int label = std::stoi(fname.substr(firstUnderscore + 1, secondUnderscore - firstUnderscore - 1));
								string name = fname.substr(secondUnderscore + 1, thirdUnderscore - secondUnderscore - 1);
								printf ("[%s] name: %s, label: %d\n", __func__, name.c_str(), label);

								Mat colorImg = imread(entry.path().string(), IMREAD_COLOR);
								if (colorImg.empty()) {
									//	LOG_WARN(QString("Failed to read image: %1").arg(entry.path().string()));
										continue;
								}

								Mat grayImg;
								cv::cvtColor(colorImg, grayImg, COLOR_BGR2GRAY);

								cv::resize(grayImg, grayImg, Size(200, 200));
								//cv::cvtColor(colorImg, colorImg, cv::COLOR_BGR2RGB);

								storedFaces[label].push_back(grayImg);
								images.push_back(grayImg);
								labels.push_back(label);

								/*
								newUser.label = label;
								newUser.name = QString::fromStdString(name);
								newUser.colorFaceImage = colorImg;
								newUser.grayFaceImage = grayImg;
								storedUsers.push_back(newUser);
								*/
						}
						
				}

			/*	
				if (!images.empty()) {
						trainOrUpdateModel(images, labels);
				}
			*/
		} else {
				emit errorOccurred("USER face directory does not exist or is not a directory.");
		}
		printf ("[%s] ----------------------------------------\n", __func__); 
}

void FaceRecognizerWorker::trainOrUpdateModel(const vector<Mat>& images, const vector<int>& labels) 
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

void FaceRecognizerWorker::loadModel()
{	
		if (!recognizer) {
				recognizer = face::LBPHFaceRecognizer::create();
		}
		try {
				recognizer->read(FACE_MODEL_FILE);
				qDebug() << "모델 로딩 성공";

		} catch (cv::Exception &e) {
					qWarning() << "No existing model found. Will create a new model.";
		}
}


int FaceRecognizerWorker::getNextLabel() 
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

void FaceRecognizerWorker::loadLabelMap()
{
		 int rc = 0;

    ifstream ifs(USER_LABEL_FILE);

    rc = Utile::checkFileExist(USER_LABEL_FILE, R_OK + W_OK);
    if (rc == -1) {
        rc = Utile::createFile(USER_LABEL_FILE, R_OK + W_OK);
        if (rc == -1) {
            Logger::writef("[%s] Failed to create file(%s)\n", __func__, USER_LABEL_FILE);
            exit(1);
        }
    }

    if (ifs.is_open()) {
        int lbl;
        string name;

        while (ifs >> lbl >> name) {
            labelMap[lbl] = name;
						printf ("[%s] label: %d, name: %s\n", __func__, lbl, name.c_str());
        }
    }
}

void FaceRecognizerWorker::saveLabelToFile(int label, const string &name)
{
	 ofstream ofs (USER_LABEL_FILE, ios::app);
   if (ofs.is_open()) ofs << label << " " << name << endl;
}

void FaceRecognizerWorker::processFrame() 
{
		Mat frame, gray;
    Mat colorFace;

    cap >> frame;
    if (frame.empty()) return;

    cvtColor(frame, gray, cv::COLOR_RGB2GRAY);
    equalizeHist(gray, gray);

    vector<Rect> faces;
    faceDetector.detectMultiScale(
        gray, faces, 1.1, 5, CASCADE_SCALE_IMAGE, Size(100, 100));

    for (const auto& face : faces) {
        colorFace = frame(face);

        Mat alignedFace = alignAndNormalizeFace(gray, face);
        if (alignedFace.empty()) continue;

        QString labelText;
        Scalar boxColor;

        if (!isRegistering) {
            int predictedLabel = -1;
            double confidence = 999.0;

            //if (!storedFaces.empty()) {
            if (!storedFaces.empty()) {
                recognizer->predict(alignedFace, predictedLabel, confidence);
            }

            if (confidence < 60.0 && labelMap.count(predictedLabel)) {
                labelText = QString::fromStdString(labelMap[predictedLabel]);
                boxColor = Scalar(0, 255, 0);
            } else {
                labelText = "Unknown";
                boxColor= Scalar(0, 0, 255);
            }

            //rectangle(frame, face, boxColor, 2);
            //putText(frame, labelText.toStdString(), Point(face.x + 100, face.y + face.height + 20),
            //       FONT_HERSHEY_SIMPLEX, 0.8, boxColor, 2);
						drawTransparentBox(frame, face, boxColor, 0.3);
						drawCornerBox(frame, face, boxColor, 2, 25);
						putText(frame, labelText.toStdString(), Point(face.x, face.y - 10),
										FONT_HERSHEY_DUPLEX, 1.0, boxColor, 2);
        }
        else if (isRegistering) {
            labelText = "Registering...";
            boxColor = Scalar(255, 0, 0);

            rectangle(frame, face, Scalar(255, 0, 0), 2);
            putText(frame, labelText.toStdString(), Point(face.x, face.y + face.height + 20),
                    FONT_HERSHEY_SIMPLEX, 0.8, boxColor, 2);

            if (captureCount == 0 && isDuplicateFace(alignedFace)) {
                isRegistering = false;
                return;
            }

            if (captureCount < 20) {
                if (!fs::exists(USER_FACES_DIR))
                    fs::create_directory(USER_FACES_DIR);


                string filename = string(USER_FACES_DIR) + "face_" + to_string(currentLabel) + "_" + userName.toStdString() + "_" + to_string(captureCount + 1) + ".png";

								/*
                UserData newUser;

                newUser.label = currentLabel;
                newUser.name  = userName;
                newUser.colorFaceImage = colorFace;
                newUser.grayFaceImage = alignedFace;

                storedUsers.push_back(newUser);
								*/

                if (!imwrite(filename, colorFace)) {
                  qDebug() << "이미지 저장 실패: " << QString::fromStdString(filename);
                }
                storedFaces[currentLabel].push_back(alignedFace);
                labelMap[currentLabel] = userName.toStdString();
                captureCount++;
                //ui->statusbar->showMessage(QString("[등록중] %1: %2/20").arg(userName).arg(captureCount));

                if (captureCount >= 20) {
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

                    isRegistering = false;

										emit recognitionResult(QString("[등록 완료] %1").arg(userName));
                }
            }
        }
    }

    // 🔴 루프가 끝난 뒤 최종 프레임을 한 번만 그리기!
    cvtColor(frame, frame, cv::COLOR_BGR2RGB);
    QImage qimg(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);
		emit frameReady(qimg.copy());


		QString msg = QString("Faces detected: %1").arg(faces.size());
		emit recognitionResult(msg);
}

void FaceRecognizerWorker::stop() {
		running = false;
}

Mat FaceRecognizerWorker::alignAndNormalizeFace(const Mat& grayFrame, const Rect& faceRect)
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

bool FaceRecognizerWorker::isDuplicateFace(const Mat& newFace)
{
		if (!recognizer || newFace.empty()) {
        std::cerr << "[오류] 얼굴 인식기가 초기화되지 않았거나 입력 이미지가 비어 있습니다." << std::endl;
        return false;
    }

    int predictedLabel = -1;
    double confidence = 0.0;

    // 예측 수행 (입력은 반드시 흑백이어야 함)
    cv::Mat gray;
    if (newFace.channels() == 3)
        cv::cvtColor(newFace, gray, cv::COLOR_RGB2GRAY);
    else
        gray = newFace;

		//if (!storedFaces.empty()) {
		if (!storedFaces.empty()) {
				recognizer->predict(gray, predictedLabel, confidence);
		}

    std::cout << "[" << __func__ << "] 예측된 라벨: " << predictedLabel << ", 신뢰도: " << confidence << std::endl;

    // 신뢰도 기준으로 중복 여부 판단
    const double DUPLICATE_THRESHOLD = 50.0; // 작을수록 엄격 (OpenCV LBPH 기준 50~100 적절)

    return (confidence < DUPLICATE_THRESHOLD) && (confidence) && (predictedLabel != -1);
}



void FaceRecognizerWorker::updateFrame() {
    Mat frame, gray;
    Mat colorFace;

    cap >> frame;
    if (frame.empty()) {
				qDebug() << "Frame is empty!!";
				return;
		}

		//qDebug() << "✅ 프레임 캡처됨, size:" << frame.cols << "x" << frame.rows;

		//cv::Mat rgbFrame;
		//cv::cvtColor(frame, rgbFrame, cv::COLOR_BGR2RGB);  // OpenCV는 BGR, Qt는 RGB

		//QImage image(rgbFrame.data, rgbFrame.cols, rgbFrame.rows, rgbFrame.step, QImage::Format_RGB888);
		//emit frameReady(image.copy());  // QImage 복사해서 전달
		//qDebug() << "✅ frameReady 시그널 emit됨";

    cvtColor(frame, gray, cv::COLOR_RGB2GRAY);
    equalizeHist(gray, gray);

    vector<Rect> faces;
    faceDetector.detectMultiScale(
        gray, faces, 1.1, 5, CASCADE_SCALE_IMAGE, Size(100, 100));

    for (const auto& face : faces) {
        colorFace = frame(face);

        Mat alignedFace = alignAndNormalizeFace(gray, face);
        if (alignedFace.empty()) continue;

        QString labelText;
        Scalar boxColor;

        if (!isRegistering) {
            int predictedLabel = -1;
            double confidence = 999.0;

            //if (!storedFaces.empty()) {
            if (!storedFaces.empty()) {
                recognizer->predict(alignedFace, predictedLabel, confidence);
            }

            if (confidence < 60.0 && labelMap.count(predictedLabel)) {
                labelText = QString::fromStdString(labelMap[predictedLabel]);
                boxColor = Scalar(0, 255, 0);
            } else {
                labelText = "Unknown";
                boxColor= Scalar(0, 0, 255);
            }

            //rectangle(frame, face, boxColor, 2);
						// putText(frame, labelText.toStdString(), Point(face.x + 100, face.y + face.height + 20),
            //        FONT_HERSHEY_SIMPLEX, 0.8, boxColor, 2);

						drawTransparentBox(frame, face, boxColor, 0.3);
						drawCornerBox(frame, face, boxColor, 2, 25);
						putText(frame, labelText.toStdString(), Point(face.x, face.y - 10),
										FONT_HERSHEY_DUPLEX, 1.0, boxColor, 2);
		
        }
        else if (isRegistering) {
            labelText = "Registering...";
            boxColor = Scalar(255, 0, 0);

            //rectangle(frame, face, Scalar(255, 0, 0), 2);
            //putText(frame, labelText.toStdString(), Point(face.x, face.y + face.height + 20),
            //       FONT_HERSHEY_SIMPLEX, 0.8, boxColor, 2);


						drawTransparentBox(frame, face, boxColor, 0.3);
						drawCornerBox(frame, face, boxColor, 2, 25);
						putText(frame, labelText.toStdString(), Point(face.x, face.y - 10),
										FONT_HERSHEY_DUPLEX, 1.0, boxColor, 2);

            if (captureCount == 0 && isDuplicateFace(alignedFace)) {
								emit duplicatedFace("이미 등록된 얼굴입니다.");
                isRegistering = false;
                return;
            }

            if (captureCount < 20) {
                if (!fs::exists(USER_FACES_DIR))
                    fs::create_directory(USER_FACES_DIR);

                string filename = CONF_PATH + string("data/face_") + to_string(currentLabel) + "_" + userName.toStdString() + "_" + to_string(captureCount + 1) + ".png";
								/*
                UserData newUser;

                newUser.label = currentLabel;
                newUser.name  = userName;
                newUser.colorFaceImage = colorFace;
                newUser.grayFaceImage = alignedFace;

                storedUsers.push_back(newUser);
								*/

                if (!imwrite(filename, colorFace)) {
                  qDebug() << "이미지 저장 실패: " << QString::fromStdString(filename);
                }
                storedFaces[currentLabel].push_back(alignedFace);
                labelMap[currentLabel] = userName.toStdString();
                captureCount++;
                //ui->statusbar->showMessage(QString("[등록중] %1: %2/20").arg(userName).arg(captureCount));

                if (captureCount >= 20) {
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
                    isRegistering = false;
                    //ui->statusbar->showMessage("[등록 완료]" + userName);
                }
            }
        }
    }

    // 🔴 루프가 끝난 뒤 최종 프레임을 한 번만 그리기!
    cvtColor(frame, frame, cv::COLOR_BGR2RGB);
    QImage qimg(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);
		emit frameReady(qimg.copy());
    //ui->cameraLabel->setPixmap(QPixmap::fromImage(qimg));
}




