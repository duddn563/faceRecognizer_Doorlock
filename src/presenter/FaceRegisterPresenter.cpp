#include "FaceRegisterPresenter.hpp"
#include <QInputDialog>
#include <QDebug>

FaceRegisterPresenter::FaceRegisterPresenter(FaceRecognitionService* service, MainWindow* view, QObject* parent)
    : QObject(parent), service(service), view(view)
{
		qDebug() << "[FaceRegisterPresenter] constructor start!"; 
		connectService();

    // 워치독 타이머 기본 설정
    m_registerTimer.setSingleShot(true);
    m_registerTimer.setTimerType(Qt::PreciseTimer);
    m_registerTimer.moveToThread(this->thread());

    // 워치독 타임아웃: 강제 종료 + 디바운스 해제 + 버튼 복구
    connect(&m_registerTimer, &QTimer::timeout, this, [this]() {
        if (m_registerTimer.isActive()) m_registerTimer.stop();

        qDebug() << "[WD] timeout fired";
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
    }, Qt::UniqueConnection);

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

void FaceRegisterPresenter::connectService() 
{
		if (!service) return;

		QObject::connect(service, &FaceRecognitionService::registerFinished, this, &FaceRegisterPresenter::presentRegistration);
}

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
    QString name = QInputDialog::getText(view, "사용자 등록", "이름을 입력하세요:");
    if (name.isEmpty()) {
				emit registrationResult(false, "이름이 입력되지 않았습니다.");
				return;
		}

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
		m_registerTimer.start(12000);		// 10,000ms


#ifdef DEBUG
    qDebug() << "[WD] started"
             << " isActive=" << m_registerTimer.isActive()
             << " rem=" << m_registerTimer.remainingTime()
             << " thread(presenter)=" << this->thread()
             << " thread(timer)=" << m_registerTimer.thread()
             << " thread(service)=" << (service ? service->thread() : nullptr);
#endif

		// 5) 실제 등록 작업 시작 (서비스 스레드로 안전하게 넘김)
    QMetaObject::invokeMethod(service, [svc=service.data(), name]() {
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



