#include "FaceRecognitionPresenter.hpp"
#include "FaceRecognitionService.hpp"
#include "MainWindow.hpp"

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
		connect(service, &FaceRecognitionService::stateChanged, this, [=](RecognitionState state) {
					view->setRecognitionState(state);			
		});

		connect(view, &MainWindow::stateChangedFromView, this, &FaceRecognitionPresenter::onViewStateChanged);
}

void FaceRecognitionPresenter::onViewStateChanged(RecognitionState state)
{
		if (state == RecognitionState::IDLE) {
				service->resetUnlockFlag();
		}
}

FaceRecognitionPresenter::~FaceRecognitionPresenter()
{
	/*
		if (service) service->stop();
		thread->quit();
		thread->wait();

		service->deleteLater();
		thread->deleteLater();

		delete service;
	*/
		std::cout << "FaceRecognition Presenter exit!!" << std::endl;

}

