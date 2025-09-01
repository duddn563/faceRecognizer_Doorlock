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

		throttleTimer_ = new QTimer(this);
		throttleTimer_->setInterval(40);
		throttleTimer_->setSingleShot(true);
		
		/*
		connect(service, &FaceRecognitionService::frameReady, this, [=](const QImage& image) {
					if (!image.isNull()) {
							view->ui->cameraLabel->setPixmap(QPixmap::fromImage(image));
					}
					else {
							std::cout << "Image is null!" << std::endl;
					}
		}, Qt::QueuedConnection);
		*/

		static bool first = true;
		connect(service, &FaceRecognitionService::frameReady, this, [=](const QImage& image) {
		    // 카메라 라벨이 화면에 "보이지" 않으면 드롭 (대기화면일 때)
		    if (!view->ui->cameraLabel->isVisible()) {
		        return;
		    }
		
		    if (!image.isNull()) {
						if (first && view->ui->stackedWidget && view->ui->cameraLabel) {
								view->ui->stackedWidget->setCurrentWidget(view->ui->cameraLabel);
								first = false;
						}
						view->ui->cameraLabel->setPixmap(QPixmap::fromImage(image));

						/*
		        const QSize target = view->ui->cameraLabel->size();
						view->ui->cameraLabel->setPixmap(QPixmap::fromImage(image));
		        QPixmap pm = QPixmap::fromImage(image)
		                        .scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		        view->ui->cameraLabel->setPixmap(pm);
		        lastFrame_ = image;  // 리사이즈 대응용 캐시
						*/
		    } else {
		        qDebug() << "Image is null!";
		    }
		}, Qt::QueuedConnection);

		
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
																										view->showStatusMessage("인증 성공!!");
																										//view->showUnlockOverlayLabel();
																										break;
							case RecognitionState::AUTH_FAIL:		
																										view->showStatusMessage("인증 실패!!");
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
		if (!view) return;

		view->reset();
}

FaceRecognitionPresenter::~FaceRecognitionPresenter()
{
		std::cout << "FaceRecognition Presenter exit!!" << std::endl;
}
