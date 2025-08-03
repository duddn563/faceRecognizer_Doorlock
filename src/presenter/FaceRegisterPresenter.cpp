#include "FaceRegisterPresenter.hpp"
#include <QInputDialog>
#include <QDebug>

FaceRegisterPresenter::FaceRegisterPresenter(FaceRecognitionService* service, MainWindow* view, QObject* parent)
    : QObject(parent), service(service), view(view)
{
		qDebug() << "[FaceRegisterPresenter] constructor start!"; 
		connectService();
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
		emit registrationStarted();

    QString name = QInputDialog::getText(view, "사용자 등록", "이름을 입력하세요:");

    if (name.isEmpty()) {
				emit registrationResult(false, "이름이 입력되지 않았습니다.");
				return;
		}

    if (service) {
        QMetaObject::invokeMethod(service, [this, name]() {
            if (this->service)
                this->service->startRegistering(name);
        }, Qt::QueuedConnection);
    } else {
				emit registrationResult(false, "FaceRecognitionService가 존재하지 않습니다.");
    }
}

