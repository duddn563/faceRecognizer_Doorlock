#include "DoorSensorPresenter.hpp"
#include "DoorSensorService.hpp"
#include "MainWindow.hpp"

DoorSensorPresenter::DoorSensorPresenter(DoorSensorService *service, MainWindow* view, QObject* parent = nullptr)
			: QObject(parent), service(service), view(view) 
{
		cout << "Create DoorSensor Presenter!" << endl;

		connect(service, &DoorSensorService::doorClosed, view, [=]() {
					std::cout << "emit doorClosed" << std::endl;
					view->showStatusMessage("문이 닫힌 상태입니다.");
		});

		connect(service, &DoorSensorService::doorOpened, view, [=]() {
					std::cout << "Emit doorOpened" << std::endl;
					view->showStatusMessage("문이 열렸습니다...");
		});
}

DoorSensorPresenter::~DoorSensorPresenter()
{
		std::cout << "[DoorSensorPresenter] The Door sensor presenter disappeared." << std::endl;
}
