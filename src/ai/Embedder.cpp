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
		else rgb = src.clone();

		resize(rgb, rgb, Size(opt_.inputSize, opt_.inputSize));
		rgb.convertTo(rgb, CV_32F, 1.0/255.0);
		return rgb;
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
