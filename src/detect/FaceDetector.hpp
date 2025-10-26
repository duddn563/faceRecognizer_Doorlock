#pragma once
#include <optional>
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/objdetect.hpp>  // cv::FaceDetectorYN
#include "include/types.hpp"			// FaceDet

// YuNet/UltraFace 등 어떤 백엔드든 래핑 가능하도록 최소 인터페이스만 둠
class FaceDetector {
	public:
		FaceDetector() = default;
		~FaceDetector() = default;

		// YuNet 초기화 (modelPath 필수)
		bool init(const std::string& modelPath,
							int inputW = 320, int inputH = 240,
							float scoreThr = 0.6f, float nmsThr = 0.3f, int topK = 500,
							int backend = cv::dnn::DNN_BACKEND_OPENCV,
							int target  = cv::dnn::DNN_TARGET_CPU);


		// 네 랭킹 규칙(중앙+큰 얼굴 선호)으로 1개만 선택
		std::optional<FaceDet> detectBest(const cv::Mat& bgr) const;

		// 프레임에서 전체 후보 반환 (원본 좌표계)
		std::vector<FaceDet> detectAll(const cv::Mat& bgr) const;

	private:
		// YuNet 출력 파서 (네 parseYuNet 그대로)
		static std::vector<FaceDet> parseYuNet(const cv::Mat& dets, float scoreThresh);

	private:
		bool ready_ = false;
		int inW_ = 320; 
		int inH_ = 240;
		float scoreThr_ = 0.6f; 
		float nmsThr_ = 0.3;
		int topK_ = 500;
		int backend_  = cv::dnn::DNN_BACKEND_OPENCV;
		int target_   = cv::dnn::DNN_TARGET_CPU;


		std::string modelPath_;
		cv::Ptr<cv::FaceDetectorYN> yunet_;		// Yunet 핸들
		mutable cv::Size yunet_InputSize_{0, 0};  // setInputSize chache
};

