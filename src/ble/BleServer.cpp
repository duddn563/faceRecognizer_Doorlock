#include "BleServer.hpp"

BleServer::BleServer(QObject* parent, FaceRecognitionService* recogServ)
  : QObject(parent), service(recogServ) {}

BleServer::~BleServer() { teardown_(); }

QString BleServer::readAll(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

QString BleServer::runCmd(const QString& cmd) {
    QProcess p; p.start("/bin/sh", {"-c", cmd});
    p.waitForFinished(1000);
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

std::string BleServer::run_cmd(const std::string& cmd, int *exit_code) {
    std::array<char, 256> buf{};
    std::string out;
    FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
    if (!pipe) {
        if (exit_code) *exit_code = -1;
        return "[run_cmd] popen failed";
    }
    while (fgets(buf.data(), buf.size(), pipe)) out += buf.data();
    int code = pclose(pipe);
    if (exit_code) *exit_code = WEXITSTATUS(code);
    return out;
}

void BleServer::run_cmds(const std::vector<std::string>& cmds, int delay_ms) {
    for (const auto& c : cmds) {
        int code = 0;
        std::string out = run_cmd(c, &code);
        std::cout << "[run_cmds] " << c << " (exit=" << code << ")\n";
        if (!out.empty()) std::cout << out << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
}

void BleServer::sendCmdResult(const QString& cmd, bool ok, const QString& msg, const QJsonObject& extra) {
    QJsonObject j;
    j["type"] = "cmd";
    j["cmd"]  = cmd;
    j["ok"]   = ok;
    if (!msg.isEmpty()) j["msg"] = msg;
    for (auto it = extra.begin(); it != extra.end(); ++it) j[it.key()] = it.value();
    sendJsonLine(j);
}

void BleServer::sendFileOverBle(const QString& path, const QString& mime, const QString& name) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QJsonObject err{{"type","cmd"},{"cmd","SNAP"},{"ok",false},{"msg","open failed"}};
        sendJsonLine(err);
        return;
    }
    const QByteArray all = f.readAll();
    f.close();

    const QString id = QString("f%1").arg(QDateTime::currentMSecsSinceEpoch());

    sendJsonLine(QJsonObject{
        {"type","file"}, {"id",id}, {"mode","begin"},
        {"name",name}, {"mime",mime}, {"size", all.size()}
    });

    const int kChunk = 512;
    int seq = 0;
    for (int off = 0; off < all.size(); off += kChunk) {
        const QByteArray part = all.mid(off, kChunk);
        const QString b64 = QString::fromLatin1(part.toBase64());
        sendJsonLine(QJsonObject{
            {"type","file"}, {"id",id}, {"mode","chunk"},
            {"seq",seq++}, {"data", b64}
        });
        QThread::msleep(3);
    }

    sendJsonLine(QJsonObject{{"type","file"}, {"id",id}, {"mode","end"}});
}

// OpenCV one-shot capture
QByteArray BleServer::captureJpegOneShot(int w, int h, int quality) {
    cv::VideoCapture cap;
    if (!cap.open(0, cv::CAP_V4L2)) return {};
    cap.set(cv::CAP_PROP_FRAME_WIDTH,  w);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, h);
    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G'));
    cv::Mat frame;
    for (int i=0;i<5;++i){ cap.read(frame); QThread::msleep(30); }
    if (!cap.read(frame) || frame.empty()) { cap.release(); return {}; }
    cap.release();
    std::vector<uchar> buf;
    std::vector<int> params{ cv::IMWRITE_JPEG_QUALITY, quality };
    if (!cv::imencode(".jpg", frame, buf, params)) return {};
    return QByteArray(reinterpret_cast<const char*>(buf.data()), (int)buf.size());
}

QByteArray BleServer::captureJpeg(int w, int h, int quality) {
    QByteArray jpg = captureJpegOneShot(w,h,quality);
    if (!jpg.isEmpty()) return jpg;
    // libcamera fallback or other logic could go here; for now return empty QByteArray
    return QByteArray();
}

/*
void BleServer::reset_ble_stack(const std::string& hci) {
    qDebug() << "[reset_ble_stack] begin hci=" << QString::fromStdString(hci);
    run_cmds({
        "systemctl restart bluetooth.service || true",
        "sleep 0.5",
        "btmgmt -i " + hci + " power off || true",
        "btmgmt -i " + hci + " bredr off || true",
        "btmgmt -i " + hci + " le on || true",
        "btmgmt -i " + hci + " privacy off || true",
        "btmgmt -i " + hci + " connectable on || true",
        "btmgmt -i " + hci + " advertising on || true",
        "btmgmt -i " + hci + " power on || true",
        "btmgmt -i " + hci + " name SmartLock || true",
        "btmgmt -i " + hci + " info || true"
    }, 5000);
}
*/
void BleServer::reset_ble_stack(const std::string& hci)
{
    qDebug() << "[reset_ble_stack] begin hci=" << QString::fromStdString(hci);

    run_cmds({
        // 항상 데몬은 켜둔다 (Qt Peripheral은 bluez dbus 필요)
        "systemctl restart bluetooth || true",
        "sleep 0.5",

        // 컨트롤러 베이직 셋업
        "btmgmt -i " + hci + " power off || true",
        "btmgmt -i " + hci + " bredr off || true",
        "btmgmt -i " + hci + " le on || true",

        // ★ 문제 원인 축소: 테스트 동안만 off
        "btmgmt -i " + hci + " privacy off || true",

        "btmgmt -i " + hci + " connectable on || true",

				"btmgmt -i " + hci + " ext-adv off || true",
				"btmgmt -i " + hci + " advertising off || true",


        // ★ 여기서 'advertising on' 하지 말 것!
        //   (남아있는 ext-adv 인스턴스도 싹 제거)
        "btmgmt -i " + hci + " rm-adv -1 || true",

        "btmgmt -i " + hci + " power on || true",
        //"btmgmt -i " + hci + " name SmartLock || true",
        "btmgmt -i " + hci + " info || true"
    }, 1000);
}

QJsonObject BleServer::getInfoJson() {
    QString prettyOs;
    const auto osRelease = readAll("/etc/os-release");
    QRegularExpression re("PRETTY_NAME\\=\\\"([^\\\"]+)\\\"");
    QRegularExpressionMatch m = re.match(osRelease);
    if (m.hasMatch()) prettyOs = m.captured(1);
    const QString kernel = runCmd("uname -r");
    QString cpuModel = runCmd(R"(sed -n 's/^Model[[:space:]]*:[[:space:]]*//p' /proc/cpuinfo | head -n1)").trimmed();
    QString tStr = readAll("/sys/class/thermal/thermal_zone0/temp");
    double cpuTempC = NAN;
    if (!tStr.isEmpty()) cpuTempC = tStr.toDouble() / 1000.0;

    long memTotalKB = 0, memAvailKB = 0;
    const QStringList memLines = readAll("/proc/meminfo").split('\n', Qt::SkipEmptyParts);
    QRegularExpression reTotal("^MemTotal:\\s*(\\d+)");
    QRegularExpression reAvail("^MemAvailable:\\s*(\\d+)");
    for (const auto& line : memLines) {
        QRegularExpressionMatch m1 = reTotal.match(line);
        if (m1.hasMatch()) { memTotalKB = m1.captured(1).toLongLong(); continue; }
        QRegularExpressionMatch m2 = reAvail.match(line);
        if (m2.hasMatch()) { memAvailKB = m2.captured(1).toLongLong(); }
    }
    const long memUsedKB = (memTotalKB>0 && memAvailKB>=0) ? (memTotalKB - memAvailKB) : 0;
    auto kbToPretty = [](long kb){ double mb = kb / 1024.0; return QString::number(mb, 'f', 0) + "MB"; };
    const QString memStr = kbToPretty(memUsedKB) + " / " + kbToPretty(memTotalKB);

    QJsonObject j;
    j["type"]    = "info";
    j["os"]      = prettyOs;
    j["kernel"]  = kernel;
    j["cpu"]     = cpuModel;
    if (!std::isnan(cpuTempC)) j["cpuTemp"] = cpuTempC;
    j["mem"]     = memStr;
    return j;
}

QJsonObject BleServer::getNetJson() {
    const QString ip   = runCmd("hostname -I | awk '{print $1}'");
    const QString ssid = runCmd("iwgetid -r");
    int pingMs = -1;
    const QString pong = runCmd("ping -c1 -W1 8.8.8.8 | sed -n 's/.*time=\\([0-9.]*\\) ms.*/\\1/p'");
    if (!pong.isEmpty()) {
        bool ok=false; double v = pong.toDouble(&ok);
        if (ok) pingMs = qRound(v);
    }
    QJsonObject j;
    j["type"]  = "net"; j["ip"] = ip; j["ssid"] = ssid; j["PingMs"]= pingMs;
    return j;
}

QJsonObject BleServer::getBtJson() {
    const QString hci = QStringLiteral("hci0");
    const QString addr = runCmd(QString("btmgmt -i %1 info | awk '/addr /{print $2; exit}'").arg(hci)).trimmed();
    const QString name = runCmd(QString("btmgmt -i %1 info | sed -n 's/^\\s*name \\(.*\\)$/\\1/p'").arg(hci)).trimmed();
    const QString conLine = runCmd("hcitool con | awk '/^LE /{print; exit}'").trimmed();
    const QString peerMac = runCmd("hcitool con | awk '/^LE /{print $2; exit}'").trimmed();
    const QString handle  = runCmd("hcitool con | awk '/^LE /{print $4; exit}'").trimmed();
    int rssi = -127;
    if (!handle.isEmpty()) {
        const QString rssiStr = runCmd(QString("hcitool rssi %1 | awk '/RSSI return value/ {print $4; exit}'").arg(handle)).trimmed();
        bool ok=false; int v = rssiStr.toInt(&ok);
        if (ok) rssi = v;
    }
    QJsonObject j;
    j["type"]   = "bt";
    j["btName"] = !name.isEmpty() ? name : QStringLiteral("Unknown");
    j["btMac"]  = addr;
    j["rssi"]   = rssi;
    return j;
}

// Send JSON line via BLE (chunked using writeCharacteristic for broad compatibility)
void BleServer::sendJsonLine(const QJsonObject& obj) {
    const auto ch = g_service_ ? g_service_->characteristic(CHAR_NOTIFY_UUID) : QLowEnergyCharacteristic();
    if (!ch.isValid()) return;
    const QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
    int mtu = 23;
    if (g_peripheral_) mtu = g_peripheral_->mtu();
    int kMax = qMax(20, mtu - 3);
    for (int off = 0; off < line.size(); off += kMax) {
        const QByteArray chunk = line.mid(off, qMin(kMax, line.size() - off));
        // Use writeCharacteristic: works as Notify if CCCD is set on client side
        if (g_service_) g_service_->writeCharacteristic(ch, chunk, QLowEnergyService::WriteWithoutResponse);
        QThread::msleep(5);
    }
}

void BleServer::run() {
    if (started_) return;
    reset_ble_stack(hciName_.toUtf8().constData());

    g_peripheral_.reset(QLowEnergyController::createPeripheral());
    QObject::connect(g_peripheral_.data(), &QLowEnergyController::errorOccurred,
                     this, [this](QLowEnergyController::Error e){
        emit log(QStringLiteral("[run] ctrl error: %1").arg(int(e)));
    }, Qt::DirectConnection);

    setupGatt_();
    startAdvertising_();

    g_auth_ = std::make_unique<AuthLogRepo>(dbPath_);
    if (!g_auth_->open()) emit log(QStringLiteral("[authdb] fallback to disabled AUTH"));

		addConnect_();

    started_ = true;
    emit ready();
}

void BleServer::addConnect_()
{

  QObject::connect(g_service_, &QLowEnergyService::stateChanged,
      this, [this](QLowEnergyService::ServiceState s) {
      qDebug() << "[main] service state:" << s;
      const auto cmd   = g_service_->characteristic(CHAR_CMD_UUID);
      const auto state = g_service_->characteristic(CHAR_STATE_UUID);
      qDebug() << "[main] CMD valid?"   << cmd.isValid() << cmd.uuid();
      qDebug() << "[main] STATE valid?" << state.isValid() << state.uuid();
		}
	);
  QObject::connect(g_service_, &QLowEnergyService::characteristicChanged,
      this, [this](const QLowEnergyCharacteristic& c, const QByteArray &value) {
      qDebug() << "[main] char CHANGE:" << c.uuid() << "val=" << value;
			const QString str = QString::fromUtf8(value);
      if (c.uuid() == CHAR_CMD_UUID) {
				handleCommand_(str);
      }
    }
	);
}

void BleServer::stop() {
    teardown_();
    started_ = false;
}

void BleServer::teardown_() {
    if (g_service_) { QObject::disconnect(g_service_, nullptr, this, nullptr); g_service_ = nullptr; }
    if (g_peripheral_) { g_peripheral_->stopAdvertising(); g_peripheral_.reset(nullptr); }
    g_auth_.reset();
}

void BleServer::setupGatt_() {
	if (!g_peripheral_) {
		g_peripheral_.reset(QLowEnergyController::createPeripheral());
		QObject::connect(g_peripheral_.data(), &QLowEnergyController::errorOccurred,
		this, [this](QLowEnergyController::Error e){ qWarning() << "[gatt] ctrl error:" << int(e); });
	}

	QLowEnergyCharacteristicData cmd;
	cmd.setUuid(CHAR_CMD_UUID);
	cmd.setProperties(QLowEnergyCharacteristic::Write
									| QLowEnergyCharacteristic::WriteNoResponse
									| QLowEnergyCharacteristic::Read);
	cmd.setValue(QByteArray());

	QLowEnergyCharacteristicData state;
	state.setUuid(CHAR_STATE_UUID);
	state.setProperties(QLowEnergyCharacteristic::Read
									| QLowEnergyCharacteristic::Notify);
	state.setValue("LOCKED");

	QLowEnergyCharacteristicData notify;
	notify.setUuid(CHAR_NOTIFY_UUID);
	notify.setProperties(QLowEnergyCharacteristic::Read
										 | QLowEnergyCharacteristic::Notify);
	notify.setValue(QByteArray());

	QLowEnergyDescriptorData cccd(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration,
																QByteArray(2, char(0x00))

													 );

	state.addDescriptor(cccd);
	notify.addDescriptor(cccd);

	QLowEnergyServiceData svc;
	svc.setType(QLowEnergyServiceData::ServiceTypePrimary);
	svc.setUuid(SERVICE_UUID);
	svc.addCharacteristic(cmd);
	svc.addCharacteristic(state);
	svc.addCharacteristic(notify);

	g_service_ = g_peripheral_->addService(svc);
	if (!g_service_) {
		qWarning() << "[setupGatt_] service setup failed";
	}
}


void BleServer::startAdvertising_()
{
    if (!g_peripheral_) { qWarning() << "[adv] peripheral null"; return; }

    // 1) 파라미터: AdvInd (connectable, legacy)
    QLowEnergyAdvertisingParameters params;
    params.setMode(QLowEnergyAdvertisingParameters::AdvInd);
    params.setInterval(160, 320);   // 100~200ms

    // 2) 광고 데이터 (31바이트 이내)
    QLowEnergyAdvertisingData adv;
    adv.setDiscoverability(QLowEnergyAdvertisingData::DiscoverabilityGeneral);
    adv.setIncludePowerLevel(false);

    //  이름을 짧게 → 페이로드를 31B 밑으로
    adv.setLocalName(QStringLiteral("SmartLock"));

    //  서비스 UUID (필요하면 1~2개만; 128-bit 1개도 OK)
    adv.setServices({ SERVICE_UUID });

    // 3) 스캔 응답 쓰지 않음 → legacy ADV 강제
    //    (중요) 세번째 인자 없이 2-인자 오버로드 사용
    g_peripheral_->startAdvertising(params, adv);

    emit log(QStringLiteral("[BLE] advertising (legacy AdvInd)…"));
}


// --- CMD 처리 본문---
void BleServer::handleCommand_(const QString& s)
{
    if (s == "INFO\n") { sendJsonLine(getInfoJson()); return; }
    if (s == "NET\n")  { sendJsonLine(getNetJson());  return; }
    if (s == "BT\n")   { sendJsonLine(getBtJson());   return; }

    if (s == "AUTH\n") {
        const int limit = 50;
        if (g_auth_ && g_auth_->open()) {
            auto rows = g_auth_->fetchRecent(limit);
            sendJsonLine(AuthLogRepo::toAuthJson(rows));
        } else {
            sendJsonLine(QJsonObject{{"type","auth"},{"count",0},{"error","db not open"}});
        }
        return;
    }

    if (s.startsWith("AUTH_IMAGE\n")) {
        QStringList toks = s.split(' ', Qt::SkipEmptyParts);
        if (toks.size() < 2) { sendJsonLine(QJsonObject{{"type","auth_img"},{"error","missing id"}}); return; }
        bool ok=false; int id=toks[1].toInt(&ok);
        if (!ok) { sendJsonLine(QJsonObject{{"type","auth_img"},{"error","invalid id"}}); return; }
        if (!g_auth_ || !g_auth_->open()) { sendJsonLine(QJsonObject{{"type","auth_img"},{"id",id},{"error","db not open"}}); return; }
        QByteArray blob = g_auth_->fetchImageBlob(id);
        if (blob.isEmpty()) { sendJsonLine(QJsonObject{{"type","auth_img"},{"id",id},{"error","no image"}}); return; }
        sendJsonLine(QJsonObject{
            {"type","auth_img"}, {"id",id},
            {"mime","application/octet-stream"},
            {"data_b64", QString::fromLatin1(blob.toBase64())}
        });
        return;
    }

    if (s == "SNAP\n") {
        const int W=640, H=480, Q=75;
        QByteArray jpg = captureJpeg(W,H,Q);
        if (jpg.isEmpty()) { sendJsonLine(QJsonObject{{"type","cmd"},{"cmd","SNAP"},{"ok",false},{"msg","capture failed"}}); return; }
        const QString tmp = "/tmp/snap.jpg";
        QFile f(tmp);
        if (!f.open(QIODevice::WriteOnly) || f.write(jpg)!=jpg.size()) {
            sendJsonLine(QJsonObject{{"type","cmd"},{"cmd","SNAP"},{"ok",false},{"msg","write temp failed"}});
            return;
        }
        f.close();
        sendJsonLine(QJsonObject{{"type","cmd"},{"cmd","SNAP"},{"ok",true},{"msg","ok"}});
        sendFileOverBle(tmp, "image/jpeg", "snap.jpg");
        return;
    }

    // 장치 제어 커맨드
    if (s == "REFRESH\n")      { sendCmdResult("REFRESH", true, "새로고침 완료"); return; }
    if (s == "CAM_RESTART\n")  { int rc=0; sendCmdResult("CAM_RESTART", rc==0, rc==0?"카메라 재시작 성공":"카메라 재시작 실패"); return; }
    if (s == "OPEN\n")         { bool ok= service->staticDoorStateChange(true); sendCmdResult("OPEN", ok,  ok?"도어 열기 성공":"도어 열기 실패"); return; }
    if (s == "CLOSE\n")         { bool ok= service->staticDoorStateChange(false); sendCmdResult("LOCK", ok,  ok?"도어 잠금 성공":"도어 잠금 실패"); return; }
    if (s == "RET_RECOG\n")    { int rc=0; sendCmdResult("RET_RECOG", rc==0, rc==0?"재학습 완료":"재학습 실패"); return; }

    if (s == "LOG_EXPORT\n") {
        if (!g_auth_ || !g_auth_->open()) { sendCmdResult("LOG_EXPORT", false, "DB 열기 실패"); return; }
        const int limit = 1000;
        const QString outPath = "/root/trunk/faceRecognizer_Doorlock/assert/db/auth_logs.csv";
        auto rows = g_auth_->fetchRecent(limit);
        QJsonArray arr = AuthLogRepo::toAuthJson(rows).value("entries").toArray();
        QSaveFile sf(outPath);
        if (!sf.open(QIODevice::WriteOnly | QIODevice::Truncate)) { sendCmdResult("LOG_EXPORT", false, "파일 열기 실패"); return; }
        sf.write("id,user_name,message,timestamp,hasImage\n");
        auto esc = [](QString s){ s.replace('"',"\"\""); return "\"" + s + "\""; };
        for (const auto &v : arr) {
            const QJsonObject e = v.toObject();
            const QString line = QString("%1,%2,%3,%4,%5\n")
                    .arg(e.value("id").toInt())
                    .arg(esc(e.value("user").toString()))
                    .arg(esc(e.value("message").toString()))
                    .arg(esc(e.value("ts").toString()))
                    .arg(e.value("hasImage").toBool()? "1":"0");
            sf.write(line.toUtf8());
        }
        if (!sf.commit()) { sendCmdResult("LOG_EXPORT", false, "파일 저장 실패"); return; }
        QJsonObject extra; extra["path"] = outPath;
        sendCmdResult("LOG_EXPORT", true, "CSV 저장 완료", extra);
        sendFileOverBle(outPath, "text/csv", "auth_logs.csv");
        return;
    }

    // 알 수 없는 커맨드
    sendCmdResult(s, false, "unknown command");
}


