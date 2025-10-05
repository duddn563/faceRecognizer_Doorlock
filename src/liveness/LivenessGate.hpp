#pragma once
#include <opencv2/opencv.hpp>

class LivenessGate {
	public:
		bool passQualityForRecog(const cv::Rect& box, const cv::Mat& face);
};
