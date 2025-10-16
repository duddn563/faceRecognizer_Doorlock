#include "MemInfoWidget.hpp"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QFile>
#include <QTextStream>
#include <QStorageInfo>
#include <QRegularExpression>

MemInfoWidget::MemInfoWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* form = new QFormLayout();
    lblRamSummary_  = new QLabel(this);
    lblSwapSummary_ = new QLabel(this);
    lblDiskSummary_ = new QLabel(this);

	QFont valueFont;
	valueFont.setPointSize(12);
	valueFont.setBold(false);

    for (QLabel* l : {lblRamSummary_, lblSwapSummary_, lblDiskSummary_}) {
        l->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        l->setWordWrap(true);
		l->setFont(valueFont);
		l->setStyleSheet("color: #222222;");
    }


	QFont titleFont;
	titleFont.setPointSize(14);
	titleFont.setBold(true);

	QLabel* lblRamTitle  = new QLabel(tr("RAM"));
	QLabel* lblSwapTitle = new QLabel(tr("스왑"));
	QLabel* lblDiskTitle = new QLabel(tr("루드 디스크"));

	lblRamTitle->setFont(titleFont);
	lblSwapTitle->setFont(titleFont);
	lblDiskTitle->setFont(titleFont);


    lblRamTitle->setStyleSheet("color: #000000;");
    lblSwapTitle->setStyleSheet("color: #000000;");
    lblDiskTitle->setStyleSheet("color: #000000;");

	form->setHorizontalSpacing(20);
	form->setVerticalSpacing(14);

    form->addRow(lblRamTitle,   lblRamSummary_);
    form->addRow(lblSwapTitle,  lblSwapSummary_);
    form->addRow(lblDiskTitle,  lblDiskSummary_);

    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({tr("항목"), tr("값")});
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree_->setStyleSheet("font-size: 12pt; color: #333333;");


    btnRefresh_ = new QPushButton(tr("새로고침"), this);
    chkAuto_    = new QCheckBox(tr("1초 자동 갱신"), this);
	btnRefresh_->setFont(valueFont);
	chkAuto_->setFont(valueFont);

    connect(btnRefresh_, &QPushButton::clicked, this, &MemInfoWidget::refresh);
    connect(chkAuto_, &QCheckBox::toggled, this, &MemInfoWidget::setAutoRefresh);
    connect(&timer_, &QTimer::timeout, this, &MemInfoWidget::refresh);
    timer_.setInterval(1000);

    auto* v = new QVBoxLayout(this);
    v->addLayout(form);
    v->addWidget(tree_);
    auto* h = new QHBoxLayout();
    h->addWidget(chkAuto_);
    h->addStretch(1);
    h->addWidget(btnRefresh_);
    v->addLayout(h);
    v->addStretch(1);
    setLayout(v);

    refresh();
}

void MemInfoWidget::setAutoRefresh(bool on) {
    if (on) timer_.start();
    else timer_.stop();
}

QString MemInfoWidget::readAll(const QString& path) {
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return {};
    QTextStream ts(&f);
    QString s = ts.readAll();
    f.close();
    return s;
}

QString MemInfoWidget::humanBytes(qulonglong bytes) {
    const char* units[] = {"B","KB","MB","GB","TB","PB"};
    int idx=0; double v = (double)bytes;
    while (v >= 1024.0 && idx < 5) { v/=1024.0; ++idx; }
    return QString::number(v, 'f', (idx==0?0:(v<10?2:(v<100?1:0)))) + " " + units[idx];
}

qulonglong MemInfoWidget::parseLineKB(const QString& text, const QString& key) {
    for (const QString& ln : text.split('\n')) {
        if (ln.startsWith(key)) {
            const QStringList parts = ln.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() >= 2) return parts[1].toULongLong();
        }
    }
    return 0;
}

void MemInfoWidget::refresh() {
    const QString info = readAll("/proc/meminfo");

    const qulonglong totalKB = parseLineKB(info, "MemTotal:");
    const qulonglong availKB = parseLineKB(info, "MemAvailable:");
    const qulonglong freeKB  = parseLineKB(info, "MemFree:");
    const qulonglong usedKB  = (totalKB>availKB)? totalKB - availKB : 0;

    const qulonglong swapTotalKB = parseLineKB(info, "SwapTotal:");
    const qulonglong swapFreeKB  = parseLineKB(info, "SwapFree:");
    const qulonglong swapUsedKB  = (swapTotalKB>swapFreeKB)? swapTotalKB - swapFreeKB : 0;

    lblRamSummary_->setText(QString("%1 / %2 사용중 (총 %3)")
        .arg(humanBytes(usedKB*1024ULL))
        .arg(humanBytes(totalKB*1024ULL - freeKB*1024ULL))
        .arg(humanBytes(totalKB*1024ULL)));

    lblSwapSummary_->setText(QString("%1 / %2 (총 %3)")
        .arg(humanBytes(swapUsedKB*1024ULL))
        .arg(humanBytes(swapTotalKB*1024ULL - swapFreeKB*1024ULL))
        .arg(humanBytes(swapTotalKB*1024ULL)));

    // 루트 디스크
    QStorageInfo root = QStorageInfo::root();
    lblDiskSummary_->setText(
        QString("%1 / %2 (마운트 %3)")
        .arg(humanBytes(root.bytesTotal() - root.bytesAvailable()))
        .arg(humanBytes(root.bytesTotal()))
        .arg(root.rootPath())
    );

    // 상세 항목 트리
    tree_->clear();
    auto add = [this](const QString& k, const QString& v){
        auto* it = new QTreeWidgetItem(tree_, QStringList() << k << v);
        tree_->addTopLevelItem(it);
    };
    add(tr("MemTotal"), humanBytes(totalKB*1024ULL));
    add(tr("MemFree"), humanBytes(freeKB*1024ULL));
    add(tr("MemAvailable"), humanBytes(availKB*1024ULL));
    add(tr("MemUsed"), humanBytes(usedKB*1024ULL));
    add(tr("SwapTotal"), humanBytes(swapTotalKB*1024ULL));
    add(tr("SwapFree"), humanBytes(swapFreeKB*1024ULL));
    add(tr("SwapUsed"), humanBytes(swapUsedKB*1024ULL));
}

