#include "Embedder.hpp"
#include <filesystem>
#include <iostream>
#include <QDebug>


using namespace cv;
namespace fs = std::filesystem;

Embedder::Embedder(const Options& opt) : opt_(opt) 
{
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
        		QStringList qn; for (auto& s : names) qn << QString::fromStdString(s);
        		qDebug() << "[Embedder] out names =" << qn;
        		for (auto& s : names) {
            		int id = net_.getLayerId(s);
            		qDebug() << "[Embedder] layerId(" << QString::fromStdString(s) << ")=" << id;
        		}
		} catch (const cv::Exception& e) {
				std::cerr << "[ERR] readNetFromONNX failed: " << e.what() << "\n";
		}

		if (opt_.inputSize != 128) {
			qWarning() << "[Embedder] inputSize overridden to 128(model is fixed 128)";
		}
}

bool Embedder::isReady() const { return ready_; }

cv::Mat Embedder::preprocess(const cv::Mat& src) const
{
		Mat rgb;
		if (opt_.useRGB) cvtColor(src, rgb, cv::COLOR_BGR2RGB);
        else 			 rgb = src;

		const int S = 128; 	// model fixed
        Mat blob = dnn::blobFromImage(
                    rgb,        						// Input 
                    1.0/128.0,  						// scalarfactor
                    cv::Size(S, S),     				// size
                    cv::Scalar(127.5, 127.5, 127.5),    // mean
                    /*swapRB=*/false, 					// 이미 useRGB면 true로 바꿔줬으니 여기선 false
                    /*crop=*/false,
                    CV_32F
        );
		qInfo() << "[Embedder] blob NCHW=" << blob.size[0] << blob.size[1] << blob.size[2] << blob.size[3];

        return blob;		// NCHW : 1x3x128x128
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
           net_.setInput(blob);

           Mat emb = net_.forward();        // NxD 또는 1xD
		   if (emb.empty() || emb.total() == 0) {
			   qCritical() << "[Embedder] forward produced empty output";
			   return false;
		   }
		   qInfo() << "[Embedder] raw out=" << emb.rows << "x" << emb.cols << "type=" << emb.type();

           emb = emb.reshape(1, 1).clone();  // 1xD, 연속 메모리 보장
           if (emb.type() != CV_32F) emb.convertTo(emb, CV_32F);

           l2normalize(emb);

           out.resize((size_t)emb.cols);
           std::memcpy(out.data(), emb.ptr<float>(0), emb.cols * sizeof(float));

		   qDebug() << "[Embedder] extract ok dim=" << emb.cols;
           return true;
    } catch (const cv::Exception& e) {
        std::cerr << "[ERR] forward failed: " << e.what() << "\n";
        return false;
    }
}

float Embedder::cosine(const std::vector<float>& a, const std::vector<float>& b)
{
		if (a.size() != b.size() || a.empty()) return -1.f;
		double dot = 0.0;
		for (size_t i = 0; i < a.size(); i++) dot += static_cast<double>(a[i])*(b[i]);

		// a, b는 L2 정규화된 전제 -> dot=cosin. 안전하게 클램프
		return static_cast<float>(dot);
}


