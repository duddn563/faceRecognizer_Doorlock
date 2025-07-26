#pragma once

#include <QMainWindow>
#include <QList>
#include <QString>
#include "services/UserImageService.hpp" // 이미지 로딩, 삭제 서비스
#include "services/DoorSensorService.hpp"
#include "services/FaceSensorService.hpp"
#include "services/FaceRecognitionService.hpp"

#include <QThread>
#include <QTimer>
#include <QInputDialog>
#include <QGraphicsDropShadowEffect>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QSizePolicy>

#include "ui_MainWindow.h"
#include "faceRecognitionState.hpp"
#include "styleConstants.hpp"
#include "logger.hpp"

using namespace std;

constexpr int WINDOW_MIN_WIDTH = 900;
constexpr int WINDOW_MIN_HEIGHT = 600;

class FaceRecognitionService;
class UserImagePresenter;;
class DoorSensorPresenter;
class FaceSensorPresenter;
class FaceRecognitionPresenter;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

enum class UiState {
		IDLE,
		REGISTERING,
		PREVIEWING
};

class MainWindow : public QMainWindow {
		Q_OBJECT

public:
			MainWindow(QWidget *parent = nullptr);
			~MainWindow();
			
		Ui::MainWindow* ui;

		// Presenter에서 호출
    void showUserImageGallery(const QList<UserImage>& images);
		void showImagePreview(const QString& imagePath);
    void showInfo(const QString& title, const QString& message);
    void showError(const QString& title, const QString& message);
		void showStatusMessage(const QString& msg);

		void setRecognitionState(RecognitionState newState);
		RecognitionState getRecognitionState();

		void setCurrentUiState(UiState state);
		UiState getCurrentUiState(); 
		

signals:
		// Presenter로 전달할 사용자 행동 시그널
    void showUserImagesRequested();
    void deleteImageRequested(const QString& imagePath);
    void imageClicked(const QString& imagePath); // 미리보기 요청

		void stateChangedFromView(RecognitionState state);
		void registerFaceRequested();

private slots:
			void onExitProgram();
			void onShowUserImages();
			void onClearUsers();
			void onShowUserList();

public slots:
			void showDuplicateUserMessage();
		

private:
			void setupUi();
			void applyStyles();
			void connectSignals();
			QList<QPushButton*> buttonList() const;

			void showErrorMessage(const QString& title, const QString& message);

			void setupButtonLayout();

			void resizeEvent(QResizeEvent *event);
			void showEvent(QShowEvent *event);
			void setupUnlockOverlayLabel();	
			void updateUnlockOverlay();

private:
			QTimer* timer = nullptr;

			UserImagePresenter* userImagePresenter;
			DoorSensorPresenter* doorSensorPresenter;
			FaceSensorPresenter* faceSensorPresenter;
			FaceRecognitionPresenter* faceRecognitionPresenter;

			FaceRecognitionService* faceRecognitionService;

			QLabel *unlockOverlayLabel;

			UiState currentUiState = UiState::IDLE;
			RecognitionState currentRecognitionState = RecognitionState::IDLE;
};

class ClickableLabel : public QLabel {
		Q_OBJECT
public:
			explicit ClickableLabel(const QString& imagePath, QWidget* parent = nullptr)
				: QLabel(parent), imagePath(imagePath) {
						setCursor(Qt::PointingHandCursor);
			}

signals:
			void clicked(const QString& imagePath);

protected:
			void mousePressEvent(QMouseEvent* event) override {
					emit clicked(imagePath);
					QLabel::mousePressEvent(event);
			}


private:
			QString imagePath;
};




