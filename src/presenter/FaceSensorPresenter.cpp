#include "FaceSensorPresenter.hpp"
#include "FaceSensorService.hpp"
#include "MainWindow.hpp"

FaceSensorPresenter::FaceSensorPresenter(FaceSensorService* service, MainWindow* view, QObject* parent = nullptr)
			: QObject(parent), service(service), view(view) 
{
		//thread = new QThread(this);

		//service->moveToThread(thread);

		//connect(thread, &QThread::started, service, &FaceSensorService::run);

		connect(service, &FaceSensorService::personDetected, view, [=]() {
						view->showStatusMessage("Face Detected");
						//view->setRecognitionState(RecognitionState::DETECTING_PERSON);
						std::cout << "Face detected" << std::endl;
		});

		connect(service, &FaceSensorService::personLeft, view, [=]() {
						view->showStatusMessage("Face Left");
						std::cout << "Face left" << std::endl;
		});
}

/*
void FaceSensorPresenter::faceSensorStart()
{
		thread->start();
}
*/

FaceSensorPresenter::~FaceSensorPresenter()
{
	/*
		if (service) service->stop();
		thread->quit();
		thread->wait();

		service->deleteLater();
		thread->deleteLater();

		delete service;
	*/
	std::cout << "[FaceSensorPresenter] The face sensor presenter disappeared." << std::endl;
}

