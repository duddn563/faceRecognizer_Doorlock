#include <QTimer>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QDateTime>
#include <QElapsedTimer>
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


// #define DEBUG 
// ===== 컨트롤러/매니저(정적) =====
static DoorlockController      g_door;
static ReedSensor			   g_reed;
static UltrasonicSensor		   g_uls;

static UnlockUntilReed::Opt    g_unlockOpt{/*pollMs*/50, /*hits*/6, /*openTimeoutMs*/5000, /*maxUnlockMs*/10000};
static UnlockUntilReed         g_unlockMgr(&g_door, &g_reed, g_unlockOpt);


namespace fs = std::filesystem;


// === helpers === 
struct Vote {
	int M;		// 원도우 크기
	int N;		// 합격에 필요한 횟수
};

class Voter {
	struct Item{ int idx; float s; };
	std::deque<Item> q_; Vote v_;
	public:
	explicit Voter(Vote v):v_(v){}
	bool feed(int idx, float s, float T_out){
		q_.push_back({idx,s});
		while((int)q_.size()>v_.M) q_.pop_front();
		int c=0; for(auto& it:q_) if(it.idx==idx && it.s>=T_out) ++c;
		return (c>=v_.N);
	}
};

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

bool FaceRecognitionService::passQualityForRecog(const cv::Rect& box, const cv::Mat& face)
{
	return liveness_.passQualityForRecog(box, face);
}

void FaceRecognitionService::init()
{
	qDebug() << "[init] Face Recognition Service initiallize!!";
	//openCamera();
	if (!this->initializeDnnOnly()) {
		SystemLogger::error("DNN", "DNN-only init failed (YuNet/MobileFaceNet)");
		return;
	}

	qDebug() << "[init] g_door or g_reed init!!";
	// ===== 여기 추가: 도어락/리드스위치 초기화 =====
	if (!g_door.init())  { qWarning() << "[init] Door init failed"; }
	if (!g_reed.init())  { qWarning() << "[init] Reed init failed"; }
	// g_unlockMgr는 위에서 g_door/g_reed 참조만 하므로 별도 init 불필요
	qDebug() << "[init] g_door or g_reed init complete!!";
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

	try {
		const int S = opt.inputSize;
		cv::Mat dummy(S, S, CV_8UC3, cv::Scalar(0,0,0));
		std::vector<float> emb;
		cv::Mat alignedBGR;
		cv::cvtColor(dummy, alignedBGR, cv::COLOR_RGB2BGR);
		if (!dnnEmbedder_->extract(alignedBGR, emb) || emb.empty()) {
			qWarning() << "[loadRecognizer] warmup extract failed (empty embedding)";
			dnnEmbedder_.reset();
			return false;
		}
		qDebug() << "[loadRecognizer] warmup ok. emb dim=" << int(emb.size());
	} catch (const cv::Exception& e) {
		qWarning() << "[loadRecognizer] warmup exception:" << e.what();
		dnnEmbedder_.reset();
		return false;
	}


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

MatchResult FaceRecognitionService::bestMatch(const std::vector<float>& emb) const
{
	return FaceMatcher::bestMatch(emb, gallery_);
}

MatchTop2 FaceRecognitionService::bestMatchTop2(const std::vector<float>& emb) const
{
	return FaceMatcher::bestMatchTop2(emb, gallery_, true);
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
	MatchTop2 m = bestMatchTop2(emb);

	Decision d = decision_.decide(m);
	if (d == Decision::Accept || d == Decision::StrongAccept) {
		if (dupId && m.bestIdx >= 0 && m.bestIdx < (int)gallery_.size()) 
			*dupId = gallery_[m.bestIdx].id;
		if (simOut) *simOut = m.bestSim;
		qDebug() << "[isDuplicateFaceDNN] matched id=" << (m.bestIdx >= 0 ? gallery_[m.bestIdx].id : -1)
			<< "sim=" << m.bestSim << "second=" << m.secondSim
			<< "decision=" << (int)d;
		return true;
	}
	return false;
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

// === 등록 메인 함수 ===
void FaceRecognitionService::handleRegistration(Mat& frame, const Rect& face, const Mat& alignedFace, QString& labelText, Scalar& boxColor)
{
	qDebug() << "[FaceRecognitionService] handleRegistration()";

	// 취소 체크(초반 빠른 탈출)
	if (m_cancelReg.load(std::memory_order_relaxed)) {
		qDebug() << "[FaceRecognitionService] canceled (early in handleRegistration)";
		forceAbortRegistration();
		emit registrationCompleted(false, QStringLiteral("취소됨/타임아웃"));
		return;
	}

	// 중복 체크 시 DNN 사용
	if (captureCount ==  0) {
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
			return;
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
			return;
		}

		saveCapturedFace(face, alignedFace, colorFace); 
		captureCount++;

		if (captureCount >= 10) {
			if (m_cancelReg.load(std::memory_order_relaxed)) {
				qDebug() << "[FRS] canceled before finalize";
				forceAbortRegistration();
				emit registrationCompleted(false, QStringLiteral("취소됨/타임아웃"));
			}
			finalizeRegistration();
		}
	}
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
		cv::Mat alignedBGR;
		cv::cvtColor(alignedFace, alignedBGR, cv::COLOR_RGB2BGR);
		if (dnnEmbedder_->extract(alignedBGR, e) && !e.empty()) {
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
	printVector(meanEmb, "meanEmb(before norm)");

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
	isRegisteringAtomic.storeRelaxed(0);		// 등록 플래그 비활성화로 전환
	captureCount = 0;												// 등록된 이미지수 초기화
	registeringUserName_.clear();						// 등록 중인 사용자 이름 초기화
	registeringUserId_ = -1;									// 등록 중인 사용자 아이디 초기화
	regEmbedsBuffers_.clear();							//  임베딩 임시버퍼 초기화
	regImageBuffers_.clear();								//  이미지 임시버퍼 초기화


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
    rv.secondSim = -1.f;
    rv.idx  = -1;
    rv.result = AUTH_FAILED;

    if (!dnnEmbedder_ || alignedFace.empty()) {
        labelText = "Unknown";
        boxColor  = cv::Scalar(0,0,255);
        return rv;
    }
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

    // ===== 1) 임베딩 =====
    std::vector<float> emb;
	cv::Mat alignedBGR;
	cv::cvtColor(alignedFace, alignedBGR, cv::COLOR_RGB2BGR);
    if (!dnnEmbedder_->extract(alignedBGR, emb) || emb.empty()) {
        labelText = "Unknown";
        boxColor  = cv::Scalar(0,0,255);
        return rv;
    }

    // ===== 2) Top-2 매칭 =====
    auto t2 = bestMatchTop2(emb);
    const int    bestIdx   = t2.bestIdx;
    const double bestSim   = (std::isfinite(t2.bestSim)   ? t2.bestSim   : -1.0);
    const double secondSim = (std::isfinite(t2.secondSim) ? t2.secondSim : -1.0);
    const double gap       = (secondSim >= 0.0 ? (bestSim - secondSim) : -1.0);

    // (안전) 범위 클램프
    auto clamp1 = [](double v){ return std::max(-1.0, std::min(1.0, v)); };
    const double bestC   = clamp1(bestSim);
    const double secondC = clamp1(secondSim);

    // ===== 3) 기준값 (멀티-유저 “강한” 기준) =====
    // 2명 이상일 때, best는 충분히 크고(good), second와의 차이도 커야(distinct) 통과
    const double T_SINGLE_STRONG  = 0.985; // 갤러리 1명일 때만 사용
    const double T_MULTI_BEST     = 0.940; // 멀티 최소 best
    const double T_MULTI_GAP      = 0.060; // 멀티 최소 마진(차이)

    // ===== 4) 이름(후보) 준비 =====
    QString bestName = "Unknown";
    int     outIdx   = -1;
    if (bestIdx >= 0 && bestIdx < numUsers) {
        bestName = getName(gallery_[bestIdx]);
        outIdx   = gallery_[bestIdx].id;
    }

    // ===== 5) 통과 여부 판단 =====
    bool allowCandidate = false;
    if (numUsers <= 1) {
        // 싱글톤: 아주 높은 컷 이상에서만 후보 인정
        allowCandidate = (bestC >= T_SINGLE_STRONG);
    } else {
        // 멀티: best와 gap 동시 만족 (이 조건 못 넘기면 절대 SUCCESSED 안 됨)
        allowCandidate = (bestC >= T_MULTI_BEST && gap >= T_MULTI_GAP);
    }

    // ===== 6) 라벨/색상 정책 =====
    // 통과 못 하면 라벨은 무조건 Unknown 으로; 이름 노출 금지(혼동 방지)
    if (allowCandidate) {
        boxColor  = cv::Scalar(0,255,0);
        labelText = (numUsers <= 1)
            ? QString("%1  cos=%2").arg(bestName).arg(QString::number(bestC,'f',3))
            : QString("%1  cos=%2  gap=%3")
                .arg(bestName)
                .arg(QString::number(bestC,'f',3))
                .arg(QString::number(gap,'f',3));
        rv.name = bestName;
        rv.idx  = outIdx;
        rv.result = AUTH_SUCCESSED;
    } else {
        // 멀티 환경에서는 오탐 혼선을 막기 위해 이름 미노출
        // (참고 표시가 필요하면 “Unknown (cos=..., gap=...)” 정도만 남기세요)
        boxColor  = cv::Scalar(0,0,255);
        if (numUsers <= 1) {
            labelText = QString("Unknown  cos=%1").arg(QString::number(bestC,'f',3));
        } else {
            labelText = QString("Unknown  cos=%1  gap=%2")
                            .arg(QString::number(bestC,'f',3))
                            .arg((gap<0? "N/A" : QString::number(gap,'f',3)));
        }
        rv.name = "Unknown";
        rv.idx  = -1;
        rv.result = AUTH_FAILED;
    }

    rv.sim       = (float)bestC;
    rv.secondSim = (secondC >= 0.0 ? (float)secondC : -1.f);

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
        overlayThumbnail(frame, thumb, Corner::TopLeft, 10);
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
	cap_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G'));

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

	openOverlayActive_ = false;

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
		QImage fallback(640, 480, QImage::Format_RGB888);
		fallback.fill(Qt::black);
		emit frameReady(fallback.copy());
	} else {
		emit frameReady(openImg.convertToFormat(QImage::Format_RGB888).copy());
	}
}

void FaceRecognitionService::showFarImage(Mat& frame)
{
	/*
	const QString standbyImgPath = QStringLiteral(IMAGES_PATH) + QStringLiteral(STANDBY_IMAGE);
	//qDebug() << "[showFarImage] standby image path: " << standbyImgPath << ", exists=" << QFile::exists(standbyImgPath);
	QImage standbyImg;
	if (!standbyImg.load(standbyImgPath) || standbyImg.isNull()) {
		QImage fallback(640, 480, QImage::Format_RGB888);
		fallback.fill(Qt::black);
		emit frameReady(fallback.copy());
	} else {
		emit frameReady(standbyImg.convertToFormat(QImage::Format_RGB888).copy());
	}
	*/
	int baseline = 0;
	cv::Size textSize;
	int x, y =  135;

	std::string text = "Come on."; 
	textSize = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.8, 2, &baseline); 
	x = frame.cols - textSize.width - 10;
	cv::putText(frame, text, {x, y}, cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 200, 255), 2);
}

// 1) 임계값: 한 곳에서만 관리
struct Thresh {
    double strong      = 0.97; // 강한 통과
    double singleton   = 0.97; // 갤러리 1명일 때
    double multi_best  = 0.93; // 다인 갤러리 최소 유사도
    double multi_gap   = 0.03; // 1등-2등 격차
    double hardReject  = 0.90; // 미만은 즉시 거부
    double enter       = 0.90; // (참고) 스트릭용 진입 임계
    double exit        = 0.85; // (참고) 스트릭용 이탈 임계
};

enum class AuthDecision { ACCEPT, VOTE, REJECT };

// 2) clamp01 (C++11 호환)
static inline double clamp01(double v) {
    if (v < -1.0) return -1.0;
    if (v >  1.0) return  1.0;
    return v;
}

// 3) 단일 정책 평가: 유사도/갭/갤러리 → ACCEPT / VOTE / REJECT
static inline AuthDecision evaluateAuthPolicy(double best, double second, int gallerySize,
                                              const Thresh& th, QString* reason=nullptr)
{
    const double b = clamp01(best);
    const double s = clamp01(second);

    if (b >= th.strong) {
        if (reason) *reason = QStringLiteral("StrongAccept");
        return AuthDecision::ACCEPT;
    }
    if (gallerySize <= 1 && b >= th.singleton) {
        if (reason) *reason = QStringLiteral("SingletonAccept");
        return AuthDecision::ACCEPT;
    }
    if (gallerySize >= 2 && b >= th.multi_best && (b - s) >= th.multi_gap) {
        if (reason) *reason = QStringLiteral("MarginAccept");
        return AuthDecision::ACCEPT;
    }
    if (b < th.hardReject) {
        if (reason) *reason = QStringLiteral("HardReject");
        return AuthDecision::REJECT;
    }
    if (reason) *reason = QStringLiteral("VoteZone");
    return AuthDecision::VOTE;
}

// 4) 스트릭 증가 쿨다운만 (히스테리시스는 FSM이 담당하므로 여기선 안 함)
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

void FaceRecognitionService::beginOpenOverlay(int ms)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int dur = (ms > 0) ? ms : openOverlayMs_;
    // 재트리거 시 남은 시간보다 길면 갱신 (겹쳐도 자연스러운 UX)
    qint64 until = now + dur;
    if (!openOverlayActive_ || until > openOverlayUntilMs_) {
        openOverlayActive_  = true;
		emit doorStateChanged(States::DoorState::Open);
        openOverlayUntilMs_ = until;

        qInfo() << "[UI] Open overlay ON for" << dur << "ms";
    }
}

// FaceRecognitionService.cpp
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
        qDebug() << "[Door] reed=" << (sensorClosed ? "CLOSED" : "OPEN")
                 << " -> doorOpened_=" << opened;
    }
}



void FaceRecognitionService::loopDirect()
{
	cv::Mat frame, frameCopy;
	while (running_.loadAcquire() == 1) {
		const bool wantReg = (isRegisteringAtomic.loadRelaxed() != 0);
		bool acceptedThisFrame = false;
		double bestForFrame = 0.0;

		syncDoorOpenedFromReed();

		if(!cap_.read(frame) || frame.empty()) {
			QThread::msleep(2);
			continue;
		}
		else { 
			frameCopy = frame.clone();
			imwrite("/tmp/snap.jpg", frameCopy);
		}

		// loopDirect() 안에서, frame 읽은 직후쯤
		const qint64 now = QDateTime::currentMSecsSinceEpoch();

		// "문 열림 화면을 보여줄지"는 두 조건의 OR
		// 1) 언락 이벤트로 켜진 타이머 (openOverlayActive_)
		// 2) 실제 리드센서가 열림 (!g_reed.isClosed())
		int readDoorState = g_reed.isClosed();

		//if (openOverlayActive_ || !g_reed.isClosed()) {
		if (openOverlayActive_ || !readDoorState) {
			showOpenImage(); // 내부에서 emit frameReady(...)
			emit doorStateChanged(States::DoorState::Open);

			// 오버레이 타이머가 켜져 있을 때만 만료 처리
			if (openOverlayActive_ && now >= openOverlayUntilMs_) {
				openOverlayActive_ = false;
				emit doorStateChanged(States::DoorState::Locked);
				qInfo() << "[UI] Open overlay OFF";
			}

			QThread::msleep(1);
			continue; // 오버레이 중엔 일반 파이프라인 잠시 멈춤
		}
		else {
			emit doorStateChanged(States::DoorState::Locked);
		}

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

		// ── 2) 채널 보정 ──
		if (frame.channels() == 4) cv::cvtColor(frame, frame, cv::COLOR_BGRA2BGR);
		else if (frame.channels() == 1) cv::cvtColor(frame, frame, cv::COLOR_GRAY2BGR);


		// ── 3) trivial 체크 ──
		if (dnnEmbedder_ && dnnEmbedder_->isTrivialFrame(frame, 30.0, 10.0)) {
			qWarning() << "[loopDirect] Trivial frame. skip";
			emit frameReady(QImage());
			continue;
		}

		// ── 4) 얼굴 검출 ──
		std::vector<FaceDet> faces;
		if (auto best = detectBestYuNet(frame)) {
			faces.push_back(*best);
		}
		else {
			printFrame(frame, DetectedStatus::FaceNotDetected); 
		}

		// FSM 상태 초기화
		setFacePresent(!faces.empty());
		setRegisterRequested(wantReg);
		setLivenessOk(true);
		setDuplicate(false);

		static Thresh th;
		static Voter voter({5, 3});

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

			// 품질 체크
			if (!passQualityForRecog(fd.box, frame)) {
				printFrame(frame, DetectedStatus::QualityOut);
				continue;
			}

			if (!wantReg) {
				// 인식 처리
				auto recogResult = handleRecognition(frame, fd.box, aligned, label, color);
				printFrame(frame, DetectedStatus::FaceDetected);

				// FSM / UI에 코사인 전달
				setRecogConfidence(recogResult.sim);

				// 안전 가드
				const double best   = std::isfinite(recogResult.sim) ? recogResult.sim : 0.0;
				const double second = std::isfinite(recogResult.secondSim) ? recogResult.secondSim : -1.0;
				bestForFrame = std::max(bestForFrame, best);

				const int nUsers  = std::max(0, (int)gallery_.size());
				QString reason;
				AuthDecision dec = evaluateAuthPolicy(best, second, nUsers, th, &reason);

				if (dec == AuthDecision::ACCEPT) {
					setAllowEntry(true);
					acceptedThisFrame = true;


				}
				else if (dec == AuthDecision::REJECT) {
					setAllowEntry(false);
					incFailCount();
				}
				else { // VOTE 영역
					bool voteOk = false;
#if 1
					voteOk = (recogResult.result == AUTH_SUCCESSED) && voter.feed(recogResult.idx, best, th.enter);
#endif
					if (voteOk) {
						setAllowEntry(true);
						acceptedThisFrame = true;
						reason = QStringLiteral("VoteAccept(5/3)");
					}
					else {
						setAllowEntry(false);
						incFailCount();
					}
				}

				if (!acceptedThisFrame) {
					continue;
				}

				const bool isKnownUser = (recogResult.idx >= 0) && !recogResult.name.trimmed().toLower().startsWith("unknown");

				if (isKnownUser) {
					authManager.handleAuthSuccess();
					tryIncStreakCooldown(recogResult.idx, [&]() { incAuthStreak(); });


					if (!hasAlreadyUnlocked && authManager.shouldAllowEntry(recogResult.name)) {
						if (!g_unlockMgr.running()) {
							g_unlockMgr.start();
							qInfo() << "[loopDirect] Unlock started (wait open, then wait close)";
						}

						beginOpenOverlay(5000);

						// DB 로그 저장(JPEG → BLOB)
						std::vector<uchar> buf;
						cv::imencode(".jpg", frameCopy, buf);
						QByteArray blob(reinterpret_cast<const char*>(buf.data()),
								static_cast<int>(buf.size()));

						db->insertAuthLog(recogResult.name, reason,
								QDateTime::currentDateTime(), blob);

						hasAlreadyUnlocked = true;
					}

					resetFailCount();
					//continue; // 다음 얼굴로
				} 
				else {
					qInfo() << "[loopDirect] Unknown detected -> skip success & failCount reset";
					resetAuthStreak();
					authManager.resetAuth();
					resetUnlockFlag();

					setAllowEntry(false);
				}
			}
			else {
				// 등록 모드
				handleRegistration(frame, fd.box, aligned, label, color);
				printFrame(frame, DetectedStatus::Registering);
			}

			if (faces.empty()) setRecogConfidence(0.0);

			// streak 처리
			if (authStreak_ > params_.authThresh) {
				resetAuthStreak();
				authManager.resetAuth();
				resetUnlockFlag();
				setAllowEntry(false);
			}

			{
				QMutexLocker lk(&snapMu_);
				setFacePresent(!faces.empty());
				setDetectScore(maxDetect);
				setRecogConfidence(bestForFrame);
				setLivenessOk(true);
				setAllowEntry(acceptedThisFrame);
				setDoorSensorOpen(!g_reed.isClosed());
			}


			// CPU 과점유 방지 (필요 시 0~2ms)
			QThread::msleep(1);
		}
	}
	//g_uls.stop();
}
void FaceRecognitionService::printFrame(Mat &frame, DetectedStatus hasBase) 
{

	//auto now = QDateTime::currentDateTime().toString("HH:mm:ss.zzz").toStdString();
	//cv::putText(frame, now, {10, 25}, cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 255), 2);


	int baseline = 0;
	cv::Size textSize;
	int x, y =  35;

	if (hasBase == DetectedStatus::FaceDetected) {
		std::string text = " Face is being recognized.";	
		textSize = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.8, 2, &baseline);
		x = frame.cols - textSize.width - 10;
		cv::putText(frame, text, {x, y}, cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);
	}
	else if (hasBase == DetectedStatus::FaceNotDetected) {
		std::string text = "No face detected.";
		textSize = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.8, 2, &baseline);
		x = frame.cols - textSize.width - 10;
		cv::putText(frame, text, {x, y}, cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 0, 255), 2);
	}
	else if (hasBase == DetectedStatus::QualityOut) {
		std::string text = "The face is too far away or the low quality.";
		textSize = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.8, 2, &baseline);
		x = frame.cols - textSize.width - 10;
		cv::putText(frame, text, {x, y}, cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 200, 255), 2);
	} else if (hasBase == DetectedStatus::Registering) {
		std::string text = "Registering face... Please hold still...";
		textSize = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.8, 2, &baseline);
		x = frame.cols - textSize.width - 10;
		cv::putText(frame, text, {x, y}, cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255, 0, 0), 2);
	}
	

	if (frame.cols != 640 || frame.rows != 480)
		cv::resize(frame, frame, cv::Size(640, 480), 0, 0, cv::INTER_AREA);		

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

	//qCDebug(LC_FSM_CTX) << "[CTX]"
	/*
	qDebug() << "[CTX]"
		<< "seq=" << c.seq
		<< "face=" << c.facePresent
		<< "det>0.8=" << (c.detectScore >= 0.8)
		<< "conf=" << c.recogConfidence
		<< "live=" << c.livenessOk
		<< "allow=" << c.allowEntry
		<< "streak=" << c.authStreak
		<< "reedOpen=" << c.doorOpened
		<< "timeout=" << c.timeout;
	*/

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



