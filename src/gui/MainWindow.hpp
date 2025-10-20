#pragma once

#include <QMainWindow>
#include <QVector>
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
#include <QFileDialog>

#include "ui_MainWindow.h"
#include "styleConstants.hpp"
#include "log/logger.hpp"
#include "services/UserImageService.hpp"
#include "include/LogDtos.hpp"
#include "ControlTabView.hpp"
#include "StyledMsgBox.hpp"
#include "include/states.hpp"



class QStandardItemModel;          
class QSortFilterProxyModel;      
class LogTab;
class DevInfoDialog;
class DevInfoTab;
class LedWidget;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

// === Window Minimum Size ===
constexpr int WINDOW_MIN_WIDTH = 900;
constexpr int WINDOW_MIN_HEIGHT = 600;

// === Types ===
enum class UiState {
		IDLE,
		REGISTERING,
		PREVIEWING
};

// === MainWindow ===
class MainPresenter;
class MainWindow : public QMainWindow {
		Q_OBJECT

public:
		// === Lifecycle / Public API ===
		MainWindow(QWidget *parent = nullptr);
		~MainWindow();
			
		// == view surface (Presenter가 직접 만지는 영역) ===
		// 재할당 금지: 포인터는 const, 내부 위젯은 수정 가능
		Ui::MainWindow* ui;

		// Presenter에서 호출용 메서드
        void showUserImageGallery(const QList<UserImage>& images);
		void showImagePreview(const QString& imagePath);
        void showInfo(const QString& title, const QString& message);
        void showError(const QString& title, const QString& message);
		void showStatusMessage(const QString& msg);
		RecognitionState getRecognitionState();
		void setCurrentUiState(UiState state);
		UiState getCurrentUiState(); 
		QDialog* getGalleryDialog() const;
		void showUserList(const QStringList& users);
		void reset();
		void showUnlockOverlayLabel();

		void PresentCamRestart(const QString& msg);
		void PresentDoorOpen(const QString& msg);
		void PresentDoorClose(const QString& msg);
		void PresentRetrainRecog(const QString& msg);

		// BLE / Door 상태 enum
		//enum class BleState { Idle, Scanning, Connected, Disconnected };
		//enum class DoorState { Locked, Open };

signals:
		// === Outgoing events to Presenter === 
    void showUserImagesRequested();
    void deleteImageRequested(const QString& imagePath);
    void imageClicked(const QString& imagePath); // 미리보기 요청
	void stateChangedFromView(RecognitionState state);
	void registerFaceRequested();
	void resetRequested();
	void requestedShowUserList();
	void registerClicked();
	void CamRestart();
	void doorOpen();
	void doorClose();
	void retrainRecog();

public slots:
	void onBleStateChanged(States::BleState s);
	void onDoorStateChanged(States::DoorState s);

private:
		// == Setup helpers ===
		void setupUi();
		void setupControlTab();
		void applyStyles();
		void setupButtonLayout();
		void showErrorMessage(const QString& title, const QString& message);
		void closeEvent(QCloseEvent* e);
		QList<QPushButton*> buttonList() const;
		LogTab* logTab = nullptr;
		DevInfoDialog* devInfoDlg_ = nullptr;
		DevInfoTab* devInfoTab = nullptr;
private:
		// === Internal state/refs === 
		QTimer* timer = nullptr;
		MainPresenter* mainPresenter;
		QPixmap standbyOrig_;
		bool firstFrameShown_ = false;


		QTimer* cameraWatchdog_ = nullptr;
		qint64 lastFrameMs_ = 0;
		QPointer<QDialog> galleryDialog = nullptr;
		UiState currentUiState = UiState::IDLE;
		RecognitionState currentRecognitionState = RecognitionState::IDLE;

        int authPage = 0, sysPage = 0;
        const int pageSize = 50;



        // View log
        // 정렬 지원용: 뷰에 QSortFilterProxyModel 적용
        QStandardItemModel* authModel = nullptr;
        QSortFilterProxyModel* authProxy = nullptr;

        QStandardItemModel* sysModel = nullptr;
        QSortFilterProxyModel* sysProxy = nullptr;

        void connectSignals();

};

// 클릭 가능한 라벨
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




