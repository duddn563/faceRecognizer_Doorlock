#include "LogTab.hpp"
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QDebug>
#include <QGraphicsDropShadowEffect>
#include "styleConstants.hpp"

LogTab::LogTab(QWidget* parent) : QWidget(parent) 
{
    auto* top = new QVBoxLayout;
    btnAuth   = new QPushButton(tr("인증 로그 보기"), this);
    btnSystem = new QPushButton(tr("시스템 로그 보기"), this);

    top->addWidget(btnAuth);
    top->addWidget(btnSystem);
    top->addStretch();

    auto* root = new QVBoxLayout(this);
    root->addLayout(top);
    setLayout(root);
	
	btnAuth->setMinimumHeight(40);
	btnSystem->setMinimumHeight(40);

	btnAuth->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	btnSystem->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	btnAuth->setStyleSheet("");
	btnSystem->setStyleSheet("");

	btnAuth->setStyleSheet(BTN_STYLE);
	btnSystem->setStyleSheet(BTN_STYLE);

	auto *shadow = new QGraphicsDropShadowEffect();
	shadow->setBlurRadius(10);
	shadow->setXOffset(0);
	shadow->setYOffset(3);
	shadow->setColor(QColor(0, 0, 0, 60));
	btnAuth->setGraphicsEffect(shadow);
	btnSystem->setGraphicsEffect(shadow);


    // 버튼 클릭 → 시그널 방출
    connect(btnAuth,   &QPushButton::clicked, this, &LogTab::showAuthLogs);
    connect(btnSystem, &QPushButton::clicked, this, &LogTab::showSystemLogs);
}

