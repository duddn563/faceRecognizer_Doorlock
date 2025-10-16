#include "DevInfoDialog.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSizePolicy>
#include <QString>
#include <QGraphicsDropShadowEffect>

#include "BasicInfoWidget.hpp"
#include "NetworkInfoWidget.hpp"
#include "CpuInfoWidget.hpp"
#include "MemInfoWidget.hpp"
#include "styleConstants.hpp"

// 사이드바 구분선 만드는 헬퍼
static QFrame* makeSeparator(Qt::Orientation ori, QWidget* parent = nullptr) {
    auto* line = new QFrame(parent);
    line->setFrameShape(ori == Qt::Vertical ? QFrame::VLine : QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

// 버튼 공통 스타일(선택사항)
static void styleNavButton(QPushButton* b) {
    b->setCheckable(true);
    b->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    b->setMinimumHeight(100);
	b->setStyleSheet("");
	b->setStyleSheet(BTN_STYLE_2);

	auto *shadow = new QGraphicsDropShadowEffect();
	shadow->setBlurRadius(10);
	shadow->setXOffset(0);
	shadow->setYOffset(3);
	shadow->setColor(QColor(0, 0, 0, 60));
	b->setGraphicsEffect(shadow);
}

DevInfoDialog::DevInfoDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("디바이스 정보"));
    setModal(true);
    resize(900, 600);

    auto* root = new QHBoxLayout(this);

    // ===== 왼쪽 네비게이션 =====
    auto* navWrap   = new QWidget(this);
    auto* navLayout = new QVBoxLayout(navWrap);
    navLayout->setContentsMargins(8, 8, 8, 8);
    navLayout->setSpacing(6);

    btnBasic_ = new QPushButton(QStringLiteral("기본 정보"), navWrap);
    btnNet_   = new QPushButton(QStringLiteral("네트워크"), navWrap);
    btnCpu_   = new QPushButton(QStringLiteral("CPU"), navWrap);
    btnMem_   = new QPushButton(QStringLiteral("메모리"), navWrap);

    styleNavButton(btnBasic_);
    styleNavButton(btnNet_);
    styleNavButton(btnCpu_);
    styleNavButton(btnMem_);

    navLayout->addWidget(btnBasic_);
    navLayout->addWidget(btnNet_);
    navLayout->addWidget(btnCpu_);
    navLayout->addWidget(btnMem_);
    navLayout->addStretch(1);

    navWrap->setFixedWidth(160);

    // ===== 중앙 구분선 =====
    auto* sep = makeSeparator(Qt::Vertical, this);

    // ===== 오른쪽 콘텐츠(스택) =====
    stack_ = new QStackedWidget(this);

	basicPage_ = new BasicInfoWidget(stack_);
	netPage_   = new NetworkInfoWidget(stack_);
	cpuPage_   = new CpuInfoWidget(stack_);
	memPage_   = new MemInfoWidget(stack_);

    stack_->addWidget(basicPage_); // index 0
    stack_->addWidget(netPage_);   // index 1
    stack_->addWidget(cpuPage_);   // index 2
    stack_->addWidget(memPage_);   // index 3

    // 루트 레이아웃 조립
    root->addWidget(navWrap);
    root->addWidget(sep);
    root->addWidget(stack_, /*stretch*/ 1);
    setLayout(root);

    // ===== 네비 버튼 → 페이지 전환 연결 =====
    connect(btnBasic_, &QPushButton::clicked, this, [this]{ switchTo(Page::Basic); });
    connect(btnNet_,   &QPushButton::clicked, this, [this]{ switchTo(Page::Net);   });
    connect(btnCpu_,   &QPushButton::clicked, this, [this]{ switchTo(Page::Cpu);   });
    connect(btnMem_,   &QPushButton::clicked, this, [this]{ switchTo(Page::Mem);   });

    // 처음에는 기본 정보 선택
    switchTo(Page::Basic);
}

void DevInfoDialog::switchTo(Page p)
{
    // 토글 상태 정리 (라디오버튼처럼 1개만 체크)
    const bool basic = (p == Page::Basic);
    const bool net   = (p == Page::Net);
    const bool cpu   = (p == Page::Cpu);
    const bool mem   = (p == Page::Mem);

    btnBasic_->setChecked(basic);
    btnNet_->setChecked(net);
    btnCpu_->setChecked(cpu);
    btnMem_->setChecked(mem);

    stack_->setCurrentIndex(static_cast<int>(p));
}

