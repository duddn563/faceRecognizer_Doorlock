#pragma once
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <QString>
#include <vector>
#include <mutex>

class Embedder {
public:
		struct Options {
				QString modelPath;
				QString detectorModel;
				int inputSize = 128;		// 128x128 입력
				bool useRGB = true;			// 모델이 RGB 입력 모델
                bool externalNorm = false;  // 내부에서 강제 크롭 여부
                enum class Norm { ZeroToOne, MinusOneToOne } norm = Norm::ZeroToOne;

		};

		explicit Embedder(const Options& opt);
		bool isReady() const;

		// 얼굴 이미지에서 256차원 백터 추출
		bool extract(const cv::Mat& face_rgb, std::vector<float>& out) const;

		// 코사인 유사도 계산
		static float cosine(const std::vector<float>& a, const std::vector<float>& b);
		bool isTrivialFrame(const cv::Mat& rgb, double meanMin=1.0, double stdMin=1.0);

private:
		Options opt_;
		mutable std::mutex mtx_;
		mutable cv::dnn::Net net_;
		bool ready_ = false;

		cv::Mat preprocess(const cv::Mat& src) const;
		static void l2normalize(cv::Mat& row);
};


