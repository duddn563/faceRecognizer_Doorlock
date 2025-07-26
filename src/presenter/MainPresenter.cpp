#include "MainPresenter.hpp"

MainPresenter::MainPresenter(MainWindow* view)
		: view(view)
{
		faceRecognitionService = new FaceRecognitionService();
		faceRecognitionThread = new QThread();
		faceRecognitionService->moveToThread(faceRecognitionThread);

		faceRecognitionPresenter = new FaceRecognitionPresenter(faceRecognitionService, view, view);
		connect(faceRecognitionThread, &QThread::started, faceRecognitionService, &FaceRecognitionService::procFrame);

		faceRegisterPresenter = new FaceRegisterPresenter(faceRecognitionService, view);

		faceSensorService = new FaceSensorService();
		faceSensorThread = new QThread();
		faceSensorService->moveToThread(faceSensorThread);
		//faceSensorService->setParent(view);

		faceSensorPresenter = new FaceSensorPresenter(faceSensorService, view, view);
		connect(faceSensorThread, &QThread::started, faceSensorService, &FaceSensorService::run);


		doorSensorService = new DoorSensorService();
		doorSensorThread = new QThread();
		doorSensorService->moveToThread(doorSensorThread);
		//doorSensorService->setParent(view);

		doorSensorPresenter = new DoorSensorPresenter(doorSensorService, view, view);
		connect(doorSensorThread, &QThread::started, doorSensorService, &DoorSensorService::run);

		userImagePresenter = new UserImagePresenter(view);

		connectUIEvents();

}

void MainPresenter::startAllServices()
{
		faceRecognitionThread->start();
		faceSensorThread->start();
		doorSensorThread->start();
}

void MainPresenter::connectUIEvents()
{
		connect(view, &MainWindow::showUserImagesRequested, userImagePresenter, &UserImagePresenter::handleShowImages);

		connect(view, &MainWindow::imageClicked, userImagePresenter, &UserImagePresenter::handleImagePreview);

		connect(view, &MainWindow::deleteImageRequested, userImagePresenter, &UserImagePresenter::handleDeleteImage);
}

MainPresenter::~MainPresenter()
{
		faceRecognitionService->stop();
		faceRecognitionThread->quit();
		faceRecognitionThread->wait();

		faceRecognitionService->deleteLater();
		faceRecognitionThread->deleteLater();
		delete faceRecognitionPresenter;

		faceSensorService->stop();
		faceSensorThread->quit();
		faceSensorThread->wait();

		faceSensorService->deleteLater();
		faceSensorThread->deleteLater();
		delete faceSensorPresenter;

		doorSensorService->stop();
		doorSensorThread->quit();
		doorSensorThread->wait();

		doorSensorService->deleteLater();
		doorSensorThread->deleteLater();
		delete doorSensorPresenter;

		delete userImagePresenter;
}
