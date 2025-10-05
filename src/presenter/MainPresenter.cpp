#include "MainPresenter.hpp"

MainPresenter::MainPresenter(MainWindow* view, QObject* p)
    : QObject(p), view(view)
{
	db_ = new QSqliteService(); 

	faceRecognitionService = new FaceRecognitionService(nullptr, nullptr, db_);
	faceRecognitionThread = new QThread();
	faceRecognitionService->moveToThread(faceRecognitionThread);

	faceRecognitionPresenter = new FaceRecognitionPresenter(faceRecognitionService, view, view);
	faceRecognitionService->setPresenter(faceRecognitionPresenter);

	bleThread = new QThread(this);

	bleServer = new BleServer(nullptr, faceRecognitionService);
	bleServer->setInterfaceName("hci0");
	bleServer->setDbPath("/root/trunk/faceRecognizer_Doorlock/assert/db/doorlock.db");

	// 스레드 이동
	bleServer->moveToThread(bleThread);
	// 수명 관리
	connect(bleThread, &QThread::finished, bleServer, &QObject::deleteLater);


	// 로깅/상태
	connect(bleServer, &BleServer::log, this, [](const QString& s){ qDebug().noquote() << s; });
	connect(bleServer, &BleServer::ready, this, [](){ qDebug() << "[Ble] ready"; });

	// 스레드 시작 시 run() 진입
	connect(bleThread, &QThread::started, bleServer, &BleServer::run, Qt::QueuedConnection);


	userImageService = new UserImageService(nullptr);
	userImagePresenter = new UserImagePresenter(userImageService, view);
	userImageService->setPresenter(userImagePresenter);


	qDebug() << "[MainPresenter] FRS addr:" << faceRecognitionService;
	qDebug() << "[MainPresenter] UIS addr:" << userImageService;

	qDebug() << "[MainPresenter] FST addr:" << faceRecognitionThread;

	connectUIEvents();
	startBle();
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

void MainPresenter::startBle()
{
	if (!bleThread->isRunning()) {
		bleThread->start();
	}
}

void MainPresenter::stopBle()
{
	if (bleThread->isRunning()) {
		QMetaObject::invokeMethod(bleServer, "stop", Qt::BlockingQueuedConnection);
		bleThread->quit();
		bleThread->wait();
	}
}


MainPresenter::~MainPresenter()
{
		faceRecognitionThread->quit();
		faceRecognitionThread->wait();

		faceRecognitionService->deleteLater();
		faceRecognitionThread->deleteLater();
		delete faceRecognitionPresenter;

		stopBle();

		delete userImagePresenter;
}
