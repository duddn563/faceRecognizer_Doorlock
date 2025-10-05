#include "LivenessGate.hpp"
#include <QtCore/QDebug>
#include <algorithm>

bool LivenessGate::passQualityForRecog(const cv::Rect& box, const cv::Mat& bgr)
{
    // 튜닝 파라미터
    const int    kMinBoxW         = 96;
    const int    kMinBoxH         = 96;
    const double kBlurThr         = 25.0;   // Laplacian variance 최소
    const double kMinMean         = 30.0;   // 노출 하한
    const double kMaxMean         = 230.0;  // 노출 상한
    const double kMinStd          = 12.0;   // 전체 대비
    const double kClipRatioMax    = 0.30;   // 0/255 클리핑 비율

	  // === 기본 체크: 입력 유효성 & 박스 크기 ===
	if (bgr.empty() || bgr.cols < 64 || bgr.rows < 64) {
		qDebug() << "[Qual:FAIL] empty/too small crop"
						 << "crop=" << bgr.cols << "x" << bgr.rows;
		return false;
	}
	if (box.width < kMinBoxW || box.height < kMinBoxH) {
		qDebug() << "[Qual:FAIL] small box"
						 << "box=" << box.width << "x" << box.height
						 << "need >= " << kMinBoxW << "x" << kMinBoxH;
		return false;
	}

	 // 1) ROI 자르기 (항상 원본 프레임에서)
  const cv::Rect full(0, 0, bgr.cols, bgr.rows);
  const cv::Rect roi = box & full;
  if (roi.width <= 0 || roi.height <= 0) return false;
  cv::Mat face = bgr(roi);

	  // === 그레이스케일 변환 ===
	cv::Mat gray;
	if (face.channels() == 3) cv::cvtColor(face, gray, cv::COLOR_BGR2GRAY);
	else if (face.channels() == 4) cv::cvtColor(face, gray, cv::COLOR_BGRA2GRAY);
	else gray = face;

	 // === 블러(샤프니스) 체크: Laplacian variance ===
	cv::Mat lap; 
	cv::Laplacian(gray, lap, CV_64F);
	cv::Scalar mu, sigma;
	cv::meanStdDev(lap, mu, sigma);
	const double lapVar = sigma[0] * sigma[0];
	if (lapVar < kBlurThr) {
		qDebug() << "[Qual:FAIL] too blur var=" << lapVar << "thr=" << kBlurThr;
		return false;
	}

   // === 전체 노출/대비 체크 ===
	cv::Scalar mean, stddev;
	cv::meanStdDev(gray, mean, stddev);
	const double m = mean[0];
	const double s = stddev[0];
	if (m < kMinMean || m > kMaxMean) {
		qDebug() << "[Qual:FAIL] bad exposure mean=" << m
						 << "range=" << kMinMean << "~" << kMaxMean;
		//return false;
	}
	if (s < kMinStd) {
		qDebug() << "[Qual:FAIL] low contrast std=" << s << "need >= " << kMinStd;
		return false;
	}

	 // === 히스토그램 클리핑 비율 체크 (0/255 바깥쪽 과다 몰림) ===
	{
		int histSize = 256;
		float range[] = {0, 256};
		const float* ranges = { range };

		// OpenCV C++ API는 배열 포인터 시그니처를 요구하므로 래핑 배열 준비
		int histSizes[]					 = { histSize };
		const float* rangesArr[] = { ranges };
		int channels[]					 = { 0 };

		cv::Mat hist;
		cv::calcHist(&gray, 1, channels, cv::Mat(), hist, 1, histSizes, rangesArr, true, false);
		const double total			= static_cast<double>(gray.total());
		const double clip0		  = hist.at<float>(0)		/ total;
		const double clip255    = hist.at<float>(255) / total;

		if (clip0 > kClipRatioMax || clip255 > kClipRatioMax) {
			qDebug() << "[Qual:FAIL] clipping"
							 << "clip0=" << clip0 << "clip255=" << clip255
							 << "limit=" << kClipRatioMax;
			return false;
		}
	}

	// === 중앙 64x64 영역 대비 체크 ===
	{
		int cx = std::max(0, std::min(face.cols/2 - 32, face.cols - 64));
		int cy = std::max(0, std::min(face.rows/2 - 32, face.rows - 64));
		cv::Rect cR(cx, cy, 64, 64);

		cv::Mat center = gray(cR);
		cv::Scalar cm, cs;
		cv::meanStdDev(center, cm, cs);

		if (cs[0] < (kMinStd - 2)) {
			qDebug() << "[Qual:FAIL] low center contrast std=" << cs[0]
							 << "need >= " << (kMinStd - 2);
			return false;
		}
	}

	// Pass all gate
	/*
	qDebug() << "[Qual::PASS]"
					 << "box=" << box.width << "x" << box.height
					 << "mean=" << m << "std=" << s << "blurVar=" << lapVar;
	*/
	
	return true;
}
