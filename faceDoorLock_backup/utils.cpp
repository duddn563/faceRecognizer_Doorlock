// -------------------- src/utils.cpp --------------------
#include "utils.hpp"

void drawLabel(Mat& img, const string& label, const Point& origin, const Scalar& color) {
    int baseline = 0;
    Size textSize = getTextSize(label, FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseline);
    rectangle(img, origin + Point(0, baseline), origin + Point(textSize.width, -textSize.height), color, FILLED);
    putText(img, label, origin, FONT_HERSHEY_SIMPLEX, 0.5, Scalar(255,255,255), 1);
}

