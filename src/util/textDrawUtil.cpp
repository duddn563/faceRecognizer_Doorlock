#include "textDrawUtil.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QRect>
#include <QDebug>

void drawLabel(QImage& img,
					  const QString& text,
					  Anchor anchor,
					  int margin,
					  const QFont& font,
					  const QColor& fg,
					  const QColor& bg,
					  bool stroke)
{
	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing);
	p.setFont(font);

	QFontMetrics fm(font);
	QRect textRect = fm.boundingRect(text);
	textRect.adjust(-12, -8, +12, +8);

	QPoint pos;
	switch (anchor) {
		case Anchor::TopCenter:
			pos = QPoint((img.width() - textRect.width()) / 2, margin + textRect.height());
			break;
		case Anchor::BottomCenter:
			pos = QPoint((img.width() - textRect.width()) / 2, img.height() - margin);
			break;
		case Anchor::TopLeft:
			pos = QPoint(margin, margin + textRect.height());
			break;
		case Anchor::TopRight:
			pos = QPoint(img.width() - textRect.width() - margin, margin + textRect.height());
			break;
	}

	QRect box(pos.x(), pos.y() - textRect.height(), textRect.width(), textRect.height());

	QColor semiBg = bg;
	semiBg.setAlpha(160);
	p.setBrush(semiBg);
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(box.adjusted(-2, -2, +2, +2), 10, 10);

	if (stroke) {
		QPainterPath path;
		path.addText(box.left() + 12, box.bottom() - 8, font, text);
		p.strokePath(path, QPen(QColor(0, 0, 0, 200), 3));
		p.fillPath(path, fg);
	} else {
		p.setPen(fg);
		p.drawText(box, Qt::AlignLeft | Qt::AlignVCenter, text);
	}

	p.end();
}

void drawKoreanTextOnMat(cv::Mat& frame,
						 const QString& text,
						 Anchor anchor,
						 int margin,
						 int pxSize, 
						 const QColor& fg,
						 const QColor& bg,
						 bool stroke)
{
	QImage img(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_BGR888);
	QFont font("NanumGothic", pxSize, QFont::Bold);
	drawLabel(img, text, anchor, margin, font, fg, bg, stroke);
}
