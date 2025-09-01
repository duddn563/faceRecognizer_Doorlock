#pragma once
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <QString>
#include <vector>

class Embedder {
public:
		struct Options {
				QString modelPath;
				int inputSize = 128;		// 128x128 입력
				bool useRGB = true;			// 모델이 RGB 입력 모델
		};

		explicit Embedder(const Options& opt);
		bool isReady() const;

		// 얼굴 이미지에서 256차원 백터 추출
		bool extract(const cv::Mat& face, std::vector<float>& out) const;

		// 코사인 유사도 계산
		static float cosine(const std::vector<float>& a, const std::vector<float>& b);

private:
		Options opt_;
		mutable cv::dnn::Net net_;
		bool ready_ = false;

		cv::Mat preprocess(const cv::Mat& src) const;
		static void l2normalize(cv::Mat& row);
};


