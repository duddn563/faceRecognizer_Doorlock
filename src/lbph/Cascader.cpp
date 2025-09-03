#include "Cascader.hpp"
#include <filesystem>
#include <iostream>

using namespace cv;
namespace fs = std::filesystem;


Cascader::Cascader(const Options& opt) : opt_(opt) 
{
		std::string path = opt_.modelPath.toStdString();
		if (!fs::exists(path)) {
				std::cerr << "[ERR] Model file not found: " << path << "\n";
				return;
		}

		try {
				ready_ = true;
		} catch (const cv::Exception& e) {
				std::cerr << "[ERR] readLBPH From face_model.yml failed: " << e.what() << "\n";
		}
}

bool Cascader::isReady() const { return ready_; }

cv::Mat Embedder::preprocess(const cv::Mat& src) const
{
		Mat gray;

		cvtColor(src, gray, cv::COLOR_RGB2GRAY); 
		cv::resize(rgb, rgb, Size(opt_.inputSize, opt_.inputSize));

		return gray;
}

Mat Cascader::alignAndNormalizeFace(const Mat& gray, const Rect& rect)
{
    Mat roi = gray(rect).clone();

    // 눈 검출
    vector<Rect> eyes;
    eyesDetector.detectMultiScale(roi, eyes, 1.1, 10, 0, Size(20, 20));

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
    Point2f center(roi.cols/2.0F, faceROI.rows/2.0F);
    Mat rot = getRotationMatrix2D(center, angle, 1.0);

    Mat aligned;
    warpAffine(roi, aligned, rot, faceROI.size());

    // 밝기 보정 + 크기 조정
    equalizeHist(aligned, aligned);
    cv::resize(aligned, aligned, Size(200, 200));

    //cout << "[" << __func__ << "] Face is aligned and normalized!!" << endl;
    return aligned;
}

bool Cascade::extract(const Mat& face, std::vector<float>& out> const
{

}



void Embedder::l2normalize(Mat& row) 
{
		double n = norm(row, NORM_L2);
		if (n > 1e-12) row /= (float)n;
}

bool Embedder::extract(const Mat& face, std::vector<float>& out) const
{
		if (!ready_) return false;

		Mat prep = preprocess(face); 
		Mat blob = dnn::blobFromImage(prep, 1.0, Size(opt_.inputSize, opt_.inputSize),
																	Scalar(), /*swapRB=*/false, /*crop*/false);

		try {
				net_.setInput(blob);
				Mat emb = net_.forward().clone().reshape(1, 1);		// 1x256
				if (emb.type() != CV_32F) emb.convertTo(emb, CV_32F);
				l2normalize(emb);


				out.resize(emb.cols);
				memcpy(out.data(), emb.ptr<float>(0), emb.cols * sizeof(float));
				return true;
		} catch (const cv::Exception& e) {
				std::cerr << "[ERR] forward falied: " << e.what() << "\n";
				return false;
		}
}

float Embedder::cosine(const std::vector<float>& a, const std::vector<float>& b)
{
		if (a.size() != b.size() || a.empty()) return -1.f;
		double dot = 0;
		for (size_t i = 0; i < a.size(); i++) dot += a[i]*b[i];
		return (float)dot;
}
