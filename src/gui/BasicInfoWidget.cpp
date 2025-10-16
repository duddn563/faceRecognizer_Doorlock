#include "BasicInfoWidget.hpp"
#include <QSysInfo>
//#include <QHostInfo>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QThread>
#include <QStorageInfo>
#include <QProcess>
#include <QHBoxLayout>
#include <QRegularExpression>

static QString runCmd(const QString& cmd, const QStringList& args = {}) {
    QProcess p;
    p.start(cmd, args);
    p.waitForFinished(1000);
    QString out = QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
    if (out.isEmpty()) out = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
    return out;
}

BasicInfoWidget::BasicInfoWidget(QWidget* parent)
    : QWidget(parent)
{
    // ===== 폰트 및 색상 설정 =====
    QFont titleFont; titleFont.setPointSize(14); titleFont.setBold(true);
    QFont valueFont; valueFont.setPointSize(12); valueFont.setBold(false);
    const char* titleColor = "#000000"; // 흰 배경용
    const char* valueColor = "#222222";

    // ===== 폼 구성 =====
    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    form->setHorizontalSpacing(20);
    form->setVerticalSpacing(14);

    // 제목 라벨들
    auto mkTitle = [&](const QString& text) {
        auto* lbl = new QLabel(text);
        lbl->setFont(titleFont);
        lbl->setStyleSheet(QString("color:%1;").arg(titleColor));
        return lbl;
    };

    // 값 라벨들
    auto mkValue = [&]() {
        auto* lbl = new QLabel(this);
        lbl->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        lbl->setWordWrap(true);
        lbl->setFont(valueFont);
        lbl->setStyleSheet(QString("color:%1;").arg(valueColor));
        return lbl;
    };

    lblHostname_    = mkValue();
    lblOs_          = mkValue();
    lblKernel_      = mkValue();
    lblArch_        = mkValue();
    lblQt_          = mkValue();
    lblDeviceModel_ = mkValue();
    lblCpuCores_    = mkValue();
    lblUptime_      = mkValue();
    lblBootTime_    = mkValue();
    lblMem_         = mkValue();
    lblDiskRoot_    = mkValue();

    form->addRow(mkTitle(tr("호스트명")),       lblHostname_);
    form->addRow(mkTitle(tr("운영체제")),       lblOs_);
    form->addRow(mkTitle(tr("커널")),           lblKernel_);
    form->addRow(mkTitle(tr("아키텍처")),       lblArch_);
    form->addRow(mkTitle(tr("Qt 버전")),        lblQt_);
    form->addRow(mkTitle(tr("디바이스 모델")),  lblDeviceModel_);
    form->addRow(mkTitle(tr("CPU 코어 수")),    lblCpuCores_);
    form->addRow(mkTitle(tr("업타임")),         lblUptime_);
    form->addRow(mkTitle(tr("부팅 시각")),      lblBootTime_);
    form->addRow(mkTitle(tr("메모리")),         lblMem_);
    form->addRow(mkTitle(tr("루트 디스크")),    lblDiskRoot_);

    // ===== 새로고침 버튼 =====
    auto* btnRefresh = new QPushButton(tr("새로고침"), this);
    btnRefresh->setFont(valueFont);
    connect(btnRefresh, &QPushButton::clicked, this, &BasicInfoWidget::refresh);

    // ===== 전체 레이아웃 =====
    auto* vbox = new QVBoxLayout(this);
    vbox->addLayout(form);
    vbox->addSpacing(8);
    vbox->addWidget(btnRefresh, 0, Qt::AlignRight);
    vbox->addStretch(1);
    setLayout(vbox);

    refresh();
}

void BasicInfoWidget::refresh()
{
    // Hostname
    //lblHostname_->setText(QHostInfo::localHostName());
    lblHostname_->setText(QSysInfo::machineHostName());

    // OS
    QString os = QSysInfo::prettyProductName();
    if (os.isEmpty()) os = runCmd("lsb_release", {"-d"}); // fallback (없으면 빈문자열)
    lblOs_->setText(os.isEmpty() ? tr("알 수 없음") : os);

    // Kernel
    QString kernel = QString("%1 %2").arg(QSysInfo::kernelType(), QSysInfo::kernelVersion());
    if (kernel.trimmed() == " ") kernel = runCmd("uname", {"-sr"});
    lblKernel_->setText(kernel);

    // Arch
    lblArch_->setText(QSysInfo::currentCpuArchitecture());

    // Qt
    lblQt_->setText(QString::fromLatin1(qVersion()));

    // Device model (Raspberry Pi 등)
    QString model = readFirstLine("/proc/device-tree/model");
    if (model.isEmpty()) model = readFirstLine("/sys/firmware/devicetree/base/model");
    lblDeviceModel_->setText(model.isEmpty() ? tr("알 수 없음") : model);

    // CPU cores
    int cores = QThread::idealThreadCount();
    lblCpuCores_->setText(cores > 0 ? QString::number(cores) : tr("알 수 없음"));

    // Uptime (/proc/uptime)
    QString upLine = readFirstLine("/proc/uptime"); // "12345.67 890.12"
    qulonglong upSecs = 0;
    if (!upLine.isEmpty()) {
        const auto parts = upLine.split(' ');
        if (!parts.isEmpty()) {
            upSecs = parts[0].split('.').value(0).toULongLong();
        }
    }
    lblUptime_->setText(humanDurationSeconds(upSecs));

    // Boot time
    QDateTime now = QDateTime::currentDateTime();
    QDateTime boot = now.addSecs(-static_cast<qint64>(upSecs));
    lblBootTime_->setText(boot.toString("yyyy-MM-dd hh:mm:ss"));

    // Memory (/proc/meminfo)
    QString meminfo = readAll("/proc/meminfo");
    qulonglong memTotalKB = 0, memAvailKB = 0;
    for (const auto& line : meminfo.split('\n')) {
        if (line.startsWith("MemTotal:")) {
            memTotalKB = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).value(1).toULongLong();
        } else if (line.startsWith("MemAvailable:")) {
            memAvailKB = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).value(1).toULongLong();
        }
    }
    const qulonglong memUsedKB = (memTotalKB > memAvailKB) ? (memTotalKB - memAvailKB) : 0;
    lblMem_->setText(QString("%1 / %2 (사용중)")
                     .arg(humanBytes(memUsedKB * 1024ULL),
                          humanBytes(memTotalKB * 1024ULL)));

    // Disk (root)
    QStorageInfo root = QStorageInfo::root();
    QString diskStr = tr("%1 / %2 (마운트: %3)")
        .arg(humanBytes(root.bytesTotal() - root.bytesAvailable()),
             humanBytes(root.bytesTotal()),
             root.rootPath());
    lblDiskRoot_->setText(diskStr);
}

QString BasicInfoWidget::readFirstLine(const QString& path) {
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return {};
    QTextStream ts(&f);
    QString line = ts.readLine().trimmed();
    f.close();
    return line;
}

QString BasicInfoWidget::readAll(const QString& path) {
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return {};
    QTextStream ts(&f);
    QString s = ts.readAll();
    f.close();
    return s;
}

QString BasicInfoWidget::humanBytes(qulonglong bytes) {
    const char* units[] = {"B","KB","MB","GB","TB","PB"};
    int idx = 0; double v = static_cast<double>(bytes);
    while (v >= 1024.0 && idx < 5) { v /= 1024.0; ++idx; }
    return QString::number(v, 'f', (idx==0?0:(v<10?2:(v<100?1:0)))) + " " + units[idx];
}

QString BasicInfoWidget::humanDurationSeconds(qulonglong secs) {
    const qulonglong days = secs / 86400; secs %= 86400;
    const qulonglong hours = secs / 3600; secs %= 3600;
    const qulonglong mins = secs / 60; secs %= 60;
    QStringList parts;
    if (days)  parts << QString::number(days) + "일";
    if (hours) parts << QString::number(hours) + "시간";
    if (mins)  parts << QString::number(mins) + "분";
    if (secs || parts.isEmpty()) parts << QString::number(secs) + "초";
    return parts.join(" ");
}

