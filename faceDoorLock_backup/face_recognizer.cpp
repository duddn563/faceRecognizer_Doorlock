#include "face_recognizer.hpp"
#include "utils.hpp"
#include <filesystem>
#include <fstream>

namespace fs = filesystem;

FaceRecognizer::FaceRecognizer() {
    faceCascade.load(FACERECOGNIZER);
    loadUserFaces(LOAD_FACES_DIR);
}

bool FaceRecognizer::loadUserFaces(const string& dir) {
		userFaces.clear();  // 기존 데이터 초기화
			
    if (!fs::exists(dir)) fs::create_directory(dir);

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".jpg") {
            string name = entry.path().stem().string();
            Mat img = imread(entry.path().string());
						if (img.empty()) continue;

            resize(img, img, Size(200, 200));
            userFaces[name] = img;
        }
    }

		cout << "[INFO] " << userFaces.size() << "명의 사용자 얼굴을 로드했습니다." << endl;
    return true;
}


void FaceRecognizer::compareAndDisplay(Mat& frame, const Mat& faceROI, const Rect& faceRect) {
    double bestSim = -1;
    string matchedName = "Unknown";

    for (const auto& [name, refFace] : userFaces) {
        Mat refGray, targetGray;
        cvtColor(refFace, refGray, COLOR_BGR2GRAY);
        cvtColor(faceROI, targetGray, COLOR_BGR2GRAY);

        Mat hist1, hist2;
        int histSize = 256;
        float range[] = {0, 256};
        const float* histRange = {range};

        calcHist(&refGray, 1, 0, Mat(), hist1, 1, &histSize, &histRange);
        calcHist(&targetGray, 1, 0, Mat(), hist2, 1, &histSize, &histRange);

        normalize(hist1, hist1, 0, 1, NORM_MINMAX);
        normalize(hist2, hist2, 0, 1, NORM_MINMAX);

        double sim = compareHist(hist1, hist2, HISTCMP_CORREL);
        if (sim > bestSim) {
            bestSim = sim;
            matchedName = name;
        }
    }

    string msg = (bestSim > 0.6) ? "Access: " + matchedName : "Access Denied";
    Scalar color = (bestSim > 0.6) ? Scalar(0, 255, 0) : Scalar(0, 0, 255);
    rectangle(frame, faceRect, color, 2);
    putText(frame, msg, Point(faceRect.x, faceRect.y + faceRect.height + 30),
                FONT_HERSHEY_SIMPLEX, 0.8, color, 2);
}

void FaceRecognizer::run() {
    VideoCapture cap(0);
    if (!cap.isOpened()) {
        cerr << "카메라 열기 실패!" << endl;
        return;
    }

    Mat frame, gray;
    vector<Rect> faces;

		string statusMessage = "";
		int messageCountdown = 0;

    while (true) {
        cap >> frame;
        if (frame.empty()) break;

        cvtColor(frame, gray, COLOR_BGR2GRAY);
        faceCascade.detectMultiScale(gray, faces, 1.1, 4);

        for (const auto& face : faces) {
            Mat faceROI = frame(face);
            resize(faceROI, faceROI, Size(200, 200));
            compareAndDisplay(frame, faceROI, face);
        }

				if (messageCountdown > 0) {
						putText(frame, statusMessage, Point(10, 40),
										FONT_HERSHEY_SIMPLEX, 0.8, Scalar(255, 255, 0), 2);
						messageCountdown--;
				}

				imshow (WINDOW_NAME, frame);
        char key = (char)waitKey(30);
				cout << "key: " << key << endl;
        if (key == 27) break; // ESC
        else if (key == 's') {
            if (!faces.empty()) {
								statusMessage = "Registering user...";
								messageCountdown = 60;

                registerNewUser(frame, faces[0]);

								statusMessage = "[Complete register]";
								messageCountdown = 60;
            }
						else {
								statusMessage = "Unable to detect face.";
								messageCountdown = 60;
						}

        }
    }
    cap.release();
    destroyAllWindows();
}

