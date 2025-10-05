#include "LandmarkAligner.hpp"
#include <algorithm>
#include <cmath>

const std::array<cv::Point2f, 5> LandmarkAligner::kDst5_112 = {{
	{38.2946f, 51.6963f},			// LE
	{73.5318f, 50.5014f},			// RE
	{56.0252f, 71.7366f},			// Nose
	{41.5493f, 92.3655f},			// LM
	{70.7299f, 92.2041f}			// RM
}};

cv::Mat LandmarkAligner::alignBy5pts(const cv::Mat& srcBgr, 
																		 const std::array<cv::Point2f,5>& src5_in,
																		 const cv::Size& outSize)
{
	// 입력/출력 크기 가드
	if (srcBgr.empty() || outSize.width <= 0 || outSize.height <= 0) 
		return cv::Mat();

	 // [LE, RE, Nose, LM, RM] 순서 유지 + 좌우 정합 보정
	std::array<cv::Point2f, 5> s = src5_in;		// [LE, RE, Nose, LM, RM]
	if (s[0].x > s[1].x) std::swap(s[0], s[1]);
	if (s[3].x > s[4].x) std::swap(s[3], s[4]);


	std::vector<cv::Point2f> src(s.begin(), s.end());
	std::vector<cv::Point2f> dst(kDst5_112.begin(), kDst5_112.end());	// 112x112 기준 타겟

	const cv::Size warpSize(112, 112);
	cv::Mat M = cv::estimateAffinePartial2D(src, dst, cv::noArray(), cv::RANSAC, 3.0, 2000, 0.99);
	if (M.empty()) return {};

	cv::Mat aligned112;
	cv::warpAffine(srcBgr, aligned112, M, warpSize,
								 cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(127, 127, 127));

	if (outSize == warpSize) return aligned112;

	cv::Mat out;
	cv::resize(aligned112, out, outSize, 0, 0, cv::INTER_LINEAR);
	return out;
}
