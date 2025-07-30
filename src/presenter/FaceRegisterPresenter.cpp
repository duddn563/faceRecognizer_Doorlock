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

		QObject::connect(service, &FaceRecognitionService::registerFinished, this, &FaceRegisterPresenter::onRegisterFinished);
}

void FaceRegisterPresenter::onRegisterFinished(bool success, const QString& message)
{
		if (view) {
				view->setCurrentUiState(UiState::IDLE);
				view->showInfo("등록 결과", message);
		}
}

void FaceRegisterPresenter::onRegisterFace() {
    if (view->getCurrentUiState() != UiState::IDLE) return;
    view->setCurrentUiState(UiState::REGISTERING);


    QString name = QInputDialog::getText(view, "사용자 등록", "이름을 입력하세요:");
    view->setCurrentUiState(UiState::IDLE);

    if (name.isEmpty()) return;

    if (service) {
        QMetaObject::invokeMethod(service, [this, name]() {
            if (this->service)
                this->service->startRegistering(name);
        }, Qt::QueuedConnection);
    } else {
        qDebug() << "[Error] FaceRecognitionService is null!";
    }
}

