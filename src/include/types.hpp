#pragma once
#include <vector>
#include <array>
#include <optional>
#include <QString>
#include <opencv2/opencv.hpp>

struct FaceDet {
	cv::Rect box;
	std::array<cv::Point2f, 5> lmk;				// leftEye, right Eye, nose, mouthL, mouthR
	float score;
};

struct DetOut {
	cv::Rect box;
	std::array<cv::Point2f,5> lm;
	float score = 0.0f;
};

// 매칭 결과
struct MatchResult {
        int     id   = -1;
        QString name;
        float   sim  = -1.0f;
};
// top-2 매칭 결과
struct MatchTop2 {
    int   bestIdx   = -1;
    float bestSim   = -2.0f;
    int   secondIdx = -1;
    float secondSim = -2.0f;
};


struct GalleryEntry {
		int									id = -1;
		QString							name;
		std::vector<float>	emb;			// 128 floats
};
struct UserEmbedding {
    int                 id = -1;
    QString             name;
    std::vector<float>  embedding; // L2 정규화된 벡터
		cv::Mat							proto;				   // 1xD, CV_32F, L2=1 고정 클론
};


