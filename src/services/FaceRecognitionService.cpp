#include <QTimer>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QDateTime>
#include <QByteArray>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <memory>
#include <QStringBuilder>
#include <array>
#include <vector>
#include <algorithm>		// for std::max_element
#include <cmath>			// for std::hypot

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QCoreApplication>
#include <QPainter>

#include "FaceRecognitionService.hpp"
#include "presenter/FaceRecognitionPresenter.hpp"
#include "MainWindow.hpp"
#include "hw/DoorlockController.hpp"
#include "hw/ReedSensor.hpp"
#include "hw/UnlockUntilReed.hpp"
#include "hw/UltrasonicSensor.hpp"
#include "fsm/fsm_logging.hpp"
#include "include/common_path.hpp"
#include "log/SystemLogger.hpp"
#include "services/QSqliteService.hpp"
#include "util/textDrawUtil.hpp"


// #define DEBUG 
// ===== 컨트롤러/매니저(정적) =====
static DoorlockController      g_door;
static ReedSensor			   g_reed;
static UltrasonicSensor		   g_uls;

static UnlockUntilReed::Opt    g_unlockOpt{/*pollMs*/50, /*hits*/6, /*openTimeoutMs*/5000, /*maxUnlockMs*/10000};
static UnlockUntilReed         g_unlockMgr(&g_door, &g_reed, g_unlockOpt);


namespace fs = std::filesystem;

// === helpers === 
// 로컬 헬퍼: L2 정규화(0 division 방지 포함)
static inline void l2normInPlace(std::vector<float>& v) {
	double s = 0.0;
	for (float x : v) s += double(x) * x;
	s = std::sqrt(std::max(1e-12, s));
	for (float& x : v) x = float(x / s);
}

FaceRecognitionService::FaceRecognitionService(QObject* parent, FaceRecognitionPresenter* presenter, QSqliteService* db) : QObject(parent), presenter(presenter), db(db)
{
	// 0) Service initialize
	init();

	// 1) 전환 테이블 구성
	setupRecognitionFsm(fsm_, params_);

	// 2) FSM stateChanged -> 서비스 stateChanged로 그대로 포워딩
	connect(&fsm_, &RecognitionFsm::stateChanged, this, 
			[this](RecognitionState s) {
				if (s == RecognitionState::IDLE || s == RecognitionState::DETECTING) {
					authManager.resetAuth();
					hasAlreadyUnlocked = false;
				}
				emit stateChanged(s);
			}
	);

	// 3) 스냅샷 타이머 시작(컨텍스트 공급)
	tick_.setInterval(33);
	connect(&tick_, &QTimer::timeout, this, &FaceRecognitionService::onTick);
	tick_.start();

	// 4) FSM 시작
	fsm_.start(RecognitionState::IDLE);

	// 5) Detect init
	std::string detect_model_name = std::string(YNMODEL_PATH) + YNMODEL;
	detector_.init(detect_model_name, 
			/*inputW*/320, /*inputH*/240, 
			/*scoreThr*/0.6f, /*nmsThr*/0.3f, /*topK*/500);

	// 6) Decision init
	decision_.setParams(DecisionParams {
			.acceptSim				= 0.90f,
			.strongAcceptSim		= 0.96f,
			.minTop2Gap				= 0.04f,
			.minBestOnly			= 0.40f
			});
	
	
	setDoorOpened(!g_reed.isClosed());

	cv::setUseOptimized(true);
	cv::setNumThreads(2);
}

void FaceRecognitionService::setPresenter(FaceRecognitionPresenter* _presenter)
{
	presenter = _presenter;
}


void FaceRecognitionService::init()
{
	qDebug() << "[init] Face Recognition Service initiallize!!";
	if (!this->initializeDnnOnly()) {
		SystemLogger::error("DNN", "DNN-only init failed (YuNet/MobileFaceNet)");
		return;
	}

	// 매칭기 초기화

	qDebug() << "[init] g_door or g_reed init!!";
	if (!g_door.init())  { qWarning() << "[init] Door init failed"; }
	if (!g_reed.init())  { qWarning() << "[init] Reed init failed"; }
	g_uls.start();
}

bool FaceRecognitionService::idExists(int id) const
{
	return std::any_of(gallery_.begin(), gallery_.end(),
			[&] (const UserEmbedding& u) { return u.id == id; });
}

void FaceRecognitionService::rebuildNextIdFromGallery()
{
	int maxId = 0; 		// Start ID or Last ID 
	for (const auto& u : gallery_) {			// 유저 버퍼에서 다음 ID를 찾음
		if (u.id > maxId) maxId = u.id;
	}

	if (maxId == 0) {							// ID가 0이면 0부터 시작하고 0이 아니면 +1을 하여 Counter에 셋팅
		nextIdCounter_.store(maxId, std::memory_order_relaxed);
	}
	else {
		nextIdCounter_.store(maxId + 1, std::memory_order_relaxed);
	}
}

int FaceRecognitionService::nextSequentialId()
{
	// 기본적으로 증가
	int id = nextIdCounter_.fetch_add(1, std::memory_order_relaxed);

	// 혹시 외부에서 수동으로 추가되며 충돌이 생길 수 있으니 마지막 방어
	while (idExists(id)) {
		id = nextIdCounter_.fetch_add(1, std::memory_order_relaxed);
	}

	return id;
}

bool FaceRecognitionService::ensureEmbFile()
{
	if (embeddingsPath_.isEmpty()) return false;
	if (QFile::exists(embeddingsPath_)) return false;

	int dim = 128;

	QJsonObject root;
	root["version"] = 1;
	root["dim"]			= dim;					// TODO: 실제 임베딩 파일에서 DIM을 받아오기
	root["count"]		= 0;
	root["items"]		= QJsonArray{};

	const QByteArray out = QJsonDocument(root).toJson(QJsonDocument::Indented);

	QFile f(embeddingsPath_);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		qWarning() << "[ensureEmbFile] open failed:" << embeddingsPath_ << f.errorString();
		return false;
	}

	if (f.write(out) != out.size()) {
		qWarning() << "[ensureEmbFile] write failed:" << embeddingsPath_;
		f.close();
		QFile::remove(embeddingsPath_);
		return false;
	}
	f.close();

	qInfo() << "[ensureEmbFile] created empty file:" << embeddingsPath_
		<< "dim=" << dim;
	return true;
}

bool FaceRecognitionService::loadRecognizer()
{
	const QString modelQ = QStringLiteral(SFACE_RECOGNIZER_PATH) + QStringLiteral(SFACE_RECOGNIZER);
	QFileInfo fi(modelQ);
	if (!fi.exists() || !fi.isFile()) {
		SystemLogger::error("FRS", QString("Recognizer not found: %1").arg(modelQ));
		qWarning() << "[loadRecognizer] Recognizer not found (" << modelQ << ")";
		return false;	
	}

	if (!fi.isReadable()) {
		SystemLogger::error("FRS", QString("Recognizer not found: %1").arg(modelQ));
		qWarning() << "[loadRecognizer] Recognizer not readable (perm):" << modelQ;
		return false;
	}

	qInfo() << "[loadRecognizer] sface recognizer onnx bytes=" << fi.size(); 


	// Embedder 옵션
	Embedder::Options opt;
	opt.modelPath 	= modelQ;
	opt.inputSize	= 112;
	opt.useRGB		= true;
	opt.norm		= Embedder::Options::Norm::MinusOneToOne;		//  입력 정규화 방식

	dnnEmbedder_ = std::make_unique<Embedder>(opt);
	if (!dnnEmbedder_) {
		qDebug() << "[loadRecognizer] SFace recognizer is nullptr";
		return false;
	}


	if (!dnnEmbedder_->isReady()) {
		SystemLogger::error("Recognizer", "SFace recognizer not ready (net load failed");
		qWarning() << "[loadRecognizer] SFace recognizer not ready";
		dnnEmbedder_.reset();
		return false;
	}

	matcher_ = std::make_unique<FaceMatcher>(dnnEmbedder_);	

	qInfo() << "[loadRecognizer] Sface recognizer is loaded(" << modelQ << ")";

	return true;
}

// ---------------------------
//  임베딩 파일 경로 세팅 및 로드
// ---------------------------
bool FaceRecognitionService::loadEmbJsonFile()
{
	bool rc = false;
	const QString embPath = QStringLiteral(EMBEDDING_JSON_PATH) + QStringLiteral(EMBEDDING_JSON);
	embeddingsPath_ = embPath.toUtf8().constData();
	if (!QFile::exists(embPath)) {
		ensureEmbFile();
	}


	// 3) Embedding.json 파일 로드 -> 사용자 DB
	rc = loadEmbeddingsFromFile();
	if (!rc) {
		SystemLogger::error("FRS", QString("Embedding json file is not found(%1)").arg(embPath));
		qDebug() << "[loadEmbJosnFile] File load failed to embeddings";	
		gallery_.clear();
		rc = false;
	}
	else {
		qInfo() << "[loadEmbJsonFile] Loaded embeddings:" << (int)gallery_.size()
			<< "users from" << embPath;
		rc = true;
	}

	return rc;
}

// ---------------------------
//  DNN 초기화  
//	@rc : result count (error count)
//	@rv : result value
//	# detector -> recognizer -> embedding json file 
// ---------------------------
bool FaceRecognitionService::initializeDnnOnly()
{
	int rc = 0;
	bool rv = false;

	if (!loadRecognizer()) rc--;

	if (!loadEmbJsonFile()) rc--;

	// 사용자 ID 초기화
	rebuildNextIdFromGallery();

	if (rc < 0) {
		SystemLogger::info("FRS", QString("DNN-only pipeline is not ready(error count:%1)").arg(rc));
		rv = false;
	}
	else {
		SystemLogger::info("FRS", "DNN-only pipeline ready");
		qDebug() << "[FRS] DNN-only pipeline ready. UltraFace + MobileFaceNet loaded.";
		rv = true;
	}

	return rv;
}

// === ROI영역을 일정 비율로 확장 ===
cv::Rect FaceRecognitionService::expandRect(const cv::Rect& r, float scale, const cv::Size& imgSz)
{
	// 입력 사각형 r의 중심점 계산
	cv::Point2f c(r.x + r.width*0.5f, r.y + r.height*0.5f);

	// 최대 가로, 세로 길이에 scale 비율을 곱해 확장된 사각형의 반절 길이 계산 
	float half = 0.5f * scale * std::max(r.width, r.height);

	// 확장된 사각형의 좌상단 좌표 계산 (중심점에서 half만큼 빼기
	int x1 = int(std::round(c.x - half));
	int y1 = int(std::round(c.y - half));

	// 확장된 사각형의 우하단 좌표 계산 (중심점에서 half만큼 빼기
	int x2 = int(std::round(c.x + half));
	int y2 = int(std::round(c.y + half));

	// 좌상단 좌표가 이미지 크기(imgSz)를 넘어가지 않도록 조정
	x1 = std::max(0, x1); 
	y1 = std::max(0, y1);

	// 좌상단 (x1, y1)과 우하단(x2, y2)좌표로 새로운 확장된 사각형 반환
	x2 = std::min(imgSz.width -1,  x2);
	y2 = std::min(imgSz.height-1, y2);

	// 좌상단(x1, y1)과 우하단(x2, y2) 좌표로 새로운 확장된 사각형 반환
	return cv::Rect(cv::Point(x1,y1), cv::Point(x2,y2));
}

// ===  취소 플래그 OFF === 
void FaceRecognitionService::cancelRegistering() 
{
	m_cancelReg.store(true, std::memory_order_relaxed);
}


// === 등록 강제 종료 ==
void FaceRecognitionService::forceAbortRegistration() {
	// 1) 진행 중 플래그/카운터 정리
	isRegisteringAtomic.storeRelaxed(0);
	setRegisterRequested(false);
	captureCount = 0;


	registeringUserName_.clear();
	registeringUserId_	 = -1;

	regEmbedsBuffers_.clear();		// 임베딩 제이슨 파일에 저장할 버퍼 초기화
	regImageBuffers_.clear();		// 메모리에 저장할 이미지 버퍼

	m_isAngleRegActive = false;
	m_recentEmbedsThisStep.clear();

	cancelRegistering();
}

// === 등록 시작 ===
void FaceRecognitionService::startRegistering(const QString& name)
{
	qDebug() << "[FaceRecognitionService] startRegisting() called on thread" << QThread::currentThread();

	if (name.trimmed().isEmpty()) {
		qWarning() << "[FRS] empty name -> ignore";
		return;
	}

	m_cancelReg.store(false, std::memory_order_relaxed);
	setRegisterRequested(true);

	registeringUserName_ = name;
	registeringUserId_ = nextSequentialId();
	captureCount = 0;
	regEmbedsBuffers_.clear();
	regImageBuffers_.clear();		// 메모리에 저장할 이미지 버퍼

	m_isAngleRegActive = true;
	m_stepIndex = 0;
	m_stepCaptured = 0;
	m_recentEmbedsThisStep.clear();

	// 안전 문열림 잔상 제거
	setAllowEntry(false);
	authManager.resetAuth();
	hasAlreadyUnlocked = false;

	qDebug() << "[startRegistering] set isRegisteringAtomic=1";
	isRegisteringAtomic.storeRelaxed(1);
	qDebug() << "[startRegistering] after store value=" << isRegisteringAtomic.loadRelaxed();

	qInfo() << "[FRS] StartRegistering id=" << registeringUserId_
		<< " name=" << name;
}

// === 등록 중인 유저 이름 getter ===
// UI에서 호출 예정
QString FaceRecognitionService::getUserName() const { return registeringUserName_; }

// === 임베딩 json파일에 저장===
// gallery_ -> 파일 (안전하게 임시 파일에 쓴 뒤 rename) 
bool FaceRecognitionService::saveEmbeddingsToFile() const
{
	if (embeddingsPath_.isEmpty()) return false;

	// snapshot
	std::vector<UserEmbedding> snapshot;
	{ QMutexLocker lk(&embMutex_); snapshot = gallery_; }

	// dim
	int dim = 128;				// TODO: 실제 임베더에 DIM 구하는 함수 구현
	if (dnnEmbedder_) {
		//int d = dnnEmbedder_->embeddingDim();
		//if (d > 0) dim = d;
	}

	QJsonArray items;
	for (const auto& ue : snapshot) {
		QJsonObject o;
		o["id"] = ue.id;
		o["name"] = ue.name;
		QJsonArray emb;
		for (float v : ue.embedding) emb.append(double(v));
		o["embedding"] = emb;
		items.append(o);
	}

	QJsonObject root;
	root["count"] = int(items.size());
	root["dim"] = dim;
	root["items"] = items;
	root["version"] = 1;

	const QByteArray out = QJsonDocument(root).toJson(QJsonDocument::Indented);
	const QString tmp = embeddingsPath_ + ".tmp";
	const QString bak = embeddingsPath_ + ".bak";

	QFile tf(tmp);
	if (!tf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		qWarning() << "[Embedding] temp open failed:" << tmp << "err=" << tf.errorString();
		return false;
	}
	if (tf.write(out) != out.size()) { tf.close(); tf.remove(); return false; }
	tf.close();

	QFile::remove(bak);
	if (QFile::exists(embeddingsPath_)) QFile::copy(embeddingsPath_, bak);
	QFile::remove(embeddingsPath_);
	if (!QFile::rename(tmp, embeddingsPath_)) {
		qWarning() << "[Embedding] rename tmp->final failed:" << tmp;
		return false;
	}
	qInfo() << "[Embedding] saved users=" << items.size() << "to" << embeddingsPath_;
	return true;
}

int FaceRecognitionService::appendUserEmbedding(const QString& name, const std::vector<float>& emb)
{
	if (emb.empty()) return -1;

	// 새 id: 현재 갤러리 id 최대값+1
	int newId = 0;
	{
		QMutexLocker lk(&embMutex_);
		for (const auto& g : gallery_) newId = std::max(newId, g.id + 1);
		UserEmbedding ue;
		ue.id = newId;
		ue.name = name;
		ue.embedding = emb;
		gallery_.push_back(std::move(ue));
	}
	if (!saveEmbeddingsToFile()) {
		qWarning() << "[Embedding] save failed after append";
	}
	return newId;
}

bool FaceRecognitionService::loadEmbeddingsFromFile()
{
	gallery_.clear();

	QFile f(embeddingsPath_);
	if (!f.exists()) {
		qWarning() << "[loadEmbeddingsFromFile] file not found ->" << embeddingsPath_;
		return false;
	}
	if (!f.open(QIODevice::ReadOnly)) {
		qWarning() << "[loadEmbeddingsFromFile]: open failed ->" << embeddingsPath_ << f.errorString();
		return false;
	}

	const QJsonDocument jd = QJsonDocument::fromJson(f.readAll());
	f.close();
	if (!jd.isObject()) return false;

	const auto root		= jd.object();
	const auto items  = root.value("items").toArray();

	std::vector<UserEmbedding> temp;
	temp.reserve(items.size());

	for (const auto& v : items) {
		const auto o = v.toObject();
		UserEmbedding ue;
		ue.id		= o.value("id").toInt(-1);
		ue.name = o.value("name").toString();

		const auto embArr = o.value("embedding").toArray();
		ue.embedding.reserve(embArr.size());
		for (const auto& ev : embArr) ue.embedding.push_back(float(ev.toDouble()));

		if (!ue.embedding.empty()) {
			ue.proto = cv::Mat(1, int(ue.embedding.size()), CV_32F, ue.embedding.data()).clone();
		}
		if (ue.id >= 0) temp.push_back(std::move(ue));
	}

	{ QMutexLocker lk(&embMutex_); gallery_ = std::move(temp); }
	qInfo() << "[loadEmbeddingsFromFile] " << int(gallery_.size()) << "users from" << embeddingsPath_;
	return true;
}

MatchTop2 FaceRecognitionService::bestMatchTop2(const std::vector<float>& emb) const
{
	return matcher_->bestMatchTop2(emb, gallery_, true);
}
std::vector<FaceDet> FaceRecognitionService::detectAllYuNet(const cv::Mat& bgr) const
{
	return detector_.detectAll(bgr);
}

std::optional<FaceDet> FaceRecognitionService::detectBestYuNet(const cv::Mat& bgr) const
{
	return detector_.detectBest(bgr);
}


// === 중볻된 얼굴인지 체크 ===
bool FaceRecognitionService::isDuplicateFaceDNN(const cv::Mat& alignedFace, int* dupId, float* simOut) const
{
	if (!dnnEmbedder_) return false;

	std::vector<float> emb;
	cv::Mat alignedBGR;
	cv::cvtColor(alignedFace, alignedBGR, cv::COLOR_RGB2BGR);
	if (!dnnEmbedder_->extract(alignedBGR, emb) || emb.empty()) return false;

	// Top-2 매칭
	MatchResult r = matcher_->bestMatch(alignedFace, gallery_);
	if (r.sim >= params_.recogEnter) {
		return true;
	}
	else {
		return false;
	}
}

enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };

static inline cv::Rect clampRect(const cv::Rect& r, const cv::Size& sz) {
	int x = std::max(0, r.x);
	int y = std::max(0, r.y);
	int w = std::min(r.width,  sz.width  - x);
	int h = std::min(r.height, sz.height - y);
	return (w>0 && h>0) ? cv::Rect(x,y,w,h) : cv::Rect();
}

static void overlayThumbnail(cv::Mat& frame, cv::Mat thumb, Corner corner, int margin=10) {
	// (필요하면 방향 맞추기: 수평/수직 플립)
	cv::flip(thumb, thumb, 1); // 미리보기 좌우반전이면 주석 해제
	cv::flip(thumb, thumb, 0); // 위아래 뒤집힘이면 주석 해제

	const int w = thumb.cols, h = thumb.rows;
	int x=margin, y=margin;
	switch (corner) {
		case Corner::TopLeft:     x = margin;                       y = margin;                       break;
		case Corner::TopRight:    x = frame.cols - w - margin;      y = margin;                       break;
		case Corner::BottomLeft:  x = margin;                       y = frame.rows - h - margin;      break;
		case Corner::BottomRight: x = frame.cols - w - margin;      y = frame.rows - h - margin;      break;
	}
	cv::Rect roi = clampRect({x,y,w,h}, frame.size());
	if (roi.empty()) return;

	// thumb가 roi보다 클 경우 잘라서 복사
	cv::Rect src(0,0, roi.width, roi.height);
	thumb(src).copyTo(frame(roi));
}

// === 등록 파이프 라인 ===
void FaceRecognitionService::drawAnglePrompt(cv::Mat& frame, const QString& text) 
{
	drawKoreanTextOnMat(frame,
				        text,
                        Anchor::TopCenter,
                        14,
                        std::max(18, int(frame.rows * 0.06)),
                        QColor(0, 0, 255));
}


void FaceRecognitionService::advanceAngleStepIfReady()
{
	if (m_stepCaptured >= m_perStepTarget) {
		m_stepIndex++;						// 다음 단계의 인덱스로 증가
		m_stepCaptured = 0;					// 다음 단계를 위해 캡처 수 초기화
		m_recentEmbedsThisStep.clear();		// 다음 단계의 임베딩을 저장하기위해 초기화
	}
}

bool FaceRecognitionService::angleRegCompleted()
{
	return (m_stepIndex >= 5);	//  0~4총 5단계 완료 시 true
}

void FaceRecognitionService::drawProgressBar(cv::Mat& frame)
{
	const int totalSteps = 5;
	const int barW = std::min(420, frame.cols - 40);
	const int barH = 14;
	const int x0 = (frame.cols - barW)/2;
	const int y0 = frame.rows - 40;

	cv::rectangle(frame, cv::Rect(x0, y0, barW, barH), cv::Scalar(200, 200, 200), 2, cv::LINE_AA);
	const int cellW = barW / (totalSteps * m_perStepTarget);

	int filled = m_stepIndex * m_perStepTarget + m_stepCaptured;
	int fillW = std::min(barW, filled * cellW);

	if (fillW > 0) {
		 cv::rectangle(frame, cv::Rect(x0, y0, fillW, barH), cv::Scalar(0,220,0), cv::FILLED, cv::LINE_AA);
	}
}

DetectedStatus FaceRecognitionService::handleRegistration(Mat& frame, const Rect& face, const Mat& alignedFace, QString& labelText, Scalar& boxColor)
{
	qDebug() << "[FaceRecognitionService] handleRegistration()";
	DetectedStatus rc;

	// 취소 체크(초반 빠른 탈출)
	if (m_cancelReg.load(std::memory_order_relaxed)) {
		qDebug() << "[FaceRecognitionService] canceled (early in handleRegistration)";
		forceAbortRegistration();
		emit registrationCompleted(false, QStringLiteral("취소됨/타임아웃"));
		return DetectedStatus::TimeoutRegistration;	// 등록 타임 아웃
	}

	// 중복 체크 시 DNN 사용
	if (captureCount <=  9) {
		int dupId = -1;
		float sim = 0.f;
		if (isDuplicateFaceDNN(alignedFace, &dupId, &sim)) {
			qDebug() << "[dep] dep?";
			// 이미 등록된 사용자로 판단 -> 중복 처리
			isRegisteringAtomic.storeRelaxed(0);
			setDuplicate(true);
			forceAbortRegistration();
			emit registrationCompleted(false, QStringLiteral("중복된 얼굴"));
			setRegisterRequested(false);

			return DetectedStatus::DuplicateFace;			// 중복 얼굴
		}
	}

	Mat colorFace = frame.clone();
	labelText = registeringUserName_ + " is Registering...";
	boxColor = Scalar(255, 0, 0);
	drawTransparentBox(frame, face, boxColor, 0.3);
	drawCornerBox(frame, face, boxColor, 2, 25);
	putText(frame, labelText.toStdString(), Point(face.x, face.y - 10),
			FONT_HERSHEY_DUPLEX, 1.0, boxColor, 2);


	qDebug() << "[FaceRecognitionService] captureCount: " << captureCount;
	if (captureCount < 10) {
		if (m_cancelReg.load(std::memory_order_relaxed)) {
			qDebug() << "[FaceRecognitionService] canceled before saveCaptureFace";
			forceAbortRegistration();
			emit registrationCompleted(false, QStringLiteral("취소됨/타임아웃"));
			return DetectedStatus::TimeoutRegistration;	// 등록 타임 아웃
		}

		if (m_isAngleRegActive) {
			// 1) 현재 단계 프롬프트 표시 (예: 상단 중앙)
			if (m_stepIndex >= 0 && m_stepIndex < 5) {
				//drawAnglePrompt(frame, m_anglePrompts[m_stepIndex]);
				if (m_stepIndex == 1) {
					rc = DetectedStatus::PoseFront;
				}
				else if (m_stepIndex == 2) {
					rc = DetectedStatus::PoseYawLeft;
				}
				else if (m_stepIndex == 3) {
					rc = DetectedStatus::PoseYawRight;
				}
				else if (m_stepIndex == 4) {
					rc = DetectedStatus::PosePitchUp;
				}
				else if (m_stepIndex == 5) {
					rc = DetectedStatus::PosePitchDown;
				}

				drawProgressBar(frame);
			}

			// 2) 현재 프레임 임베딩 추출 (기존 extract() 그대로 사용, BGR alignedFace 준수)
			std::vector<float> curEmbed;
			if (!dnnEmbedder_->extract(alignedFace, curEmbed) && !curEmbed.empty()) {
				// 추출 실패면 이번 프레임은 패스
				labelText = QStringLiteral("임베딩 추출 실패");
				boxColor = cv::Scalar(0, 0, 255);
				return DetectedStatus::FailedRegistration; // 등록 루프는 계속 돎
			}

			// 3) 동일 단계 내 과도한 유사도(=자세 안바뀜)면 캡처 보류 + 보조 문구
			bool tooSimilar = false;
			for (const auto& prev : m_recentEmbedsThisStep) {
				float sim = dnnEmbedder_->cosine(curEmbed, prev);
				if (sim >= m_dupSimThreshold) { tooSimilar = true; break; }
			}
			if (tooSimilar) {
				// 보조 안내 (같은 단계에서만)
				//drawAnglePrompt(frame, QStringLiteral("조금만 다르게 움직여주세요."));
				// 이 프레임은 저장/카운트 증가 없이 종료
				return DetectedStatus::RetryRegistration; // 등록 루프는 계속 돎
			}

			// 4) 여기까지 왔다면 "채택 가능한 각도" → 실제 저장 로직 직전에
			//    (기존 저장 경로를 건드리지 않고, '사전 동의'처럼 흔적만 남김)
			m_recentEmbedsThisStep.push_back(std::move(curEmbed));
			m_stepCaptured++;

			// 5) 단계 충족 시 다음 단계로
			advanceAngleStepIfReady();

			// 6) 전체 목표(예: 10장) 완료되면 finalizeRegistration() 트리거
			if (angleRegCompleted()) {
				// 기존 finalizeRegistration() 호출 이미 있는 곳과 '동일 지점'에서 자연스럽게 이어지게 함.
				// 만약 현재 분기에서 finalizeRegistration()을 직접 불러야 한다면:
				finalizeRegistration();
				return DetectedStatus::CompleteRegistration;	// 등록 완료
			}
		}

		saveCapturedFace(face, alignedFace, colorFace); 
		captureCount++;

		if (captureCount >= 10) {
			if (m_cancelReg.load(std::memory_order_relaxed)) {
				qDebug() << "[FRS] canceled before finalize";
				forceAbortRegistration();
				emit registrationCompleted(false, QStringLiteral("취소됨/타임아웃"));
				return DetectedStatus::TimeoutRegistration;	// 등록 타임 아웃
			}
			finalizeRegistration();
			return DetectedStatus::CompleteRegistration;	// 등록 완료
		}
	}
	//return DetectedStatus::Registering;
	return rc;
}


void FaceRecognitionService::saveCapturedFace(const Rect& face, const Mat& alignedFace, const Mat& frame) 
{
	if (!fs::exists(USER_FACES_DIR))
		fs::create_directory(USER_FACES_DIR);


	if (frame.empty()) qDebug() << "[saveCaptureFace] Frame is empty";
	// 이미지 버퍼에 저장 후 finalizeRegistration에서 최종 메모리에 저장
	regImageBuffers_.push_back(frame);

	// ---- DNN 임시 임베딩 버퍼 추가 ----
	if (dnnEmbedder_) {
		std::vector<float> e;
		if (dnnEmbedder_->extract(alignedFace, e) && !e.empty()) {
			regEmbedsBuffers_.push_back(std::move(e));
		}
	}
	else {
		qDebug() << "[saveCaptureFace] Embedder is nullptr";
		forceAbortRegistration();
	}
	qDebug() << "[FaceRecognitionService]" << registeringUserName_  << " image has been saved and loaded";
}

static void printVector(const std::vector<float>& v, const QString& tag = "embedding")
{
	if (v.empty()) {
		qDebug() << "[" << tag << "] (empty)";
		return;
	}

	qDebug().noquote() << QString("[%1] dim=%2").arg(tag).arg(v.size());
	for (size_t i = 0; i < v.size(); ++i) {
		qDebug().noquote() << QString("[%1] %2")
							  .arg(i, 3)
							  .arg(v[i], 0, 'f', 20);
	}
}

void FaceRecognitionService::finalizeRegistration()
{
	qDebug() << "[FRS] finalizeRegistration()";

	// 1) 취소 체크
	if (m_cancelReg.load(std::memory_order_relaxed)) {
		qDebug() << "[FRS] canceled at finalizeRegistration()";
		forceAbortRegistration();
		emit registrationCompleted(false, QStringLiteral("취소됨/타임아웃"));
		return;
	}

	// 2) DNN 평균 임베딩 계산
	std::vector<float> meanEmb;
	int used = 0;
	for (const auto& e : regEmbedsBuffers_) {
		if (e.empty()) continue;
		if (meanEmb.empty()) meanEmb.assign(e.size(), 0.0f);
		if (e.size() != meanEmb.size()) continue;
		for (size_t i = 0; i < e.size(); i++) meanEmb[i] += e[i];
		++used;
	}

	if (regEmbedsBuffers_.empty()) {
		qDebug() << "[finalizeRegistration] register buffer is empty";
	}

	if (used == 0) {
		qWarning() << "[FRS] No embeddings collected";
		forceAbortRegistration();
		emit registrationCompleted(false, QStringLiteral("파이널 등록 실패"));
		return;
	}

	for (auto& v : meanEmb) v /= static_cast<float>(used);

	l2normInPlace(meanEmb);
	//printVector(meanEmb, "meanEmb(before norm)");

	// 3) gallery_ 업데이트 (registeringUserId_는 UI/흐름에서 미리 지정)
	if (registeringUserId_ < 0) {
		registeringUserId_ = nextSequentialId();
	}


	// 갤러리 업데이트 락
	{
		QMutexLocker lk(&embMutex_);
		auto it = std::find_if(gallery_.begin(), gallery_.end(),
				[&] (const UserEmbedding& u) {
					return u.id == registeringUserId_;
				});


		if (it == gallery_.end()) {
			UserEmbedding ue;
			ue.id = registeringUserId_;
			ue.name = registeringUserName_;
			ue.embedding = std::move(meanEmb);
			ue.proto = cv::Mat(1, int(ue.embedding.size()), CV_32F, ue.embedding.data()).clone();
			gallery_.push_back(std::move(ue));
		}
		else {
			it->embedding = std::move(meanEmb);
			it->proto = cv::Mat(1, int(it->embedding.size()), CV_32F,
					it->embedding.data()).clone();
		}
	}

	// 4) 파일 저장
	if (!saveEmbeddingsToFile()) {
		qWarning() << "[FRS] embeddings.json save failed";
	}

	if (m_cancelReg.load(std::memory_order_relaxed)) {
		qDebug() << "[FRS] canceled while composing dataset";
		forceAbortRegistration();				
		emit registrationCompleted(false, QStringLiteral("취소됨/타임아웃"));
		return;
	}


	// 최종적으로 이미지 저장
	int i = 0;
	for (auto& entry : regImageBuffers_) {
		std::string filename = std::string(USER_FACES_DIR) + "face_" +
			std::to_string(registeringUserId_) + "_" +
			registeringUserName_.toStdString() + "_" +
			std::to_string(i + 1) + ".png";
		if (!imwrite(filename, entry)) {
			qDebug() << "이미지 저장 실패:" << QString::fromStdString(filename);
		}
		++i;
	}

	// 상태 초기화
	setRegisterRequested(false);						// FSM 등록 요청 스냅샵 비활성화
	isRegisteringAtomic.storeRelaxed(0);				// 등록 플래그 비활성화로 전환
	captureCount = 0;									// 등록된 이미지수 초기화
	registeringUserName_.clear();						// 등록 중인 사용자 이름 초기화
	registeringUserId_ = -1;								// 등록 중인 사용자 아이디 초기화
	regEmbedsBuffers_.clear();							//  임베딩 임시버퍼 초기화
	regImageBuffers_.clear();								//  이미지 임시버퍼 초기화
	
	m_isAngleRegActive = false;
	m_recentEmbedsThisStep.clear();


	// UI에 등록 완료 제출
	emit registrationCompleted(true, QStringLiteral("등록 완료"));
}

void FaceRecognitionService::resetUnlockFlag()
{
	hasAlreadyUnlocked = false;
}

void FaceRecognitionService::fetchReset()
{
	if (!presenter) {
		qDebug() <<  "[FRS] presenter is nullptr!";
	}
	QMutexLocker locker(&frameMutex);
	qDebug() << "[FaceRecognitionService] fetchReset() called";

	QDir dir(USER_FACES_DIR);
	dir.removeRecursively();

	QFile::remove(embeddingsPath_);

	gallery_.clear();
	registeringUserId_ = -1;
	registeringUserName_.clear();
	rebuildNextIdFromGallery();

	authManager.resetAuth();
	hasAlreadyUnlocked = false;	

	presenter->presentReset();
}

void FaceRecognitionService::drawTransparentBox(Mat& img, Rect rect, Scalar color, double alpha = 0.4)
{
	Mat overlay;
	img.copyTo(overlay);
	rectangle(overlay, rect, color, FILLED);
	addWeighted(overlay, alpha, img, 1 - alpha, 0, img);
}

void FaceRecognitionService::drawCornerBox(Mat& img, Rect rect, Scalar color, int thickness = 2, int length = 20)
{
	int x = rect.x, y = rect.y, w = rect.width, h = rect.height;

	line(img, {x, y}, {x + length, y}, color, thickness);
	line(img, {x, y}, {x, y + length}, color, thickness);

	line(img, {x + w, y}, {x + w - length, y}, color, thickness);
	line(img, {x + w, y}, {x + w, y + length}, color, thickness);

	line(img, {x, y + h}, {x + length, y + h}, color, thickness);
	line(img, {x, y + h}, {x, y + h - length}, color, thickness);

	line(img, {x + w, y + h}, {x + w - length, y + h}, color, thickness);
	line(img, {x + w, y + h}, {x + w, y + h - length}, color, thickness);
}

static inline const std::vector<float>& getVec(const UserEmbedding& u)
{
	return u.embedding;
}

static inline const QString& getName(const UserEmbedding& u)
{
	return u.name;
}

recogResult_t FaceRecognitionService::handleRecognition(cv::Mat& frame,
        const cv::Rect& face,
        const cv::Mat& alignedFace,
        QString& labelText,
        cv::Scalar& boxColor)
{
    // ===== 0) 기본 세팅 =====
    recogResult_t rv{};
    rv.name = "Unknown";
    rv.sim  = -1.f;
    rv.idx  = -1;
    rv.result = AUTH_FAILED;

    if (!dnnEmbedder_ || alignedFace.empty()) {
        labelText = "Unknown";
        boxColor  = cv::Scalar(0,0,255);
        return rv;
    }

	// 등록된 유저가 없을 때
    const int numUsers = (int)gallery_.size();
    if (numUsers <= 0) {
        labelText = "Unknown";
        boxColor  = cv::Scalar(0,0,255);
        // 시각화
        drawTransparentBox(frame, face, boxColor, 0.3);
        drawCornerBox(frame, face, boxColor, 2, 25);
        putText(frame, labelText.toStdString(), {face.x, face.y - 10},
                cv::FONT_HERSHEY_DUPLEX, 0.9, boxColor, 2);
        return rv;
    }

	// ==== Top-1 maching ====
	MatchResult r = matcher_->bestMatch(alignedFace, gallery_);
	if (r.sim >= params_.recogEnter) {
		rv.idx  = r.id;
		rv.name = gallery_[r.id].name;
		rv.sim  = r.sim;
		rv.result = AUTH_SUCCESSED;
		boxColor  = cv::Scalar(0,255,0);
        labelText = QString("%1  cos=%2").arg(rv.name).arg(QString::number(rv.sim,'f',3));
	} else if (r.sim > 0) {
		rv.idx  = -1;
		rv.sim = r.sim;
        rv.name = "Unknown";
        rv.result = AUTH_FAILED;
		boxColor  = cv::Scalar(0,0,255);
        labelText = QString("Unknown  cos=%1").arg(QString::number(rv.sim,'f',3));
    } else {
		rv.idx  = -1;
        rv.name = "Unknown";
        rv.result = AUTH_FAILED;
		boxColor  = cv::Scalar(0,0,255);
        labelText = QString("Unknown");
	}

    // ===== 7) 시각화 =====
    drawTransparentBox(frame, face, boxColor, 0.3);
    drawCornerBox(frame, face, boxColor, 2, 25);
    putText(frame, labelText.toStdString(), {face.x, face.y - 10},
            cv::FONT_HERSHEY_DUPLEX, 0.9, boxColor, 2);

    if (!alignedFace.empty()) {
        cv::Mat thumb;
        cv::resize(alignedFace, thumb, {96,96});
        cv::rotate(thumb, thumb, cv::ROTATE_180); // 필요시 사용
        cv::rectangle(thumb, {0,0,thumb.cols-1,thumb.rows-1}, boxColor, 2);
        overlayThumbnail(frame, thumb, Corner::BottomLeft, 10);
    }

    return rv;
}





// ArcFace 112x112 템플릿 (출력은 128로 warp)
static const std::array<cv::Point2f, 5> kDst5_112 = {{
	{38.2946f, 51.6963f}, {73.5318f, 51.5014f},
		{56.0252f, 71.7366f}, {41.5493f, 92.3655f},
		{70.7299f, 92.2041f}
}};


// 유틸: YuNet 결과 → Rect 벡터
static std::vector<FaceDet> parseYuNet(const cv::Mat& dets, float scoreThresh=0.6f)
{
	std::vector<FaceDet> out;
	if (dets.empty() || dets.cols < 15) return out;

	for (int i = 0; i < dets.rows; ++i) {
		const float x  = dets.at<float>(i, 0);
		const float y  = dets.at<float>(i, 1);
		const float w  = dets.at<float>(i, 2);
		const float h  = dets.at<float>(i, 3);

		// ★ score는 맨 끝(14)
		const float score = dets.at<float>(i, 14);
		if (score < scoreThresh) continue;

		FaceDet f;
		f.box   = cv::Rect(cv::Point2f(x, y), cv::Size2f(w, h));
		f.score = score;

		// ★ 랜드마크는 4~13 (픽셀 좌표, 정규화 아님)
		f.lmk[0] = cv::Point2f(dets.at<float>(i, 4),  dets.at<float>(i, 5));   // left eye
		f.lmk[1] = cv::Point2f(dets.at<float>(i, 6),  dets.at<float>(i, 7));   // right eye
		f.lmk[2] = cv::Point2f(dets.at<float>(i, 8),  dets.at<float>(i, 9));   // nose
		f.lmk[3] = cv::Point2f(dets.at<float>(i,10),  dets.at<float>(i,11));   // mouth left
		f.lmk[4] = cv::Point2f(dets.at<float>(i,12),  dets.at<float>(i,13));   // mouth right

		out.push_back(f);
	}
	return out;
}

// === 폴백용 레터박스: 비율 유지 + 패딩 후 정사각 리사이즈 ===
static inline cv::Mat letterboxSquare(const cv::Mat& src, int out=128) {
	if (src.empty()) return src;
	int w = src.cols, h = src.rows, side = std::max(w, h);
	int top=(side-h)/2, bottom=side-h-top, left=(side-w)/2, right=side-w-left;
	cv::Mat pad;
	cv::copyMakeBorder(src, pad, top,bottom,left,right,
			cv::BORDER_CONSTANT, cv::Scalar(127,127,127));
	cv::Mat outImg; cv::resize(pad, outImg, {out,out}, 0,0, cv::INTER_LINEAR);
	return outImg;
}

// 112x112 ArcFace 템플릿을 outSize에 맞게 스케일
static inline std::array<cv::Point2f,5>
scaleDst112(const cv::Size& outSize) {
	const float sx = outSize.width  / 112.0f;
	const float sy = outSize.height / 112.0f;
	std::array<cv::Point2f,5> d = kDst5_112;
	for (auto& p : d) { p.x *= sx; p.y *= sy; }
	return d;
}

// (중요) 입력 랜드마크를 좌/우 눈, 코, 입 좌/우 순서로 강제정렬
static inline std::array<cv::Point2f,5>
normalizeSrc5(const std::array<cv::Point2f,5>& in) {
	// in이 어떤 순서로 오든, x좌표로 왼/오를 판단
	// 대략적인 휴리스틱: y가 가장 위 2개 = 눈, 가장 아래 2개 = 입, 남은 1개 = 코
	// (YuNet은 보통 [LE, RE, Nose, LM, RM] 이지만 안전하게 재정렬)
	std::vector<cv::Point2f> v(in.begin(), in.end());

	// y(위->아래) 정렬
	std::sort(v.begin(), v.end(), [](auto& a, auto& b){ return a.y < b.y; });

	// 후보군 분할
	std::vector<cv::Point2f> eyes   = { v[0], v[1] };          // 위의 두 점
	std::vector<cv::Point2f> mouth  = { v[3], v[4] };          // 아래의 두 점
	cv::Point2f nose                =  v[2];                   // 가운데 점

	// 왼/오 구분(x)
	auto Leye  = (eyes[0].x < eyes[1].x) ? eyes[0] : eyes[1];
	auto Reye  = (eyes[0].x < eyes[1].x) ? eyes[1] : eyes[0];
	auto Lm    = (mouth[0].x < mouth[1].x)? mouth[0]: mouth[1];
	auto Rm    = (mouth[0].x < mouth[1].x)? mouth[1]: mouth[0];

	return { Leye, Reye, nose, Lm, Rm };
}

// ArcFace 112x112 템플릿 (kDst5_112) 그대로 사용
// YuNet landmark order: [LE, RE, Nose, LM, RM]
cv::Mat FaceRecognitionService::alignBy5pts(const cv::Mat& srcBgr, const std::array<cv::Point2f,5>& src5_in, const cv::Size& outSize)
{
	return aligner_.alignBy5pts(srcBgr, src5_in, outSize);
}

bool FaceRecognitionService::startDirectCapture(int cam)
{
	stopDirectCapture();

	if (!cap_.open(cam, cv::CAP_V4L2)) {
		qWarning() << "[startDirectCapture] open failed cam=" << cam;
		return false;
	}

	// 저지연 튜닝
	cap_.set(cv::CAP_PROP_BUFFERSIZE, 1);
	cap_.set(cv::CAP_PROP_FRAME_WIDTH, 640);
	cap_.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
	cap_.set(cv::CAP_PROP_FPS, 30);
	//cap_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G'));
	cap_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('Y','U','Y','V'));

	running_.storeRelease(1);

	capThread_ = QThread::create([this] {
			this->loopDirect();
	});
	capThread_->setObjectName(QStringLiteral("DirectCapture"));
	QObject::connect(capThread_, &QThread::finished, capThread_, &QObject::deleteLater);
	capThread_->start(QThread::TimeCriticalPriority);

	qInfo() << "[startDirectCapture] started 640x480@30, buffersize=1";
	return true;
}

void FaceRecognitionService::camRestart()
{
	QString msg;
	qInfo() << "[camRestart] Restaring camera thread...";

	stopDirectCapture();

	QThread::msleep(200);

	if (startDirectCapture(-1)) {
		msg = QStringLiteral("카메라 재시작 성공.");
		presenter->presentCamRestart(msg);
		return;
	}
	else {
		SystemLogger::error("CAMERA", "Failed to camera restart.");	
		msg = QStringLiteral("카메라 재시작 실패");
		presenter->presentCamRestart(msg);
		return;
	}
}

void FaceRecognitionService::stopDirectCapture()
{
	if (running_.fetchAndStoreOrdered(0) == 1) {
		// loopDirect가 정상적으로 탈출하도록 조금 기다렸다가
	}

	g_unlockMgr.stop();
	//g_uls.stop();

	// 스레드 종료 대기/정리
	if (capThread_) {
		capThread_->wait();		// returning wait loopDirect()  
		capThread_ = nullptr;	// finished 연결로 deleteLater 호출됨
	}

	// 카메라 해제
	if (cap_.isOpened()) {
		cap_.release();
	}
	currentState = RecognitionState::IDLE;
}

void FaceRecognitionService::showOpenImage()
{
	const QString openImgPath = QStringLiteral(IMAGES_PATH) + QStringLiteral(OPEN_IMAGE);
	//qDebug() << "[showOpenImage] open image path: " << openImgPath << ", exists=" << QFile::exists(openImgPath);
	QImage openImg;
	if (!openImg.load(openImgPath) || openImg.isNull()) {
		QImage fallback(640, 480, QImage::Format_BGR888);
		fallback.fill(Qt::black);
		emit frameReady(fallback.copy());
	} else {
		emit frameReady(openImg.convertToFormat(QImage::Format_BGR888).copy());
	}
}

void FaceRecognitionService::showFarImage(Mat& frame)
{
	QFont font("Malgun Gothic", 24);
	QFontMetrics fm(font);
	int textWidth = fm.horizontalAdvance(QStringLiteral("가까이 다가와주세요."));
	int textHeight = fm.height();
	int x = 10;
	int y = frame.rows - 10;


	drawKoreanTextOnMat(frame,
							QStringLiteral("얼굴을 감지되지 않았습니다"),
							Anchor::BottomLeft,
							14,
							std::max(18, int(frame.rows * 0.06)),
							QColor(255, 0, 0));

}

static inline void tryIncStreakCooldown(int userIdx, std::function<void()> incAuthStreak)
{
    static qint64 s_lastIncMs   = 0;
    static int    s_lastUserIdx = -1;
    const int kIncCooldownMs = 120; // 최소 간격(ms)

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool sameUser = (s_lastUserIdx == -1) || (s_lastUserIdx == userIdx);

    if (sameUser && (now - s_lastIncMs >= kIncCooldownMs)) {
        incAuthStreak();                       // 네 기존 함수 호출
        s_lastIncMs   = now;
        s_lastUserIdx = userIdx;
        qInfo() << "[FSM] streak++ user=" << userIdx;
    }
}

// 리드 센서의 값과 Fsm 스냅샵 동기화
void FaceRecognitionService::syncDoorOpenedFromReed()
{
    // 센서 해석: 닫힘= true -> 문열림 = false
    const bool sensorClosed = g_reed.isClosed();
    const bool opened = !sensorClosed;

    // (옵션) 40ms 디바운스
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastReedEdgeMs_ < 40 && opened != doorOpened_) {
        // 너무 빠른 변동은 무시
        return;
    }
    if (opened != doorOpened_) {
        lastReedEdgeMs_ = now;
        setDoorOpened(opened);  // ★ 오직 여기서만 doorOpened_ 변경
        qDebug() << "[syncDoorOpenedFromReed] reed=" << (sensorClosed ? "CLOSED" : "OPEN")
                 << " -> doorOpened_=" << opened;
    }
}

// 프레임 좌상단에 R/G/B 프로브를 그려 파이프라인을 확정한다.
static void drawColorProbe(cv::Mat& frame)
{
    // 크기와 위치는 자유 조절
    int s = 30; // 정사각형 변
    cv::rectangle(frame, {5, 5, s, s},     cv::Scalar(0, 0, 255),  cv::FILLED); // BGR에서 '빨강'
    cv::rectangle(frame, {10+s, 5, s, s},  cv::Scalar(0, 255, 0),  cv::FILLED); // BGR에서 '초록'
    cv::rectangle(frame, {15+2*s, 5, s, s},cv::Scalar(255, 0, 0),  cv::FILLED); // BGR에서 '파랑'
}

QString FaceRecognitionService::nameFromId(int userId)
{
	QString name;
	if (userId < 0) 
	{
		name = QStringLiteral("Unkown");
		return name;
	}
	
	if (gallery_.size() <= 0) {
		name = QStringLiteral("Unkown");
		return name;
	}

	name = gallery_[userId].name;	
	return name;
}

void FaceRecognitionService::loopDirect()
{
	cv::Mat frame, frameCopy;
	DetectedStatus dState;
	recogResult_t recogResult;
	while (running_.loadAcquire() == 1) {
		const bool wantReg = (isRegisteringAtomic.loadRelaxed() != 0);
		bool acceptedThisFrame = false;

		//syncDoorOpenedFromReed();
		setDoorOpened(false);	

		if(!cap_.read(frame) || frame.empty()) {
			QThread::msleep(2);
			continue;
		}
		else { 
			// make snapshot image for android app webcam
			frameCopy = frame.clone();
			//imwrite("/tmp/snap.jpg", frameCopy);
		}

		// Find CV Frame Color order
		// drawColorProbe(frame);
		// cv::imshow("test", frame);

		// loopDirect() 안에서, frame 읽은 직후쯤
		// const qint64 now = QDateTime::currentMSecsSinceEpoch();

		// "문 열림 화면을 보여줄지"는 두 조건의 OR
		// 2) 실제 리드센서가 열림 (!g_reed.isClosed())
		int readDoorState = g_reed.isClosed();

		/*
		   if (!readDoorState) {
		   showOpenImage(); // 내부에서 emit frameReady(...)
		   emit doorStateChanged(States::DoorState::Open);
		   presenter->onDoorAuth(false, recogResult.idx, recogResult.sim, 1000);

		   {
		   QMutexLocker lk(&snapMu_);
		   resetFailCount();					
		   authManager.resetAuth();
		   resetUnlockFlag();
		   setAllowEntry(true);
		   }


		   QThread::msleep(1);
		   continue; // 오버레이 중엔 일반 파이프라인 잠시 멈춤
		   }
		   else {
		   emit doorStateChanged(States::DoorState::Locked);
		   }
		 */

		showFarImage(frame);
		double dist = g_uls.latestDist();
		if (dist > 50.0) {
			showFarImage(frame);
			//continue;
		}
		// qDebug() << "[loopframRDirect] dist: " << dist;

		if (frame.empty()) {
			qWarning() << "[loopDirect] frame is empty after consume()";
			continue;
		}

		// ── 3) trivial 체크 ──
		/*
		if (dnnEmbedder_ && dnnEmbedder_->isTrivialFrame(frame, 30.0, 10.0)) {
			qWarning() << "[loopDirect] Trivial frame. skip";
			emit frameReady(QImage());
			continue;
		}
		*/

		constexpr int authCooldownMs = 4000;
		if (authCooldown.isValid() && authCooldown.elapsed() < authCooldownMs) {
			{
				QMutexLocker lk(&snapMu_);
				resetFailCount();
				resetAuthStreak();
				authManager.resetAuth();
				resetUnlockFlag();
				setAllowEntry(false);
				setRegisterRequested(false);

				setFacePresent(false);
				setDetectScore(0.0);
				setRecogConfidence(0.0);
				setLivenessOk(true);
				setAllowEntry(acceptedThisFrame);
				setDoorSensorOpen(!g_reed.isClosed());
			}
			printFrame(frame, DetectedStatus::AuthSuccessed);
			continue;
		}

		constexpr int failCooldownMs = 4000;
		if (failCooldown.isValid() && failCooldown.elapsed() < failCooldownMs) {
			{
				QMutexLocker lk(&snapMu_);
				resetFailCount();
				resetAuthStreak();
				authManager.resetAuth();
				resetUnlockFlag();
				setAllowEntry(false);
				setRegisterRequested(false);

				setFacePresent(false);
				setDetectScore(0.0);
				setRecogConfidence(0.0);
				setLivenessOk(true);
				setAllowEntry(acceptedThisFrame);
				setDoorSensorOpen(!g_reed.isClosed());
			}
			printFrame(frame, DetectedStatus::AuthFailed);
			continue;
		}

		// ── 4) 얼굴 검출 ──
		std::vector<FaceDet> faces;
		if (auto best = detectBestYuNet(frame)) {
			faces.push_back(*best);
		}
		else {
			printFrame(frame, DetectedStatus::FaceNotDetected); 
			continue;
		}

		// FSM 상태 초기화
		{
			QMutexLocker lk(&snapMu_);
			setFacePresent(!faces.empty());
			setRegisterRequested(wantReg);
			setLivenessOk(true);
			setDuplicate(false);
		}

		double maxDetect = 0.0;
		// ── 5) 얼굴별 처리 ──
		for (const auto& fd : faces) {
			maxDetect = std::max(maxDetect, static_cast<double>(fd.score));
			setDetectScore(maxDetect);
			//qDebug() << "[loopDirect] maxDetect:" << maxDetect;

			cv::Mat aligned = alignBy5pts(frame, fd.lmk, cv::Size(128,128));
			if (aligned.empty()) {
				cv::Rect roi = expandRect(fd.box, 1.3f, frame.size());
				if (roi.area() > 0) {
					cv::Mat crop = frame(roi).clone();
					if (!crop.empty()) aligned = letterboxSquare(crop, 128);
				}
			}
			if (aligned.empty()) continue;

			QString label;
			cv::Scalar color;


			if (!wantReg) {
				// 품질 체크
				dState = liveness_.passQualityForRecog(fd.box, frame);
				if (dState != DetectedStatus::FaceDetected) {
					printFrame(frame, dState);
					{
						QMutexLocker lk(&snapMu_);

						resetFailCount();
						resetAuthStreak();
						authManager.resetAuth();
						resetUnlockFlag();
						setAllowEntry(false);
					}

					continue;
				}


				// 인식 처리
				recogResult = handleRecognition(frame, fd.box, aligned, label, color);

				const int nUsers  = std::max(0, (int)gallery_.size());

				if (recogResult.result == AUTH_SUCCESSED) {
					acceptedThisFrame = true;
					dState = DetectedStatus::FaceDetected;
				}
				else if (recogResult.result == AUTH_FAILED) {
					acceptedThisFrame = false;
				}

				if (!acceptedThisFrame) {
					{
						QMutexLocker lk(&snapMu_);
						setAllowEntry(false);
						incFailCount();
						authManager.handleAuthFailure();
						qDebug() << "[loopDirect] failCount:" << failCount_;
					}
				} else {
					{
						QMutexLocker lk(&snapMu_);
						resetFailCount();
						tryIncStreakCooldown(recogResult.idx, [&]() { incAuthStreak(); });
						//incAuthStreak();
					}

					authManager.handleAuthSuccess(authStreak_);
					if (!hasAlreadyUnlocked && authManager.shouldAllowEntry(recogResult.name)) {
						setAllowEntry(true);
						authCooldown.restart();
						if	(!g_unlockMgr.running()) {
							g_unlockMgr.start(); qInfo() << "[loopDirect] Unlock started (wait open, then wait close)";
							}
						qDebug() << "hasAlreadyUnlocked: " << (int)hasAlreadyUnlocked;

						// DB 로그 저장(JPEG → BLOB)
						std::vector<uchar> buf;
						cv::imencode(".jpg", frameCopy, buf);
						QByteArray blob(reinterpret_cast<const char*>(buf.data()),
								static_cast<int>(buf.size()));

						db->insertAuthLog(recogResult.name, QStringLiteral("인식 성공"),
								QDateTime::currentDateTime(), blob);

						hasAlreadyUnlocked = true;	
					}
				}
				// FSM 
				{
					QMutexLocker lk(&snapMu_);


					// AuthStreak 처리
					if (authStreak_ >= params_.authThresh) {
						presenter->onDoorAuth(true, recogResult.idx, recogResult.sim, 1000);
						resetAuthStreak();
						authManager.resetAuth();
						resetUnlockFlag();
						setAllowEntry(false);
						setRecogConfidence(0.0);
					}

					// FailCount 처리
					if (failCount_ >= params_.lockoutFails) {
						presenter->onDoorAuth(false, recogResult.idx, recogResult.sim, 1000);

						// DB 로그 저장(JPEG → BLOB)
						std::vector<uchar> buf;
						cv::imencode(".jpg", frameCopy, buf);
						QByteArray blob(reinterpret_cast<const char*>(buf.data()),
								static_cast<int>(buf.size()));

						db->insertAuthLog(recogResult.name, QStringLiteral("인식 실패"),
								QDateTime::currentDateTime(), blob);

						resetFailCount();					
						authManager.resetAuth();
						resetUnlockFlag();
						setAllowEntry(false);
						setRecogConfidence(0.0);

						failCooldown.restart();
					}

					setFacePresent(!faces.empty());
					setDetectScore(maxDetect);
					setRecogConfidence(recogResult.sim);
					setLivenessOk(true);
					setAllowEntry(acceptedThisFrame);
					setDoorSensorOpen(!g_reed.isClosed());
				}
				printFrame(frame, dState);
			}
			else {
				// 등록 모드
				dState = handleRegistration(frame, fd.box, aligned, label, color);

				{
					QMutexLocker lk(&snapMu_);
					resetFailCount();
					resetAuthStreak();
					authManager.resetAuth();
					resetUnlockFlag();
					setAllowEntry(false);
					setRegisterRequested(true);

					setFacePresent(!faces.empty());
					setDetectScore(maxDetect);
					setRecogConfidence(0.0);
					setLivenessOk(true);
					setAllowEntry(acceptedThisFrame);
					setDoorSensorOpen(!g_reed.isClosed());
				}

				printFrame(frame, dState);
			}

			if (faces.empty()) setRecogConfidence(0.0);


			// CPU 과점유 방지 (필요 시 0~2ms)
			QThread::msleep(1);
		}
	}
}

void FaceRecognitionService::printFrame(cv::Mat &frame, DetectedStatus hasBase)
{
    // 공통 드로잉 헬퍼: 매번 같은 파라미터(앵커/폰트/배경)를 반복하지 않도록
    auto draw = [&](const QString& msg,
                    const QColor fg = QColor(255,255,255),
                    const QColor bg = QColor(0,0,0,180),
                    int fontPx = 28) {
        drawKoreanTextOnMat(frame,
                            msg,
                            Anchor::TopCenter,
                            /*topMarginPx*/ 40,
                            fontPx,
                            fg,
                            bg,
                            /*withShadowOrRound*/ true);
    };

    switch (hasBase) {
    case DetectedStatus::FaceNotDetected: {
        // 오탈자 수정: "얼굴이 감지되지 않았습니다"
        drawKoreanTextOnMat(frame,
                            QStringLiteral("얼굴이 감지되지 않았습니다"),
                            Anchor::TopCenter,
                            /*topMarginPx*/ 14,
                            std::max(18, int(frame.rows * 0.06)),
                            QColor(255, 0, 0));
        break;
    }
    case DetectedStatus::FaceDetected: {
        draw(QStringLiteral("얼굴 인식 중입니다."));
        break;
    }
    case DetectedStatus::CenterOff: {
        draw(QStringLiteral("얼굴을 화면 중앙에 맞춰주세요."));
        break;
    }
    case DetectedStatus::TooSmall: {
        draw(QStringLiteral("조금 더 가까이 와주세요."));
        break;
    }
    case DetectedStatus::TooBlurry: {
        draw(QStringLiteral("화면이 흔들려요!"));
        break;
    }
    case DetectedStatus::TooDark: {
        draw(QStringLiteral("조명이 어두워요!"));
        break;
    }
    case DetectedStatus::LowContrast: {
        draw(QStringLiteral("얼굴 중앙이 흐릿해요."));
        break;
    }
    case DetectedStatus::Registering: {
        draw(QStringLiteral("등록 중입니다."), QColor(0,0,255));
        break;
    }
    case DetectedStatus::PoseFront: {
        draw(QStringLiteral("정면을 봐주세요."), QColor(0,0,255));
        break;
    }
    case DetectedStatus::PoseYawLeft: {
        draw(QStringLiteral("머리를 약간 왼쪽으로 돌려주세요."), QColor(0,0,255));
        break;
    }
    case DetectedStatus::PoseYawRight: {
        draw(QStringLiteral("머리를 약간 오른쪽으로 돌려주세요."), QColor(0,0,255));
        break;
    }
    case DetectedStatus::PosePitchUp: {
        draw(QStringLiteral("턱을 약간 올려주세요."), QColor(0,0,255));
        break;
    }
    case DetectedStatus::PosePitchDown: {
        draw(QStringLiteral("턱을 약간 내려주세요."), QColor(0,0,255));
        break;
    }
    case DetectedStatus::FailedRegistration: {
        draw(QStringLiteral("등록을 실패했습니다."), QColor(0,0,255));
        break;
    }
    case DetectedStatus::TimeoutRegistration: {
        draw(QStringLiteral("등록 타임아웃."), QColor(0,0,255));
        break;
    }
    case DetectedStatus::RetryRegistration: {
        draw(QStringLiteral("조금만 다르게 움직여주세요."), QColor(0,0,255));
        break;
    }
    case DetectedStatus::CompleteRegistration: {
        draw(QStringLiteral("등록이 완료됐습니다."), QColor(0,0,255));
        break;
    }
    case DetectedStatus::DuplicateFace: {
        draw(QStringLiteral("중복된 얼굴입니다."), QColor(0,0,255));
        break;
    }
	case DetectedStatus::AuthSuccessed: {
		draw(QStringLiteral("인증 완료 (쿨타임 4초)."), QColor(0, 255, 0));
		break;
	}
	case DetectedStatus::AuthFailed: {
		draw(QStringLiteral("인증 실패 (쿨타임 4초)."), QColor(255, 0, 0));
		break;
	}
    }

    // == 아래는 함수 스코프 안에 있어야 함 ==
    if (frame.cols != 640 || frame.rows != 480) {
        cv::resize(frame, frame, cv::Size(640, 480), 0, 0, cv::INTER_AREA);
    }

    cv::Mat rgb;
    cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
    QImage qimg(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
    emit frameReady(qimg.copy());
}

// === 스냅샷 API ===
void FaceRecognitionService::setDetectScore(double v)				{ detectScore_ = v; facePresent_ = (v > 0.0); }
void FaceRecognitionService::setRecogConfidence(double v)		    { recogConf_ = v; }
void FaceRecognitionService::setFacePresent(bool v)					{ facePresent_ = v; }
void FaceRecognitionService::setDuplicate(bool v)					{ isDup_ = v; }
void FaceRecognitionService::setRegisterRequested(bool v)           { regReq_ = v; }
void FaceRecognitionService::setLivenessOk(bool v)					{ livenessOk_ = v; }
void FaceRecognitionService::setDoorOpened(bool v)					{ doorOpened_ = v; }
void FaceRecognitionService::incFailCount()							{ failCount_++; }
void FaceRecognitionService::incAuthStreak()						{ authStreak_++; }
void FaceRecognitionService::setAllowEntry(bool v)					{ allowEntry_ = v; }
void FaceRecognitionService::resetFailCount()						{ failCount_ = 0; }
void FaceRecognitionService::resetAuthStreak()						{ authStreak_ = 0;	}
void FaceRecognitionService::setDoorCommandOpen(bool v) 
{
	if (v) {
		g_unlockMgr.start();
	}
}
void FaceRecognitionService::setDoorSensorOpen(bool v) { doorOpened_ = v; }

void FaceRecognitionService::onTick() 
{
	FsmContext c;
	{
		QMutexLocker lk(&snapMu_);
		// 1) 프레임 신호 (Service 단일 신호)
		c.facePresent					= facePresent_;		// 검출 루프에서 setFacePresent
		c.detectScore					= detectScore_;		// 0.0~1.0 (가드는 >= 0.8 비교)
		c.recogConfidence			    = recogConf_;		// -1~1 스냅샷(재판정 금지)
		c.livenessOk					= livenessOk_;		// 라이브니스 최신 결과
		c.doorOpened					= doorOpened_;		// 리드센서 상태만 (명령 아님)

		// 2) 정책/세션 신호
		c.allowEntry					= allowEntry_;		// 임계/투표 결과
		c.authStreak					= authStreak_;		// 쿨다운 + 동일인으로 증가
		c.failCount						= failCount_;		// REJECT/VOTE 실패에서 증가

		// 3) 등록/세션 신호
		c.isDuplicate					= isDup_;
		c.registerRequested		        = regReq_;
	}
	c.nowMs							= monotonic_.elapsed();

	// 4) 상태별 타임아웃
	// - RECOGNIZING 상태에서 5초 초과 시 timeout = true
	// - LOCKED_OUT에서는 lockoutMs 초과 시 timeout = true
	c.timeout						= computeTimeout(c);

	// 5) 시퀀스 번호
	c.seq							= ++seq_;

	qCDebug(LC_FSM_CTX) << "[CTX]"
	//qDebug() << "[CTX]"
		<< "seq=" << c.seq
		<< "face=" << c.facePresent
		<< "detectScore=" << c.detectScore
		<< "conf=" << c.recogConfidence
		<< "live=" << c.livenessOk
		<< "allow=" << c.allowEntry
		<< "streak=" << c.authStreak
		<< "reedOpen=" << c.doorOpened
		<< "timeout=" << c.timeout
		<< "recogSim=" << c.recogConfidence
		<< "authStreak=" << c.authStreak
		<< "failCount=" << c.failCount;


	fsm_.updateContext(c);
}

bool FaceRecognitionService::computeTimeout(const FsmContext& c) 
{
	auto st = fsm_.current();
	const qint64 elapsed = stateTimer_.elapsed();

	// 상태 바뀔 때마다 재시작
	if (prevState_ != st) {
		prevState_ = st;
		stateTimer_.restart();
		return false;
	}
/*
	if (st == RecognitionState::RECOGNIZING) {
		return elapsed > params_.recogTimeoutMs;
	}
	if (st == RecognitionState::LOCKED_OUT) {
		return elapsed > params_.lockoutMs;
	}
*/

	switch (st) {
		case RecognitionState::RECOGNIZING:
			return elapsed > params_.recogTimeoutMs;
		case RecognitionState::AUTH_SUCCESS:
			return elapsed > params_.successHoldMs;
		case RecognitionState::LOCKED_OUT:
			return elapsed > params_.lockoutMs;
		default:
			return false;
    }

	return false;
}

void FaceRecognitionService::requestedDoorOpen()
{
	QString msg;
	if (!g_reed.isClosed()) {		
		qDebug() << "[requestedDoorOpen] already door was opend!";	
		msg = QStringLiteral("이미 문이 열려 있습니다.");
		presenter->presentDoorOpen(msg);
		return;
	}


	if (!g_door.isReady()) {
		qDebug() << "[requestedDoorOpen] g_door is nullptr";
		msg = QStringLiteral("문 열기에 실패 했습니다."); 
		presenter->presentDoorOpen(msg);
		return;
	}		

	g_door.setUnlocked(true);
	qDebug() << "[requestedDoorOpen] door was opend!";	
	msg = QStringLiteral("문이 열렸습니다."); presenter->presentDoorOpen(msg); QThread::msleep(1000);
	//QThread::msleep(2000);
	//g_door.setUnlocked(false);
}

void FaceRecognitionService::requestedDoorClose()
{
	QString msg;
	// 테스트로 항상 true이기 때문에 리드 스위치 결합하면 주석풀기   
	/*
	   if (g_reed.isClosed()) {
	   qDebug() << "[requestedDoorClose] already door was close!";
	   msg = QStringLiteral("이미 문이 닫혀 있습니다.");
	   presenter->presentDoorClose(msg);
	   return;
	   }
	 */

	if (!g_door.isReady()) {
		qDebug() << "[requestedDoorClose] g_door si nullptr";
		msg = QStringLiteral("문 닫기에 실패했습니다.");
		presenter->presentDoorClose(msg);
		return;

	}
	/*
	// =====  리드 스위치 감지 전까지 열림 유지 시작 =====
	if (!g_unlockMgr.running()) {
	g_unlockMgr.start();
	qInfo() << "[Door] Unlock started (wait open, then wait close)";
	}
	 */
	qDebug() << "[requestedDoorClose] door was closed!";	
	msg = QStringLiteral("문이 닫혔습니다.");
	g_door.setUnlocked(false);
	presenter->presentDoorClose(msg);
	//QThread::msleep(2000);
	//g_door.setUnlocked(true);
}

int FaceRecognitionService::staticDoorStateChange(bool state)
{
	g_door.setUnlocked(state);
	if (state) {
		emit doorStateChanged(States::DoorState::Open);	
	}
	else {
		emit doorStateChanged(States::DoorState::Locked);	
	}
	return 0;
}

void FaceRecognitionService::requestedRetrainRecog()
{
	QString msg;
	if (embeddingsPath_.isEmpty()) {
		msg = QStringLiteral("인식기 모델 경로를 알 수 없습니다.");
		presenter->presentRetrainRecog(msg);
		return;
	}


	if (!loadEmbeddingsFromFile()) {
		msg = QStringLiteral("인식기학습을 실패했습니다."); 
		presenter->presentRetrainRecog(msg);
		return;
	}
	else {
		msg = QStringLiteral("인식기가 학습되었습니다.");
		presenter->presentRetrainRecog(msg);
		return;
	}
}



