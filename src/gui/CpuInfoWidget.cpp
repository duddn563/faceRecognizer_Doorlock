#include "CpuInfoWidget.hpp"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QProcess>

QString CpuInfoWidget::readFirstLine(const QString& path){
    QFile f(path);
    if(!f.exists() || !f.open(QIODevice::ReadOnly)) return {};
    QTextStream ts(&f); QString s = ts.readLine().trimmed(); f.close(); return s;
}
QString CpuInfoWidget::readAll(const QString& path){
    QFile f(path);
    if(!f.exists() || !f.open(QIODevice::ReadOnly)) return {};
    QTextStream ts(&f); QString s = ts.readAll(); f.close(); return s;
}
QString CpuInfoWidget::runCmd(const QString& cmd, const QStringList& args, int msec){
    QProcess p; p.start(cmd, args); p.waitForFinished(msec);
    QByteArray out = p.readAllStandardOutput(); if(out.isEmpty()) out = p.readAllStandardError();
    return QString::fromLocal8Bit(out).trimmed();
}
QString CpuInfoWidget::humanMHz(qint64 khz){
    if(khz<=0) return QObject::tr("알 수 없음");
    double mhz = khz/1000.0; return QString::number(mhz, 'f', (mhz<100?1:0)) + " MHz";
}
QString CpuInfoWidget::humanTempMilliC(qint64 milliC){
    if(milliC<=0) return QObject::tr("알 수 없음");
    double c = milliC/1000.0; return QString::number(c, 'f', 1) + " °C";
}

QStringList CpuInfoWidget::listCpuIds(){
    // /sys/devices/system/cpu/cpu0 ... online 코어만 표시 목적
    QDir d("/sys/devices/system/cpu");
    QStringList ids;
    for(const QFileInfo& fi : d.entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot)){
        const QString name = fi.fileName(); // cpu0,cpu1...
        if(name.startsWith("cpu")){
            bool ok=false; name.mid(3).toInt(&ok);
            if(ok) ids << name;
        }
    }

	std::sort(ids.begin(), ids.end(), [](const QString& a, const QString& b){
	bool oka=false, okb=false;
	int ia = a.mid(3).toInt(&oka);	
	int ib = b.mid(3).toInt(&okb);
    if (oka && okb) return ia < ib;
	if (oka) return true;
	if (okb) return false;
        return a < b; // fallback
    });
    return ids;
}

// ----- /proc/stat 읽기 -----
QMap<QString, CpuInfoWidget::CpuTimes> CpuInfoWidget::readProcStat(){
    QMap<QString, CpuTimes> m;
    QFile f("/proc/stat");
    if(!f.open(QIODevice::ReadOnly)) return m;
    QTextStream ts(&f);
    while(!ts.atEnd()){
        const QString ln = ts.readLine().trimmed();
        if(!ln.startsWith("cpu")) continue;
        // cpu  user nice system idle iowait irq softirq steal guest guest_nice
        const QStringList t = ln.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if(t.size()<5) continue;
        CpuTimes c;
        auto at=[&](int i)->quint64{ return (i<(int)t.size()) ? t[i].toULongLong() : 0ULL; };
        // t[0]="cpu" or "cpuN"
        c.user   = at(1); c.nice   = at(2); c.system = at(3); c.idle = at(4);
        c.iowait = at(5); c.irq    = at(6); c.softirq= at(7); c.steal= at(8);
        c.guest  = at(9); c.guest_nice = at(10);
        m.insert(t[0], c);
    }
    return m;
}
double CpuInfoWidget::usagePercent(const CpuTimes& p, const CpuTimes& c){
    const quint64 idlePrev = p.idle + p.iowait;
    const quint64 idleCur  = c.idle + c.iowait;
    const quint64 nonPrev  = p.user + p.nice + p.system + p.irq + p.softirq + p.steal;
    const quint64 nonCur   = c.user + c.nice + c.system + c.irq + c.softirq + c.steal;
    const qint64  idleDelta = (qint64)idleCur - (qint64)idlePrev;
    const qint64  totalDelta= (qint64)(idleCur + nonCur) - (qint64)(idlePrev + nonPrev);
    if(totalDelta<=0) return 0.0;
    double used = (double)(totalDelta - idleDelta) * 100.0 / (double)totalDelta;
    if(used<0) used=0; if(used>100) used=100;
    return used;
}

// ----- UI -----
CpuInfoWidget::CpuInfoWidget(QWidget* parent): QWidget(parent){
    auto* form = new QFormLayout();
    lblModel_    = new QLabel(this);
    lblCores_    = new QLabel(this);
    lblLoadAvg_  = new QLabel(this);
    lblTemp_     = new QLabel(this);
    lblGovernor_ = new QLabel(this);
    for(QLabel* l : {lblModel_,lblCores_,lblLoadAvg_,lblTemp_,lblGovernor_}){
        l->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        l->setWordWrap(true);
    }
    form->addRow(tr("모델명"),    lblModel_);
    form->addRow(tr("코어 수"),   lblCores_);
    form->addRow(tr("Load Avg"), lblLoadAvg_);
    form->addRow(tr("온도"),      lblTemp_);
    form->addRow(tr("Governor"), lblGovernor_);

    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(5);
    tree_->setHeaderLabels({tr("코어"), tr("상태"), tr("주파수(현재/최대)"), tr("Governor/Driver"), tr("사용률")});
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(4, QHeaderView::Stretch);

    btnRefresh_ = new QPushButton(tr("새로고침"), this);
    chkAuto_    = new QCheckBox(tr("1초 자동 갱신"), this);
    connect(btnRefresh_, &QPushButton::clicked, this, &CpuInfoWidget::refresh);
    connect(chkAuto_, &QCheckBox::toggled, this, &CpuInfoWidget::setAutoRefresh);
    connect(&timer_, &QTimer::timeout, this, &CpuInfoWidget::refresh);
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

    // 초기 prev_ 세팅 후 첫 refresh
    prev_ = readProcStat();
    refresh();
}

void CpuInfoWidget::setAutoRefresh(bool on){
    if(on) timer_.start(); else timer_.stop();
}

void CpuInfoWidget::fillSummary(){
    // 모델명 (/proc/cpuinfo)
    QString cpuinfo = readAll("/proc/cpuinfo");
    QString model;
    for(const QString& ln : cpuinfo.split('\n')){
        if(ln.startsWith("model name") || ln.startsWith("Hardware") || ln.startsWith("Model")){
            int p = ln.indexOf(':');
            if(p>=0){ model = ln.mid(p+1).trimmed(); break; }
        }
    }
    if(model.isEmpty()){
        // 라즈베리파이 계열은 device-tree model이 더 정확한 경우 있음
        model = readFirstLine("/proc/device-tree/model");
    }
    lblModel_->setText(model.isEmpty()? tr("알 수 없음") : model);

    // 코어 수 (online 코어 기준)
    const QStringList cores = listCpuIds();
    lblCores_->setText(cores.isEmpty()? tr("알 수 없음") : QString::number(cores.size()));

    // Load average (/proc/loadavg)
    const QString load = readFirstLine("/proc/loadavg"); // "1.23 0.78 0.55 ..."
    if(!load.isEmpty()){
        const QStringList t = load.split(' ');
        if(t.size()>=3) lblLoadAvg_->setText(QString("%1, %2, %3").arg(t[0], t[1], t[2]));
        else lblLoadAvg_->setText(load);
    } else {
        lblLoadAvg_->setText(tr("알 수 없음"));
    }

    // 온도 (thermal_zone*/temp) → milli °C
    // 라즈베리파이는 thermal_zone0가 보통 CPU
    QString tempPath;
    for(int z=0; z<10; ++z){
        QString cand = QString("/sys/class/thermal/thermal_zone%1/temp").arg(z);
        if(QFile::exists(cand)){ tempPath = cand; break; }
    }
    QString tempStr;
    if(!tempPath.isEmpty()){
        bool ok=false; qint64 mC = readFirstLine(tempPath).toLongLong(&ok);
        tempStr = ok ? humanTempMilliC(mC) : QString();
    }
    if(tempStr.isEmpty()){
        // vcgencmd (있을 때만)
        const QString v = runCmd("vcgencmd", {"measure_temp"});
        if(v.startsWith("temp=")) tempStr = v.mid(5); // "xx.x'C"
    }
    lblTemp_->setText(tempStr.isEmpty()? tr("알 수 없음") : tempStr);

    // Governor (cpu0 기준)
    QString gov = readFirstLine("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    lblGovernor_->setText(gov.isEmpty()? tr("알 수 없음") : gov);
}

void CpuInfoWidget::fillPerCore(){
    tree_->clear();

    // 현재 샘플
    const auto cur = readProcStat();

    // total(cpu) 행 먼저 사용률 계산 (요약용으로 쓰고 싶다면 확장 가능)
    // per-core 행
    const QStringList ids = listCpuIds();
    for(const QString& id : ids){
        // online 상태
        QString onlinePath = QString("/sys/devices/system/cpu/%1/online").arg(id);
        QString online = QFile::exists(onlinePath) ? readFirstLine(onlinePath) : "1";
        const QString state = (online == "1") ? "ONLINE" : "OFFLINE";

        // freq
        auto rd = [&](const QString& leaf)->qint64{
            const QString p = QString("/sys/devices/system/cpu/%1/cpufreq/%2").arg(id, leaf);
            bool ok=false; qint64 v = readFirstLine(p).toLongLong(&ok); return ok? v: -1;
        };
        const qint64 curK = rd("scaling_cur_freq");
        const qint64 maxK = rd("cpuinfo_max_freq");
        const QString freqStr = QString("%1 / %2")
                .arg(humanMHz(curK), humanMHz(maxK));

        // gov/driver
        QString gov = readFirstLine(QString("/sys/devices/system/cpu/%1/cpufreq/scaling_governor").arg(id));
        QString drv = readFirstLine(QString("/sys/devices/system/cpu/cpufreq/policy%1/scaling_driver").arg(id.mid(3)));
        QString govDrv = gov; if(!drv.isEmpty()) govDrv += " / " + drv;

        // usage %
        double use = 0.0;
        const QString key = id; // "cpu0"
        if(prev_.contains(key) && cur.contains(key)){
            use = usagePercent(prev_[key], cur[key]);
        }

        auto* it = new QTreeWidgetItem(tree_, QStringList()
            << id
            << state
            << freqStr
            << (govDrv.isEmpty()? tr("알 수 없음") : govDrv)
            << QString::number(use, 'f', 1) + " %"
        );
        tree_->addTopLevelItem(it);
    }

    // prev_ 갱신
    prev_ = cur;
}

void CpuInfoWidget::refresh(){
    fillSummary();
    fillPerCore();
}

