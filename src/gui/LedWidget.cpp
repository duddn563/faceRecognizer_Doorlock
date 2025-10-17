// ledwidget.cpp
#include "LedWidget.h"
#include <QPainter>
#include <QRadialGradient>

LedWidget::LedWidget(QWidget *p): QWidget(p), mode_(Mode::Off) {
    setMinimumSize(18,18);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void LedWidget::setMode(Mode m) { mode_ = m; update(); }

QColor LedWidget::toColor(Mode m) const {
    switch(m){
    case Mode::Green:  return QColor("#37d67a");
    case Mode::Red:    return QColor("#ff4d4f");
    case Mode::Blue:   return QColor("#4aa3ff");
    case Mode::Yellow: return QColor("#ffd666");
    default:           return QColor("#cfcfcf"); // Off/Idle
    }
}

void LedWidget::paintEvent(QPaintEvent *){
    const int r = qMin(width(), height());
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 외곽(베젤)
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30,30,30));
    p.drawEllipse(rect().adjusted(1,1,-1,-1));

    // LED + 글로우
    QRectF inner = rect().adjusted(3,3,-3,-3);
    QColor c = toColor(mode_);
    QRadialGradient g(inner.center(), inner.width()/2.0);
    g.setColorAt(0.0, c.lighter(140));
    g.setColorAt(0.6, c);
    g.setColorAt(1.0, QColor(0,0,0,180));
    p.setBrush(g);
    p.drawEllipse(inner);
}

