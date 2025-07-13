#include "FaceRecognitionPresenter.hpp"
#include "FaceRecognitionService.hpp"
#include "MainWindow.hpp"

FaceRecognitionPresenter::FaceRecognitionPresenter(FaceRecognitionService* service, MainWindow* view, QObject* parent = nullptr)
			: QObject(parent), service(service), view(view)
{
		std::cout << "Face Recognition Presenter be create thread!!" << std::endl;
		thread = new QThread(this);

		service->moveToThread(thread);

		connect(thread, &QThread::started, service, &FaceRecognitionService::procFrame);
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
}

void FaceRecognitionPresenter::faceRecognitionStart()
{
		std::cout << "FaceRecognition Presenter Start!!" << std::endl;
		thread->start();
}

FaceRecognitionPresenter::~FaceRecognitionPresenter()
{
		if (service) service->stop();
		thread->quit();
		thread->wait();

		service->deleteLater();
		thread->deleteLater();

		std::cout << "FaceRecognition Presenter exit!!" << std::endl;

		delete service;
}
