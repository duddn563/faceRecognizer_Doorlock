#pragma once
#include <array>
#include <opencv2/opencv.hpp>

class LandmarkAligner {
	public:
		LandmarkAligner()  = default;
		~LandmarkAligner() = default;

		// 5점 기준 정렬. 성공 시 정렬된 BGR 얼굴(출력 크기 outSize), 실패 시 빈 Mat
    // lmk 순서: [LE, RE, Nose, LM, RM]  (YuNet/네 계약과 동일)
		cv::Mat alignBy5pts(const cv::Mat& srcBgr,
										  const std::array<cv::Point2f,5>& src5_in,
											const cv::Size& outSize = {112, 112});

	private:
		// 기준 좌표(ArcFace 112x112 계열). outSize에 맞춰 스케일링해서 사용.
    static const std::array<cv::Point2f,5> kDst5_112;
};
