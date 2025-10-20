#include "FaceRegisterPresenter.hpp"
#include <QInputDialog>
#include <QFont>
#include <QMessageBox>
#include <QDebug>

FaceRegisterPresenter::FaceRegisterPresenter(FaceRecognitionService* service, MainWindow* view, QObject* parent)
    : QObject(parent), service(service), view(view)
{
	qDebug() << "[FaceRegisterPresenter] constructor start!"; 
	//connectService();
	registeredUserCnt = 1;

    // 워치독 타이머 기본 설정
    m_registerTimer.setSingleShot(true);
    m_registerTimer.setTimerType(Qt::PreciseTimer);
	m_registerTimer.setParent(this);
    //m_registerTimer.moveToThread(this->thread());

    // 워치독 타임아웃: 강제 종료 + 디바운스 해제 + 버튼 복구
    connect(&m_registerTimer, &QTimer::timeout, this, [this]() {
		// sigleShot이면 이미 inactive라 stop()은 생략해도 됨
        // if (m_registerTimer.isActive()) m_registerTimer.stop();

        qDebug() << "[WD] timeout fired (presenter=" << this << " timer=" << &m_registerTimer << ")";
        if (this->service) {
            qDebug() << "[Presenter] Register mode Off (watchdog timeout)";
            this->service->setRegisterRequested(false);
			this->service->cancelRegistering();
			this->service->forceAbortRegistration();
        }
        m_registerInProgress = false;

		if (this->view && this->view->ui && this->view->ui->registerButton)
			this->view->ui->registerButton->setEnabled(true);
        emit registrationResult(false, "등록 타임아웃");
    });

    // 등록 완료(성공/실패/취소 공통) 신호
    connect(this->service,
            &FaceRecognitionService::registrationCompleted,      // void registrationCompleted(bool, const QString&)
            this,
            &FaceRegisterPresenter::onRegistrationCompleted,
            Qt::UniqueConnection);

    // 버튼 핸들러 연결도 여기서 (setupUi() 이후 보장)
    connect(view->ui->registerButton, &QPushButton::clicked,
            this, &FaceRegisterPresenter::onRegisterFace,
            Qt::UniqueConnection);
}

FaceRegisterPresenter::~FaceRegisterPresenter() 
{
    qDebug() << "[Presenter] dtor presenter=" << this << "timer=" << &m_registerTimer;
}

/*
void FaceRegisterPresenter::connectService() 
{
		if (!service) return;

		QObject::connect(service, &FaceRecognitionService::registerFinished, this, &FaceRegisterPresenter::presentRegistration);
}
*/

void FaceRegisterPresenter::presentRegistration(bool success, const QString& message)
{
		emit registrationResult(success, message);
}

void FaceRegisterPresenter::onRegisterFace() {

	// 0) 연타 디바운스
	if (m_registerInProgress) {
		qDebug() << "[Presenter] Register click ignored (already in progress)";
		return;
	}

	// 1) UI 입력
	emit registrationStarted();

	QInputDialog dlg(view);
	dlg.setWindowTitle(tr("사용자 등록"));
	dlg.setLabelText(tr("이름을 입력하시겠습니까?\n\n"
						"입력하지 않으면 'Registered user'로 저장 됩니다."));
	dlg.setInputMode(QInputDialog::TextInput);
	dlg.setStyleSheet(INPUT_DIALOG_STYLE);
	dlg.resize(500, 300);

	if (dlg.exec() != QDialog::Accepted) {
		qDebug() << "[onRegisterFace] cancelled -> skip";
		emit registrationResult(false, "등록이 취소되었습니다.");
		return;
	}

	QString name = dlg.textValue().trimmed();

	if (name.isEmpty()){
		name = QStringLiteral("Registered user_%1").arg(registeredUserCnt++);
	}


	qInfo() << "[FaceRegisterPresenter] registering name: " << name;	


	// 2) Service 존재/수명 체크
	if (!service) {
		emit registrationResult(false, "FaceRecognitionService가 존재하지 않습니다.");
		return;
	}

	// 3) 진행중 플래그 + 버튼 잠금 + 요청 On
	m_registerInProgress = true;


	if (view && view->ui && view->ui->registerButton) 
		view->ui->registerButton->setEnabled(false);

	service->setRegisterRequested(true);
	qDebug() << "[Presenter] Register mode On";

	if (m_registerTimer.isActive()) m_registerTimer.stop();


#ifdef DEBUG
	qDebug() << "[WD] started"
		<< " isActive=" << m_registerTimer.isActive()
		<< " rem=" << m_registerTimer.remainingTime()
		<< " thread(presenter)=" << this->thread()
		<< " thread(timer)=" << m_registerTimer.thread()
		<< " thread(service)=" << (service ? service->thread() : nullptr);
#endif

	    // [GUARD] 앱 루트에 매단 30초 단발 가드
    QPointer<FaceRegisterPresenter> self(this);
    auto guardHolder = new QObject(qApp);         // 앱 수명
    auto* guardTimer = new QTimer(guardHolder);
    guardTimer->setSingleShot(true);
    guardTimer->setTimerType(Qt::CoarseTimer);
    QObject::connect(guardTimer, &QTimer::timeout, guardHolder, [self, guardHolder]{
        qDebug() << "[WD-GUARD] fired";
        if (!self) { qDebug() << "[WD-GUARD] presenter destroyed -> skip"; guardHolder->deleteLater(); return; }
        if (!self->m_registerInProgress) { qDebug() << "[WD-GUARD] already resolved -> skip"; guardHolder->deleteLater(); return; }

        qDebug() << "[WD-GUARD] forcing abort";
        if (self->service) {
            self->service->setRegisterRequested(false);
            self->service->cancelRegistering();
            self->service->forceAbortRegistration();
        }
        self->m_registerInProgress = false;
        if (self->view && self->view->ui && self->view->ui->registerButton)
            self->view->ui->registerButton->setEnabled(true);

        emit self->registrationResult(false, "등록 타임아웃");
        guardHolder->deleteLater();
    }, Qt::QueuedConnection);
    guardTimer->start(10000);
    qDebug() << "[WD-GUARD] started";

    // 정상 종료 시 가드 제거
    auto clearGuard = [guardHolder](){
        if (guardHolder) { qDebug() << "[WD-GUARD] cleared"; guardHolder->deleteLater(); }
    };
    connect(this, &FaceRegisterPresenter::registrationResult, this, [clearGuard](bool, const QString&){
        clearGuard();
    }, Qt::UniqueConnection);

	QMetaObject::invokeMethod(&m_registerTimer, [this]() {
			m_registerTimer.start(30000);
			qDebug() << "[onRegisterFace] start(30000) on thread=" << QThread::currentThread();
			qDebug() << "[WD] started"
					 << " isActive=" << m_registerTimer.isActive()
					 << " rem(ms)=" << m_registerTimer.remainingTime()
					 << " presenter=" << this
					 << " timer=" << &m_registerTimer
					 << " thread=" << m_registerTimer.thread();
	}, Qt::QueuedConnection);

	qDebug() << "[onRegisterFace] watchdog set 30,000ms";

	// 5) 실제 등록 작업 시작 (서비스 스레드로 안전하게 넘김)
	QMetaObject::invokeMethod(service, [svc=service.data(), name]() {
 			qDebug() << "[Presenter->Service] invoke startRegistering name=" << name;
			if (svc) svc->startRegistering(name);
			}, Qt::QueuedConnection);

}

void FaceRegisterPresenter::onRegistrationCompleted(bool ok, const QString& msg)
{
		// 타이머 멈춤(경합 방지)
		if (m_registerTimer.isActive()) m_registerTimer.stop();

		// Off 보장 (중복 Off 방지: 현재 On일 때만)
		if (service) {
				qDebug() << "[Presenter] Register mode Off (completed)";
				service->setRegisterRequested(false);
		}


		// 디바운스 해제 + 결과 통지
		m_registerInProgress = false;
		if (view && view->ui && view->ui->registerButton)
				view->ui->registerButton->setEnabled(true);
		emit registrationResult(ok, msg);
}



