#include "Embedder.hpp"
#include <filesystem>
#include <iostream>
#include <QDebug>


using namespace cv;
namespace fs = std::filesystem;

Embedder::Embedder(const Options& opt) : opt_(opt) 
{
	qDebug() << "[Embedder] ctor path=" << opt_.modelPath;


	std::string path = opt_.modelPath.toStdString();
	if (!fs::exists(path)) {
		std::cerr << "[ERR] Model file not found: " << path << "\n";
		return;
	}

	try {
		net_ = dnn::readNetFromONNX(path);
		net_.setPreferableBackend(dnn::DNN_BACKEND_OPENCV);
		net_.setPreferableTarget(dnn::DNN_TARGET_CPU);
		ready_ = true;

		// 출력 레이어/ID 로그
		auto names = net_.getUnconnectedOutLayersNames();
		QStringList qn; 
		for (auto& s : names) 
			qn << QString::fromStdString(s);
		qDebug() << "[Embedder] out names =" << qn;

	} catch (const cv::Exception& e) {
		std::cerr << "[ERR] readNetFromONNX failed: " << e.what() << "\n";
		ready_ = false;
	}

	qDebug() << "[Embedder] Contructor";
}

bool Embedder::isReady() const { return ready_; }

bool Embedder::isTrivialFrame(const cv::Mat& bgr, double meanMin, double stdMin) 
{
	if (bgr.empty() || bgr.type() != CV_8UC3) return true;
	cv::Scalar mu, sigma;
	cv::meanStdDev(bgr, mu, sigma);
	double m =  (mu[0] + mu[1] + mu[2]) / 3.0;
	double s = (sigma[0] + sigma[1] + sigma[2]) / 3.0;
	return (m < meanMin || s < stdMin);
}

cv::Mat Embedder::preprocess(const cv::Mat& src) const
{
    // ── 0) 기본 유효성 검사 ──────────────────────────────────────────────
    if (src.empty()) {
        qWarning() << "[preprocess] ERR: src empty";
        return cv::Mat();
    }

    if (src.channels() != 3 || src.type() != CV_8UC3) {
        qWarning() << "[preprocess] ERR: src type/channels invalid"
                   << " type=" << src.type() << " ch=" << src.channels();
        return cv::Mat();
    }

#ifdef DEBUG
    qInfo() << "[preprocess] src size=" << src.cols << "x" << src.rows
            << " type=" << src.type() << " ch=" << src.channels();
#endif

    // ── 1) 색상 경로 선택 (일관된 단일 입력 in 사용) ───────────────────────
    cv::Mat in;   // 이 in 만을 이후 모든 단계에서 사용
    bool swapRB = false;
    if (opt_.useRGB) {
        // 이미 RGB로 바꿔서 모델에 넣는다 → blobFromImage에서는 swapRB=false
        cv::cvtColor(src, in, cv::COLOR_BGR2RGB);
        swapRB = false;
        //qInfo() << "[preprocess] path=RGB (BGR->RGB cvtColor), swapRB=false";
    } else {
        // BGR 원본을 넣고 blob 내부에서 R/B 스왑 → swapRB=true
        in = src;
        swapRB = true;
        //qInfo() << "[preprocess] path=BGR (no convert), swapRB=true";
    }

    // ── 2) 채널 평균/표준편차 로그 (in 기준) ──────────────────────────────
    {
        std::vector<cv::Mat> chs;
        cv::split(in, chs);
        cv::Scalar mB = cv::mean(chs[0]);
        cv::Scalar mG = cv::mean(chs[1]);
        cv::Scalar mR = cv::mean(chs[2]);
        //qInfo() << "[preprocess] split-mean (B,G,R) =" << mB[0] << mG[0] << mR[0];

        cv::Scalar mu, sigma;
        cv::meanStdDev(in, mu, sigma);
        //qInfo() << "[preprocess] mean(B,G,R)=" << mu[0] << mu[1] << mu[2]
        //        << " std(B,G,R)=" << sigma[0] << sigma[1] << sigma[2];
    }

    // ── 3) blob 생성 (모델 규약 확인: size/scale/mean/swapRB) ─────────────
    //   ArcFace/MobileFaceNet 계열: 보통 (img-127.5)/128, RGB 입력, 112x112 또는 128x128
    const int S = 112; // 모델 고정 크기 (필요 시 128로 바꿔도 됨: onnx 규약을 확인)
    cv::Mat blob = cv::dnn::blobFromImage(
        in,                           // ← 위에서 선택한 단일 입력
        1.0/128.0,                    // scale
        cv::Size(S, S),               // size
        cv::Scalar(127.5,127.5,127.5),// mean
        swapRB,                       // 색상 경로에 따라 설정
        /*crop=*/false,
        CV_32F
    );

    // ── 4) blob 형태/채널별 범위 안전 로그 ────────────────────────────────
    if (blob.dims != 4) {
        qWarning() << "[preprocess] ERR: blob.dims=" << blob.dims << "(expect 4)";
        return cv::Mat();
    }
    int N = blob.size[0], C = blob.size[1], H = blob.size[2], W = blob.size[3];
    //qInfo() << "[preprocess] blob NCHW=" << N << C << H << W;
    if (N!=1 || C!=3 || H!=S || W!=S) {
        qWarning() << "[preprocess] ERR: unexpected blob shape";
        return cv::Mat();
    }

    // 채널별 min/max (안전한 2D 뷰로 계산; reshape 사용 안 함)
    auto chw = blob; // alias
    double cmin[3], cmax[3];
    for (int c=0; c<3; ++c) {
        cv::Mat ch2D(H, W, CV_32F, (void*)chw.ptr<float>(0, c));
        cv::minMaxLoc(ch2D, &cmin[c], &cmax[c]);
    }
#ifdef DEBUG
    qInfo() << "[preprocess] blob ch0[min,max]=" << cmin[0] << cmax[0]
            << " ch1[min,max]=" << cmin[1] << cmax[1]
            << " ch2[min,max]=" << cmin[2] << cmax[2];
#endif

    // ── 5) 최종 반환 ──────────────────────────────────────────────────────
    return blob; // NCHW 1x3xSxS
}

void Embedder::l2normalize(Mat& row) 
{
		double n = norm(row, NORM_L2);
		if (n > 1e-12) row /= static_cast<float>(n);
}


bool Embedder::extract(const Mat& face, std::vector<float>& out) const
{
	if (!ready_) return false;
	std::lock_guard<std::mutex> lk(mtx_);

	try {
		Mat blob = preprocess(face);

		if (blob.empty()) {
			qWarning() << "[extract] empty blob. skip forward.";
			return false;
		}

		net_.setInput(blob);

		Mat emb = net_.forward();        // NxD 또는 1xD
		if (emb.empty() || emb.total() == 0) {
			qCritical() << "[Embedder] forward produced empty output";
			return false;
		}
		//qInfo() << "[Embedder] raw out=" << emb.rows << "x" << emb.cols << "type=" << emb.type();

		emb = emb.reshape(1, 1).clone();  // 1xD, 연속 메모리 보장
		if (emb.type() != CV_32F) emb.convertTo(emb, CV_32F);

		double l2_before = cv::norm(emb, cv::NORM_L2);
		cv::Scalar mu, sigma;
		cv::meanStdDev(emb, mu, sigma);
		qInfo() << "[extract][check] L2_before=" << l2_before
				<< " mean=" << mu[0] << "std=" << sigma[0];

		l2normalize(emb);

		double l2_after = cv::norm(emb, cv::NORM_L2);
		qDebug() << "[extract][check] L2_after=" << l2_after;

		// (옵션) 값 범위 간단 체크
		double minv, maxv;
		cv::minMaxLoc(emb, &minv, &maxv);
		//qDebug() << "[Dbg][Embedder] min/max after norm =" << minv << maxv;

		out.resize((size_t)emb.cols);
		std::memcpy(out.data(), emb.ptr<float>(0), emb.cols * sizeof(float));

		//qDebug() << "[Embedder] extract ok dim=" << emb.cols;
		return true;
	} catch (const cv::Exception& e) { std::cerr << "[ERR] forward failed: " << e.what() << "\n";
		return false;
	}
}

float Embedder::cosine(const std::vector<float>& a, const std::vector<float>& b)
{
	if (a.size() != b.size() || a.empty()) return -1.f;
	double dot = 0.0;
	for (size_t i = 0; i < a.size(); i++) dot += static_cast<double>(a[i])*(b[i]);

	if (dot >  1.0) dot =  1.0;
	if (dot < -1.0) dot = -1.0;

	// a, b는 L2 정규화된 전제 -> dot=cosin. 안전하게 클램프
	return static_cast<float>(dot);
}


