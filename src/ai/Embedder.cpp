#include "Embedder.hpp"
#include <filesystem>
#include <iostream>


using namespace cv;
namespace fs = std::filesystem;

Embedder::Embedder(const Options& opt) : opt_(opt) 
{
		std::string path = opt_.modelPath.toStdString();
		if (!fs::exists(path)) {
				std::cerr << "[ERR] Model file not found: " << path << "\n";
				return;
		}

		try {
				net_ = dnn::readNetFromONNX(path);
				net_.setPreferableBackend(dnn::DNN_BACKEND_OPENCV);
				net_.setPreferableTarget(dnn::DNN_TARGET_CPU);
				ready_ = true;
		} catch (const cv::Exception& e) {
				std::cerr << "[ERR] readNetFromONNX failed: " << e.what() << "\n";
		}
}

bool Embedder::isReady() const { return ready_; }

cv::Mat Embedder::preprocess(const cv::Mat& src) const
{
		Mat rgb;
		if (opt_.useRGB) cvtColor(src, rgb, cv::COLOR_BGR2RGB);
		//else rgb = src.clone();
        else rgb = src;

        Size sz(opt_.inputSize, opt_.inputSize);

        // ZeroToOne: 1/255, MinusOneToOne: (x-127.5)/127.5
        double scalef = (opt_.norm == Options::Norm::ZeroToOne) ? 1.0/255.0 : 1.0/127.5;
        Scalar meanV  = (opt_.norm == Options::Norm::ZeroToOne) ? Scalar() : Scalar(127.5, 127.5, 127.5); 

        // blobFromImage가 리사이즈 + 정규화까지 한 번에 처리
        Mat blob = dnn::blobFromImage(
                    rgb,        // 입력
                    scalef,     // scalarfactor
                    sz,         // size
                    meanV,      // mean
                    /*swapRB=*/false, // 이미 useRGB면 true로 바꿔줬으니 여기선 false
                    /*crop=*/false,
                    CV_32F
        );

        return blob;

        /* 이전 사이즈 조정 부분
		resize(rgb, rgb, Size(opt_.inputSize, opt_.inputSize));
		rgb.convertTo(rgb, CV_32F, 1.0/255.0);
		return rgb;
        */
}

void Embedder::l2normalize(Mat& row) 
{
		double n = norm(row, NORM_L2);
		if (n > 1e-12) row /= (float)n;
}


bool Embedder::extract(const Mat& face, std::vector<float>& out) const
{
    if (!ready_) return false;

	//	Mat prep = preprocess(face); 
	//	Mat blob = dnn::blobFromImage(prep, 1.0, Size(opt_.inputSize, opt_.inputSize),
	//																Scalar(), /*swapRB=*/false, /*crop*/false);
    try {
           Mat blob = preprocess(face);
           net_.setInput(blob);
           
           Mat emb = net_.forward();        // NxD 또는 1xD
           emb = emb.reshape(1, 1).clone();  // 1xD, 연속 메모리 보장
           if (emb.type() != CV_32F) emb.convertTo(emb, CV_32F);

           l2normalize(emb);

           out.resize((size_t)emb.cols);
           std::memcpy(out.data(), emb.ptr<float>(0), emb.cols * sizeof(float));
           return true;
    } catch (const cv::Exception& e) {
        std::cerr << "[ERR] forward failed: " << e.what() << "\n";
        return false;
    }

    /*
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
    */
}

float Embedder::cosine(const std::vector<float>& a, const std::vector<float>& b)
{
		if (a.size() != b.size() || a.empty()) return -1.f;
		double dot = 0;
		for (size_t i = 0; i < a.size(); i++) dot += a[i]*b[i];
		return (float)dot;
}
