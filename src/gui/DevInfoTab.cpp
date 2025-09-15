#include "DevInfoTab.hpp"
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QDebug>
#include <QGraphicsDropShadowEffect>
#include "styleConstants.hpp"

DevInfoTab::DevInfoTab(QWidget* parent) : QWidget(parent) 
{
    auto* top = new QVBoxLayout;
    btnDevInfo   = new QPushButton(tr("장비 정보"), this);
    btnSysInfo	 = new QPushButton(tr("시스템 정보"), this);

    top->addWidget(btnDevInfo);
    top->addWidget(btnSysInfo);
    top->addStretch();

    auto* root = new QVBoxLayout(this);
    root->addLayout(top);
    setLayout(root);
	
	btnDevInfo->setMinimumHeight(40);
	btnSysInfo->setMinimumHeight(40);

	btnDevInfo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	btnSysInfo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	btnDevInfo->setStyleSheet("");
	btnSysInfo->setStyleSheet("");

	btnDevInfo->setStyleSheet(BTN_STYLE);
	btnSysInfo->setStyleSheet(BTN_STYLE);

	auto *shadow = new QGraphicsDropShadowEffect();
	shadow->setBlurRadius(10);
	shadow->setXOffset(0);
	shadow->setYOffset(3);
	shadow->setColor(QColor(0, 0, 0, 60));
	btnDevInfo->setGraphicsEffect(shadow);
	btnSysInfo->setGraphicsEffect(shadow);


    // 버튼 클릭 → 시그널 방출
    connect(btnDevInfo,   &QPushButton::clicked, this, &DevInfoTab::showDevInfo);
    connect(btnSysInfo, &QPushButton::clicked, this, &DevInfoTab::showSysInfo);
}

