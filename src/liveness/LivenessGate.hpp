#pragma once
#include <opencv2/opencv.hpp>
#include "include/states.hpp"

struct QualResult {
	enum Reason {
		Ok = 0,
		TooSmall,
		TooBlur,
		OverUnderExposure,
		LowContrast,
		HistClipping,
		LowCenterContrast,
		FaceOffCenter,
		InvalidInput
	};

	bool pass = false;
	Reason reason = Reason::Ok;

	double mean		= 0.0;
	double std		= 0.0;
	double blurVar	= 0.0;
	double clip0	= 0.0;
	double clip255	= 0.0;
	cv::Rect usedRoi;
};

class LivenessGate {
	public:
		DetectedStatus passQualityForRecog(const cv::Rect& box, cv::Mat& face);
		QualResult checkQuality(const cv::Rect& box, const cv::Mat& rgb);
		
};
