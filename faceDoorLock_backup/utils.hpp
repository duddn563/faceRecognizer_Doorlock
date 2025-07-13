// -------------------- src/utils.hpp --------------------
#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>
#include <opencv2/opencv.hpp>

using namespace std;
using namespace cv;

void drawLabel(Mat& img, const string& label, const Point& origin, const Scalar& color);

#endif
