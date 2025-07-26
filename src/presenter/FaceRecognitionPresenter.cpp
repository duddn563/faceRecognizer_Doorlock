#include "FaceRecognitionPresenter.hpp"
#include "FaceRecognitionService.hpp"
#include "MainWindow.hpp"

#include <QInputDialog>
#include <QMessageBox>

FaceRecognitionPresenter::FaceRecognitionPresenter(FaceRecognitionService* service, MainWindow* view, QObject* parent = nullptr)
			: QObject(parent), service(service), view(view)
{
		connect(service, &FaceRecognitionService::frameReady, this, [=](const QImage& image) {
					if (!image.isNull()) {
							view->ui->cameraLabel->setPixmap(QPixmap::fromImage(image));
					}
					else {
							std::cout << "Image is null!" << std::endl;
					}
		}, Qt::QueuedConnection);
		connect(service, &FaceRecognitionService::stateChanged, this, &FaceRecognitionPresenter::handleStateChanged);
		connect(service, &FaceRecognitionService::stateChanged, this, [=](RecognitionState state) {
					if (state == RecognitionState::REGISTERING) {
							QString msg = QString("'%1' 사용자 등록 중...").arg(service->getUserName());
							view->showStatusMessage(msg);
					}
					else if (state != RecognitionState::DUPLICATEDFACE) {
							view->setRecognitionState(state);			
					}
		});

		connect(view, &MainWindow::stateChangedFromView, this, &FaceRecognitionPresenter::onViewStateChanged);
}

void FaceRecognitionPresenter::onViewStateChanged(RecognitionState state)
{
		if (state == RecognitionState::IDLE) {
				service->resetUnlockFlag();
		}
}

void FaceRecognitionPresenter::handleStateChanged(RecognitionState state) 
{
		if (state == RecognitionState::DUPLICATEDFACE) {
				view->showDuplicateUserMessage();
		}
}

FaceRecognitionPresenter::~FaceRecognitionPresenter()
{
		std::cout << "FaceRecognition Presenter exit!!" << std::endl;
}

