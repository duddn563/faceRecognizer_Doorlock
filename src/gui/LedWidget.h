// ledwidget.h
#pragma once
#include <QWidget>
#include <QColor>

class LedWidget : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Off, Green, Red, Blue, Yellow };
    explicit LedWidget(QWidget *parent=nullptr);
    QSize sizeHint() const override { return {22,22}; }
public slots:
    void setMode(Mode m);
protected:
    void paintEvent(QPaintEvent *e) override;
private:
    Mode mode_;
    QColor toColor(Mode m) const;
};

