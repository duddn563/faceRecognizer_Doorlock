#include "MainPresenter.hpp"

MainPresenter::MainPresenter(MainWindow* view, QObject* p)
    : QObject(p), view(view)
{
        db_ = new QSqliteService(); 

        //connect(this, &MainPresenter::deliverAuthLogs, view, &MainWindow::renderAuthLogs);
        //connect(this, &MainPresenter::deliverSystemLogs, view, &MainWindow::renderSystemLogs);

		faceRecognitionService = new FaceRecognitionService(nullptr, nullptr, db_);
		faceRecognitionThread = new QThread();
		faceRecognitionService->moveToThread(faceRecognitionThread);

		faceRecognitionPresenter = new FaceRecognitionPresenter(faceRecognitionService, view, view);
		//connect(faceRecognitionThread, &QThread::started, faceRecognitionService, &FaceRecognitionService::procFrame);
		// QThread 시작 시 서비스 타이머 연결
		QObject::connect(faceRecognitionThread, &QThread::started, faceRecognitionService, [svc=faceRecognitionService](){
				auto timer = new QTimer(svc);
				QObject::connect(timer, &QTimer::timeout, svc, &FaceRecognitionService::procFrame);
				timer->start(30);  // 30ms마다 1프레임
		});

		faceRecognitionService->setPresenter(faceRecognitionPresenter);

		faceSensorService = new FaceSensorService();
		faceSensorThread = new QThread();
		faceSensorService->moveToThread(faceSensorThread);

		faceSensorPresenter = new FaceSensorPresenter(faceSensorService, view, view);
		connect(faceSensorThread, &QThread::started, faceSensorService, &FaceSensorService::run);


		/*
		doorSensorService = new DoorSensorService();
		doorSensorThread = new QThread();
		doorSensorService->moveToThread(doorSensorThread);
		*/

		//doorSensorPresenter = new DoorSensorPresenter(doorSensorService, view, view);
		//connect(doorSensorThread, &QThread::started, doorSensorService, &DoorSensorService::run);

		userImageService = new UserImageService(nullptr);
		userImagePresenter = new UserImagePresenter(userImageService, view);
		userImageService->setPresenter(userImagePresenter);


		qDebug() << "[MainPresenter] FRS addr:" << faceRecognitionService;
		qDebug() << "[MainPresenter] DSS addr:" << faceSensorService;
		qDebug() << "[MainPresenter] UIS addr:" << userImageService;

		qDebug() << "[MainPresenter] FRT addr:" << faceSensorThread;
		qDebug() << "[MainPresenter] FST addr:" << faceRecognitionThread;

		connectUIEvents();

}


void MainPresenter::requestAuthPage(int page, int pageSize, const QString& userLike) 
{
    const int offset = page * pageSize;
    QVector<AuthLog> rows; int total=0;
    if (db_->selectAuthLogs(offset, pageSize, userLike, &rows, &total)) {
        emit deliverAuthLogs(rows, page, total, pageSize);
    }
}

void MainPresenter::requestSystemPage(int page, int pageSize, int minLevel,
                                      const QString& tagLike, const QString& sinceIso) 
{
    const int offset = page * pageSize;
    QVector<SystemLog> rows; int total=0;
    if (db_->selectSystemLogs(offset, pageSize, minLevel, tagLike, sinceIso, &rows, &total)) {
        emit deliverSystemLogs(rows, page, total, pageSize);
    }
}

void MainPresenter::startAllServices()
{
		faceRecognitionThread->start();
		faceSensorThread->start();
		//doorSensorThread->start();
}

void MainPresenter::connectUIEvents()
{
    connect(view, &MainWindow::showUserImagesRequested, userImagePresenter, &UserImagePresenter::onShowImages);
    connect(view, &MainWindow::imageClicked, userImagePresenter, &UserImagePresenter::handleImagePreview);
    connect(view, &MainWindow::deleteImageRequested, userImagePresenter, &UserImagePresenter::handleDeleteImage);
    connect(view, &MainWindow::registerFaceRequested, faceRecognitionPresenter, &FaceRecognitionPresenter::onRegisterFace);
    connect(view, &MainWindow::resetRequested, faceRecognitionPresenter, &FaceRecognitionPresenter::onReset);
    connect(view, &MainWindow::requestedShowUserList, userImagePresenter, &UserImagePresenter::onShowUserList);

    qDebug() << "[Presenter] UI events connected";
}


MainPresenter::~MainPresenter()
{
		faceSensorService->stop();
		faceSensorThread->quit();
		faceSensorThread->wait();

		faceSensorService->deleteLater();
		faceSensorThread->deleteLater();
		delete faceSensorPresenter;


		faceRecognitionService->stop();
		faceRecognitionThread->quit();
		faceRecognitionThread->wait();

		faceRecognitionService->deleteLater();
		faceRecognitionThread->deleteLater();
		delete faceRecognitionPresenter;
		//doorSensorService->stop();
		//doorSensorThread->quit();
		//doorSensorThread->wait();

		//doorSensorService->deleteLater();
		//doorSensorThread->deleteLater();

		delete userImagePresenter;
}
