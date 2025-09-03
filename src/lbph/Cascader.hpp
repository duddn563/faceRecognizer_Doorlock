#pragma once
#include <opencv2/openv.hpp>
#include <opencv2/face.hpp>

class Cascader {
public:
		struct Options {
				QString modelPath;
				int inputSize = 200;			// resize number
				QString faceDetector;
				QString eyesDetector;
		}


		explicit Cascader(const Options& opt);

		cv::Mat Embedder::preprocess(const cv::Mat& src) const
		bool isReady() const;

private:
			Options opt_;
			bool ready_ = false;

}

