#include "LivenessGate.hpp"
#include "util/textDrawUtil.hpp"
#include <QtCore/QDebug>
#include <algorithm>


#define DEBUG
#define DEMO


DetectedStatus LivenessGate::passQualityForRecog(const cv::Rect& box, cv::Mat& bgr)
{
    if (bgr.empty() || bgr.cols < 64 || bgr.rows < 64) return DetectedStatus::FaceNotDetected;
    // === 0) 중앙 위치 체크 ===
    {
        const int frameCx = bgr.cols / 2;
        const int frameCy = bgr.rows / 2;
        const int faceCx  = box.x + box.width  / 2;
        const int faceCy  = box.y + box.height / 2;
        double dx = std::abs(frameCx - faceCx) / static_cast<double>(bgr.cols / 2);
        double dy = std::abs(frameCy - faceCy) / static_cast<double>(bgr.rows / 2);
        const double kMaxCenterOffset = 0.30; // 30% 이상 벗어나면 경고

        if (dx > kMaxCenterOffset || dy > kMaxCenterOffset) {
           return DetectedStatus::CenterOff; // 발표용으로는 계속 통과
        }
    }

    // === 1) 얼굴 박스 크기 체크 ===
    const int kMinBoxW = 96, kMinBoxH = 96;
    if (box.width < kMinBoxW || box.height < kMinBoxH) {
        return DetectedStatus::TooSmall;
    }

    // === 2) 흐림 체크 (블러) ===
    cv::Mat gray;
    if (bgr.channels() == 3) cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    cv::Mat lap;
    cv::Laplacian(gray, lap, CV_64F);
    cv::Scalar mu, sigma;
    cv::meanStdDev(lap, mu, sigma);
    const double lapVar = sigma[0] * sigma[0];
    if (lapVar < 25.0) {
        return DetectedStatus::TooBlurry;
    }

    // === 3) 노출(밝기) 체크 ===
    cv::Scalar mean, stddev;
    cv::meanStdDev(gray, mean, stddev);
    if (mean[0] < 40.0) {
        return DetectedStatus::TooDark;
    }

    // === 4) 중앙부 대비 체크 ===
    int cx = std::max(0, std::min(gray.cols/2 - 32, gray.cols - 64));
    int cy = std::max(0, std::min(gray.rows/2 - 32, gray.rows - 64));
    cv::Rect cR(cx, cy, 64, 64);
    cv::Mat center = gray(cR);
    cv::Scalar cm, cs;
    cv::meanStdDev(center, cm, cs);
    if (cs[0] < 10.0) {
         return DetectedStatus::LowContrast;
    }

    return DetectedStatus::FaceDetected; // 발표용: 항상 통과
}

QualResult LivenessGate::checkQuality(const cv::Rect& box, const cv::Mat& rgb)
{
    // 튜닝 파라미터
    const int    kMinBoxW         = 96;
    const int    kMinBoxH         = 96;
    const double kBlurThr         = 25.0;   // Laplacian variance 최소
    const double kMinMean         = 30.0;   // 노출 하한
    const double kMaxMean         = 230.0;  // 노출 상한
    const double kMinStd          = 12.0;   // 전체 대비
    const double kClipRatioMax    = 0.30;   // 0/255 클리핑 비율

	QualResult qr;

	  // === 기본 체크: 입력 유효성 & 박스 크기 ===
	if (rgb.empty() || rgb.cols < 64 || rgb.rows < 64) {
		qDebug() << "[Qual:FAIL] empty/too small crop"
						 << "crop=" << rgb.cols << "x" << rgb.rows;
		qr.reason = QualResult::Reason::InvalidInput;
		return qr;
	}
	if (box.width < kMinBoxW || box.height < kMinBoxH) {
		qDebug() << "[Qual:FAIL] small box"
						 << "box=" << box.width << "x" << box.height
						 << "need >= " << kMinBoxW << "x" << kMinBoxH;
		qr.reason = QualResult::Reason::TooSmall;
		return qr;
	}

	 // 1) ROI 자르기 (항상 원본 프레임에서)
	const cv::Rect full(0, 0, rgb.cols, rgb.rows);
	const cv::Rect roi = box & full;
	if (roi.width <= 0 || roi.height <= 0) {
		qr.reason = QualResult::Reason::InvalidInput;
		return qr;
	}
	cv::Mat face = rgb(roi);
	qr.usedRoi = roi;

	  // === 그레이스케일 변환 ===
	cv::Mat gray;
	if (face.channels() == 3) cv::cvtColor(face, gray, cv::COLOR_RGB2GRAY);
	else if (face.channels() == 4) cv::cvtColor(face, gray, cv::COLOR_RGBA2GRAY);
	else gray = face;

	 // === 블러(샤프니스) 체크: Laplacian variance ===
	cv::Mat lap; 
	cv::Laplacian(gray, lap, CV_64F);
	cv::Scalar mu, sigma;
	cv::meanStdDev(lap, mu, sigma);
	const double lapVar = sigma[0] * sigma[0];
	if (lapVar < kBlurThr) {
		qDebug() << "[Qual:FAIL] too blur var=" << lapVar << "thr=" << kBlurThr;
		qr.reason = QualResult::Reason::TooBlur;
		return qr;
	}

   // === 전체 노출/대비 체크 ===
	cv::Scalar mean, stddev;
	cv::meanStdDev(gray, mean, stddev);
	const double m = mean[0];
	const double s = stddev[0];

	bool exposureBad = (m < kMinMean || m > kMaxMean);
	if (exposureBad) {
		qDebug() << "[Qual:WARN] bad exposure mean=" << m
						 << "range=" << kMinMean << "~" << kMaxMean;
		//return false;
	}
	if (s < kMinStd) {
		qDebug() << "[Qual:FAIL] low contrast std=" << s << "need >= " << kMinStd;
		qr.reason = QualResult::Reason::LowContrast;
		return qr;
	}

	 // === 히스토그램 클리핑 비율 체크 (0/255 바깥쪽 과다 몰림) ===
	{
		int histSize = 256;
		float range[] = {0, 256};
		const float* ranges = { range };

		// OpenCV C++ API는 배열 포인터 시그니처를 요구하므로 래핑 배열 준비
		int histSizes[]					 = { histSize };
		const float* rangesArr[]		 = { ranges };
		int channels[]					 = { 0 };

		cv::Mat hist;
		cv::calcHist(&gray, 1, channels, cv::Mat(), hist, 1, histSizes, rangesArr, true, false);
		const double total			= static_cast<double>(gray.total());
		const double clip0		  = hist.at<float>(0)	/ total;
		const double clip255    = hist.at<float>(255)	/ total;
		qr.clip0	= clip0;
		qr.clip255  = clip255;

		if (clip0 > kClipRatioMax || clip255 > kClipRatioMax) {
			qDebug() << "[Qual:FAIL] clipping"
							 << "clip0=" << clip0 << "clip255=" << clip255
							 << "limit=" << kClipRatioMax;
			qr.reason = QualResult::Reason::HistClipping;
			return qr;
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
			qr.reason = QualResult::Reason::LowCenterContrast;
			return qr;
		}
	}

	{
		// 프레임 전체의 중앙점
		cv::Point frameCenter(rgb.cols / 2, rgb.rows / 2);

		// 얼굴 박스의 중앙점
		cv::Point faceCenter(box.x + box.width / 2, box.y + box.height / 2);

		double dx = std::abs(frameCenter.x - faceCenter.x);
		double dy = std::abs(frameCenter.y - faceCenter.y);
		double distRatioX = dx / (rgb.cols / 2.0);
		double distRatioY = dy / (rgb.rows / 2.0);

		if (distRatioX > 0.20 || distRatioY > 0.20) {
			qDebug() << "[Qual:FAIL] face off-center"
					 << "distRatioX=" << distRatioX << "distRatioY=" << distRatioY;
			qr.reason = QualResult::Reason::FaceOffCenter;		
			return qr;
		}
	}

	// Pass all gate
#ifdef DEBUG
	qDebug() << "[Qual::PASS]"
					 << "box=" << box.width << "x" << box.height
					 << "mean=" << m << "std=" << s << "blurVar=" << lapVar;
#endif

	qr.pass = true;
	if (exposureBad) {
		qr.reason = QualResult::Reason::OverUnderExposure;
	} else {
		qr.reason = QualResult::Reason::Ok;
	}
	
	return qr;
}
