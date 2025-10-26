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
	btnDelAuth  = new QPushButton(tr("인증 로그 초기화"), this);
	btnDelSys   = new QPushButton(tr("시스템 로그 초기화"), this);
	btnDelAll   = new QPushButton(tr("모든 로그 초기화"), this);


    top->addWidget(btnAuthLog);
    top->addWidget(btnSysLog);
	top->addWidget(btnDelAuth);
	top->addWidget(btnDelSys);
	top->addWidget(btnDelAll);
    top->addStretch();

    auto* root = new QVBoxLayout(this);
    root->addLayout(top);
    setLayout(root);
	
	btnAuthLog->setMinimumHeight(40);
	btnSysLog->setMinimumHeight(40);
	btnDelAuth->setMinimumHeight(20);
	btnDelSys->setMinimumHeight(20);
	btnDelAll->setMinimumHeight(20);

	btnAuthLog->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	btnSysLog->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	btnDelAuth->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	btnDelSys->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	btnDelAll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	btnAuthLog->setStyleSheet("");
	btnSysLog->setStyleSheet("");
	btnDelAuth->setStyleSheet("");
	btnDelSys->setStyleSheet("");
	btnDelAll->setStyleSheet("");

	btnAuthLog->setStyleSheet(BTN_STYLE);
	btnSysLog->setStyleSheet(BTN_STYLE);
	btnDelAuth->setStyleSheet(BTN_STYLE);
	btnDelSys->setStyleSheet(BTN_STYLE);
	btnDelAll->setStyleSheet(BTN_STYLE);

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
    connect(btnDelAuth, &QPushButton::clicked, this, &LogTab::DelAuthLogs);
    connect(btnDelSys, &QPushButton::clicked, this, &LogTab::DelSysLogs);
    connect(btnDelAll, &QPushButton::clicked, this, &LogTab::DelAllLogs);
}

