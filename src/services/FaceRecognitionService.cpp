#include <QImage>
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
#include "fsm/fsm_logging.hpp"
#include "include/common_path.hpp"
#include "log/SystemLogger.hpp"
#include "services/QSqliteService.hpp"

// #define DEBUG 
// ===== 하드웨어 핀/옵션 =====
static constexpr const char* GPIOCHIP = "/dev/gpiochip0";
static constexpr unsigned RELAY_GPIO  = 17;   // 릴레이 IN
static constexpr bool     RELAY_ACTIVE_HIGH = false; // LOW=ON 릴레이면 false

static constexpr unsigned REED_GPIO   = 0;   // 리드 스위치 DOUT
static constexpr bool     REED_ACTIVE_HIGH = true;   // 자석 감지 시 HIGH면 true, LOW면 false (현장 확인 필요)

// ===== 컨트롤러/매니저(정적) =====
//static DoorlockController    g_door(GPIOCHIP, RELAY_GPIO, RELAY_ACTIVE_HIGH);
static DoorlockController      g_door;
//static ReedSensor            g_reed(GPIOCHIP, REED_GPIO, REED_ACTIVE_HIGH);
static ReedSensor			   g_reed;
static UnlockUntilReed::Opt    g_unlockOpt{/*pollMs*/50, /*hits*/6, /*openTimeoutMs*/5000, /*maxUnlockMs*/10000};
static UnlockUntilReed         g_unlockMgr(&g_door, &g_reed, g_unlockOpt);

using json = nlohmann::json;
namespace fs = std::filesystem;


// === helpers === 
// ---- Matching helpers (file-scope) ----
struct Thresh { float T_in=0.84f, T_out=0.80f, DELTA=0.08f; float z_thr=2.5f; };
struct Vote  { int M=5, N=3; };

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
	connect(&fsm_, &RecognitionFsm::stateChanged, this, [this](RecognitionState s) {
			emit stateChanged(s);
			});

	// 3) 스냅샷 타이머 시작(컨텍스트 공급)
	tick_.setInterval(33);
	connect(&tick_, &QTimer::timeout, this, &FaceRecognitionService::onTick);
	tick_.start();

	// 4) FSM 시작
	fsm_.start(RecognitionState::IDLE);

	cv::setUseOptimized(true);
	cv::setNumThreads(2);
}

void FaceRecognitionService::setPresenter(FaceRecognitionPresenter* _presenter)
{
	presenter = _presenter;
}

// === YuNet helpers ====
// Laplacian variance 기반 블러 판정

// 등록/인식 분리 게이트

bool FaceRecognitionService:: passQualityForRecog(const cv::Rect& box, const cv::Mat& face) 
{
	if (face.empty() || face.cols < 64 || face.rows < 64) {
		qDebug() << "[Qual:FAIL] empty/too small crop"
			<< "crop=" << face.cols << "x" << face.rows;
		return false;
	}

	const int     kMinBoxW      = 120;
	const int     kMinBoxH      = 120;
	const double  kBlurThr      = 60.0;
	const double  kMinMean      = 50.0;
	const double  kMaxMean      = 205.0;
	const double  kMinStd       = 18.0;
	const double  kClipRatioMax = 0.25;

	if (box.width < kMinBoxW || box.height < kMinBoxH) {
		qDebug() << "[Qual:FAIL] small box"
			<< "box=" << box.width << "x" << box.height
			<< "need>=" << kMinBoxW << "x" << kMinBoxH;
		return false;
	}

	cv::Mat gray;
	if (face.channels() == 3) cv::cvtColor(face, gray, cv::COLOR_BGR2GRAY);
	else if (face.channels() == 4) cv::cvtColor(face, gray, cv::COLOR_BGRA2GRAY);
	else gray = face;

	cv::Mat lap; cv::Laplacian(gray, lap, CV_64F);
	cv::Scalar mu, sigma;
	cv::meanStdDev(lap, mu, sigma);
	const double lapVar = sigma[0]*sigma[0];
	//qDebug() << "[passQualityForRecog] Blur check: variance=" << lapVar << "thr=" << kBlurThr;
	if (lapVar < kBlurThr) {
		qDebug() << "[Qual:FAIL] too blur var=" << lapVar << "thr=" << kBlurThr;
		return false;
	}

	cv::Scalar mean, stddev;
	cv::meanStdDev(gray, mean, stddev);
	const double m = mean[0];
	const double s = stddev[0];
	if (m < kMinMean || m > kMaxMean) {
		qDebug() << "[Qual:FAIL] bad exposure mean=" << m
			<< "range=" << kMinMean << "~" << kMaxMean;
		return false;
	}
	if (s < kMinStd) {
		qDebug() << "[Qual:FAIL] low contrast std=" << s << "need>=" << kMinStd;
		return false;
	}

	int histSize = 256; float range[] = {0,256}; const float* ranges = { range };
	cv::Mat hist;
	cv::calcHist(&gray, 1, 0, cv::Mat(), hist, 1, &histSize, &ranges, true, false);
	const double total = (double)gray.total();
	const double clip0   = hist.at<float>(0)   / total;
	const double clip255 = hist.at<float>(255) / total;
	if (clip0 > kClipRatioMax || clip255 > kClipRatioMax) {
		qDebug() << "[Qual:FAIL] clipping"
			<< "clip0=" << clip0 << "clip255=" << clip255
			<< "limit=" << kClipRatioMax;
		return false;
	}

	{
		int cx = std::max(0, std::min(face.cols/2 - 32, face.cols - 64));
		int cy = std::max(0, std::min(face.rows/2 - 32, face.rows - 64));
		cv::Rect cR(cx, cy, 64, 64);
		cv::Mat center = gray(cR);
		cv::Scalar cm, cs; cv::meanStdDev(center, cm, cs);
		if (cs[0] < (kMinStd - 2)) {
			qDebug() << "[Qual:FAIL] low center contrast std=" << cs[0]
				<< "need>=" << (kMinStd - 2);
			return false;
		}
	}

#ifdef DEBUG
	qDebug() << "[Qual:PASS]"
		<< "box=" << box.width << "x" << box.height
		<< "mean=" << m << "std=" << s << "var=" << lapVar;
#endif
	return true;
}

void FaceRecognitionService::init()
{
	qDebug() << "[init] Face Recognition Service initiallize!!";
	openCamera();
	if (!this->initializeDnnOnly()) {
		SystemLogger::error("DNN", "DNN-only init failed (YuNet/MobileFaceNet)");
		return;
	}

	// ===== 여기 추가: 도어락/리드스위치 초기화 =====
	if (!g_door.init())  { qWarning() << "[HW] Door init failed"; }
	if (!g_reed.init())  { qWarning() << "[HW] Reed init failed"; }
	// g_unlockMgr는 위에서 g_door/g_reed 참조만 하므로 별도 init 불필요


	QTimer *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &FaceRecognitionService::procFrame);
	timer->start(30);
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

bool FaceRecognitionService::loadDetector()
{
	try {
		const QString yuModelQ = QStringLiteral(YNMODEL_PATH) + QStringLiteral(YNMODEL);
		const std::string model = yuModelQ.toStdString();

		QFileInfo fi(yuModelQ);
		if (!fi.exists() || !fi.isFile()) {
			qWarning() << "[loadDetector] YuNet file not found:" << yuModelQ
				<< " size=" << fi.size();
			return false;
		}

		if (!fi.isReadable()) {
			qWarning() << "[loadDetector] YuNet file nost readable (perm):" << yuModelQ;
			return false;
		}
		if (fi.size() <= 1024) {
			qWarning() << "[loadDetector] YuNet file too small (" << fi.size() << "bytes):" << yuModelQ;
			return false;
		}

		yunet_ = cv::FaceDetectorYN::create(
				model, "", cv::Size(320, 320),
				/*scoreThreshold=*/0.7f,
				/*nmsThreshold=*/0.3f,
				/*topK=*/1000,
				cv::dnn::DNN_BACKEND_OPENCV,
				cv::dnn::DNN_TARGET_CPU
				);

		if (yunet_.empty()) {
			qWarning() << "[loadDetector] YuNet create failed. path=" << yuModelQ
				<< " size=" << fi.size();
			return false;
		}

		// Warnming up
		{
			cv::Mat dummy(320, 320, CV_8UC3, cv::Scalar(0,0,0));	
			cv::Mat out;
			try {
				yunet_->setInputSize(dummy.size());
				yunet_->detect(dummy, out);
				qDebug() << "[loadDetector] warmup dectet ok out shape="
					<< out.rows << "x" << out.cols;
			} catch (const cv::Exception& e) {
				qWarning() << "[loadDetector] warnmup detect failed:" << e.what()
					<< " path=" << yuModelQ;
				yunet_.release();
				return false;
			}
		}

		qInfo() << "[loadDetector] YuNet detect loaded:" << yuModelQ
			<< " (" << fi.size() << " bytes)";
		return true;
		if (yunet_.empty()) {
			qWarning() << "[loadDetector] YuNet create failed";
			return false;
		}
		qInfo() << "[loadDetector] Yunet detector is loaded(" << yuModelQ << ")";
		return true;
	} catch (const cv::Exception& e) {
		qWarning() << "[loadDetector] YuNet load error:" << e.what();
		return false;
	}
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
		if (!dnnEmbedder_->extract(dummy, emb) || emb.empty()) {
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
	if (!QFile::exists(embPath)) {
		qDebug() << "[loadEmbeddingJsonFile] Embedding json file is not found(" << embPath << ")";
		return rc;
	}
	else {
		rc = true;
		embeddingsPath_ = embPath.toUtf8().constData();
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
		qInfo() << "[DNN] Loaded embeddings:" << (int)gallery_.size()
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

	if (!loadDetector()) rc--;

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

// === 서비스 중단 ===
void FaceRecognitionService::stop()
{
	std::cout << "Face Recognition Service stop" << std::endl;

	// 이미 중단 됬는지 확인	
	if (!isRunning) return;

	// 중단 확정 
	isRunning = false;

	// 카메라 닫기 
	if (cap.isOpened()) {
		cap.release();
	}

	// 현재 상태 아무것도 않함으로 변경
	currentState = RecognitionState::IDLE;
}

// === 카메라 열기 (V4L2 전용) ===
void FaceRecognitionService::openCamera()
{
    qDebug() << "[FaceRecognitionService] openCamera is called";

    try {
        // 1) V4L2: 경로(/dev/video1)로 직접 오픈
        if (!cap.open("/dev/video1", cv::CAP_V4L2)) {
            qWarning() << "[openCamera] V4L2 open by path failed, try index 1";
            cap.open(1, cv::CAP_V4L2);
        }

        if (!cap.isOpened()) {
            std::cout << "[FRS] Failed to camera open!!" << std::endl;
            SystemLogger::error("FRS", "Failed to camera open.");
            return;
        }

        // 2) 장치 설정 (우선 MJPG)
        cap.set(cv::CAP_PROP_FRAME_WIDTH,  640);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
        cap.set(cv::CAP_PROP_FPS,          30);
        cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G'));

        // 실제 적용된 값 확인
        int fourcc = static_cast<int>(cap.get(cv::CAP_PROP_FOURCC));
        double w   = cap.get(cv::CAP_PROP_FRAME_WIDTH);
        double h   = cap.get(cv::CAP_PROP_FRAME_HEIGHT);
        double fps = cap.get(cv::CAP_PROP_FPS);
        qInfo() << "[Cam] applied fourcc=" << fourcc << " w=" << w << " h=" << h << " fps=" << fps;

        // 3) 적용 실패 시(YUYV로 재시도)
        if (fps <= 0.0 || w != 640 || h != 480) {
            qWarning() << "[Cam] MJPG config not honored. Retrying with YUYV...";
            cap.release();

            if (!cap.open("/dev/video1", cv::CAP_V4L2)) {
                qWarning() << "[openCamera] V4L2 reopen by path failed, try index 1";
                cap.open(1, cv::CAP_V4L2);
            }
            if (!cap.isOpened()) {
                std::cout << "[FRS] Failed to camera open after retry!!" << std::endl;
                SystemLogger::error("FRS", "Failed to camera open after retry.");
                return;
            }

            cap.set(cv::CAP_PROP_FRAME_WIDTH,  640);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
            cap.set(cv::CAP_PROP_FPS,          30);
            cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('Y','U','Y','V'));

            fourcc = static_cast<int>(cap.get(cv::CAP_PROP_FOURCC));
            w      = cap.get(cv::CAP_PROP_FRAME_WIDTH);
            h      = cap.get(cv::CAP_PROP_FRAME_HEIGHT);
            fps    = cap.get(cv::CAP_PROP_FPS);
            qInfo() << "[Cam:YUYV] applied fourcc=" << fourcc << " w=" << w << " h=" << h << " fps=" << fps;
        }

        // 4) 워밍업 프레임 드랍
        for (int i = 0; i < 15; ++i) {
            cv::Mat tmp; cap >> tmp;
#ifdef QT_CORE_LIB
            QThread::msleep(10);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif
        }

        std::cout << "[FRS] Camera be opend!!" << std::endl;
        SystemLogger::info("FRS", "Success to camera open.");

    } catch (const cv::Exception& e) {
        std::cout << "[FRS] OpenCV exception: " << e.what() << std::endl;
    }
}

void FaceRecognitionService::camRestart()
{
	qDebug() << "[FRS] Camera is restart!";
	try {
		cap.open(CAM_NUM);
		if (!cap.isOpened()) {
			SystemLogger::error("FRS", "Failed to camera restart.");
		}
		std::cout << "[FRS] Camera restart!!" << std::endl;
		SystemLogger::info("FRS", "Success to camera restart.");
	} catch(const cv::Exception& e) {
		std::cout << "[FRS] OpenCV exception: " << e.what() << std::endl;
	}
	presenter->presentCamRestart();
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
bool FaceRecognitionService::saveEmbeddingsToFile()
{
	try {
		// 1) 스키마 메타 포함 JSON구성
		nlohmann::json root;
		root["version"] = 1;		// 스키마 버전
		root["dim"] 	= gallery_.empty() ? 0 : int(gallery_.front().embedding.size());
		root["count"] 	= int(gallery_.size());
		root["items"]	= nlohmann::json::array();

		for (const auto& u : gallery_) {
			nlohmann::json o = nlohmann::json::object();
			o["id"]			= u.id;
			o["name"]		= std::string(u.name.toUtf8().constData());		// UTF-8보장
			o["embedding"]	= u.embedding;		// 이미 L2 정규화된 상태를 저장(권장)
			root["items"].push_back(std::move(o));
		}

		// 2) 경로 보장(디렉토리 없으면 생성)
		const QString qpath = QString::fromStdString(embeddingsPath_);
		const QFileInfo fi(qpath);
		if (!fi.absoluteDir().exists()) {
			QDir().mkpath(fi.absolutePath());
		}

		// 3) 원자적 저장: QSaveFile
		// 		- 임시 파일에 씀 -> fsync -> commit() 에서 rename
		QSaveFile file(qpath);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			qWarning() << "[DNN] saveEmbeddingsToFile: open failed" << qpath << file.errorString();
			return false;
		}

		const QByteArray bytes = QByteArray::fromStdString(root.dump(2)); // pretty print
		const qint64 written = file.write(bytes);
		if (written != bytes.size()) {
			qWarning() << "[DNN] saveEmbeddingsToFile: short write" << written << "/" << bytes.size();
			file.cancelWriting();
			return false;
		}

		// 4) commit()이 성공하면 원자적으로 교체
		if (!file.commit()) {
			qWarning() << "[DNN] saveEmbeddingsToFile: commit failed" << qpath << file.errorString();
			return false;
		}


		qInfo() << "[DNN] Saved embeddings:" << root["count"].get<int>() 
			<< "users, dim=" << root["dim"].get<int>()
			<< "->" << qpath;
		return true;

	} catch (const std::exception& e) { 
		qWarning() << "[DNN] saveEmbeddingsToFile error:" << e.what();
		return false; 
	} catch (...) {
		qWarning() << "[DNN] saveEmbeddingsToFile unknown error";
		return false;
	}
}


// 로컬 헬퍼: JSON array -> std::vector<float>
static inline bool jsonToFloatVec(const nlohmann::json& arr, std::vector<float>& out) {
	if (!arr.is_array() || arr.empty()) return false;
	out.clear(); out.reserve(arr.size());
	try {
		for (const auto& v : arr) {
			// 숫자면 double/float 상관없이 float로 캐스팅
			if (!v.is_number()) return false;
			out.push_back(static_cast<float>(v.get<double>()));
		}
		return true;
	} catch (...) { return false; }
}

bool FaceRecognitionService::loadEmbeddingsFromFile()
{
	try {
		const QString qpath = QString::fromStdString(embeddingsPath_);
		QFile f(qpath);
		if (!f.exists()) {
			qWarning() << "[DNN] loadEmbeddingsFromFile: file not found ->" << qpath;
			return false;
		}
		if (!f.open(QIODevice::ReadOnly)) {
			qWarning() << "[DNN] loadEmbeddingsFromFile: open failed ->" << qpath << f.errorString();
			return false;
		}
		const QByteArray bytes = f.readAll();
		f.close();

		nlohmann::json root = nlohmann::json::parse(bytes.constData(), bytes.constData() + bytes.size());

		std::vector<UserEmbedding> tmp; // 임시 컨테이너에 먼저 채운 뒤 교체(부분 로드 방지)
		tmp.reserve(256);

		int dim = 0;
		auto pushItem = [&](int id, const QString& name, const nlohmann::json& embJ) -> bool {
			UserEmbedding u;
			u.id = id;
			u.name = name;
			if (!jsonToFloatVec(embJ, u.embedding)) return false;
			l2normInPlace(u.embedding);

			// dim 일관성 체크
			if (dim == 0) dim = int(u.embedding.size());
			else if (int(u.embedding.size()) != dim) {
				qWarning() << "[DNN] load: dimension mismatch for" << name
					<< "expected" << dim << "got" << int(u.embedding.size());
				return false;
			}
			u.proto = cv::Mat(1, dim, CV_32F, u.embedding.data()).clone();

			// L2 정규화(로드 시 1회) - 저장 시에도 정규화했더라도 한 번 더 걸어도 무해
			l2normInPlace(u.embedding);
			tmp.push_back(std::move(u));
			return true;
		};

		// ----- 신 스키마 -----
		if (root.is_object() && root.contains("items") && root["items"].is_array()) {
			const int ver   = root.value("version", 1);
			if (ver != 1) {
				qWarning() << "[loadEmbeddingsFromFile] Unsupported schema version:" << ver;
				return false;
			}

			const int count = root.value("count", 0);
			const int sdim  = root.value("dim", 0);

			for (const auto& o : root["items"]) {
				if (!o.is_object()) continue;
				// id/name/embedding 필수
				if (!o.contains("id") || !o.contains("name") || !o.contains("embedding")) continue;

				const int id = o["id"].get<int>();
				const QString name = QString::fromStdString(o["name"].get<std::string>());

				if (!pushItem(id, name, o["embedding"])) {
					qWarning() << "[DNN] load: skip item (invalid embedding)" << name;
					continue;
				}
			}

			if (sdim && dim && sdim != dim) {
				qWarning() << "[DNN] load: header dim mismatch, header=" << sdim << "actual=" << dim;
			}
			if (count && count != (int)tmp.size()) {
				qInfo() << "[DNN] load: header count" << count << "!= actual" << (int)tmp.size();
			}
			qInfo() << "[DNN] Loaded (schema v" << ver << ")"
				<< (int)tmp.size() << "users, dim=" << dim;
		}
		else {
			qWarning() << "[DNN] loadEmbeddingsFromFile: invalid schema root";
			return false;
		}

		// 간단한 중복/무결성 검사(선택)
		{
			QSet<int> seenId;
			QSet<QString> seenName;
			for (const auto& u : tmp) {
				if (seenId.contains(u.id)) {
					qWarning() << "[DNN] duplicate id:" << u.id;
				}
				if (seenName.contains(u.name)) {
					qWarning() << "[DNN] duplicate name:" << u.name;
				}
				seenId.insert(u.id);
				seenName.insert(u.name);
			}
		}

		// 최종 교체(원자성: 전체 성공 시점에만 반영)
		gallery_.swap(tmp);
		qDebug() << "[FRS] gallery loaded +  normalized,, users=" << int(gallery_.size())
			<< "dim=" << (gallery_.empty() ? 0 : int(gallery_.front().embedding.size()));

		return !gallery_.empty();

	} catch (const std::exception& e) {
		qWarning() << "[DNN] loadEmbeddingsFromFile error:" << e.what();
		return false;
	} catch (...) {
		qWarning() << "[DNN] loadEmbeddingsFromFile unknown error";
		return false;
	}
}

MatchResult FaceRecognitionService::bestMatch(const std::vector<float>& emb) const
{
	MatchResult r;

	if (gallery_.empty()) {
		qWarning() << "[FRS] bestMatch: gallery_ is empty";
		return r;
	}
	if (emb.empty()) {
		qWarning() << "[FRS] bestMatch: input embedding is empty";
		return r;
	}

	const int d_in = static_cast<int>(emb.size());
	int mismatch = 0;

	for (const auto& u : gallery_) {
		const int d_g = static_cast<int>(u.embedding.size());
		if (d_g != d_in) {
			++mismatch;
			continue; // 차원 다른 건 스킵
		}


		float s = Embedder::cosine(emb, u.embedding); // L2 정규화 가정 → dot=cosine
		if (s > r.sim) {
			r.sim  = s;
			r.id   = u.id;
			r.name = u.name;
		}
	}

	if (mismatch > 0) {
		qWarning() << "[FRS] bestMatch: dim mismatch count =" << mismatch
			<< " input_dim=" << d_in;
	}

	// 디버깅: 최고 유사도 찍기
	qDebug() << "[FRS] bestMatch result:" << r.name << "sim=" << r.sim;

	return r;
}

MatchTop2 FaceRecognitionService::bestMatchTop2(const std::vector<float>& emb) const {
    MatchTop2 r;
    if (gallery_.empty() || emb.empty()) return r;

    const int dim = int(emb.size());
    cv::Mat q(1, dim, CV_32F, const_cast<float*>(emb.data()));
    q = q.clone(); // 헤더 공유 차단
    double nq = cv::norm(q, cv::NORM_L2);
    if (nq <= 0.0) return r;
    q /= (nq + 1e-9); // 쿼리 L2=1로 보정

    int mismatch = 0;
    for (int i = 0; i < (int)gallery_.size(); ++i) {
        const auto& u = gallery_[i];

        // 차원 체크
        if ((int)u.embedding.size() != dim) { ++mismatch; continue; }

        // proto 존재/타입 가드
        if (u.proto.empty() || u.proto.type() != CV_32F || u.proto.total() != (size_t)dim) {
            qWarning() << "[FRS] proto invalid at i=" << i << " empty/type/size";
            continue;
        }

        // === DEBUG & 안전 검사 (원인 추적용) ===
        if (q.data == u.proto.data) {
            qWarning() << "[bestMatchTop2] self-match: q and proto share SAME data i=" << i;
        }
        double np = cv::norm(u.proto, cv::NORM_L2);
        if (np <= 0.0) {
            qWarning() << "[bestMatchTop2] proto L2==0 at i=" << i;
            continue;
        }

        // 각도/차이 로그 (필요시 주석 처리 가능)
        {
            double cosn = q.dot(u.proto) / ((1.0) * (np + 1e-9)); // q는 이미 L2=1
            cosn = std::max(-1.0, std::min(1.0, cosn));
            double deg = std::acos(cosn) * 180.0 / M_PI;
            double diff = cv::norm(q - (u.proto / (np + 1e-9)), cv::NORM_L2);
            qDebug() << "[bestMatchTop2] i=" << i
                     << " cos=" << cosn
                     << " angle(deg)=" << deg
                     << " ||q-p||=" << diff;
        }
        // ================================

        // 최종 유사도 (정상 경로): q는 L2=1, proto도 L2정규화해서 dot
        float s = q.dot(u.proto / (np + 1e-9f));

        if (s > r.bestSim) {
            r.secondIdx = r.bestIdx;
            r.secondSim = r.bestSim;
            r.bestIdx = i;
            r.bestSim = s;
        } else if (s > r.secondSim) {
            r.secondIdx = i;
            r.secondSim = s;
        }
    }

    if (mismatch > 0) {
        qWarning() << "[bestMatchTop2] dim mismatch=" << mismatch << " input_dim=" << dim;
    }
    return r;
}
struct FaceDet {
	cv::Rect box;
	std::array<cv::Point2f, 5> lmk;				// leftEye, right Eye, nose, mouthL, mouthR
	float score;
};


// === 중볻된 얼굴인지 체크 ===
bool FaceRecognitionService::isDuplicateFaceDNN(const cv::Mat& alignedFace, int* dupId, float* simOut) const
{
	if (!dnnEmbedder_) return false;

	std::vector<float> emb;
	if (!dnnEmbedder_->extract(alignedFace, emb) || emb.empty()) return false;

	// L2 정규화 강제
	l2normInPlace(emb);

	// Top-2 매칭
	MatchTop2 m = bestMatchTop2(emb);

	const double COS_THRESH = 0.94;   // 등록자 유사도 임계값
	const double MIN_MARGIN = 0.15;   // 최소 마진

	//bool marginOk = (m.secondIdx < 0) ? true : ((m.bestSim - m.secondSim) >= MIN_MARGIN);
	//bool isKnown  = (m.bestSim >= COS_THRESH) && marginOk;
	bool isKnown = (m.bestSim >= COS_THRESH);

	if (isKnown) {
		if (dupId) *dupId = gallery_[m.bestIdx].id;
		if (simOut) *simOut = static_cast<float>(m.bestSim);
		qDebug() << "[DuplicateCheck] matched id=" << gallery_[m.bestIdx].id
			<< "sim=" << m.bestSim << " second=" << m.secondSim;
			//<< " marginOk=" << marginOk;
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

	storedFaces[registeringUserId_].push_back(alignedFace);

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
	

	// 3) gallery_ 업데이트 (registeringUserId_는 UI/흐름에서 미리 지정)
	if (registeringUserId_ < 0) {
		registeringUserId_ = nextSequentialId();
	}

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
	else it->embedding = std::move(meanEmb);

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
		string filename = string(USER_FACES_DIR) + "face_" + to_string(registeringUserId_) + "_" +
			registeringUserName_.toStdString() + "_" + to_string(i + 1) + ".png";
		if (!imwrite(filename, entry)) {
			qDebug() << "이미지 저장 실패: " << QString::fromStdString(filename);
		}
		i++;
	}

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

	QFile::remove(QString::fromStdString(embeddingsPath_));

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
#ifdef DEBUG
	qDebug() << "[handleRecognition] embed input size=" << alignedFace.cols << "x" << alignedFace.rows
			 << " channels=" << alignedFace.channels();
#endif
	recogResult_t rv{};
	QString name  = "Unknown";
	int id = -1;
	bool matched  = false;
	float cosBest = -1.f;

	// 0) 기본 가드
	if (!dnnEmbedder_ || alignedFace.empty()) {
		rv.name="Unknown"; rv.sim=-1.f; rv.result=AUTH_FAILED;
		labelText="Unknown"; boxColor=cv::Scalar(0,0,255);
		return rv;
	}
	if (gallery_.empty()) {
		// 등록 데이터가 없으면 무조건 Unknown
		rv.name="Unknown"; rv.sim=-1.f; rv.result=AUTH_FAILED;
		labelText="Unknown"; boxColor=cv::Scalar(0,0,255);
		return rv;
	}

	// 1) 임베딩 추출 (원본만; 좌우 플립 생략해 속도/안정성↑)
	std::vector<float> emb;
	if (!dnnEmbedder_->extract(alignedFace, emb) || emb.empty()) {
		rv.name="Unknown"; rv.sim=-1.f; rv.result=AUTH_FAILED;
		labelText="Unknown"; boxColor=cv::Scalar(0,0,255);
		return rv;
	}

	// 간단 임계/마진 (고정값)
	//   - 현장 튜닝 포인트
	//const double COS_THRESH = 0.94;
	//const double MIN_MARGIN = 0.15;
	const float T_HARD	= 0.85f;
	const float T_IN	= 0.80f;
	const float MARGIN	= 0.020f;

#if 0 
	//Top-1만으로 판정
	const auto r = bestMatch(emb);
	cosBest = float(r.sim);
	const double COS_THRESH = 0.90;
	const double MIN_MARGIN = 0.15;

	const bool isKnown  = (r.sim >= COS_THRESH) && marginOk;
	name = isKnown ? r.name : "Unknown";
#else
	// Top-2 마진 판정
	auto t2 = bestMatchTop2(emb);
	cosBest = float(t2.bestSim);
	//bool marginOk = (t2.secondIdx < 0) ? true : ((t2.bestSim - t2.secondSim) >= MIN_MARGIN);
	//bool isKnown = (t2.bestSim >= COS_THRESH) && marginOk;
	const float margin = (t2.secondIdx < 0) ? 1.f : (t2.bestSim - t2.secondSim);

#ifdef DEBUG
	qDebug() << "[MATCH] best=" << t2.bestSim
         << " second=" << t2.secondSim
         << " margin=" << margin
         << " idx=" << t2.bestIdx;
#endif
	bool isKnown =
      	(t2.bestSim >= T_HARD)
   		|| ((t2.bestSim >= T_IN) && (margin >= MARGIN));

	// 갤러리가 1명일 때는 더 보수적으로 (옵션)
	if ((int)gallery_.size() == 1) {
    	if (t2.bestSim < 0.85f) isKnown = false;
	}

	name = isKnown ? getName(gallery_[t2.bestIdx]) : "Unknown";
#endif

	if (isKnown) {
		name = getName(gallery_[t2.bestIdx]);
		id = t2.bestIdx;	
		matched = true;
	} else {
		name = "Unknown";
		id = -1;
		matched = false;
		setAllowEntry(false);
		hasAlreadyUnlocked = false;
	}

#if 0	// 기본 사각형
	boxColor  = matched ? cv::Scalar(0,255,0) : cv::Scalar(0,0,255);
	labelText = matched
		? QString("%1  cos=%2").arg(name).arg(QString::number(cosBest,'f',3))
		: QString("Unknown  cos=%1").arg(QString::number(cosBest,'f',3));

	// 문제 소지가 없는 기본 도형/텍스트만 사용 (ROI 예외 없음)
	cv::rectangle(frame, face, boxColor, 2);
	cv::putText(frame, labelText.toStdString(),
			cv::Point(face.x, std::max(0, face.y - 8)),
			cv::FONT_HERSHEY_DUPLEX, 0.9, boxColor, 2);
#else	// 음영 사각 테두리 사각형
	boxColor  = matched ? cv::Scalar(0,255,0) : cv::Scalar(0,0,255);
	labelText = matched
		? QString("%1  cos=%2").arg(name).arg(QString::number(cosBest,'f',3))
		: QString("Unknown  cos=%1").arg(QString::number(cosBest,'f',3));

	drawTransparentBox(frame, face, boxColor, 0.3);
	drawCornerBox(frame, face, boxColor, 2, 25);
	putText(frame, labelText.toStdString(), cv::Point(face.x, face.y - 10),
			cv::FONT_HERSHEY_DUPLEX, 0.9, boxColor, 2);
#endif	

	if (!alignedFace.empty()) {
		cv::Mat thumb; 
		cv::resize(alignedFace, thumb, {96,96});
		cv::rotate(thumb, thumb, cv::ROTATE_180);
		cv::rectangle(thumb, {0,0,thumb.cols-1,thumb.rows-1}, boxColor, 2);
		//cv::Rect roi(10,10,thumb.cols,thumb.rows);
		//if (roi.x>=0 && roi.y>=0 && roi.x+roi.width<=frame.cols && roi.y+roi.height<=frame.rows)
		//thumb.copyTo(frame(roi));
		overlayThumbnail(frame, thumb, Corner::TopLeft, 10);
	}

	// 7) 반환
	rv.name = name;
	rv.sim  = cosBest;
	rv.idx  = id;
	rv.result = matched ? AUTH_SUCCESSED : AUTH_FAILED;
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
static inline cv::Mat alignBy5pts(
		const cv::Mat& srcBgr,
		const std::array<cv::Point2f,5>& src5_in,
		const cv::Size& outSize /* = {112,112} */)   // ★ 기본 112 권장
{
	if (srcBgr.empty()) return {};

	// 1) 좌/우 눈, 입 좌/우 순서 가드
	std::array<cv::Point2f,5> s = src5_in; // [LE, RE, Nose, LM, RM]
	if (s[0].x > s[1].x) std::swap(s[0], s[1]);   // LE.x < RE.x
	if (s[3].x > s[4].x) std::swap(s[3], s[4]);   // LM.x < RM.x

	// (선택) 간단한 품질 가드: 눈간 비율/롤 각도
	const double iod   = cv::norm(s[0] - s[1]);
	const double ratio = iod / std::max(1.0f, std::max(1.0f, (float)std::abs(s[1].x - s[0].x)));
	double rollDeg = std::atan2(s[1].y - s[0].y, s[1].x - s[0].x) * 180.0 / CV_PI;
	if (std::abs(rollDeg) > 25.0) {
		// 너무 기울면 정렬 실패 가능성 ↑ → 필요시 스킵/감점
		// return {};  // 강하게 걸고 싶으면 활성화
	}

	std::vector<cv::Point2f> src(s.begin(), s.end());
	std::vector<cv::Point2f> dst(kDst5_112.begin(), kDst5_112.end());

	// 2) 112x112로 바로 워프 (임베더 입력과 동일 크기)
	const cv::Size warpSize(112,112);
	cv::Mat M = cv::estimateAffinePartial2D(
			src, dst, cv::noArray(), cv::RANSAC, 3.0, 2000, 0.99);

	if (M.empty()) return {};

	cv::Mat aligned112;
	cv::warpAffine(srcBgr, aligned112, M, warpSize,
			cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(127,127,127));


	// --- 워프 완료 직후 (aligned112가 만들어진 다음에) 붙이기 ---
	auto applyAffine = [&](const cv::Point2f& p)->cv::Point2f {
		// M은 estimateAffinePartial2D 결과 2x3, double/float 혼용 대비
		const double m00 = M.at<double>(0,0), m01 = M.at<double>(0,1), m02 = M.at<double>(0,2);
		const double m10 = M.at<double>(1,0), m11 = M.at<double>(1,1), m12 = M.at<double>(1,2);
		return cv::Point2f(
				static_cast<float>(m00*p.x + m01*p.y + m02),
				static_cast<float>(m10*p.x + m11*p.y + m12)
				);
	};
#ifdef DEBUG
	// 1) 원본 5점을 어파인으로 보낸 좌표 vs 템플릿 좌표의 평균오차/최대오차
	double mae = 0.0, maxErr = 0.0;
	cv::Mat vis = aligned112.clone();
	for (int k=0; k<5; ++k) {
		cv::Point2f warped = applyAffine(s[k]);          // 워프 후 좌표
		const cv::Point2f tgt = kDst5_112[k];            // 템플릿 좌표

		const double err = cv::norm(warped - tgt);
		mae += err;  if (err > maxErr) maxErr = err;

		// 2) 시각화: 노랑=워프된 점, 빨강=템플릿 점, 파랑=오차 벡터
		cv::circle(vis, warped, 2, {0,255,255}, -1);
		cv::circle(vis, tgt,    2, {0,0,255},   -1);
		cv::line  (vis, warped, tgt, {255,0,0}, 1);
	}
	mae /= 5.0;

	qDebug() << "[ALIGN] mae(px)=" << mae << " maxErr(px)=" << maxErr;

	// 3) 눈 라인 기울기(정렬 후): 0°에 가까울수록 좋음
	double rollDegAfter =
		std::atan2(kDst5_112[1].y - kDst5_112[0].y, kDst5_112[1].x - kDst5_112[0].x) * 180.0 / CV_PI;
	qDebug() << "[ALIGN] roll_after(deg)=" << rollDegAfter;

	// 4) 스냅샷
	cv::imwrite("dbg_B_verify.jpg", vis);
#endif
	// 3) 최종 출력: 대부분 112로 그대로 반환
	if (outSize == warpSize) return aligned112;

	cv::Mat out;
	cv::resize(aligned112, out, outSize, 0, 0, cv::INTER_LINEAR);
	return out;
}




void FaceRecognitionService::procFrame()
{
	recogResult_t recogResult;
	const bool wantReg = (isRegisteringAtomic.loadRelaxed() != 0);

	const bool isClosedDoor = g_reed.isClosed();
	qDebug() << "[procFrame] door is Closed?" << isClosedDoor;


	if (isClosedDoor) {
		// 카메라 체크
		if (!cap.isOpened()) {
			qDebug() << "[FRS] The camera has shutdown. it will restart";
			camRestart();
			if (!cap.isOpened()) {
				emit frameReady(QImage());
				return;
			}
		}

		// 프레임 취득
		cv::Mat frame;
		{
			QMutexLocker locker(&frameMutex);
			if (!cap.read(frame) || frame.empty()) {
				qDebug() << "[procFrame] Empty frame";
				emit frameReady(QImage());
				return;
			}
		}

		if (dnnEmbedder_->isTrivialFrame(frame, 1.0, 1.0)) {
			qWarning() << "[procFrame] Trivial frame (mean/std too small). skip";
			emit frameReady(QImage());
			return;
		}

		
		cv::Mat frameCopy = frame.clone();

		// 채널 보정(BGR 기준으로 통일)
		if (frameCopy.channels() == 4) cv::cvtColor(frameCopy, frameCopy, cv::COLOR_BGRA2BGR);
		else if (frameCopy.channels() == 1) cv::cvtColor(frameCopy, frameCopy, cv::COLOR_GRAY2BGR);

		// YuNet 입력 크기 갱신 (프레임 크기 변경 시 필수)
		if (yunet_) {
			const cv::Size cur = frameCopy.size();
			if (cur != yunet_InputSize_) {
				yunet_->setInputSize(cur);
				yunet_InputSize_ = cur;
			}
		}

		// --- YuNet detect ---
		std::vector<FaceDet> faces;
		if (yunet_) {
			cv::Mat dets;
			yunet_->detect(frameCopy, dets); // BGR 그대로 입력 가능
#ifdef DEBUG
			qDebug() << "[Debug] dets type=" << dets.type() << " depth=" << dets.depth() << " channels=" << dets.channels();

			qDebug() << "[A] frameCopy size=" << frameCopy.cols << "x" << frame.rows
				<< " yunet_InputSize=" << yunet_InputSize_.width << "x" << yunet_InputSize_.height;
			for (int i = 0; i < dets.rows; i++) {
				qDebug() << "raw row" << i << ":"
					<< dets.at<float>(i,0) << dets.at<float>(i,1)
					<< dets.at<float>(i,2) << dets.at<float>(i,3)
					<< "score=" << dets.at<float>(i,4);
			}
#endif


			faces = parseYuNet(dets /*scoreThresh=*/);
#ifdef DEBUG
			qDebug() << "[procFrame] raw dets=" << dets.rows
				<< " cols=" << dets.cols
				<< " ch="   << dets.channels()
				<< " total=" << (int)dets.total()
				<< " -> parsed faces=" << faces.size();
#endif

			for (size_t i = 0; i < faces.size(); ++i) {
				const auto& f = faces[i];
				auto inRange = [&] (const cv::Point2f& p) {
					return (p.x >= 0 && p.y >= 0 && p.x < frameCopy.cols && p.y < frameCopy.rows);
				};
				bool boxOk = (f.box.x + f.box.width  <= frameCopy.cols &&
						f.box.y + f.box.height <= frameCopy.rows); 
				bool lmOk = true;
				for (int k = 0; k < 5; k++) lmOk = lmOk && inRange(f.lmk[k]);

#ifdef DEBUG
				qDebug() << "[A] face" << i
					<< "box=" << f.box.x << f.box.y << f.box.width << f.box.height
					<< "score=" << f.score
					<< "boxOk=" << boxOk << "lmOk=" << lmOk;
#endif
			}

			//A3. 시각화 스냅삿
			cv::Mat visA = frameCopy.clone();
			for (const auto& f : faces) {
				cv::rectangle(visA, f.box, {0,255,0}, 2);
				for (int k =0; k < 5; k++) cv::circle(visA, f.lmk[k], 3, {0,255,255}, -1);
			}
			//cv::imwrite("dbg_A_detect_landmarks.jpg", visA);
		}
		//qDebug() << "[Debug] Detector Ready!";

		// 여러 얼굴 중 하나만 유지(가장 중앙+큰 얼굴 선호)
		if (faces.size() > 1) {
			auto rank = [&](const FaceDet& d) {
				double area = static_cast<double>(d.box.area());
				cv::Point2f c(d.box.x + d.box.width*0.5f,
						d.box.y + d.box.height*0.5f);
				cv::Point2f fc(frameCopy.cols*0.5f,
						frameCopy.rows*0.5f);
				double dist = cv::norm(c - fc) /
					std::hypot(static_cast<double>(frameCopy.cols),
							static_cast<double>(frameCopy.rows));
				return area * (1.0 - 0.35*dist); // 중심에서 멀수록 감점
			};
			auto it = std::max_element(
					faces.begin(), faces.end(),
					[&](const FaceDet& a, const FaceDet& b){ return rank(a) < rank(b); });
			FaceDet keep = *it;
			faces.clear();
			faces = { keep };
		}
		//qDebug() << "[Debug] face selecting is done"; 


		// === FSM 스냅샷 ===
		const bool faceFound = !faces.empty();
		setDetectScore(faceFound ? 0.8 : 0.0);
		setRegisterRequested(isRegisteringAtomic.loadRelaxed() != 0);
		setLivenessOk(true);
		setDuplicate(false);

		// -------------------------------
		// 2) 얼굴별 처리: 정렬(우선) -> 폴백(레터박스) -> 임베딩/매칭
		// -------------------------------
		for (const auto& fd : faces)
		{
#ifdef DEBUG
			qDebug() << "[B] align lnput lmk="
				<< fd.lmk[0].x << fd.lmk[0].y << ","
				<< fd.lmk[1].x << fd.lmk[1].y << ","
				<< fd.lmk[2].x << fd.lmk[2].y << ","
				<< fd.lmk[3].x << fd.lmk[3].y << ","
				<< fd.lmk[4].x << fd.lmk[4].y;
			double iod = cv::norm(fd.lmk[0]-fd.lmk[1]);
			qDebug() << "[B] inter-ocular=" << iod << " boxW=" << fd.box.width << " ratio=" << (iod / std::max(1, fd.box.width));
#endif
			// 1) 랜드마크 정렬 우선
			cv::Mat alignedFace = alignBy5pts(frameCopy, fd.lmk, cv::Size(128, 128));
#ifdef DEBUG
			qDebug() << "[B] alignedFace size=" << alignedFace.cols << "x" << alignedFace.rows;

			if (!alignedFace.empty()) {
				cv::imwrite("dbg_B_aligned.jpg", alignedFace);
			}
#endif

			// 2) 실패 시 폴백: 박스 확장 크롭 -> 레터박스 정사각
			if (alignedFace.empty()) {
				cv::Rect roi = expandRect(fd.box, 1.3f, frameCopy.size());
				qDebug() << "[C] fallback roi=" << roi.x  << roi.y << roi.width << roi.height;
				cv::Mat crop = frameCopy(roi).clone();
				if (crop.empty()) continue;
				if (!crop.empty()) {
					//cv::imwrite("dbg_c_crop.jpg", crop);
					alignedFace = letterboxSquare(crop, 128);
					//cv::imwrite("dbg_c_letterbox.jpg", alignedFace);
				}
			}

			if (alignedFace.empty()) continue;

#ifdef DEBUG 
			imwrite("debug_reg_x.jpg", alignedFace);
#endif 

			QString labelText;
			cv::Scalar boxColor;

			if (!wantReg) {
				//qDebug() << "[procFrame] RECOGNITION MODE branch";
				// 인식 품질 게이트 통과 여부
				if (!passQualityForRecog(fd.box, alignedFace)) {
					qDebug() << "[proc] quality fail"; 
					continue;
				}

				const qint64 t_rec0 = QDateTime::currentMSecsSinceEpoch();
				recogResult = handleRecognition(frameCopy, fd.box, alignedFace, labelText, boxColor);

				static Voter  voter({5,3});   // 3/5 합의
				static Thresh th;             // T_out=0.80

				if (recogResult.result == AUTH_SUCCESSED) {
					// user index 얻기: 이미 있으면 그걸 쓰고, 없으면 이름→인덱스 맵을 사용
					int userIdx = recogResult.idx;  // 없으면 적절히 치환
					if (!voter.feed(userIdx, recogResult.sim, th.T_out)) {
#ifdef DEBUG
						qDebug() << "[Decision] Pending by N-of-M: idx=" << userIdx
							<< " s=" << recogResult.sim;
#endif
						setRecogConfidence(recogResult.sim);
						// 합의 전이므로 성공/실패 카운트, 문열림 등 기존 성공 처리로 내려가지 않게
						continue; // 다음 얼굴/프레임으로
					}
				}

				// === FSM 스냅샷: 인식 결과 반영 ===
				if (recogResult.result == AUTH_SUCCESSED) {
					setAllowEntry(true);
					setRecogConfidence(recogResult.sim);
					incAuthStreak();

					//qDebug() << "[procFrame] AuthStreak(" << authStreak_ << ")";
					if (!hasAlreadyUnlocked) {
						authManager.handleAuthSuccess();
						if (authManager.shouldAllowEntry()) {
							qDebug() << "[FaceRecognitionService] Authenticate 5 time success -> Door open!";
							setDoorOpened(true);

							// ===== 감지 전까지 열림 유지 시작 =====
							if (!g_unlockMgr.running()) {
								g_unlockMgr.start();
								qInfo() << "[Door] Unlock started (wait open, then wait close)";
							}

							// DB 로그 저장(JPEG 압축 후 BLOB)
							std::vector<uchar> buf;
							cv::imencode(".jpg", frame, buf);
							QByteArray blob(reinterpret_cast<const char*>(buf.data()),
									static_cast<int>(buf.size()));

							//QSqliteService svc;
							db->insertAuthLog(recogResult.name,
									"Authenticate 5 times success -> Door open",
									QDateTime::currentDateTime(), blob);

							hasAlreadyUnlocked = true;
						}
					}
					resetFailCount();
				} else {
					setRecogConfidence(recogResult.sim);
					incFailCount();
					setDoorOpened(false);
					//authManager.resetAuth(); // (FSM 정책에 맞게 유지/해제)
				}
			} else {
				//qDebug() << "[procFrame] REGISTER MODE branch";
				// 등록 모드
				qDebug() << "[FRS] registration branch entered";
				if (!passQualityForRecog(fd.box, alignedFace)) continue;

				{
#ifdef DEBUG
					const std::string ts = QDateTime::currentDateTime()
						.toString("yyyyMMdd_HHmmss_zzz").toStdString();
					cv::imwrite("dbg_reg_align_" + ts + ".jpg", alignedFace);
#endif

					cv::Rect roi = expandRect(fd.box, 1.0f, frameCopy.size());
					cv::Mat crop = frameCopy(roi).clone();

#ifdef DEBUG
					if (!crop.empty()) cv::imwrite("dbg_reg_crop_" + ts + ".jpg", crop);
#endif
				}
				handleRegistration(frameCopy, fd.box, alignedFace, labelText, boxColor);
			}
		}

		// 얼굴이 하나도 없을 때 신뢰도 낮게 유지(플리커 방지)
		if (!faceFound) setRecogConfidence(0.0);

		// 스트릭 5회 처리
		if (authStreak_ >= 5) {
			resetAuthStreak();
			authManager.resetAuth();
			resetUnlockFlag();
		}

		// (센서 붙기 전 테스트용) 문 상태 유지
		setDoorOpened(true);

		cv::resize(frameCopy, frameCopy, cv::Size(640, 480), 0, 0, cv::INTER_AREA);
		QImage qimg(frameCopy.data, frameCopy.cols, frameCopy.rows, frameCopy.step, QImage::Format_BGR888);
		emit frameReady(qimg.copy());
	}
	else {
		const QString openImgPath = QStringLiteral(IMAGES_PATH) + QStringLiteral(OPEN_IMAGE);
		QImage openImg;

		bool ok = openImg.load(openImgPath);

		if (!ok || openImg.isNull()) {
			QImage fallback(640, 480, QImage::Format_RGB888);
			fallback.fill(Qt::black);
			emit frameReady(fallback.copy());
		}
		else {
			QImage scaled = openImg.scaled(640, 480, Qt::KeepAspectRatio, Qt::SmoothTransformation);
			QImage rgb = scaled.convertToFormat(QImage::Format_RGB888);
        	emit frameReady(rgb.copy());
		}
		emit frameReady(openImg.copy());
	}

	QThread::msleep(1); // UI에 양보
}



// === 스냅샷 API ===
void FaceRecognitionService::setDetectScore(double v)				{ detectScore_ = v; facePresent_ = (v > 0.0); }
void FaceRecognitionService::setRecogConfidence(double v)		    { recogConf_ = v; }
void FaceRecognitionService::setDuplicate(bool v)					{ isDup_ = v; }
void FaceRecognitionService::setRegisterRequested(bool v)           { regReq_ = v; }
void FaceRecognitionService::setLivenessOk(bool v)					{ livenessOk_ = v; }
void FaceRecognitionService::setDoorOpened(bool v)					{ doorOpened_ = v; }
void FaceRecognitionService::incFailCount()							{ failCount_++; }
void FaceRecognitionService::incAuthStreak()						{ authStreak_++; }
void FaceRecognitionService::setAllowEntry(bool v)					{ allowEntry_ = v; }
void FaceRecognitionService::resetFailCount()						{ failCount_ = 0; }
void FaceRecognitionService::resetAuthStreak()						{ authStreak_ = 0;	}

void FaceRecognitionService::onTick() 
{
	FsmContext c;
	c.detectScore					= detectScore_;
	c.recogConfidence			    = recogConf_;
	c.isDuplicate					= isDup_;
	c.registerRequested		        = regReq_;
	c.livenessOk					= livenessOk_;
	c.doorOpened					= doorOpened_;
	c.failCount						= failCount_;
	c.authStreak					= authStreak_;
	c.facePresent					= facePresent_;
	c.allowEntry					= allowEntry_;
	c.nowMs							= monotonic_.elapsed();

	// 상태별 타임아웃 계산 예시:
	// - RECOGNIZING 상태에서 5초 초과 시 timeout = true
	// - LOCKED_OUT에서는 lockoutMs 초과 시 timeout = true
	c.timeout = computeTimeout(c);

	//static int k = 0;
	//if ((++k % 10) ==0) {
	qCDebug(LC_FSM_CTX) 
		//qDebug() 
		<< "ctx detect=" << c.detectScore
		<< "conf="			 << c.recogConfidence
		<< "face="			 << c.facePresent
		<< "dup="				 << c.isDuplicate
		<< "live="			 << c.livenessOk
		<< "fail="			 << c.failCount
		<< "Auth="			 << c.authStreak
		<< "reqReg="		 << c.registerRequested
		<< "allowEntry=" << c.allowEntry
		<< "timeout="		 << c.timeout;
	//}

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

	if (st == RecognitionState::RECOGNIZING) {
		return elapsed > params_.recogTimeoutMs;
	}
	if (st == RecognitionState::LOCKED_OUT) {
		return elapsed > params_.lockoutMs;
	}

	return false;
}


