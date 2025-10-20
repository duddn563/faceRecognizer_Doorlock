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

	if (!service->startDirectCapture(-1)) {
		qWarning() << "[FaceRecognitionPresenter] DirectCapture start failed";
	}
	else {
		qInfo() << "[FaceRecognitionPresenter] DirectCapture started";
	}

	static bool first = true;
	connect(service, &FaceRecognitionService::frameReady, this, [=](const QImage& image) {
		// 카메라 라벨이 화면에 "보이지" 않으면 드롭 (대기화면일 때)
		if (image.isNull()) return;

		auto* label = view->ui->cameraLabel;
		if (!label->isVisible()) label->show();

		lastFrame_ = image;

		if (image.size() == label->size()) {
			label->setPixmap(QPixmap::fromImage(image));
		}

		repaintCameraLabel(label, lastFrame_);

	}, Qt::QueuedConnection);

	connect(service, &FaceRecognitionService::doorStateChanged, this, [=] (States::DoorState s) {
		view->onDoorStateChanged(s);
	});


	connect(service, &FaceRecognitionService::stateChanged, this, [=] (RecognitionState s) {
			switch (s) {
				case RecognitionState::IDLE: 	
					view->showStatusMessage("대기 중...");
					break;
				case RecognitionState::DOOR_OPEN:
					view->showStatusMessage("문이 열렸습니다!");
					break;
				case RecognitionState::WAIT_CLOSE:
					view->showStatusMessage("문을 닫아주세요.");
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

	connect(faceRegisterPresenter, &FaceRegisterPresenter::registrationStarted, 
			this, [=]() { 
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

void FaceRecognitionPresenter::repaintCameraLabel(QLabel* label, const QImage& img)
{
	if (!label) return;
	if (img.isNull()) { label->clear(); return; }

	const QSize area = label->contentsRect().size();
	if (area.isEmpty()) return;

	const qreal dpr = label->devicePixelRatioF();

	const int tw = qMax(1, qRound(area.width() * dpr));
	const int th = qMax(1, qRound(area.height() * dpr));
	const QSize targetPx(tw, th);

	QImage scaled = img.scaled(
			targetPx,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation
	);

	label->setAlignment(Qt::AlignCenter);
	label->setPixmap(QPixmap::fromImage(scaled));
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
}
