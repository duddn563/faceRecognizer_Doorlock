#pragma once

#include <QMainWindow>
#include <QList>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QInputDialog>
#include <QGraphicsDropShadowEffect>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QSizePolicy>
#include <QPointer>

#include "ui_MainWindow.h"
#include "faceRecognitionState.hpp"
#include "styleConstants.hpp"
#include "logger.hpp"
#include "services/UserImageService.hpp"

using namespace std;
class MainPresenter;

constexpr int WINDOW_MIN_WIDTH = 900;
constexpr int WINDOW_MIN_HEIGHT = 600;

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
		QDialog* getGalleryDialog() const;
		void showUserList(const QStringList& users);
		void reset();
		

signals:
		// Presenter로 전달할 사용자 행동 시그널
    void showUserImagesRequested();
    void deleteImageRequested(const QString& imagePath);
    void imageClicked(const QString& imagePath); // 미리보기 요청

		void stateChangedFromView(RecognitionState state);
		void registerFaceRequested();
		void resetRequested();
		void requestedShowUserList();


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

			MainPresenter* mainPresenter;

			QLabel *unlockOverlayLabel;
			QPointer<QDialog> galleryDialog = nullptr;

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




