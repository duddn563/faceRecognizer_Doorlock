#pragma once

// Qt
#include <QObject>
#include <QImage>
#include <QFont>
#include <QTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>
#include <QString>
#include <QVector>
#include <QSet>
#include <QThread>
#include <QAtomicInt>
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>

// STL
#include <atomic>
#include <memory>
#include <vector>
#include <map>
#include <string>
#include <fstream>
#include <unistd.h>

// OpenCV
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/dnn.hpp>

// Embedding
#include "ai/Embedder.hpp"

// nlohmann json
#include <nlohmann/json.hpp>

// Common Path 
#include "include/common_path.hpp"
#include "include/types.hpp"
#include "include/states.hpp"

// Authtication Manager
#include "services/AuthManager.hpp"

#include "liveness/LivenessGate.hpp"

#include "match/FaceMatcher.hpp"
#include "match/SimilarityDecision.hpp"

#include "detect/LandmarkAligner.hpp"
#include "detect/FaceDetector.hpp"

// FSM 
#include "fsm/recognition_fsm.hpp"
#include "fsm/recognition_fsm_setup.hpp"

// Camera number
#define CAM_NUM							-1	

// Recognition Result
#define AUTH_SUCCESSED 					1
#define AUTH_FAILED						0


// ==== 외부 컴포넌트(전방 선언) ====
using namespace std;
using namespace cv;

class FaceRecognitionPresenter;
class QSqliteService;

// YuNet
namespace cv {
	class FaceDetectorYN;
}

// 간단한 리턴 구조체
struct recogResult_t {
	QString name;
	int		idx = -1;
	float   sim = -1.0f;			// 임베딩 결과 
	bool	result = AUTH_FAILED;		// 인식 결과
};

// 편의형 원자 래퍼 (cpp에서 loadRelaxed/storeRelaxed 사용)
struct RelaxedAtomicInt {
	std::atomic<int> v{0};
	void storeRelaxed(int x)            { v.store(x, std::memory_order_relaxed); }
	int  loadRelaxed() const            { return v.load(std::memory_order_relaxed); }
	operator int() const                { return v.load(std::memory_order_relaxed); }
};

class FaceRecognitionService : public QObject {
	Q_OBJECT
	public:
		explicit FaceRecognitionService(QObject* parent = nullptr,
				FaceRecognitionPresenter* presenter = nullptr , QSqliteService* db = nullptr);
		int staticDoorStateChange(bool state);	

		void requestedDoorOpen();
		void requestedDoorClose();
		void requestedRetrainRecog();

		void camRestart();

		// Presenter 연결
		void setPresenter(FaceRecognitionPresenter* presenter);

		// 등록 제어
		void startRegistering(const QString& name);
		void cancelRegistering();
		void forceAbortRegistration();
		void resetUnlockFlag();
		void fetchReset();
		QString getUserName() const;

		// 프레임 처리 엔트리
		QSqliteService *db;

		bool startDirectCapture(int cam = 0);
		void stopDirectCapture(); 

		QString nameFromId(int userId);
signals:
		// 상태 변경 (FSM → UI)
		void stateChanged(RecognitionState s);

		// 등록 완료 통지
		void registrationCompleted(bool ok, const QString& message);

		// UI 로 이미지 전달 
		void frameReady(const QImage& frame);

		void doorStateChanged(States::DoorState s);

public slots:
		// UI(QML/Qt)에서 접근하는 setter — moc에서 호출되므로 무조건 정의 필요
		// 링커 에러 방지 위해 헤더에서 인라인 구현
		void setDetectScore(double v);
		void setFacePresent(bool v);
		void setRecogConfidence(double v); 
		void setDuplicate(bool v);            
		void setRegisterRequested(bool v);   
		void setLivenessOk(bool v);         
		void setDoorOpened(bool v);       
		void setAllowEntry(bool v);       

		void setDoorCommandOpen(bool v);		// 하드웨어 트리거용(리드센서와는 무관)
		void setDoorSensorOpen(bool v);			// 리드센서 해석 결과만

		void incAuthStreak();               
		void resetAuthStreak();          

		void incFailCount();           
		void resetFailCount();        

		// 서비스 주기 동작 hook (타이머가 호출)
		void onTick();			// 컨텍스트 스냅샷 -> FSM

	private:
		// ===== 내부 구현 메서드 =====
		void init();
		bool initializeDnnOnly();
		bool loadDetector();
		bool loadRecognizer();
		bool loadEmbJsonFile();

		// 파일 IO
		bool loadEmbeddingsFromFile();
		bool saveEmbeddingsToFile() const;
		void showOpenImage();
		void showFarImage(Mat& frame);

		// 유틸
		bool ensureDir(const QString& dirPath);
		void rebuildNextIdFromGallery();
		bool idExists(int id) const;
		int  nextSequentialId();
		bool ensureEmbFile();
		int appendUserEmbedding(const QString& name, const std::vector<float>& emb);

		// 검출/정렬/크롭
		static cv::Rect expandRect(const cv::Rect& r, float scale, const cv::Size& imgSz);

		// 품질 게이트
		bool passQualityForRecog(const cv::Rect& box, cv::Mat& face);

		// 등록 파이프라인
		DetectedStatus handleRegistration(cv::Mat& frame,
				const cv::Rect& face,
				const cv::Mat& alignedFace,
				QString& labelText,
				cv::Scalar& boxColor);
		void saveCapturedFace(const cv::Rect& face,
				const cv::Mat& alignedFace,
				const cv::Mat& frame);
		void finalizeRegistration();
		bool isDuplicateFaceDNN(const cv::Mat& alignedFace, int* dupIdOut, float* simOut) const;

		void drawAnglePrompt(cv::Mat& frame, const QString& text);
		void drawProgressBar(cv::Mat& frame);
		void advanceAngleStepIfReady();
		bool angleRegCompleted();

		// 인식 파이프라인
		recogResult_t handleRecognition(cv::Mat& frame,
				const cv::Rect& face,
				const cv::Mat& alignedFace,
				QString& labelText,
				cv::Scalar& boxColor);
		MatchTop2 bestMatchTop2(const std::vector<float>& emb) const;
		MatchResult bestMatch(const std::vector<float>& emb) const;
		void printFrame(cv::Mat &frame, DetectedStatus hasBase);

		// 드로잉
		static void drawTransparentBox(cv::Mat& img, cv::Rect rect, cv::Scalar color, double alpha);
		static void drawCornerBox(cv::Mat& img, cv::Rect rect, cv::Scalar color, int thickness, int length);

		// FSM
		bool computeTimeout(const FsmContext& c);
		cv::Mat alignBy5pts(const cv::Mat& srcBgr, const std::array<cv::Point2f,5>& src5_in, const cv::Size& outSize);

		std::vector<FaceDet> detectAllYuNet(const cv::Mat& bgr) const;
		std::optional<FaceDet> detectBestYuNet(const cv::Mat& bgr) const;

		bool detectAndAlign(const cv::Mat& bgr, FaceDet& outDet, cv::Mat& outAligned);
		static QImage toQImage(const cv::Mat& bgr);

		void beginOpenOverlay(int ms = -1);
		void syncDoorOpenedFromReed();

	private:
		// ===== 멤버 =====
		FaceRecognitionPresenter* presenter = nullptr;

		// 카메라
		QAtomicInt running_{0};					
		QThread* capThread_ = nullptr;
		cv::VideoCapture cap_;

		void loopDirect();

		// YuNet
		cv::Ptr<cv::FaceDetectorYN> yunet_;
		cv::Size                    yunet_InputSize_{0,0};

		// 임베더(MobileFaceNet)
		std::shared_ptr<Embedder>   dnnEmbedder_;
		// 컨텍스트 스냅샷 -> FSM
		// 갤러리 및 임베딩 파일 경로
		QString							embeddingsPath_;
		std::vector<UserEmbedding>		gallery_;
		mutable QMutex				    embMutex_;
		std::atomic<int>				nextIdCounter_{1};

		// 등록 파이프 라인 
		const QString m_anglePrompts[5] = {
			QStringLiteral("정면을 바라봐주세요"),
			QStringLiteral("머리를 약간 왼쪽으로 돌려주세요"),
			QStringLiteral("머리를 약간 오른쪽으로 돌려주세요"),
			QStringLiteral("턱을 약간 올려주세요"),
			QStringLiteral("턱을 약간 내려주세요"),
		};
		const int m_perStepTarget = 2;
		const float m_dupSimThreshold = 0.98f;
		int m_stepIndex = 0;
		int m_stepCaptured = 0;
		bool m_isAngleRegActive = false;
		std::vector<std::vector<float>> m_recentEmbedsThisStep;

		QString							registeringUserName_;
		int								registeringUserId_ = -1;
		int								captureCount = 0;

		std::vector<std::vector<float>> regEmbedsBuffers_;
		std::vector<cv::Mat>            regImageBuffers_;



		// 원자 플래그
		RelaxedAtomicInt        isRegisteringAtomic;
		RelaxedAtomicInt		isOpenDoorAtomic;
		std::atomic<bool>       m_cancelReg{false};

		// 동기화
		QMutex                  frameMutex;
		QMutex					snapMu_;

		// FSM 
		RecognitionFsm			fsm_{this};
		FsmParams 				params_;
		QTimer					tick_;
		QElapsedTimer			monotonic_;
		QElapsedTimer			stateTimer_;

		// 상태 값 (QML에서 바인딩할 수 있게 유지)
		double detectScore_		= 0.0;
		double recogConf_		= 0.0;
		bool   isDup_			= false;
		bool   regReq_			= false;
		bool   livenessOk_		= false;
		bool   doorOpened_		= false;
		int	   failCount_		= 0;
		int    authStreak_		= 0;
		bool   facePresent_		= false;
		bool   allowEntry_		= false;

		// UI/상태
		RecognitionState 			currentState = RecognitionState::IDLE;
		RecognitionState 			prevState_ = RecognitionState::IDLE;

		// 실행 상태
		bool isRunning = true;

		// 인증 매니저
		AuthManager authManager;
		bool hasAlreadyUnlocked = false;

		// 인증 성공/실패시 쿨다운
		QElapsedTimer authCooldown;
		QElapsedTimer failCooldown;

		LivenessGate		liveness_;
		LandmarkAligner		aligner_;
		FaceDetector		detector_;
		SimilarityDecision  decision_;
		std::unique_ptr<FaceMatcher> matcher_;

		qint64 lastReedEdgeMs_ = 0;

		uint32_t seq_ = 0;

};


