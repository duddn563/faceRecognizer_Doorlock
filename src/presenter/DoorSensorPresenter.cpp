#include "DoorSensorPresenter.hpp"
#include "DoorSensorService.hpp"
#include "MainWindow.hpp"
//#include "faceRecognizerWorker.hpp"

DoorSensorPresenter::DoorSensorPresenter(DoorSensorService *service, MainWindow* view, QObject* parent = nullptr)
			: QObject(parent), service(service), view(view) 
{
		//thread = new QThread(this);
		//service->moveToThread(thread);

		//connect(thread, &QThread::started, service, &DoorSensorService::run);
		cout << "DoorSensor Presenter Create" << endl;

		connect(service, &DoorSensorService::doorClosed, view, [=]() {
					std::cout << "emit doorClosed" << std::endl;
					view->showStatusMessage("문이 닫힌 상태입니다.");
		});

		connect(service, &DoorSensorService::doorOpened, view, [=]() {
					std::cout << "Emit doorOpened" << std::endl;
					view->showStatusMessage("문이 열렸습니다...");
					view->setRecognitionState(RecognitionState::IDLE);
		});
}

/*
void DoorSensorPresenter::doorSensorStart()
{
		thread->start();
}
*/

DoorSensorPresenter::~DoorSensorPresenter()
{
		//if (service) service->stop();
		//thread->quit();
		//thread->wait();

		//service->deleteLater();
		//thread->deleteLater();

		//delete service;

		std::cout << "[DoorSensorPresenter] The Door sensor presenter disappeared." << std::endl;
}
