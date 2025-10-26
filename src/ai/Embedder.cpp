#include "Embedder.hpp"
#include <filesystem>
#include <iostream>
#include <QDebug>

// #define DEBUG

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
}

bool Embedder::isReady() const { return ready_; }

bool Embedder::isTrivialFrame(const cv::Mat& rgb, double meanMin, double stdMin) 
{
	if (rgb.empty() || rgb.type() != CV_8UC3) return true;
	cv::Scalar mu, sigma;
	cv::meanStdDev(rgb, mu, sigma);
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

	    // ── 3) blob 생성 (모델 규약 확인: size/scale/mean/swapRB) ─────────────
    //   ArcFace/MobileFaceNet 계열: 보통 (img-127.5)/128, RGB 입력, 112x112 또는 128x128
	//   output: (N,C,H,W) 배치, 채널, 높이, 너비)`
	cv::Mat in = src;
	bool swapRB = opt_.useRGB;
    const int S = 112; // 모델 고정 크기
	double scale;
	bool crop = false;
	cv::Scalar mean;
	if (opt_.externalNorm) {
		scale = 1.0/128;
		mean = cv::Scalar(127.5, 127.5, 127.5);
	}
	else {
		scale = 1.0;
		mean = cv::Scalar(0, 0, 0);
	}

    cv::Mat blob = cv::dnn::blobFromImage(
        in,                           // 단일 입력
        scale,                    
        cv::Size(S, S),               
		mean, 
        swapRB,                      
		crop,
        CV_32F
    );

    // ── 4) blob 형태/채널별 범위 안전 로그 ────────────────────────────────
    if (blob.dims != 4) {
        qWarning() << "[preprocess] ERR: blob.dims=" << blob.dims << "(expect 4)";
        return cv::Mat();
    }
    int N = blob.size[0], C = blob.size[1], H = blob.size[2], W = blob.size[3];
    if (N!=1 || C!=3 || H!=S || W!=S) {
        qWarning() << "[preprocess] ERR: unexpected blob shape";
        return cv::Mat();
    }

#ifdef DEBUG
    // 채널별 min/max (안전한 2D 뷰로 계산; reshape 사용 안 함)
    auto chw = blob; // alias
    double cmin[3], cmax[3];
    for (int c=0; c<3; ++c) {
        cv::Mat ch2D(H, W, CV_32F, (void*)chw.ptr<float>(0, c));
        cv::minMaxLoc(ch2D, &cmin[c], &cmax[c]);
    }

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

bool Embedder::extract(const cv::Mat& face_rgb, std::vector<float>& out) const
{
    if (!ready_) return false;
    std::lock_guard<std::mutex> lk(mtx_);

    try {
        // ── 1) 원본 전처리 (BGR → preprocess 내부에서 RGB, 112x112, (x-127.5)/128) ──
        cv::Mat blob1 = preprocess(face_rgb);
        if (blob1.empty()) {
            qWarning() << "[extract] empty blob (orig).";
            return false;
        }

        // ── 2) 추론 #1: 원본 ────────────────────────────────────────────────
        net_.setInput(blob1);
        cv::Mat emb1 = net_.forward();
        if (emb1.empty() || emb1.total() == 0) {
            qCritical() << "[extract] forward empty (orig).";
            return false;
        }

        // ── 3) 좌우반전 이미지 전처리 & 추론 #2 ─────────────────────────────
        cv::Mat flipped_rgb; cv::flip(face_rgb, flipped_rgb, 1);
        cv::Mat blob2 = preprocess(flipped_rgb);
        if (blob2.empty()) {
            qWarning() << "[extract] empty blob (flip).";
            return false;
        }
        net_.setInput(blob2);
        cv::Mat emb2 = net_.forward();
        if (emb2.empty() || emb2.total() == 0) {
            qCritical() << "[extract] forward empty (flip).";
            return false;
        }

        // ── 4) 1xD로 평탄화 & dtype 보정 ───────────────────────────────────
        emb1 = emb1.reshape(1, 1).clone();
        if (emb1.type() != CV_32F) emb1.convertTo(emb1, CV_32F);
        emb2 = emb2.reshape(1, 1).clone();
        if (emb2.type() != CV_32F) emb2.convertTo(emb2, CV_32F);

        if (emb1.cols != emb2.cols) {
            qCritical() << "[extract] dim mismatch:" << emb1.cols << "vs" << emb2.cols;
            return false;
        }

        // ── 5) Flip-TTA 평균 후 L2 정규화 ───────────────────────────────────
        cv::Mat emb = 0.5f * (emb1 + emb2);
        l2normalize(emb);  // 이미 프로젝트에 있는 L2 정규화 유틸 사용

#ifdef DEBUG
        double l2 = cv::norm(emb, cv::NORM_L2);
        double minv, maxv; cv::minMaxLoc(emb, &minv, &maxv);
        qDebug() << "[extract] L2=" << l2 << " min/max=" << minv << maxv << " dim=" << emb.cols;
#endif

        // ── 6) std::vector<float> 로 복사 ──────────────────────────────────
        out.resize(static_cast<size_t>(emb.cols));
        std::memcpy(out.data(), emb.ptr<float>(0), static_cast<size_t>(emb.cols) * sizeof(float));
        return true;
    }
    catch (const cv::Exception& e) {
        std::cerr << "[extract][cv::Exception] " << e.what() << "\n";
        return false;
    }
}


float Embedder::cosine(const std::vector<float>& a, const std::vector<float>& b)
{
	if (a.size() != b.size() || a.empty() || b.empty()) return -1.f;

	double dot = 0.0, na = 0.0, nb = 0.0;
	const size_t n = std::min(a.size(), b.size());
	for (size_t i = 0; i < a.size(); i++) {
		dot += a[i]*b[i];
		na  += a[i]*a[i];
		nb  += b[i]*b[i];
	}
	if (na == 0.0 || nb == 0.0) return 0.0f;
	return static_cast<float>(dot / (std::sqrt(na)*std::sqrt(nb)));
}


