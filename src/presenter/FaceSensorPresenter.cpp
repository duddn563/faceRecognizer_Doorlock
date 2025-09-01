#include "FaceSensorPresenter.hpp"
#include "FaceSensorService.hpp"
#include "MainWindow.hpp"

FaceSensorPresenter::FaceSensorPresenter(FaceSensorService* service, MainWindow* view, QObject* parent = nullptr)
			: QObject(parent), service(service), view(view) 
{
		connect(service, &FaceSensorService::personDetected, view, [=]() {
						//view->showStatusMessage("Face Detected");
						//view->setRecognitionState(RecognitionState::DETECTING_PERSON);
						std::cout << "Face detected" << std::endl;
						view->ui->cameraLabel->setVisible(true);
						if (view->ui->standbyLabel) view->ui->standbyLabel->setVisible(false);

		});

		connect(service, &FaceSensorService::personLeft, view, [=]() {
						view->showStatusMessage("Face Left");
						view->ui->cameraLabel->setVisible(false);
						if (view->ui->standbyLabel) view->ui->standbyLabel->setVisible(true);
						//std::cout << "Face left" << std::endl;
		});
}

FaceSensorPresenter::~FaceSensorPresenter()
{
		std::cout << "[FaceSensorPresenter] The face sensor presenter disappeared." << std::endl;
}

