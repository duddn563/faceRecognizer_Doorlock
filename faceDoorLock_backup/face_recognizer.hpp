#ifndef FACE_RECOGNIZER_HPP
#define FACE_RECOGNIZER_HPP

#include <opencv2/opencv.hpp>
#include <string>
#include <map>

using namespace std;
using namespace cv;

const string FACE_ETC_DIR = "/usr/local/etc/face_doorlock/";
const string LOAD_FACES_DIR = FACE_ETC_DIR + "load_faces/";
const string FACERECOGNIZER = FACE_ETC_DIR + "haarcascade_frontalface_default.xml";
const string WINDOW_NAME = "Face doorlock";


class FaceRecognizer {
public:
    FaceRecognizer();
    void run();

private:
    CascadeClassifier faceCascade;
    map<string, Mat> userFaces;

    bool loadUserFaces(const string& dir);
    void registerNewUser(const Mat& frame, const Rect& faceRect);
    void compareAndDisplay(Mat& frame, const Mat& faceROI, const Rect& faceRect);
};

#endif

