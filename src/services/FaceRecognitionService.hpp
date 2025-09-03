#ifndef FACERECOGNITIONSERVICE_H
#define FACERECOGNITIONSERVICE_H

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QElapsedTimer>
#include <opencv2/opencv.hpp>
#include <opencv2/face.hpp>
#include <fstream>
#include <unistd.h>
#include <map>
#include <QMutexLocker>
#include <QMutex>
#include <QAtomicInt>
#include <atomic>
#include <vector>
#include <nlohmann/json.hpp>

#include <QDebug>

#include "faceRecognitionState.hpp"
#include "fsm/recognition_fsm.hpp"
#include "fsm/recognition_fsm_setup.hpp"
#include "AuthManager.hpp"
#include "util.hpp"

#include "ai/Embedder.hpp"
//#include "lbph/Cascader.hpp"

#include "include/common_path.hpp"

#define CAM_NUM												0

#define AUTH_SUCCESSED								1
#define AUTH_FAILED										0

using namespace std;
using namespace cv;
using namespace cv::face;

class FaceRecognitionPresenter;


typedef struct RecogResult_t {
        double   confidence = 999;	  // 얼굴인식 신뢰도
		int		 result = false;	  // 인식 결과
        float    sim = -1.f;          // 임베딩 결과
} recogResult_t;

struct UserEmbedding {
		int id;													// 내부 식별자 (녕속 변호/UUID등)
		QString name;										// 표시할 이름
		std::vector<float> embedding;		// size 256 floats(현재 모델)
};


struct MatchResult {
		int id = -1;
		QString name;
		float sim = -1.f;
};


class FaceRecognitionService : public QObject {
		Q_OBJECT

public:
				explicit FaceRecognitionService(QObject *parent = nullptr, FaceRecognitionPresenter* presenter = nullptr);
				void setPresenter(FaceRecognitionPresenter* presenter);
				void init();
				void stop();
				void procFrame();

				QString getUserName();
				void startRegistering(const QString& name);

				 // 워치독/사용자 취소를 위한 API
				void cancelRegistering();          // 진행 중 등록을 협조적(cancel-flag)으로 중단
				void forceAbortRegistration();     // 상태머신/리소스를 강제 정리(즉시 IDLE 복귀)

				void resetUnlockFlag();
				void fetchReset();

				RecognitionState getState() const { return currentState; }
signals:
				void frameReady(const QImage& frame);
				
				void registrationCompleted(bool ok, const QString& msg);
				void registerFinished(bool ok, const QString& msg);
				void stateChanged(RecognitionState s);

public slots:
				void setDetectScore(double v);
				void setRecogConfidence(double v);
				void setDuplicate(bool v);
				void setRegisterRequested(bool v);
				void setLivenessOk(bool v);
				void setDoorOpened(bool v);
				void incAuthStreak();
				void resetAuthStreak();
				void incFailCount();
				void resetFailCount();
				void setAllowEntry(bool v);

private slots:
				void onTick();			// 컨텍스트 스냅샷 -> FSM

private:
				void openCamera();
				void registerExistingUser(); 

				void drawTransparentBox(Mat& img, Rect rect, Scalar color, double alpha);
				void drawCornerBox(Mat& img, Rect rect, Scalar color, int thickness, int length);

				Mat alignAndNormalizeFace(const Mat& grayFrame, const Rect& faceRect);
				void trainOrUpdateModel(const vector<Mat>& images, const vector<int>& labels);
				bool isDuplicateFace(const Mat& newFace);
				void saveCapturedFace(const Rect& face, const Mat& aligendFace, const Mat& frame);

				recogResult_t  handleRecognition(Mat& frame, const Rect& face, const Mat& alignedFace, QString& labelText, Scalar& boxColor);
				void handleRegistration(Mat& frame, const Rect& face, const Mat& alignedFace, QString& labelText, Scalar& boxColor);
				void finalizeRegistration();

				bool computeTimeout(const FsmContext& c);
                bool loadDetector();


				// TODO: LBPH is seperate to another class
				/* LBPH 초기 모델 */
				//std::unique_ptr<Cascader> lbphCascader_;
				//std::vector<UserRecognizing> lbphGallery_;					// 사용자 lphh DB
				//std::vector<std::vector<float> regLbphBuffers_;			// 등록 중 임시 버퍼

				void initializeLBPH();
				void reloadModel();
				vector<Mat> regImageBuffers_;
				//std::string lbphModelPath_;													// lbph_model/face_model.yml												
				//float confThresh = 60.00;														// 인식 임계( 튜닝)


				/* DNN 초기 모델 */
				std::unique_ptr<Embedder> dnnEmbedder_;
				std::vector<UserEmbedding> gallery_;								// 사용자 임베딩 DB
				std::vector<std::vector<float>> regEmbedsBuffers_;	// 등록 중 임시 임베딩 버퍼
				int registeringUserId_ = -1;												// 등록 대상 ID
				QString registeringUserName_;											// 등록 대상 이름 
				std::string embeddingsPath_;												// data/embeddings.json
				float cosineThreshold_ = 0.60f;											// 인식 임계 (튜닝)

				// 초기화/파일 I/O
				void initializeDnn();							// 모델 경로 + 임베딩 로드
				bool loadEmbeddingsFromFile();		// 파일 -> gallery_
				bool saveEmbeddingsToFile();			// gallery_ -> 파일
				static bool ensureDir(const QString& path);		// 디렉토리 생성 유틸
				void clearRegistrationBuffers();	

				// 매칭 유틸
				bool isDuplicateFaceDNN(const cv::Mat& alignedFace, int* dupIdOut = nullptr, float* simOut = nullptr) const;
				MatchResult bestMatch(const std::vector<float>& emb) const;
				
				// User Id 초기화 및 순차 생성
				std::atomic<int> nextIdCounter_{1};		// 다음에 줄 ID(시작 값 1)

				// helpers
				void rebuildNextIdFromGallery();			// gallery_ 로드 후 nextIdCount_ 재계산
				int nextSequentialId();								// 새 등록 시작 시 호출
				bool idExists(int id) const;

private:
				FaceRecognitionPresenter* presenter;
				std::atomic<bool> isRunning = true;
				VideoCapture cap;

				CascadeClassifier faceDetector;
				CascadeClassifier eyesDetector;

				Ptr<LBPHFaceRecognizer> recognizer;

				RecognitionState currentState = RecognitionState::IDLE;
				

				QMutex frameMutex;
				AuthManager authManager;
				
				std::atomic<bool> m_cancelReg{false};

				map<int, vector<Mat>> storedFaces;
				QAtomicInt isRegisteringAtomic;
				bool registerRequested = false;
				int currentLabel = -1;
				int captureCount = 0;

				bool hasAlreadyUnlocked = false;

				RecognitionFsm fsm_{this};
				FsmParams params_;
				QTimer tick_;
				QElapsedTimer monotonic_;
				QElapsedTimer stateTimer_;
				RecognitionState prevState_ = RecognitionState::IDLE;

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


				/* 테스트 모드 */
public:
				void enableTestMode(bool on);
				void testScriptStep();			// 시뮬레이션 step
private:
				bool testMode_ = false;
				int step_ = 0;
};

#endif		// FACERECOGNITIONSERVICE_H
