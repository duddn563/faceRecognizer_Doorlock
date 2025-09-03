#include <QImage>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <memory>

#include "FaceRecognitionService.hpp"
#include "presenter/FaceRecognitionPresenter.hpp"
#include "MainWindow.hpp"
#include "hw/DoorlockController.hpp"
#include "hw/ReedSensor.hpp"
#include "hw/UnlockUntilReed.hpp"
#include "fsm/fsm_logging.hpp"
#include "include/common_path.hpp"

// #define DEBUG 
// #define USE_LBPH_RECOGNIZER

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

// 간단한 OSD 로그 헬퍼(선택)
static inline void drawOsd(cv::Mat& frame, const std::string& msg, cv::Point org={10,30}) {
    cv::putText(frame, msg, org, cv::FONT_HERSHEY_PLAIN, 1.4, cv::Scalar(0,255,255), 2);
}


using json = nlohmann::json;


namespace fs = std::filesystem;

FaceRecognitionService::FaceRecognitionService(QObject* parent, FaceRecognitionPresenter* presenter) : QObject(parent), presenter(presenter)
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

void FaceRecognitionService::init()
{
    std::cout << "Face Recognition Service initiallize!!" << std::endl;
    openCamera();

    loadDetector();

#ifdef USE_LBPH_RECOGNIZER
    initializeLBPH();
#endif
    initializeDnn();


		registerExistingUser();

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
		int maxId = 0;
		for (const auto& u : gallery_) {
				if (u.id > maxId) maxId = u.id;
		}

		if (maxId == 0) {
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
        faceDetector.load(FACEDETECTOR);
        eyesDetector.load(EYESDETECTOR);
        return true;
    } catch (Exception &e) {
        qDebug() << "[FRS] Detector load failed..(" << e.what() << ")";
        return false;
    }
}

#ifdef USE_LBPH_RECOGNIZER
// LBPH 초기화 + LBPH 파일 로드
void FaceRecognitionService::initializeLBPH()
{
		try {

				// 2) Face Recognizer create
				recognizer = face::LBPHFaceRecognizer::create();
				if (!recognizer) {
						qDebug() << "[FRS] Failed to create LBPH recognizer";
						return;
				}

				// 3) Face Recognizer read model file
				if (fs::exists(LBPH_MODEL_FILE)) {
						recognizer->read(LBPH_MODEL_FILE);
						qDebug() << "[FaceRecognitionService] Recognizer is loaded.";
				}
				else {
						cout << "No existing model found. Will create a new model." << endl;
				}
		}
		catch (Exception &e) {
				qDebug() << "[FRS] Failed to initialize LBPH(" << e.what() << ")";				
				return;
		}

}
#endif

// DNN 초기화 + 임베딩 파일 로드
void FaceRecognitionService::initializeDnn()
{
		const QString appDir = QCoreApplication::applicationDirPath();
		const QString models = QDir(appDir).filePath(MOBILEFACE_PATH);
		const QString dataDir = QDir(appDir).filePath(EMBEDDING_JSON_PATH);

		ensureDir(models);
		ensureDir(dataDir);

		// 현재 사용하고 있는 ONNX (128x128 입력, 256차원 출력)
		const QString onnxPath = QDir(models).filePath(MOBILEFACE_MODEL);

		// Embedder 옵션
		Embedder::Options opt;
		//opt.modelPath = QDir(QCoreApplication::applicationDirPath()).filePath("models/mobileface.onnx");
		opt.modelPath   = onnxPath;
		opt.inputSize   = 128;
		opt.useRGB      = true;
        opt.norm        = Embedder::Options::Norm::MinusOneToOne;
        // opt.norm     = Embedder::Options::Norm::ZeroToOne;

		dnnEmbedder_ = std::make_unique<Embedder>(opt);
		if (!dnnEmbedder_->isReady()) {
				qWarning() << "[DNN] not ready, fallback LBPH";
				dnnEmbedder_.reset();
		} else {
				qInfo() << "[DNN] MobileFaceNet ready.";
		}

		// 임베딩 파일 경로 세팅 및 로드
		const QString embPath = QDir(dataDir).filePath(EMBEDDING_JSON);
		embeddingsPath_ = embPath.toStdString();

		if (loadEmbeddingsFromFile()) {
				qInfo() << "[DNN] Loaded embeddings:" << (int)gallery_.size()
								<< "users from" << embPath;
		} else {
				qWarning() << "[DNN] No embeddings loaded (file may not exist):" << embPath;
				gallery_.clear();
		}

		rebuildNextIdFromGallery();
}

void FaceRecognitionService::stop()
{
		std::cout << "Face Recognition Service stop" << std::endl;
		// isRunning값은 현재 시작할 때 초기화 되고 있지 않음
		if (!isRunning) return;

		isRunning = false;

        if (cap.isOpened()) {
				cap.release();
		}

		currentState = RecognitionState::IDLE;
		registeringUserName_.clear();

		this->moveToThread(QCoreApplication::instance()->thread());
}

void FaceRecognitionService::openCamera()
{
		qDebug() << "[FaceRecognitionService] openCamera is called";

		// 0) preprocessing
		cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 0.25);		// 0.75(자동) → 0.25(수동, 드라이버마다 상이)
		cap.set(cv::CAP_PROP_EXPOSURE, -3);						// -6 ~ -3 범위 테스트
		cap.set(cv::CAP_PROP_AUTO_WB, 0);
		cap.set(cv::CAP_PROP_WB_TEMPERATURE, 4500);

		
		try {
				cap.open(CAM_NUM);
				if (!cap.isOpened()) {
						std::cout << "[FRS] Failed to camera open!!" << std::endl;
				}
				std::cout << "[FRS] Camera be opend!!" << std::endl;
		} catch(const cv::Exception& e) {
				std::cout << "[FRS] OpenCV exception: " << e.what() << std::endl;
		}
}


#ifdef USE_LBPH_RECOGNIZER
void FaceRecognitionService::reloadModel()
{
		if (!recognizer) 
				recognizer = face::LBPHFaceRecognizer::create();

		try {
				if (fs::exists(LBPH_MODEL_FILE)) {
						recognizer->read(LBPH_MODEL_FILE);
						qDebug() << "[FRS] Recognizer is reloaded.";
				}
				else {
						qDebug() << "[FRS] Failed to reload LBPH model";
				}
		} catch (cv::Exception& e) {
				qDebug() << "[FRS] Failed to reload LBHP Model(" << e.what() << ")";
		}
}
#endif

void FaceRecognitionService::registerExistingUser()
{
		bool rc = 0;
		size_t firstUnderscore, secondUnderscore, thirdUnderscore;
		string name;

		rc = fs::is_directory(USER_FACES_DIR);

		if (rc) {
				for (const auto& entry : fs::directory_iterator(USER_FACES_DIR)) {
						string fname = entry.path().filename().string();				// format: face_3_GS_1.png
						if (!fname.empty()) {
								firstUnderscore	= fname.find('_');
								secondUnderscore = fname.find('_', firstUnderscore + 1); 



								if (firstUnderscore == string::npos || secondUnderscore == string::npos) {
										std::cout << "[" << __func__ <<  "] " << fname << "is invalid file name format!!" << std::endl;
										continue;
								}

								int		label = std::stoi(fname.substr(firstUnderscore + 1, secondUnderscore - firstUnderscore - 1));

#ifdef DEBUG
								thirdUnderscore  = fname.find('_', secondUnderscore + 1);

								if (thirdUnderscore == string::npos) {
										std::cout << "[" << __func__ <<  "] " << fname << "is invalid file name format!!" << std::endl;
										continue;
								}
								string name = fname.substr(secondUnderscore + 1, thirdUnderscore - secondUnderscore - 1);

								printf ("[%s] name: %s, label: %d\n", __func__, name.c_str(), label);
#endif

								Mat gray = imread(entry.path().string(), IMREAD_GRAYSCALE);
								cv::resize(gray, gray, Size(200, 200));

								storedFaces[label].push_back(gray);

						}
				}

				qDebug() << "[FaceRecognitionService] Existing user is stored.";
		}
		else {
				fs::create_directory(USER_FACES_DIR);
				std::cout << "User face directory does not exist or is not a directory." << std::endl;
		}

}

Mat FaceRecognitionService::alignAndNormalizeFace(const Mat& grayFrame, const Rect& faceRect)
{
		Mat faceROI = grayFrame(faceRect).clone();

    // 눈 검출
    vector<Rect> eyes;
    eyesDetector.detectMultiScale(faceROI, eyes, 1.1, 10, 0, Size(20, 20));

    if (eyes.size() < 2) return {};  // 눈이 2개 미만이면 실패

    // 눈 2개를 좌/우로 정렬
    Point eye1 = (eyes[0].x < eyes[1].x) ? eyes[0].tl() : eyes[1].tl();
    Point eye2 = (eyes[0].x < eyes[1].x) ? eyes[1].tl() : eyes[0].tl();

    // 눈 중심 계산
    eye1 += Point(eyes[0].width/2, eyes[0].height/2);
    eye2 += Point(eyes[1].width/2, eyes[1].height/2);

    // 각도 계산
    double dy = eye2.y - eye1.y;
    double dx = eye2.x - eye1.x;
    double angle = atan2(dy, dx) * 180.0 / CV_PI;

    // 얼굴 중앙 기준 회전
    Point2f center(faceROI.cols/2.0F, faceROI.rows/2.0F);
    Mat rot = getRotationMatrix2D(center, angle, 1.0);

    Mat aligned;
    warpAffine(faceROI, aligned, rot, faceROI.size());

    // 밝기 보정 + 크기 조정
    equalizeHist(aligned, aligned);
    cv::resize(aligned, aligned, Size(200, 200));

    //cout << "[" << __func__ << "] Face is aligned and normalized!!" << endl;
    return aligned;
}

#ifdef USE_LBPH_RECOGNIZER
void FaceRecognitionService::trainOrUpdateModel(const vector<Mat>& images, const vector<int>& labels)
{
    if (!recognizer) {
        recognizer = face::LBPHFaceRecognizer::create();
    }

    if (fs::exists(LBPH_MODEL_FILE)) {
        recognizer->update(images, labels);
        LOG_INFO("Update existing face recognition model.");
    }
    else {
        recognizer->train(images, labels);
        LOG_INFO("Trained new face recognition model.");
    }

    recognizer->save(LBPH_MODEL_FILE);
    qDebug() << "✅ 모델이 저장되었습니다: " << LBPH_MODEL_FILE;
}
#endif

// ---------- 추가: 취소/강제 종료 API ----------
void FaceRecognitionService::cancelRegistering() 
{
    m_cancelReg.store(true, std::memory_order_relaxed);
}


void FaceRecognitionService::forceAbortRegistration() {
    // 1) 진행 중 플래그/카운터 정리
    isRegisteringAtomic.storeRelaxed(0);
    setRegisterRequested(false);
    captureCount = 0;


    registeringUserName_ = "\0";
    registeringUserId_	 = 0;

    regImageBuffers_.clear();
    regEmbedsBuffers_.clear();

    cancelRegistering();

    // (필요 시) 카메라/타이머/쓰레드 자원 정리 추가
}

void FaceRecognitionService::startRegistering(const QString& name)
{
    qDebug() << "[FaceRecognitionService] Registration has begun";
    captureCount = 0;

    registeringUserName_ = name;
    registeringUserId_ = nextSequentialId();
    regEmbedsBuffers_.clear();

    // 새 세션 시작 시 취소 플래그 리셋
    m_cancelReg.store(false, std::memory_order_relaxed);

    setRegisterRequested(true);
    isRegisteringAtomic.storeRelaxed(1);

    qInfo() << "[FRS] StartRegistering id=" << registeringUserId_
               << " name=" << name;
}

QString FaceRecognitionService::getUserName() { return registeringUserName_; }

// gallery_ -> 파일 (안전하게 임시 파일에 쓴 뒤 rename) 
bool FaceRecognitionService::saveEmbeddingsToFile()
{
		try {
				json j = json::array();
				for (const auto& u : gallery_) {
						nlohmann::json o = nlohmann::json::object();
						o["id"]					= u.id;
						o["name"]				= u.name.toUtf8().constData();
						o["embedding"]	= u.embedding;
						j.push_back(std::move(o));

				}

				// atomic write: temp -> rename
				const QString qpath = QString::fromStdString(embeddingsPath_);
				const QString qtmp  = qpath + ".tmp";

				{
						std::ofstream ofs(qtmp.toStdString(), std::ios::trunc);
						if (!ofs.is_open()) return false;
						ofs << j.dump(2);
				}


				QFile::remove(qpath);	
				if (!QFile::rename(qtmp, qpath)) {
						// rename 실패 시 직접 덮어쓰기 시도
						QFile::remove(qtmp);
						return false;
				}
				
				return true;
		} catch (const std::exception& e) { 
				qWarning() << "[DNN] saveEmbeddingsToFile error:" << e.what();
				return false; 
		}
}

// ------- 로드 -------- 
bool FaceRecognitionService::loadEmbeddingsFromFile()
{
		gallery_.clear();
		std::ifstream ifs(embeddingsPath_);
		if (!ifs.is_open()) return false;		// 첫 실행 시 파일이 없을 수도 있음

		try {
				json j; 
				ifs >> j;
				if (!j.is_array()) return false;

				for (const auto& o : j) {
						UserEmbedding u;
						u.id = o.at("id").get<int>();
						u.name = QString::fromStdString(o.at("name").get<std::string>());
						u.embedding = o.at("embedding").get<std::vector<float>>();

						if (!u.embedding.empty()) gallery_.push_back(std::move(u));
				}

				return true;
		} catch (const std::exception& e) { 
				qWarning() << "[DNN] loadEmbeddingsFromFile parse error:" << e.what();
				gallery_.clear();
				return false; 
		}
}

MatchResult FaceRecognitionService::bestMatch(const std::vector<float>& emb) const
{
		MatchResult r;
		for (const auto& u : gallery_) {
				if (u.embedding.size() != emb.size()) continue;
				float s = Embedder::cosine(emb, u.embedding);
				if (s > r.sim) {
						r.sim = s;
						r.id = u.id;
						r.name = u.name;
				}
		}

		return r;
}


// ----- 등록: 여러장 평균 ----
/*
bool FaceRecognitionService::registerUserSamples(int userId, QString userName, const std::vector<cv::Mat>& faceSamples)
{
		if (!dnnEmbedder_ || faceSamples.empty()) return false;

		std::vector<float> meanEmb;		// 차원은 첫 추출에서 자동 결정
		int used = 0;

		for (const auto& roi : faceSamples) {
				std::vector<float> emb;
				if (dnnEmbedder_->extract(roi, emb) && !emb.empty()) {
						if (meanEmb.empty()) meanEmb.assign(emb.size(), 0.0f);
						if (emb.size() != meanEmb.size()) continue;		// 차원 불일치
						for (size_t i = 0; i < emb.size(); ++i) meanEmb[i] += emb[i];
						++used;
				}
		}

		if (used == 0) return false;
		for (float& v : meanEmb) v /= static_cast<float>(used);

		// 기존 사용자면 갤러리 업데이트, 없으면 추가
		auto it = std::find_if(gallery_.begin(), gallery_.end(),
													 [&] (const UserEmbedding& u) { return u.id == userId; });
		if (it == gallery_.end()) {
				//gallery_.push_back({userId, std::move(meanEmb)});
				// registerUserSamples(userId, userName, ...)
				UserEmbedding ue;
				ue.id = userId;
				ue.name = userName;
				ue.embedding = std::move(meanEmb);
				gallery_.push_back(std::move(ue));
		} else {										
				it->embedding = std::move(meanEmb);
		}


		saveEmbeddingsToFile();
		return true;
}
*/

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


recogResult_t FaceRecognitionService::handleRecognition(Mat& frame, const Rect& face, const Mat& alignedFace, QString& labelText, Scalar& boxColor)
{
    recogResult_t rv;
	QString name = "UnKown";
    int authFlag = -1;

    bool dnnOk = false;
    float sim = -1.f;

#ifdef USE_LBPH_RECOGNIZER
    double confident = 999.0;
	bool lbphOk = false;
	int pl = -1;						// predict Label: 예상 라벨
                                            //
    
    // 1) LBPH로 게이트만
    if (recognizer && !recognizer->empty() && !storedFaces.empty() &&	fs::exists(LBPH_MODEL_FILE)) {
        pl = -1;
        recognizer->predict(alignedFace, pl, confident);
        lbphOk = (confident < 70.00);			// confidence가 작으면 작을수록 일치
    }

    // 2) DNN으로 식별 + 이름
    if (dnnEmbedder_ && !gallery_.empty()) {
        std::vector<float> emb;
        if (dnnEmbedder_->extract(alignedFace, emb) && !emb.empty()) {
            auto m = bestMatch(emb);
            sim = m.sim;
            if (m.id >= 0 && m.sim >= 0.55f) {
                name = m.name;
                dnnOk = true;
            }
        }
    }
    qDebug() << "[FRS] confidence:" << confident << "emb sim:" << sim;

    // 3) 결론: DNN이 이름 제공 -> 표기, 생상
    if (lbphOk) {
        // DNN 우선 (표시/식별)
        labelText = name;
        if (dnnOk) { boxColor = {0,255,0}; authFlag = AUTH_SUCCESSED; }
        else			 { boxColor = {0,0,255}; authFlag = AUTH_FAILED;		}
    } else {
        // LBPH만 통과했지만 DNN 실패인 드문 케이스 → Unknown로 처리(또는 이름 유지 정책 정의)
        labelText = "Unknown";
        boxColor  = {0,0,255};
        authFlag	= AUTH_FAILED;
    }
    rv.sim = sim;     
    rv.confidence = confident;
    rv.result = authFlag;
#endif

    // 0) DNN으로 식별 + 이름
    if (dnnEmbedder_ && !gallery_.empty()) {
        std::vector<float> emb;
        if (dnnEmbedder_->extract(alignedFace, emb) && !emb.empty()) {
            auto m = bestMatch(emb);
            sim = m.sim;
            if (m.id >= 0 && m.sim >= 0.55f) {
                name = m.name;
                dnnOk = true;
            }
        }
    }
    qDebug() << "[FRS] emb sim:" << sim;
				
    // DNN 우선 (표시/식별)
    labelText = name;
    if (dnnOk) { boxColor = {0,255,0}; authFlag = AUTH_SUCCESSED; }
    else	   { boxColor = {0,0,255}; authFlag = AUTH_FAILED; }


    drawTransparentBox(frame, face, boxColor, 0.3);
    drawCornerBox(frame, face, boxColor, 2, 25);
    putText(frame, labelText.toStdString(), Point(face.x, face.y - 10),
            FONT_HERSHEY_DUPLEX, 1.0, boxColor, 2);

    rv.sim = sim;
    rv.result = authFlag;

    return rv;
}

void FaceRecognitionService::handleRegistration(Mat& frame, const Rect& face, const Mat& alignedFace, QString& labelText, Scalar& boxColor)
{
		qDebug() << "[FaceRecognitionService] handleRegistration()";

    // 취소 체크(초반 빠른 탈출)
    if (m_cancelReg.load(std::memory_order_relaxed)) {
        qDebug() << "[FaceRecognitionService] canceled (early in handleRegistration)";
        forceAbortRegistration();
        emit registerFinished(false, "등록 취소됨/타임아웃");
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
            emit registerFinished(false, QString("중복된 얼굴입니다. (id=%1, sim=%.3f)").arg(dupId).arg(sim));
            forceAbortRegistration();
            emit registrationCompleted(false, QStringLiteral("중복된 얼굴"));
            setRegisterRequested(false);
            return;
        }
    }


		
    Mat colorFace = frame.clone();
    labelText = "Registering...";
    boxColor = Scalar(255, 0, 0);

    drawTransparentBox(frame, face, boxColor, 0.3);
    drawCornerBox(frame, face, boxColor, 2, 25);
    putText(frame, labelText.toStdString(), Point(face.x, face.y - 10),
            FONT_HERSHEY_DUPLEX, 1.0, boxColor, 2);

#ifdef USE_LBPH_RECOGNIZER
    if (captureCount == 0 && isDuplicateFace(alignedFace)) {
        isRegisteringAtomic.storeRelaxed(0);

        if (m_cancelReg.load(std::memory_order_relaxed)) {
            qDebug() << "[FaceRecognitionService] canceled during duplicate check";
            forceAbortRegistration();
            emit registerFinished(false, "등록 취소됨/타임아웃");
            emit registrationCompleted(false, QStringLiteral("취소됨/타임아웃"));
            return;
        }

        setDuplicate(true);
        setRegisterRequested(false);
        emit registerFinished(false, "중복된 얼굴입니다.");
        qDebug() << "[FaceRecognitionPresenter] Register mode Off";
        return;
    }
#endif

    qDebug() << "[FaceRecognitionService] captureCount: " << captureCount;
    if (captureCount < 10) {
        if (m_cancelReg.load(std::memory_order_relaxed)) {
            qDebug() << "[FaceRecognitionService] canceled before saveCaptureFace";
            forceAbortRegistration();
            emit registerFinished(false, "등록 취소됨/타임아웃");
            emit registrationCompleted(false, QStringLiteral("취소됨/타임아웃"));
            return;
        }

        saveCapturedFace(face, alignedFace, colorFace); 
        captureCount++;

        if (captureCount >= 10) {
            if (m_cancelReg.load(std::memory_order_relaxed)) {
                qDebug() << "[FRS] canceled before finalize";
                forceAbortRegistration();
                emit registerFinished(false, "등록 취소됨/타임아웃");
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
        emit registerFinished(false, "등록 취소됨/타임아웃");
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

        emit registerFinished(false, "임베딩 수집 실패");
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
        //gallery_.push_back({registeringUserId_, std::move(meanEmb)});
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

#ifdef USE_LBPH_RECOGNIZER
    // 5) LBPH 데이터 저장/학습 
    vector<Mat> images;
    vector<int> labels;
    for (const auto& entry : storedFaces) {
        for (const auto& img : entry.second) {
            images.push_back(img);
            labels.push_back(entry.first);
        }
    }

    // LBPH 모델 업데이트
    trainOrUpdateModel(images, labels);

    // 업데이트된 모델 파일 다시 로드
    reloadModel();
#endif

    if (m_cancelReg.load(std::memory_order_relaxed)) {
        qDebug() << "[FRS] canceled while composing dataset";
        forceAbortRegistration();				
        emit registerFinished(false, "등록 취소됨/타임아웃");
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
    registeringUserName_ = "\0";						// 등록 중인 사용자 이름 초기화
    registeringUserId_ = 0;									// 등록 중인 사용자 아이디 초기화
    regEmbedsBuffers_.clear();							//  임베딩 임시버퍼 초기화
    regImageBuffers_.clear();								//  이미지 임시버퍼 초기화


    // UI에 등록 완료 제출
    emit registerFinished(true, "등록 성공");
    emit registrationCompleted(true, QStringLiteral("등록 완료"));
}

#ifdef USE_LBPH_RECOGNIZER
bool FaceRecognitionService::isDuplicateFace(const Mat& newFace)
{
		bool rv = false;

    // 신뢰도 기준으로 중복 여부 판단
    const double DUPLICATE_THRESHOLD = 50.0; // 작을수록 엄격 (OpenCV LBPH 기준 50~100 적절)
	
    int predictedLabel = -1;
    double confidence = 0.0;

	
    if (!recognizer || newFace.empty()) {
        std::cerr << "[오류] 얼굴 인식기가 초기화되지 않았거나 입력 이미지가 비어 있습니다." << std::endl;
        return rv;

    }

		if (fs::exists(LBPH_MODEL_FILE)) {
					qDebug() << "[FRS] " << LBPH_MODEL_FILE << " is not exists";
					return rv;
		}
    cv::Mat gray;
    if (newFace.channels() == 3)
        cv::cvtColor(newFace, gray, cv::COLOR_RGB2GRAY);
    else
        gray = newFace;

		if (!storedFaces.empty() && fs::exists(LBPH_MODEL_FILE)) {
        recognizer->predict(gray, predictedLabel, confidence);
    }

    std::cout << "[" << __func__ << "] 예측된 라벨: " << predictedLabel << ", 신뢰도: " << confidence << std::endl;

		rv = (confidence < DUPLICATE_THRESHOLD) && (confidence) && (predictedLabel != -1);

		return rv;
}
#endif

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

#ifdef USE_LBPH_RECOGNIZER
		QFile::remove(LBPH_MODEL_FILE);
#endif
		QFile::remove(QString::fromStdString(embeddingsPath_));

		gallery_.clear();
		registeringUserId_ = 0;
		registeringUserName_ = "\0";
		rebuildNextIdFromGallery();

		authManager.resetAuth();
		hasAlreadyUnlocked = false;	

#ifdef USE_LBPH_RECOGNIZER
		// NOTE: LBPH 모델 미학습 상태에서 predict() 금지.
		recognizer = face::LBPHFaceRecognizer::create();
		recognizer->save(LBPH_MODEL_FILE);
#endif

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

void FaceRecognitionService::procFrame()
{
    recogResult_t recogResult;
    // 리드 스위치 준비되면 주석 풀기
    //bool doorIsOpen = g_reed.isClosed();
    //qDebug() << "[FRS] isClose?" << doorIsOpen;

    if (!cap.isOpened()) {
        qDebug() << "[FRS] The camera has shutdown. it will restart";
        openCamera();
    }

    Mat frame, frameCopy;
    {
        QMutexLocker locker(&frameMutex);
        if (!cap.read(frame) || frame.empty()) return;
    }

    frameCopy = frame.clone();
    Mat gray;
    cvtColor(frameCopy, gray, cv::COLOR_RGB2GRAY);	
    equalizeHist(gray, gray);

    vector<Rect> faces;
    faceDetector.detectMultiScale(gray, faces, 1.1, 5, CASCADE_SCALE_IMAGE, Size(100, 100));

    // === FSM 스냅샷: 얼굴 검출 신호 공급 (히스테리시스용 detectScore) ===
    // 검출기에서 정량 스코어가 없다면, 임시로 얼굴 개수 기반으로 0.0/0.8 같은 프록시 신호를 사용
    // (실제 스코어가 있다면 setDetectScore(<실스코어>)로 바꿔줘)
    const bool faceFound = !faces.empty();
    setDetectScore(faceFound ? 0.8 : 0.0);		// facePresent_도 내부에서 갱신됨

    // === FSM 스냅샷: 등록 모드 ===
    // isRegisteringAtomic 은 원자적으로 접근
    setRegisterRequested(isRegisteringAtomic.loadRelaxed() != 0);
    //qDebug() << "[FRS] registerequested: " << regReq_;

    // == FSM 스냅샷: 라이브니스(현재 모듈이 없으므로 true고정; 추후 실제 결과로 대체) ===
    setLivenessOk(true);
		
    // == FSM 스냅샷: 중복 얼굴 (얼굴이 중복되면 true, 중복이 아니면 false) ===
    setDuplicate(false);



    for (const auto& face : faces) 
    {
        Mat alignedFace = alignAndNormalizeFace(gray, face);
        if (alignedFace.empty()) continue;

        QString labelText;
        Scalar boxColor;

        // NOTE: isRegisteringAtomic은 타 스레드 접근 가능. 반드시 원자적 load()사용.
        if (isRegisteringAtomic.loadRelaxed() == 0) {
            recogResult = handleRecognition(frameCopy, face, alignedFace, labelText, boxColor);


            // === FSM 스냅샷: 인식 신뢰도/성공/실패 누적 ===
            // 당장 값이 없다면 임시로 성공/실패에 따른 프록시 값을 사용(히스테리시스가 안정화시켜줌).
            if (recogResult.result == AUTH_SUCCESSED) {						// 등록된 얼굴일 경우 if문 진입 
                //setRecogConfidence(recogResult.confidence);				// FSM을 위한 스냅샷setting			
                setRecogConfidence(recogResult.sim);				// FSM을 위한 스냅샷setting			
                resetFailCount();																	// 성공 시 실패 누적 리셋
                incAuthStreak();																		// 인증 성공 시 성공 횟수 누적
								

                qDebug() << "[FSM] AuthStreak(" << authStreak_ << ")";
                if (!hasAlreadyUnlocked) {
                    authManager.handleAuthSuccess();
					
                    if (authManager.shouldAllowEntry()) {
                        qDebug() << "[FaceRecognitionService] Authenticate 5 time success -> Door open!";
                        authManager.resetAuth();
                        hasAlreadyUnlocked = true;
                        resetAuthStreak();		// 최종 적으로 인식이 성공하면 성공 횟수 초기화 

                        // =====  감지 전까지 열림 유지 시작 =====
                        if (!g_unlockMgr.running()) {
                            g_unlockMgr.start();
                            qInfo() << "[Door] Unlock started (wait open, then wait close)";
                        }

                        setAllowEntry(true);
                        setDoorOpened(true);

                    }	
                }
            } else {
                //setRecogConfidence(recogResult.confidence);			 
                setRecogConfidence(recogResult.sim);			 
                
                incFailCount();								 // 실패 누적
                setDoorOpened(false);

                //qDebug() << "[FSM] Reset AuthStreak(" << authStreak_ << ")";
                //qDebug() << "[FSM] failCount: " << failCount_;
                resetAuthStreak();		// 중간에 얼굴인식 인증이 실패하면 인증 성공횟수 초기화
                authManager.resetAuth();
                //doorIsOpen = false;
            }
        }
        else {
            if(isRegisteringAtomic.testAndSetRelaxed(1, 1)) {
                handleRegistration(frameCopy, face, alignedFace, labelText, boxColor);
            }
        }
    }

		 // 얼굴이 하나도 없을 때 신뢰도는 낮게 유지(플리커 방지에 도움)
    if (!faceFound) {
        setRecogConfidence(0.0);
    }

    if (authStreak_ > 6) {
        resetAuthStreak();		// 얼굴이 동시에 인증 성공 했을 때 스트릭이 초기화 되지 않아 임시 방법으로 사용 
        authManager.resetAuth();
        resetUnlockFlag();
    }

    //setDoorOpened(doorIsOpen); // TODO: 센서 연결되어 있으면 사용


		
    cvtColor(frameCopy, frameCopy, cv::COLOR_BGR2RGB);
    QImage qimg(frameCopy.data, frameCopy.cols, frameCopy.rows, frameCopy.step, QImage::Format_RGB888);
    emit frameReady(qimg.copy());
}


void FaceRecognitionService::setDetectScore(double v)				{ detectScore_ = v; facePresent_ = (v > 0.0); }
void FaceRecognitionService::setRecogConfidence(double v)		{ recogConf_ = v; }
void FaceRecognitionService::setDuplicate(bool v)						{ isDup_ = v; }
void FaceRecognitionService::setRegisterRequested(bool v)   { regReq_ = v; }
void FaceRecognitionService::setLivenessOk(bool v)					{ livenessOk_ = v; }
void FaceRecognitionService::setDoorOpened(bool v)					{ doorOpened_ = v; }
void FaceRecognitionService::incFailCount()									{ failCount_++; }
void FaceRecognitionService::incAuthStreak()								{ authStreak_++; }
void FaceRecognitionService::setAllowEntry(bool v)					{ allowEntry_ = v; }
void FaceRecognitionService::resetFailCount()								{ failCount_ = 0; }
void FaceRecognitionService::resetAuthStreak()							{ authStreak_ = 0;	}

void FaceRecognitionService::onTick() 
{
		if (testMode_) testScriptStep();

		FsmContext c;
		c.detectScore					= detectScore_;
		c.recogConfidence			= recogConf_;
		c.isDuplicate					= isDup_;
		c.registerRequested		= regReq_;
		c.livenessOk					= livenessOk_;
		c.doorOpened					= doorOpened_;
		c.failCount						= failCount_;
		c.authStreak					= authStreak_;
		c.facePresent					= facePresent_;
		c.allowEntry					= allowEntry_;
		c.nowMs								= monotonic_.elapsed();

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


/*	테스트 모드 */
void FaceRecognitionService::enableTestMode(bool on) {
    testMode_ = on;
    step_ = 0;
}

void FaceRecognitionService::testScriptStep() {
    if (!testMode_) return;

    // 33ms마다 한 스텝 진행: IDLE→DETECTING→RECOGNIZING→SUCCESS→DOOR_OPEN→IDLE
    switch (step_) {
    case 0 ... 5:   setDetectScore(0.0); setRecogConfidence(0.0); setRegisterRequested(false); break; // IDLE 유지 ~200ms
    case 6 ... 18:  setDetectScore(0.9); setRecogConfidence(0.2); setRegisterRequested(false); break; // DETECTING
    case 19 ... 30: setDetectScore(0.9); setRecogConfidence(0.92); setRegisterRequested(false); break; // RECOGNIZING→SUCCESS
    case 31 ... 36: /* DOOR_OPEN 유지 */ break;
    default:        setDetectScore(0.0); setRecogConfidence(0.0); setRegisterRequested(false); step_ = -1; break; // IDLE 복귀
    }
    step_++;
}



