#include "detect/FaceDetector.hpp"
#include <QtCore/QDebug>


bool FaceDetector::init(const std::string& modelPath,
												int inputW, int inputH,
												float scoreThr, float nmsThr, int topK,
												int backend, int target)
{
	modelPath_ = modelPath;
	inW_ = inputW; inH_ = inputH;
	scoreThr_ = scoreThr; nmsThr_ = nmsThr;
	topK_ = topK; backend_ = backend; target_ = target;

	try {
		yunet_ = cv::FaceDetectorYN::create(
				modelPath_, /*config=*/"", cv::Size(inW_, inH_),
				scoreThr_, nmsThr_, topK_, backend_, target_);
	} catch (const cv::Exception& e) {
		qWarning() << "[FaceDetector] YuNet create failed:" << e.what();
		ready_ = false;
		return false;
	}

	ready_ = (yunet_ != nullptr);
	yunet_InputSize_ = cv::Size(0,0);			// 첫 프리임 갱신
	if (!ready_) {
		qWarning() << "[FaceDetector] YuNet not ready";
		return false;
	}

	qDebug() << "[FaceDetector] YuNet init Ok"
					 << "model=" << QString::fromStdString(modelPath_)
					 << "in="    << inW_		  << "x" << inH_
					 << "thr="   << scoreThr_ <<  "/" << nmsThr_
					 << "topK="  << topK_			<< " backend=" << backend_
					 << "target=" << target_;

	return true;
}
// ==== 네 parseYuNet (score는 맨 끝(14), lmk는 4~13) ====
std::vector<FaceDet> FaceDetector::parseYuNet(const cv::Mat& dets, float scoreThresh)
{
	std::vector<FaceDet> out;
	if (dets.empty() || dets.cols < 15) return out;

	for (int i = 0; i < dets.rows; ++i) {
		const float x = dets.at<float>(i, 0);
		const float y = dets.at<float>(i, 1);
		const float w = dets.at<float>(i, 2);
		const float h = dets.at<float>(i, 3);

		const float score = dets.at<float>(i, 14);
		if (score < scoreThresh) continue;

		FaceDet f;
		f.box			= cv::Rect(cv::Point2f(x, y), cv::Size(w, h));
		f.score		= score;

		// landmark: [LE, RE, Nose, LM, RM]
		f.lmk[0]  = cv::Point2f(dets.at<float>(i, 4), dets.at<float>(i, 5));
		f.lmk[1]  = cv::Point2f(dets.at<float>(i, 6), dets.at<float>(i, 7));
		f.lmk[2]  = cv::Point2f(dets.at<float>(i, 8), dets.at<float>(i, 9));
		f.lmk[3]  = cv::Point2f(dets.at<float>(i, 10), dets.at<float>(i, 11));
		f.lmk[4]  = cv::Point2f(dets.at<float>(i, 12), dets.at<float>(i, 13));

		out.push_back(std::move(f));
	}

	return out;
}

std::vector<FaceDet> FaceDetector::detectAll(const cv::Mat& bgr) const
{
	std::vector<FaceDet> out;
	if (!ready_) return out;
	if (bgr.empty()) return out;

	// YuNet 입력 크기 갱신 (프레임 크기 변경 시 필수)
	try {
		const cv::Size cur = bgr.size();
		if (yunet_ && cur != yunet_InputSize_) {
			yunet_->setInputSize(cur);
			yunet_InputSize_ = cur;
		}
	} catch (const cv::Exception& e) {
		qWarning() << "[FaceDetector] setInputSize falied:" << e.what();
		return out;
	}

	// -- YuNet detect ---
	cv::Mat dets;
	try {
		yunet_->detect(bgr, dets);		// BGR 그대로 입력 같능
	} catch (const cv::Exception& e) {
		qWarning() << "[FaceDetector] detect failed:" << e.what();
		return out;
	}

	#ifdef DEBUG
  qDebug() << "[FaceDetector] dets rows=" << dets.rows
           << " cols=" << dets.cols
           << " type=" << dets.type()			
					 << " ch=" << dets.channels();
#endif

    // 네 파서 사용
    out = parseYuNet(dets, scoreThr_);

#ifdef DEBUG
  qDebug() << "[FaceDetector] parsed faces=" << (int)out.size();
  for (size_t i = 0; i < out.size(); ++i) {
      const auto& f = out[i];
      bool boxOk = (f.box.x >= 0 && f.box.y >= 0 &&
                    f.box.x + f.box.width  <= bgr.cols &&
                    f.box.y + f.box.height <= bgr.rows);
      auto inRange = [&](const cv::Point2f& p) {
          return (p.x >= 0 && p.y >= 0 && p.x < bgr.cols && p.y < bgr.rows);
      };
      bool lmOk = true; for (int k=0;k<5;k++) lmOk = lmOk && inRange(f.lmk[k]);
      qDebug() << "[FaceDetector] face" << (int)i
               << "box=" << f.box.x << f.box.y << f.box.width << f.box.height
               << "score=" << f.score << "boxOk=" << boxOk << "lmOk=" << lmOk;
  }
#endif

    return out;
}

std::optional<FaceDet> FaceDetector::detectBest(const cv::Mat& bgr) const
{
	auto faces = detectAll(bgr);
	if (faces.empty()) return std::nullopt;

	if (faces.size() > 1) {
		// rull: area * (1 - 0.35 * dest_to_center)
		auto rank = [&](const FaceDet& d) {
			double area = static_cast<double>(d.box.area());
			cv::Point2f c(d.box.x + d.box.width  * 0.5f,
										d.box.y + d.box.height * 0.5f);
			cv::Point2f fc(bgr.cols * 0.5f, bgr.rows * 0.5f);
			double dist = cv::norm(c - fc) /
									  std::hypot(static_cast<double>(bgr.cols),
															 static_cast<double>(bgr.rows));
			return area * (1.0 - 0.35 * dist);
		};
		auto it = std::max_element(
				faces.begin(), faces.end(),
				[&] (const FaceDet& a, const FaceDet& b) { 
					return rank(a) < rank(b);
				});

		return *it;
	}

	return faces.front();
}





