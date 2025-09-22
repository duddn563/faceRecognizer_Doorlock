#pragma once

// Qt
#include <QObject>
#include <QImage>
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
//#include <opencv2/face.hpp>
#include <opencv2/dnn.hpp>

// Embedding
#include "ai/Embedder.hpp"

// Recognition State
#include "faceRecognitionState.hpp"

// nlohmann json
#include <nlohmann/json.hpp>

// Common Path 
#include "include/common_path.hpp"

// Authtication Manager
#include "services/AuthManager.hpp"

// FSM 
#include "fsm/recognition_fsm.hpp"
#include "fsm/recognition_fsm_setup.hpp"

// Utile
//#include "util.hpp"

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
	int idx = -1;
    float   sim = -1.0f;			// 임베딩 결과 
    bool result = AUTH_FAILED;		// 인식 결과
};
// 매칭 결과
struct MatchResult {
        int     id   = -1;
        QString name;
        float   sim  = -1.0f;
    };
// top-2 매칭 결과
struct MatchTop2 {
    int   bestIdx   = -1;
    float bestSim   = -2.0f;
    int   secondIdx = -1;
    float secondSim = -2.0f;
};

// 갤러리 항목
struct UserEmbedding {
    int                 id = -1;
    QString             name;
    std::vector<float>  embedding; // L2 정규화된 벡터
	cv::Mat proto;				   // 1xD, CV_32F, L2=1 고정 클론
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

    // 수명/카메라
    void stop();
    void camRestart();
    void openCamera();

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
    Q_INVOKABLE void procFrame();
	QSqliteService *db;

signals:
    // 상태 변경 (FSM → UI)
    void stateChanged(RecognitionState s);

    // 등록 완료 통지
    void registrationCompleted(bool ok, const QString& message);

    // UI 로 이미지 전달 
	void frameReady(const QImage& frame);

public slots:
    // UI(QML/Qt)에서 접근하는 setter — moc에서 호출되므로 무조건 정의 필요
    // 링커 에러 방지 위해 헤더에서 인라인 구현
    void setDetectScore(double v);
    void setRecogConfidence(double v); 
    void setDuplicate(bool v);            
    void setRegisterRequested(bool v);   
    void setLivenessOk(bool v);         
    void setDoorOpened(bool v);       
    void setAllowEntry(bool v);       

    void incAuthStreak();               
    void resetAuthStreak();          

    void incFailCount();           
    void resetFailCount();        

    // 서비스 주기 동작 hook (타이머가 호출)
    void onTick();			// 컨텍스트 스냅샷 -> FSM

signals:
    // Q_PROPERTY 스타일 변경 알림(있으면 QML 바인딩 가능)
    void detectScoreChanged(double);
    void recogConfidenceChanged(double);
    void duplicateChanged(bool);
    void registerRequestedChanged(bool);
    void livenessOkChanged(bool);
    void doorOpenedChanged(bool);
    void allowEntryChanged(bool);
    void authStreakChanged(int);
    void failCountChanged(int);

private:
    // ===== 내부 구현 메서드 =====
    void init();
    bool initializeDnnOnly();
    bool loadDetector();
	bool loadRecognizer();
	bool loadEmbJsonFile();

    // 파일 IO
    bool loadEmbeddingsFromFile();
    bool saveEmbeddingsToFile();

    // 유틸
    bool ensureDir(const QString& dirPath);
    void rebuildNextIdFromGallery();
    bool idExists(int id) const;
    int  nextSequentialId();

    // 검출/정렬/크롭
    static cv::Rect expandRect(const cv::Rect& r, float scale, const cv::Size& imgSz);

    // 품질 게이트
    static bool passQualityForRecog(const cv::Rect& box, const cv::Mat& face);

    // 등록 파이프라인
    void handleRegistration(cv::Mat& frame,
                            const cv::Rect& face,
                            const cv::Mat& alignedFace,
                            QString& labelText,
                            cv::Scalar& boxColor);
    void saveCapturedFace(const cv::Rect& face,
                          const cv::Mat& alignedFace,
                          const cv::Mat& frame);
    void finalizeRegistration();
    bool isDuplicateFaceDNN(const cv::Mat& alignedFace, int* dupIdOut, float* simOut) const;

    // 인식 파이프라인
    recogResult_t handleRecognition(cv::Mat& frame,
                                    const cv::Rect& face,
                                    const cv::Mat& alignedFace,
                                    QString& labelText,
                                    cv::Scalar& boxColor);
    MatchTop2 bestMatchTop2(const std::vector<float>& emb) const;
    
    MatchResult bestMatch(const std::vector<float>& emb) const;

    // 드로잉
    static void drawTransparentBox(cv::Mat& img, cv::Rect rect, cv::Scalar color, double alpha);
    static void drawCornerBox(cv::Mat& img, cv::Rect rect, cv::Scalar color, int thickness, int length);

	// FSM
	bool computeTimeout(const FsmContext& c);

private:
    // ===== 멤버 =====
    FaceRecognitionPresenter* presenter = nullptr;

    // 카메라
    cv::VideoCapture cap;

    // YuNet
    cv::Ptr<cv::FaceDetectorYN> yunet_;
    cv::Size                    yunet_InputSize_{0,0};

    // 임베더(MobileFaceNet)
    std::unique_ptr<Embedder>   dnnEmbedder_;
		// 컨텍스트 스냅샷 -> FSM
    // 갤러리 및 임베딩 파일 경로
    std::string embeddingsPath_;
    std::vector<UserEmbedding> gallery_;
    std::atomic<int>           nextIdCounter_{1};

    // 등록 버퍼
    QString                     registeringUserName_;
    int                         registeringUserId_ = -1;
    int                         captureCount = 0;
    std::vector<std::vector<float>> regEmbedsBuffers_;
    std::vector<cv::Mat>            regImageBuffers_;
    std::map<int, std::vector<cv::Mat>> storedFaces;

    // 원자 플래그
    RelaxedAtomicInt            isRegisteringAtomic;
	RelaxedAtomicInt			isOpenDoorAtomic;
    std::atomic<bool>           m_cancelReg{false};

    // 동기화
    QMutex                      frameMutex;

	// FSM 
	RecognitionFsm 				fsm_{this};
	FsmParams 					params_;
    QTimer                      tick_;
	QElapsedTimer				monotonic_;
	QElapsedTimer				stateTimer_;

    // 상태 값 (QML에서 바인딩할 수 있게 유지)
	double detectScore_ = 0.0;
	double recogConf_		= 0.0;
	bool	 isDup_				= false;
	bool	 regReq_			= false;
	bool	 livenessOk_	= false;
	bool	 doorOpened_  = false;
	int		 failCount_   = 0;
	int    authStreak_	= 0;
	bool	 facePresent_ = false;
	bool	 allowEntry_  = false;
    // UI/상태
    RecognitionState 			currentState = RecognitionState::IDLE;
	RecognitionState 			prevState_ = RecognitionState::IDLE;

    // 실행 상태
    bool isRunning = true;

	// 인증 매니저
	AuthManager authManager;
	bool hasAlreadyUnlocked = false;
};


