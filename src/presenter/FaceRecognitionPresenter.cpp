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
		
		connect(service, &FaceRecognitionService::frameReady, this, [=](const QImage& image) {
					if (!image.isNull()) {
							view->ui->cameraLabel->setPixmap(QPixmap::fromImage(image));
					}
					else {
							std::cout << "Image is null!" << std::endl;
					}
		}, Qt::QueuedConnection);

		//connect(service, &FaceRecognitionService::stateChanged, this, &FaceRecognitionPresenter::handleStateChanged);
		/*
		connect(service, &FaceRecognitionService::stateChanged, this, [=](RecognitionState state) {
					if (state == RecognitionState::REGISTERING) {
							QString msg = QString("'%1' 사용자 등록 중...").arg(service->getUserName());
							view->showStatusMessage(msg);
					}
					else if (state != RecognitionState::DUPLICATEDFACE) {
							view->setRecognitionState(state);			
					}
		});
		*/

		connect(service, &FaceRecognitionService::stateChanged, this, [=] (RecognitionState s) {
					switch (s) {
							case RecognitionState::IDLE: 	
																										view->showStatusMessage("대기 중...");
																										break;
							case RecognitionState::DOOR_OPEN:
																										view->showStatusMessage("Door was opened");
																										break;
							case RecognitionState::DETECTING:
																										view->showStatusMessage("Face was detected");
																										break;
							case RecognitionState::REGISTERING:
																										view->showStatusMessage("등록중...");
																										break;
							case RecognitionState::DUPLICATE_FACE:
																										view->showStatusMessage("Duplicated face...");
																										view->showInfo("중복 사용자", "이미 등록된 얼굴입니다.");
																										break;
							case RecognitionState::RECOGNIZING:
																										view->showStatusMessage("Recognizing...");
																										break;
							case RecognitionState::AUTH_SUCCESS:
																										view->showStatusMessage("Auth success!!");
																										view->showUnlockOverlayLabel();
																										break;
							case RecognitionState::AUTH_FAIL:		
																										view->showStatusMessage("Auth fail!!");
																										break;
							case RecognitionState::LOCKED_OUT:		
																										view->showStatusMessage("Locked out...");
																										break;

							default:
				 																					view->showStatusMessage("Unkown state!!");
					}

		});



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
/*
void FaceRecognitionPresenter::handleStateChanged(RecognitionState state) 
{
		if (state == RecognitionState::DUPLICATEDFACE) {
				view->showInfo("중복 사용자", "이미 등록된 얼굴입니다.");
		}
}
*/

void FaceRecognitionPresenter::onRegisterFace() 
{
		qDebug() << "[FaceRecognitionPresenter] onRegisterFace() is called";
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
