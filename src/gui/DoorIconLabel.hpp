#include <QPainter>
#include <QPen>
#include <QPainterPath>
#include <QPaintEvent>
#include <QtGlobal>
#include <cmath>

class DoorIconLabel : public QLabel {
    Q_OBJECT
    Q_PROPERTY(qreal openProgress READ openProgress WRITE setOpenProgress)
    Q_PROPERTY(bool  locked       READ locked       WRITE setLocked)
public:
    explicit DoorIconLabel(QWidget* parent=nullptr)
        : QLabel(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    qreal openProgress() const { return m_progress; }
    void setOpenProgress(qreal p) {
        m_progress = qBound<qreal>(0.0, p, 1.0);
        update();
    }

    bool locked() const { return m_locked; }
    void setLocked(bool v) {
        if (m_locked == v) return;
        m_locked = v;
        update();
    }

protected:
    void paintEvent(QPaintEvent* ev) override {
        Q_UNUSED(ev);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const int   w   = width();
        const int   h   = height();
        const QRectF box(0, 0, w, h);

        // 반투명 배경(유리 느낌)
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0,0,0,60));
        p.drawRoundedRect(box, 10, 10);

        // 문틀
        QRectF frameRect = box.adjusted(w*0.12, h*0.12, -w*0.12, -h*0.12);
        QPen framePen(QColor(230,230,230), 2);
        p.setPen(framePen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(frameRect, 8, 8);

        // === 닫힘 이미지(locked) 우선 처리 ===
        if (m_locked && m_progress <= 0.0001) {
            drawClosedImage(p, frameRect, w, h);
            return; // 닫힘 상태에선 여기서 종료
        }

        // === 열림/애니메이션 렌더 ===
        // openProgress 0.0 = 닫힘, 1.0 = 90도 열림
        qreal angle = 90.0 * m_progress;

        // 힌지 기준 회전
        const qreal hingeX = frameRect.left() + 1.0;
        const qreal hingeY = frameRect.top();
        const qreal doorW  = frameRect.width();
        const qreal doorH  = frameRect.height();

        p.save();
        p.translate(hingeX, hingeY + doorH/2.0);
        p.rotate(-angle);
        p.translate(-hingeX, -(hingeY + doorH/2.0));

        // 문짝
        QRectF doorRect(hingeX, hingeY, doorW, doorH);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(250,250,250));
        p.drawRoundedRect(doorRect, 6, 6);

        // 손잡이
        qreal  knobX      = doorRect.right() - doorW*0.12;
        qreal  knobY      = doorRect.center().y();
        QColor knobColor  = QColor(200, 200, 200, 255 - int(m_progress*80));
        p.setBrush(knobColor);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(knobX, knobY), w*0.035, w*0.035);

        p.restore();

        // 프레임 글로우(열릴 때만)
        if (m_progress > 0.0 && m_progress < 1.0) {
            QPen glow(QColor(255,255,255, int(120 * std::sin(m_progress*M_PI))));
            glow.setWidthF(2.0);
            p.setPen(glow);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(frameRect.adjusted(-1,-1,1,1), 9, 9);
        }
    }

private:
    void drawClosedImage(QPainter& p, const QRectF& frameRect, int w, int h) {
        // 붉은 틴트 배경(닫힘/거부 느낌)
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 60, 60, 45));
        p.drawRoundedRect(frameRect.adjusted(-2,-2,2,2), 10, 10);

        // 문짝(닫힘 상태)
        QRectF doorRect = frameRect;
        p.setBrush(QColor(245,245,245));
        p.drawRoundedRect(doorRect, 6, 6);

        // 자물쇠 아이콘(패드락)
        // 비율로 그려서 어떤 크기에서도 깔끔하게
        const qreal padW = frameRect.width()  * 0.28;
        const qreal padH = frameRect.height() * 0.32;
        QRectF body(frameRect.center().x() - padW/2.0,
                    frameRect.center().y() - padH/2.0 + h*0.02,
                    padW, padH);

        // 몸통
        p.setBrush(QColor(220, 0, 0));
        p.setPen(QPen(QColor(180,0,0), 2));
        p.drawRoundedRect(body, 4, 4);

        // U자형 고리
        const qreal arcR   = padW * 0.45;
        QPointF    arcC(body.center().x(), body.top() - arcR*0.15);
        QPen arcPen(QColor(180,0,0), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(arcPen);
        p.setBrush(Qt::NoBrush);
        QRectF arcRect(arcC.x()-arcR, arcC.y()-arcR, arcR*2, arcR*2);
        // 상단 반원만
        p.drawArc(arcRect, 40*16, 100*16);

        // 키홀
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 230, 230));
        QPointF keyC(body.center().x(), body.center().y()+padH*0.05);
        p.drawEllipse(keyC, padW*0.045, padW*0.045);
        QPainterPath keyStem;
        keyStem.moveTo(keyC + QPointF(0, padW*0.045));
        keyStem.lineTo(keyC + QPointF(0, padW*0.14));
        QPen keyPen(QColor(255,230,230), 3, Qt::SolidLine, Qt::RoundCap);
        p.setPen(keyPen);
        p.drawPath(keyStem);

        // 테두리 강조
        QPen border(QColor(200,0,0,180), 2);
        p.setPen(border);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(frameRect, 8, 8);

        // “LOCKED” 텍스트 (선택)
        QFont f = font();
        f.setBold(true);
        f.setPointSizeF(std::max(10.0, h * 0.06));
        p.setFont(f);
        p.setPen(QColor(200,0,0,220));
        p.drawText(frameRect.adjusted(0, frameRect.height()*0.62, 0, 0),
                   Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("LOCKED"));
        p.restore();
    }

private:
    qreal m_progress = 0.0;
    bool  m_locked   = true; // 기본은 잠김으로 시작(원하면 false로 초기화)
};

