#include "LogTab.hpp"
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QDebug>
#include <QGraphicsDropShadowEffect>
#include "styleConstants.hpp"

LogTab::LogTab(QWidget* parent) : QWidget(parent) 
{
    auto* top 	= new QVBoxLayout;
    btnAuthLog  = new QPushButton(tr("인증 로그"), this);
    btnSysLog   = new QPushButton(tr("시스템 로그"), this);

    top->addWidget(btnAuthLog);
    top->addWidget(btnSysLog);
    top->addStretch();

    auto* root = new QVBoxLayout(this);
    root->addLayout(top);
    setLayout(root);
	
	btnAuthLog->setMinimumHeight(40);
	btnSysLog->setMinimumHeight(40);

	btnAuthLog->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	btnSysLog->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	btnAuthLog->setStyleSheet("");
	btnSysLog->setStyleSheet("");

	btnAuthLog->setStyleSheet(BTN_STYLE);
	btnSysLog->setStyleSheet(BTN_STYLE);

	auto *shadow = new QGraphicsDropShadowEffect();
	shadow->setBlurRadius(10);
	shadow->setXOffset(0);
	shadow->setYOffset(3);
	shadow->setColor(QColor(0, 0, 0, 60));
	btnAuthLog->setGraphicsEffect(shadow);
	btnSysLog->setGraphicsEffect(shadow);


    // 버튼 클릭 → 시그널 방출
    connect(btnAuthLog,   &QPushButton::clicked, this, &LogTab::showAuthLogs);
    connect(btnSysLog, &QPushButton::clicked, this, &LogTab::showSysLogs);
}

