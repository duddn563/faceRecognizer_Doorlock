#pragma once
#include <QWidget>
#include <QTreeWidget>
#include <QLabel>
#include <QPushButton>

class NetworkInfoWidget : public QWidget {
    Q_OBJECT
public:
    explicit NetworkInfoWidget(QWidget* parent = nullptr);

public slots:
    void refresh();

private:
    // 상단 요약
    QLabel* lblDefaultIf_ = nullptr;
    QLabel* lblGateway_   = nullptr;
    QLabel* lblDns_       = nullptr;

    // 인터페이스 상세
    QTreeWidget* tree_    = nullptr;

    // helpers
    static QString runCmd(const QString& cmd, const QStringList& args = {}, int msec = 1200);
    static QString readTextFile(const QString& path);
    static QString readFirstLine(const QString& path);
    static QStringList listInterfaces(); // /sys/class/net/*
    static QString humanBitsPerSec(qint64 mbps);
    static QString humanBytes(qulonglong bytes);
    static QString boolOnOff(const QString& state); // "up"/"down"→"UP"/"DOWN"

    struct RouteInfo {
        QString defIf;
        QString gateway;
    };
    static RouteInfo queryDefaultRoute(); // ip route show default
    static QStringList queryDnsServers(); // /etc/resolv.conf

    static QStringList queryIpAddrs(const QString& ifname, bool v6); // ip -o -4/-6 addr show dev IF

    struct IfStats {
        qulonglong rxBytes=0, txBytes=0;
        qulonglong rxPackets=0, txPackets=0;
    };
    static IfStats readIfStats(const QString& ifname);
    static QString readIfValue(const QString& ifname, const QString& leaf); // e.g. "address","mtu","operstate","speed"
};

