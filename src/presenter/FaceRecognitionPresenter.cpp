#include "FaceRecognitionPresenter.hpp"
#include "FaceRecognitionService.hpp"
#include "FaceRegisterPresenter.hpp"
#include "MainWindow.hpp"

#include <QInputDialog>
#include <QMessageBox>

FaceRecognitionPresenter::FaceRecognitionPresenter(FaceRecognitionService* service, MainWindow* view, QObject* parent = nullptr)
	: QObject(parent), service(service), view(view)
{
	faceRegisterPresenter = new FaceRegisterPresenter(service, view);

	//throttleTimer_ = new QTimer(this);
	//throttleTimer_->setInterval(40);
	//throttleTimer_->setSingleShot(true);

	static bool first = true;
	auto ok = connect(service, &FaceRecognitionService::frameReady, this, [=](const QImage& image) {
			// 카메라 라벨이 화면에 "보이지" 않으면 드롭 (대기화면일 때)
			if (!view->ui->cameraLabel->isVisible()) {
				qDebug() << "[FRP] cameraLabel not visible -> drop";
				return;
			}

			if (!image.isNull()) {
					if (first && view->ui->stackedWidget && view->ui->cameraLabel) {
						view->ui->stackedWidget->setCurrentWidget(view->ui->cameraLabel);
						first = false;
					}
					view->ui->cameraLabel->setPixmap(QPixmap::fromImage(image));
				} else {
					qDebug() << "Image is null!";
				}
			}, Qt::QueuedConnection);

	if (!ok) {
		qCritical() << "[FRP] connect to frameReady failed!";
	}


	connect(service, &FaceRecognitionService::stateChanged, this, [=] (RecognitionState s) {
			switch (s) {
				case RecognitionState::IDLE: 	
					view->showStatusMessage("대기 중...");
					break;
				case RecognitionState::DOOR_OPEN:
					view->showStatusMessage("문이 열렸습니다!");
					break;
				case RecognitionState::DETECTING:
					view->showStatusMessage("얼굴이 감지 되었습니다!");
					break;
				case RecognitionState::REGISTERING:
					view->showStatusMessage("등록중...");
					break;
				case RecognitionState::DUPLICATE_FACE:
					view->showStatusMessage("중복된 얼굴입니다...");
					view->showInfo("중복 사용자", "이미 등록된 얼굴입니다.");
					break;
				case RecognitionState::RECOGNIZING:
					view->showStatusMessage("인식중...");
					break;
				case RecognitionState::AUTH_SUCCESS:
					view->showStatusMessage("인증 대기 중...");
					//view->showUnlockOverlayLabel();
					break;
				case RecognitionState::AUTH_FAIL:		
					view->showStatusMessage("인증 실패!");
					break;
				case RecognitionState::LOCKED_OUT:		
					view->showStatusMessage("문이 잠깁니다...");
					break;
				default:
					view->showStatusMessage("현재 상태를 알 수없습니다...");
			}

	});

	auto btn = view->ui->registerButton;

	if (!btn) {
		qWarning() << "[Presenter] registerButton is NULL) (setupUi이전에 잘못된 포인터)";
		return;
	}

	connect(view, &MainWindow::stateChangedFromView, this, &FaceRecognitionPresenter::onViewStateChanged);

	connect(faceRegisterPresenter, &FaceRegisterPresenter::registrationStarted, [=]() { 
				view->setCurrentUiState(UiState::REGISTERING);
			});
	connect(faceRegisterPresenter, &FaceRegisterPresenter::registrationResult,
			this, [=](bool success, const QString& message) {
				if (view) {
					view->setCurrentUiState(UiState::IDLE);
					view->showInfo("등록 결과", message);
				}
			});

	// 카메라 다시 시작 버튼 방출
	connect(view, &MainWindow::CamRestart, this, &FaceRecognitionPresenter::onCamRestart);
	connect(view, &MainWindow::doorOpen, this, &FaceRecognitionPresenter::onDoorOpen);
	connect(view, &MainWindow::doorClose, this, &FaceRecognitionPresenter::onDoorClose);
	connect(view, &MainWindow::retrainRecog, this, &FaceRecognitionPresenter::onRetrainRecog);
}


void FaceRecognitionPresenter::onCamRestart()
{
	if (!service) return;
	service->camRestart();
}
void FaceRecognitionPresenter::presentCamRestart(const QString& msg)
{
	if (!view) return;

	view->PresentCamRestart(msg);
}

void FaceRecognitionPresenter::onDoorOpen()
{
	if(!service) return;
	service->requestedDoorOpen();
}

void FaceRecognitionPresenter::presentDoorOpen(const QString& msg)
{
	if (!view) return;
	view->PresentDoorOpen(msg);
}

void FaceRecognitionPresenter::onDoorClose()
{
	if (!service) return;
	service->requestedDoorClose();
}

void FaceRecognitionPresenter::presentDoorClose(const QString& msg)
{
	if (!view) return;
	view->PresentDoorClose(msg);
}

void FaceRecognitionPresenter::onRetrainRecog()
{
	if (!service) return;
	service->requestedRetrainRecog();
}

void FaceRecognitionPresenter::presentRetrainRecog(const QString& msg)
{
	if (!view) return;
	view->PresentRetrainRecog(msg);
}

void FaceRecognitionPresenter::onViewStateChanged(RecognitionState state)
{
	if (state == RecognitionState::IDLE) {
		service->resetUnlockFlag();
	}
}

void FaceRecognitionPresenter::onRegisterFace() 
{
	qDebug() << "[FaceRecognitionPresenter] Register mode On";
	service->setRegisterRequested(true);
	if (faceRegisterPresenter) 
		faceRegisterPresenter->onRegisterFace();
}

void FaceRecognitionPresenter::onReset()
{
	qDebug() << "[FaceRecognitionPresenter] onReset() is called";
	if (!service) return;

	service->fetchReset();
}

void FaceRecognitionPresenter::presentReset()
{
	qDebug() << "[FaceRecognitionPresenter] presentReset() is called";
	if (!view) {
		qDebug() << "[FRP] view is null";
		return;
	}

	view->reset();
}

FaceRecognitionPresenter::~FaceRecognitionPresenter()
{
	std::cout << "FaceRecognition Presenter exit!!" << std::endl;
	service->stop();
}
