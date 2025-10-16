#include "NetworkInfoWidget.hpp"
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QRegularExpression>

static QString trimEachLine(const QString& s) {
    QStringList out;
    for (auto& ln : s.split('\n', Qt::SkipEmptyParts)) out << ln.trimmed();
    return out.join('\n');
}

QString NetworkInfoWidget::runCmd(const QString& cmd, const QStringList& args, int msec) {
    QProcess p;
    p.start(cmd, args);
    p.waitForFinished(msec);
    QByteArray out = p.readAllStandardOutput();
    if (out.isEmpty()) out = p.readAllStandardError();
    return QString::fromLocal8Bit(out).trimmed();
}

QString NetworkInfoWidget::readTextFile(const QString& path) {
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return {};
    QTextStream ts(&f);
    const QString s = ts.readAll();
    f.close();
    return s;
}
QString NetworkInfoWidget::readFirstLine(const QString& path) {
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return {};
    QTextStream ts(&f);
    const QString s = ts.readLine().trimmed();
    f.close();
    return s;
}

QStringList NetworkInfoWidget::listInterfaces() {
    QStringList names;
    QDir d("/sys/class/net");
    for (const QFileInfo& fi : d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        names << fi.fileName();
    }
    names.sort();
    return names;
}

QString NetworkInfoWidget::humanBitsPerSec(qint64 mbps) {
    if (mbps <= 0) return QObject::tr("알 수 없음");
    // 표준은 Mb/s단위가 흔함
    return QString::number(mbps) + " Mb/s";
}

QString NetworkInfoWidget::humanBytes(qulonglong bytes) {
    const char* units[] = {"B","KB","MB","GB","TB","PB"};
    int idx = 0; double v = static_cast<double>(bytes);
    while (v >= 1024.0 && idx < 5) { v /= 1024.0; ++idx; }
    return QString::number(v, 'f', (idx==0?0:(v<10?2:(v<100?1:0)))) + " " + units[idx];
}

QString NetworkInfoWidget::boolOnOff(const QString& state) {
    // /sys/class/net/IF/operstate → "up"|"down"|...
    if (state.compare("up", Qt::CaseInsensitive) == 0) return "UP";
    if (state.compare("down", Qt::CaseInsensitive) == 0) return "DOWN";
    return state;
}

NetworkInfoWidget::RouteInfo NetworkInfoWidget::queryDefaultRoute() {
    RouteInfo r;
    const QString out = runCmd("ip", {"route", "show", "default"});
    // 예: "default via 192.168.0.1 dev eth0 proto dhcp src 192.168.0.20 metric 100"
    const QStringList toks = out.split(' ', Qt::SkipEmptyParts);
    for (int i=0; i<toks.size(); ++i) {
        if (toks[i] == "dev" && i+1 < toks.size()) r.defIf = toks[i+1];
        if (toks[i] == "via" && i+1 < toks.size()) r.gateway = toks[i+1];
    }
    return r;
}

QStringList NetworkInfoWidget::queryDnsServers() {
    QStringList dns;
    const QString text = readTextFile("/etc/resolv.conf");
    for (const QString& ln : text.split('\n')) {
        const QString s = ln.trimmed();
        if (s.startsWith("nameserver")) {
            const QStringList parts = s.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() >= 2) dns << parts[1];
        }
    }
    dns.removeDuplicates();
    return dns;
}

QStringList NetworkInfoWidget::queryIpAddrs(const QString& ifname, bool v6) {
    QStringList addrs;
    const QString fam = v6 ? "-6" : "-4";
    // 예: ip -o -4 addr show dev eth0
    const QString out = runCmd("ip", {"-o", fam, "addr", "show", "dev", ifname});
    // 한 줄 예: "2: eth0    inet 192.168.0.20/24 brd 192.168.0.255 scope global dynamic eth0"
    for (const QString& ln : out.split('\n', Qt::SkipEmptyParts)) {
        const QStringList tok = ln.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        int idx = tok.indexOf(v6 ? "inet6" : "inet");
        if (idx >= 0 && idx+1 < tok.size()) addrs << tok[idx+1]; // "IP/프리픽스"
    }
    return addrs;
}

NetworkInfoWidget::IfStats NetworkInfoWidget::readIfStats(const QString& ifname) {
    IfStats s;
    auto rd = [&ifname](const QString& leaf)->qulonglong {
        return readFirstLine(QString("/sys/class/net/%1/statistics/%2").arg(ifname, leaf)).toULongLong();
    };
    s.rxBytes   = rd("rx_bytes");
    s.txBytes   = rd("tx_bytes");
    s.rxPackets = rd("rx_packets");
    s.txPackets = rd("tx_packets");
    return s;
}

QString NetworkInfoWidget::readIfValue(const QString& ifname, const QString& leaf) {
    return readFirstLine(QString("/sys/class/net/%1/%2").arg(ifname, leaf));
}

NetworkInfoWidget::NetworkInfoWidget(QWidget* parent)
    : QWidget(parent)
{
    // ===== 폰트/색 공통 =====
    QFont titleFont;      titleFont.setPointSize(14); titleFont.setBold(true);
    QFont valueFont;      valueFont.setPointSize(12); valueFont.setBold(false);
    const char* titleColor = "#000000";   // 흰 배경용
    const char* valueColor = "#222222";   // 가독 좋은 진회색

    // ===== 상단 요약 (제목 라벨 + 값 라벨) =====
    auto* form = new QFormLayout();

    auto* tIf   = new QLabel(tr("기본 인터페이스"));
    auto* tGw   = new QLabel(tr("게이트웨이"));
    auto* tDns  = new QLabel(tr("DNS"));
    for (QLabel* t : {tIf, tGw, tDns}) {
        t->setFont(titleFont);
        t->setStyleSheet(QString("color:%1;").arg(titleColor));
    }

    lblDefaultIf_ = new QLabel(this);
    lblGateway_   = new QLabel(this);
    lblDns_       = new QLabel(this);
    for (QLabel* l : {lblDefaultIf_, lblGateway_, lblDns_}) {
        l->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        l->setWordWrap(true);
        l->setFont(valueFont);
        l->setStyleSheet(QString("color:%1;").arg(valueColor));
    }

    form->setHorizontalSpacing(20);
    form->setVerticalSpacing(14);
    form->addRow(tIf,  lblDefaultIf_);
    form->addRow(tGw,  lblGateway_);
    form->addRow(tDns, lblDns_);

    // ===== 인터페이스 트리 =====
    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({tr("항목"), tr("값")});
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree_->setIndentation(16);
    tree_->setStyleSheet(
        "QTreeWidget { font-size: 12pt; color: #333333; } "
        "QHeaderView::section { font-size: 12pt; font-weight: 600; padding: 6px 8px; }"
    );

    // ===== 새로고침 버튼 =====
    auto* btn = new QPushButton(tr("새로고침"), this);
    btn->setFont(valueFont);

    connect(btn, &QPushButton::clicked, this, &NetworkInfoWidget::refresh);

    // ===== 레이아웃 =====
    auto* v = new QVBoxLayout(this);
    v->addLayout(form);
    v->addWidget(tree_);
    v->addWidget(btn, 0, Qt::AlignRight);
    v->addStretch(1);
    setLayout(v);

    refresh();
}

void NetworkInfoWidget::refresh() {
    // 상단 요약
    const RouteInfo route = queryDefaultRoute();
    lblDefaultIf_->setText(route.defIf.isEmpty() ? tr("알 수 없음") : route.defIf);
    lblGateway_->setText(route.gateway.isEmpty() ? tr("알 수 없음") : route.gateway);

    const QStringList dns = queryDnsServers();
    lblDns_->setText(dns.isEmpty() ? tr("알 수 없음") : dns.join(", "));

    // 인터페이스 목록 채우기
    tree_->clear();
    const QStringList ifs = listInterfaces();
    for (const QString& ifname : ifs) {
        QTreeWidgetItem* root = new QTreeWidgetItem(tree_, QStringList() << ifname << "");
        root->setFirstColumnSpanned(true);
        tree_->addTopLevelItem(root);

        // 기본 속성
        const QString mac  = readIfValue(ifname, "address");
        const QString mtu  = readIfValue(ifname, "mtu");
        const QString oper = readIfValue(ifname, "operstate");
        const QString spd  = readIfValue(ifname, "speed"); // Mb/s (없을 수 있음)
        const IfStats st   = readIfStats(ifname);
        const QStringList ip4 = queryIpAddrs(ifname, false);
        const QStringList ip6 = queryIpAddrs(ifname, true);

        auto add = [root](const QString& k, const QString& v) {
            auto* it = new QTreeWidgetItem(root, QStringList() << k << v);
            root->addChild(it);
        };

        add(tr("상태"), boolOnOff(oper));
        add(tr("MAC"), mac.isEmpty() ? tr("알 수 없음") : mac);
        add(tr("MTU"), mtu.isEmpty() ? tr("알 수 없음") : mtu);
        add(tr("링크 속도"), spd.isEmpty() ? tr("알 수 없음") : humanBitsPerSec(spd.toLongLong()));

        if (!ip4.isEmpty()) add(tr("IPv4"), ip4.join(", "));
        if (!ip6.isEmpty()) add(tr("IPv6"), ip6.join(", "));

        add(tr("RX 바이트"), humanBytes(st.rxBytes));
        add(tr("TX 바이트"), humanBytes(st.txBytes));
        add(tr("RX 패킷"), QString::number(st.rxPackets));
        add(tr("TX 패킷"), QString::number(st.txPackets));

        root->setExpanded(true);
    }
}

