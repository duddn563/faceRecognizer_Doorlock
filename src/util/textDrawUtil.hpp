#pragma once 
#include <opencv2/opencv.hpp>
#include <QString>
#include <QFont>
#include <QColor>

enum class Anchor { 
	TopLeft, TopCenter, TopRight,
	CenterLeft, Center ,CenterRight, 
	BottomLeft, BottomCenter, BottomRight 
};

void drawKoreanTextOnMat(cv::Mat& frame,
						 const QString& text,
						 Anchor anchor,
						 int margin,
						 int pxSize,
						 const QColor& fg = QColor(255, 255, 255),
						 const QColor& bg = QColor(0, 0, 0, 160),
						 bool stroke = true);
void drawLabel(QImage& img,
			   const QString& text,
			   Anchor anchor,
			   int margin,
			   const QFont& font,
			   const QColor& fg,
			   const QColor& bg,
			   bool stroke);


