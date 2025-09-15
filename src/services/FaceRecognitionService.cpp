#include <QImage>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
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
namespace {
	QString g_lockedName;
	double g_lockedSim = 0.0;
}
static bool strongCounterEvidence(const QString& candName, double candSim)
{
    if (g_lockedName.isEmpty()) return true;        // 락 없음 → 자유
    if (candName.isEmpty()) return false;           // Unknown은 약함
    if (candName == g_lockedName) return true;      // 같은 사람 → OK
    return (candSim >= g_lockedSim + 0.12);
}
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
static DoorlockController      g_door(GPIOCHIP, RELAY_GPIO, RELAY_ACTIVE_HIGH);
static ReedSensor              g_reed(GPIOCHIP, REED_GPIO, REED_ACTIVE_HIGH);
static UnlockUntilReed::Opt    g_unlockOpt{/*pollMs*/50, /*hits*/6, /*openTimeoutMs*/5000, /*maxUnlockMs*/10000};
static UnlockUntilReed         g_unlockMgr(&g_door, &g_reed, g_unlockOpt);

using json = nlohmann::json;
namespace fs = std::filesystem;


// === helpers === 
// === 유사도 EMA(지수평활) ===
struct SimEMA {
    double v=0.0; bool init=false;
    double apply(double s, double alpha=0.7) {
        if (!init){ v=s; init=true; }
        else v = alpha*v + (1.0-alpha)*s;
        return v;
    }
};

static constexpr float REC_ENTER	= 0.76f;		// 인식 성공 판단
static constexpr float REC_EXIT		= 0.66f;		// 유지/해제
static constexpr float MIN_MARGIN	= 0.05f;		// 1등-2등 차이 
static SimEMA g_simSmoother;						// 프레임간 스무딩
// === helpers === 


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
}

void FaceRecognitionService::setPresenter(FaceRecognitionPresenter* _presenter)
{
		presenter = _presenter;
}

bool FaceRecognitionService::ensureDir(const QString& dirPath)
{
		QDir d;
		return d.mkpath(dirPath);
}
// === YuNet helpers ====
// Laplacian variance 기반 블러 판정
static inline bool isTooBlurWithThreshold(const cv::Mat& img, double varThreshold = 40.0)
{
    if (img.empty()) return true;

    cv::Mat gray;
    if (img.channels() == 3) {
        cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    } else if (img.channels() == 4) {
        cv::cvtColor(img, gray, cv::COLOR_BGRA2GRAY);
    } else {
        gray = img;
    }

    cv::Mat lap;
    cv::Laplacian(gray, lap, CV_64F);
    cv::Scalar mean, stddev;
    cv::meanStdDev(lap, mean, stddev);

    double variance = stddev.val[0] * stddev.val[0];

    // 디버깅 로그
     qDebug() << "[BlurCheck] variance=" << variance << "thr=" << varThreshold;

    return (variance < varThreshold);
}

static inline bool tooSmallRect(const cv::Rect& r, int minw = 80, int minh = 80) 
{
	return (r.width < minw || r.height < minh);
}

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
    qDebug() << "[BlurCheck] variance=" << lapVar << "thr=" << kBlurThr;
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

    qDebug() << "[Qual:PASS]"
             << "box=" << box.width << "x" << box.height
             << "mean=" << m << "std=" << s << "var=" << lapVar;
    return true;
}

void FaceRecognitionService::init()
{
    std::cout << "Face Recognition Service initiallize!!" << std::endl;
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
		const QString appDir  = QCoreApplication::applicationDirPath();		
    	const QString models = QDir(appDir).filePath(YNMODEL_PATH);
    	const QString unModel = QDir(models).filePath(YNMODEL);

		const std::string model = unModel.toStdString();
		yunet_ = cv::FaceDetectorYN::create(
            model, "", cv::Size(320, 320),
            /*scoreThreshold=*/0.9f,
            /*nmsThreshold=*/0.3f,
            /*topK=*/5000,
            cv::dnn::DNN_BACKEND_OPENCV,
            cv::dnn::DNN_TARGET_CPU
        );
        if (yunet_.empty()) {
            qWarning() << "[FRS] YuNet create failed";
            return false;
        }
        qInfo() << "[FRS] YuNet detector loaded";
        return true;
    } catch (const cv::Exception& e) {
        qWarning() << "[FRS] YuNet load error:" << e.what();
        return false;
    }
}


bool FaceRecognitionService::initializeDnnOnly()
{
	loadDetector();


    // 2) MobileFaceNet (mobileface.onnx) -> 얼굴 인식기
    const QString appDir  = QCoreApplication::applicationDirPath();		
    const QString models = QDir(appDir).filePath(MOBILEFACE_PATH);
    const QString onnxEmb = QDir(models).filePath(MOBILEFACE_MODEL);

    if (!QFile::exists(onnxEmb)) {
        SystemLogger::error("FRS", QString("Embedder not found: %1").arg(onnxEmb));
        return false;
    }

	// Embedder 옵션
	Embedder::Options opt;
	opt.modelPath   = onnxEmb;		// 얼굴 임베딩 생성에 사용할 ONNX 모델 
	opt.inputSize   = 128;//112;			// 모델 입력 이미지 크기 (128x128 권장)
	opt.useRGB      = true;			// 입력 이미지가 RGB 순서인지 여부
	opt.norm        = Embedder::Options::Norm::MinusOneToOne;	// 입력 정규화 방식 설정
	// opt.norm     = Embedder::Options::Norm::ZeroToOne;		// 모델 스펙에 따라 결정

	
	dnnEmbedder_ = std::make_unique<Embedder>(opt);
	if (!dnnEmbedder_ || !dnnEmbedder_->isReady()) {
		qWarning() << "[DNN] not ready, fallback LBPH";
		SystemLogger::error("DNN", "DNN not ready");
		dnnEmbedder_.reset();
	}  
	

	// 임베딩 파일 경로 세팅 및 로드
	const QString embPath = QStringLiteral(EMBEDDING_JSON_PATH) + QStringLiteral(EMBEDDING_JSON);
	embeddingsPath_ = embPath.toStdString();

	// 3) Embedding.json 파일 로드 -> 사용자 DB
	if (loadEmbeddingsFromFile()) {
		qInfo() << "[DNN] Loaded embeddings:" << (int)gallery_.size()
			<< "users from" << embPath;
	} else {
		qWarning() << "[DNN] No embeddings loaded (file may not exist):" << embPath;
			gallery_.clear();
	}

	// 사용자 ID 초기화
    rebuildNextIdFromGallery();

    SystemLogger::info("FRS", "DNN-only pipeline ready");
	qDebug() << "[FRS] DNN-only pipeline ready. UltraFace + MobileFaceNet loaded.";

    return true;
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

// === 카메라 열기 ===
void FaceRecognitionService::openCamera()
{
		qDebug() << "[FaceRecognitionService] openCamera is called";

		// 0) preprocessing
		//cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 0.25);		// 노출을 직접 제어  0.75(자동) → 0.25(수동, 드라이버마다 상이)
		//cap.set(cv::CAP_PROP_EXPOSURE, -3);				// 노출 값 직접 설정 값이 작을수록 이미지 밝기가 어두워짐 (-6 ~ -3 범위 테스트)
		//cap.set(cv::CAP_PROP_AUTO_WB, 0);				// 자동 화이트 발런스 끄기 
		//cap.set(cv::CAP_PROP_WB_TEMPERATURE, 4500);		// 화이트 발런스 4500(노란색 성분) 로 설정 

		
		try {
				cap.open(CAM_NUM);
				if (!cap.isOpened()) {
						std::cout << "[FRS] Failed to camera open!!" << std::endl;
                        SystemLogger::error("FRS", "Failed to camera open.");
				}
				std::cout << "[FRS] Camera be opend!!" << std::endl;
                SystemLogger::info("FRS", "Success to camera open.");
		} catch(const cv::Exception& e) {
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
    qDebug() << "[FaceRecognitionService] Registration has begun";
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

    isRegisteringAtomic.storeRelaxed(1);

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

// 로컬 헬퍼: L2 정규화(0 division 방지 포함)
static inline void l2normInPlace(std::vector<float>& v) {
    double s = 0.0;
    for (float x : v) s += double(x) * x;
    s = std::sqrt(std::max(1e-12, s));
    for (float& x : v) x = float(x / s);
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

            // dim 일관성 체크
            if (dim == 0) dim = int(u.embedding.size());
            else if (int(u.embedding.size()) != dim) {
                qWarning() << "[DNN] load: dimension mismatch for" << name
                           << "expected" << dim << "got" << int(u.embedding.size());
                return false;
            }

            // L2 정규화(로드 시 1회) - 저장 시에도 정규화했더라도 한 번 더 걸어도 무해
            l2normInPlace(u.embedding);
            tmp.push_back(std::move(u));
            return true;
        };

        // ----- 신 스키마 -----
        if (root.is_object() && root.contains("items") && root["items"].is_array()) {
            const int ver   = root.value("version", 1);
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
        // ----- 구 스키마(루트 배열) -----
        else if (root.is_array()) {
            for (const auto& o : root) {
                if (!o.is_object()) continue;
                if (!o.contains("id") || !o.contains("name") || !o.contains("embedding")) continue;

                const int id = o["id"].get<int>();
                const QString name = QString::fromStdString(o["name"].get<std::string>());
                if (!pushItem(id, name, o["embedding"])) {
                    qWarning() << "[DNN] load(legacy): skip item" << name;
                    continue;
                }
            }
            qInfo() << "[DNN] Loaded (legacy)" << (int)tmp.size() << "users, dim=" << dim;
        }
        // ----- 형식 불일치 -----
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


// === 중볻된 얼굴인지 체크 ===
bool FaceRecognitionService::isDuplicateFaceDNN(const cv::Mat& alignedFace, int* dupIdOut, float* simOut) const
{
		if (!dnnEmbedder_ || gallery_.empty()) return false;
		std::vector<float> emb;
		if (!dnnEmbedder_->extract(alignedFace, emb) || emb.empty()) return false;

		int bestId = -1;
		float bestSim = -1.f;

		for (const auto& u : gallery_) {
				if (u.embedding.size() != emb.size()) continue;
				float sim = Embedder::cosine(emb, u.embedding);
				if (sim > bestSim) { 
						bestSim = sim;
						bestId = u.id;
				}
		}

		if (dupIdOut) *dupIdOut = bestId;
		if (simOut)		*simOut		= bestSim;

		return (bestId >= 0 && bestSim >= 0.70f);
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
    qDebug() << "[FaceRecognitionService]" << registeringUserName_  << " image has been saved and loaded";
}

void FaceRecognitionService::clearRegistrationBuffers()
{
		regEmbedsBuffers_.clear();
		registeringUserId_ = -1;
}

void FaceRecognitionService::finalizeRegistration()
{
    qDebug() << "[FRS] finalizeRegistration()";


    // 1) 취소 체크
    if (m_cancelReg.load(std::memory_order_relaxed)) {
        qDebug() << "[FRS] canceled at finalizeRegistration()";
        forceAbortRegistration();
        clearRegistrationBuffers();
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
		
    if (used == 0) {
        qWarning() << "[FRS] No embeddings collected";
        forceAbortRegistration();
        clearRegistrationBuffers();
        emit registrationCompleted(false, QStringLiteral("실패"));
        return;
    }

    for (auto& v : meanEmb) v /= static_cast<float>(used);

    // 3) gallery_ 업데이트 (registeringUserId_는 UI/흐름에서 미리 지정)
    if (registeringUserId_ < 0) {
        // fallback: 새 ID 만들거나, UI에서 받은 ID 사용
        registeringUserId_ = static_cast<int>(QDateTime::currentMSecsSinceEpoch() & 0x7FFFFFFF);
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
    recogResult_t rv{};
    QString name  = "Unknown";
    bool matched  = false;
    float cosBest = -1.f;

    if (!dnnEmbedder_ || gallery_.empty() || alignedFace.empty()) {
        rv.name="Unknown"; rv.sim=-1.f; rv.result=AUTH_FAILED;
        labelText="Unknown"; boxColor=cv::Scalar(0,0,255);
        return rv;
    }

    // ---------------------------
    // 0) 보조 함수
    // ---------------------------
    auto l2norm = [](std::vector<float>& v){
        double s=0; for(float x:v) s+=double(x)*double(x);
        s = std::sqrt(std::max(1e-12, s));
        for(float& x:v) x=float(x/s);
    };
    auto cosine = [&](const std::vector<float>& a, const std::vector<float>& b)->double{
        // b 정규화 보정
        double sb=0; for(float x:b) sb+=double(x)*double(x);
        sb = std::sqrt(std::max(1e-12, sb));
        double dot=0; const size_t n=std::min(a.size(), b.size());
        for(size_t i=0;i<n;++i) dot += double(a[i]) * double(b[i]/sb);
        if(dot>1.0) dot=1.0; if(dot<-1.0) dot=-1.0;
        return dot;
    };
    auto clamp = [](double x, double a, double b){ return std::max(a,std::min(b,x)); };

    // ---------------------------
    // 1) 임베딩 (원본 + 좌우 플립)
    // ---------------------------
    std::vector<float> emb, embFlip;
    if(!dnnEmbedder_->extract(alignedFace, emb) || emb.empty()){
        rv.name="Unknown"; rv.sim=-1.f; rv.result=AUTH_FAILED;
        labelText="Unknown"; boxColor=cv::Scalar(0,0,255); return rv;
    }
    l2norm(emb);

    cv::Mat flipped; cv::flip(alignedFace, flipped, 1);
    if(dnnEmbedder_->extract(flipped, embFlip) && !embFlip.empty()){
        l2norm(embFlip);
    } else {
        embFlip.clear(); // 실패 시 비활성
    }

    // ---------------------------
    // 2) 간이 yaw 추정(비대칭도)
    //     - 얼굴 좌/우 반쪽의 유사도를 측정 (MAE)
    //     - 값이 클수록 yaw 큼(측면)
    // ---------------------------
    cv::Mat g; cv::cvtColor(alignedFace, g, cv::COLOR_BGR2GRAY);
    cv::resize(g, g, {128,128});
    cv::Mat left  = g(cv::Rect(0,0,64,128));
    cv::Mat right = g(cv::Rect(64,0,64,128));
    cv::Mat rightFlip; cv::flip(right, rightFlip, 1);        // 좌우 맞춘 후 비교
    cv::Mat diff; cv::absdiff(left, rightFlip, diff);
    double mae = cv::mean(diff)[0] / 255.0;                   // 0~1
    // 경험치: 0.06~0.20 구간 사용
    double yawScore = clamp((mae - 0.06) / (0.20 - 0.06), 0.0, 1.0);

    // ---------------------------
    // 3) 거리 기반(얼굴 크기) + yaw 기반 동적 임계/마진
    // ---------------------------
    const int fmin = std::min(face.width, face.height);
    double tSize = clamp((fmin - 140.0) / (360.0 - 140.0), 0.0, 1.0);

    // (가까움/정면) 기준
    const double THR_NEAR   = 0.66;
    const double MAR_NEAR   = 0.12;
    // (멀리/고개 듦) 기준
    const double THR_FAR    = 0.60;
    const double MAR_FAR    = 0.15;
    // (측면 yaw 큼) 기준 — 더 낮은 임계, 더 큰 마진
    const double THR_PROFILE= 0.69;
    const double MAR_PROFILE= 0.18;

    // 크기 보정 1차
    double thr_size = THR_FAR * (1.0 - tSize) + THR_NEAR * tSize;
    double mar_size = MAR_FAR * (1.0 - tSize) + MAR_NEAR * tSize;

    // yaw 보정 2차 (profile 쪽으로 추가 보간)
    //double THRESH_COS = THR_PROFILE * yawScore + thr_size * (1.0 - yawScore);
    //double MIN_MARGIN = MAR_PROFILE * yawScore + mar_size * (1.0 - yawScore);
	const double YAW_W = 0.35;  // 0~1 (야각 영향도를 60%만 반영)
	double THRESH_COS = thr_size * (1.0 - YAW_W * yawScore)
                  + THR_PROFILE * (YAW_W * yawScore);
	double MIN_MARGIN = mar_size * (1.0 - 0.50 * yawScore)    // 마진은 절반만 보정
                  + MAR_PROFILE * (0.50 * yawScore);
	THRESH_COS = std::min(THRESH_COS, 0.655); 
	MIN_MARGIN = std::max(0.12, MIN_MARGIN);


    // ---------------------------
    // 4) 갤러리 Top-2 (원본/플립 중 최대 유사도 사용)
    // ---------------------------
    std::pair<int,double> best{-1,-1.0}, second{-1,-1.0};
    for (int i=0; i<(int)gallery_.size(); ++i) {
        const std::vector<float>& gv = getVec(gallery_[i]);
        double s0 = cosine(emb, gv);
        double s1 = embFlip.empty() ? -1.0 : cosine(embFlip, gv);
        double s  = std::max(s0, s1);     // 좌우 플립 중 더 좋은 값 채택

        if (s > best.second) { second = best; best = {i, s}; }
        else if (s > second.second) { second = {i, s}; }
    }

    cosBest = float(best.second);
    const bool marginOk = (second.first < 0) ? true : ((best.second - second.second) >= MIN_MARGIN);
    const bool isKnown  = (best.second >= THRESH_COS) && marginOk;

    qDebug() << "[YawThr] fmin=" << fmin
             << "mae=" << mae << "yaw=" << yawScore
             << "thr=" << THRESH_COS << "margin=" << MIN_MARGIN
             << "best=" << best.second << "second=" << second.second
             << "diff=" << (best.second - second.second)
             << "known=" << isKnown;

    if (isKnown) {
        name = getName(gallery_[best.first]);
        matched = true;
    } else {
        name = "Unknown";
        matched = false;
        setAllowEntry(false);
        //authManager.resetAuth();
        hasAlreadyUnlocked = false;
    }

    // ---------------------------
    // 5) 시각화
    // ---------------------------
    boxColor  = matched ? cv::Scalar(0,255,0) : cv::Scalar(0,0,255);
    labelText = matched
        ? QString("%1  cos=%2").arg(name).arg(QString::number(cosBest,'f',3))
        : QString("Unknown  cos=%1").arg(QString::number(cosBest,'f',3));

    drawTransparentBox(frame, face, boxColor, 0.3);
    drawCornerBox(frame, face, boxColor, 2, 25);
    putText(frame, labelText.toStdString(), cv::Point(face.x, face.y - 10),
            cv::FONT_HERSHEY_DUPLEX, 0.9, boxColor, 2);

    if (!alignedFace.empty()) {
        cv::Mat thumb; cv::resize(alignedFace, thumb, {96,96});
        cv::rectangle(thumb, {0,0,thumb.cols-1,thumb.rows-1}, boxColor, 2);
        cv::Rect roi(10,10,thumb.cols,thumb.rows);
        if (roi.x>=0 && roi.y>=0 && roi.x+roi.width<=frame.cols && roi.y+roi.height<=frame.rows)
            thumb.copyTo(frame(roi));
    }

    rv.name = name;
    rv.sim  = cosBest;
    rv.result = matched ? AUTH_SUCCESSED : AUTH_FAILED;
    return rv;
}



static inline float cosineDot(const std::vector<float>& a, const std::vector<float>& b){
    size_t n = std::min(a.size(), b.size());
    double dot=0.0;
    for (size_t i=0;i<n;++i) dot += double(a[i])*double(b[i]);
    if (dot >  1.0) dot =  1.0;
    if (dot < -1.0) dot = -1.0;
    return float(dot);
}

MatchTop2 FaceRecognitionService::bestMatchTop2(const std::vector<float>& emb) const {
    MatchTop2 r;
    if (gallery_.empty() || emb.empty()) return r;

    const int din = int(emb.size());
    int mismatch = 0;

    for (int i=0; i<(int)gallery_.size(); ++i) {
        const auto& u = gallery_[i];
        if ((int)u.embedding.size() != din) { ++mismatch; continue; }
        float s = cosineDot(emb, u.embedding); // 갤러리는 load 시 L2 정규화 완료 가정
        if (s > r.bestSim) { r.secondIdx=r.bestIdx; r.secondSim=r.bestSim; r.bestIdx=i; r.bestSim=s; }
        else if (s > r.secondSim) { r.secondIdx=i; r.secondSim=s; }
    }
    if (mismatch>0) {
        qWarning() << "[FRS] bestMatchTop2: dim mismatch=" << mismatch << " input_dim=" << din;
    }
    return r;
}

auto dumpShape = [](const cv::Mat& m) {
    std::ostringstream oss;
    oss << "dims=" << m.dims << " sizes=[";
    for (int i=0;i<m.dims;++i){ oss << m.size[i]; if(i+1<m.dims) oss<<",";}
    oss << "] type=" << m.type() << " total=" << (long long)m.total();
    return QString::fromStdString(oss.str());
};

// === YuNet helpers ====

struct FaceDet {
	cv::Rect box;
	std::array<cv::Point2f, 5> lmk;				// leftEye, right Eye, nose, mouthL, mouthR
	float score;
};

// ArcFace 112x112 템플릿 (출력은 128로 warp)
static const std::array<cv::Point2f, 5> kDst5_112 = {{
	{38.2946f, 51.6963f}, {73.5318f, 51.5014f},
	{56.0252f, 71.7366f}, {41.5493f, 92.3655f},
	{70.7299f, 92.2041f}
}};


// 유틸: YuNet 결과 → Rect 벡터
static inline std::vector<FaceDet> parseYuNet(const cv::Mat& dets, float scoreThresh=0.9f) 
{
    std::vector<FaceDet> out;
    if (dets.empty()) return out;

    for (int i = 0; i < dets.rows; ++i) {
        const float* r = dets.ptr<float>(i);
        float x = r[0], y = r[1], w = r[2], h = r[3], s = r[4];			 
        if (s < scoreThresh);

		FaceDet d;
		d.box = cv::Rect(cv::Point2f(x,y), cv::Size2f(w,h));
		d.lmk = { cv::Point2f(r[5], r[6]), 
				  cv::Point2f(r[7], r[8]),
				  cv::Point2f(r[9], r[10]), 
				  cv::Point2f(r[11], r[12]),
				  cv::Point2f(r[13], r[14]) };
		d.score = s;
		out.push_back(d);
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

static inline cv::Mat alignBy5pts(const cv::Mat& srcBgr,
                                  const std::array<cv::Point2f,5>& src5,
                                  const cv::Size& outSize = {128, 128})
{
    // std::array -> std::vector 변환
    std::vector<cv::Point2f> src(src5.begin(), src5.end());
    std::vector<cv::Point2f> dst(kDst5_112.begin(), kDst5_112.end());

    // 112 템플릿 그대로 사용, 출력만 outSize로 워프
    cv::Mat M = cv::estimateAffinePartial2D(src, dst, cv::noArray(),
                                            cv::RANSAC, 3.0, 2000, 0.99);

    cv::Mat out;
    if (!M.empty())
        cv::warpAffine(srcBgr, out, M, outSize, cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    return out;
}

void FaceRecognitionService::procFrame()
{
    recogResult_t recogResult;
    const bool wantReg = (isRegisteringAtomic.loadRelaxed() != 0);

    // 카메라 체크
    if (!cap.isOpened()) {
        qDebug() << "[FRS] The camera has shutdown. it will restart";
        //openCamera();
        if (!cap.isOpened()) return;
    }

    // 프레임 취득
    cv::Mat frame, frameCopy;
    {
        QMutexLocker locker(&frameMutex);
        if (!cap.read(frame) || frame.empty()) return;
    }
    frameCopy = frame.clone();

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
        faces = parseYuNet(dets, /*scoreThresh=*/0.9f);
    }

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
        // 1) 랜드마크 정렬 우선
        cv::Mat alignedFace = alignBy5pts(frameCopy, fd.lmk, cv::Size(128, 128));

        // 2) 실패 시 폴백: 박스 확장 크롭 -> 레터박스 정사각
        if (alignedFace.empty()) {
            cv::Rect roi = expandRect(fd.box, 1.3f, frameCopy.size());
            cv::Mat crop = frameCopy(roi).clone();
            if (crop.empty()) continue;
            alignedFace = letterboxSquare(crop, 128);
        }
        if (alignedFace.empty()) continue;

        QString labelText;
        cv::Scalar boxColor;

        if (!wantReg) {
            // 인식 품질 게이트 통과 여부
            if (!passQualityForRecog(fd.box, alignedFace)) continue;

            recogResult = handleRecognition(frameCopy, fd.box, alignedFace, labelText, boxColor);

            // === FSM 스냅샷: 인식 결과 반영 ===
            if (recogResult.result == AUTH_SUCCESSED) {
                setAllowEntry(true);
                setRecogConfidence(recogResult.sim);
                incAuthStreak();

                qDebug() << "[FSM] AuthStreak(" << authStreak_ << ")";
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
            // 등록 모드
            qDebug() << "[FRS] registration branch entered";
            if (!passQualityForRecog(fd.box, alignedFace)) continue;
            handleRegistration(frameCopy, fd.box, alignedFace, labelText, boxColor);
        }
    }

    // 얼굴이 하나도 없을 때 신뢰도 낮게 유지(플리커 방지)
    if (!faceFound) setRecogConfidence(0.0);

    // 스트릭 5회 처리
    if (authStreak_ >= 5) {
        qDebug() << "[FRS] init outside loop";
        resetAuthStreak();
        authManager.resetAuth();
        resetUnlockFlag();
    }

    // (센서 붙기 전 테스트용) 문 상태 유지
    setDoorOpened(true);

    QImage qimg(frameCopy.data, frameCopy.cols, frameCopy.rows, frameCopy.step, QImage::Format_BGR888);
    emit frameReady(qimg.copy());
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


