#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QDialog>
#include <QPixmap>
#include <QMouseEvent>
#include <QMessageBox>
#include <QDialogButtonBox>

#include "presenter/MainPresenter.hpp"

//#define DEBUG

using namespace std;

namespace {
    inline void setActionMsg(Ui::MainWindow* ui, const QString& msg) {
        if (ui->actionStatusLabel) {
            ui->actionStatusLabel->setText(msg);
        } else {
            // fallback: 상태바에라도 보여주기
            if (ui->statusbar) ui->statusbar->showMessage(msg, 3000);
        }
    }
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) 
{
    qRegisterMetaType<RecognitionState>("RecognitionState");

		setupUi();

		mainPresenter = new MainPresenter(this);
		if (!mainPresenter) {
				qDebug() << "[MainWindow] Failed to allocate memory"; 
				return;
		}
		mainPresenter->startAllServices();

		// 컨트롤 탭 버튼/라벨이 실제로 존재하는지 방어
    Q_ASSERT(ui->rightTabWidget);
    Q_ASSERT(ui->tabControl);
    Q_ASSERT(ui->registerButton);
    Q_ASSERT(ui->resetButton);
    Q_ASSERT(ui->showUsersList);
    Q_ASSERT(ui->showUserImages);
    Q_ASSERT(ui->ExitButton);
    Q_ASSERT(ui->recognitionLabel);

		setupControlTab();
}

void MainWindow::setupControlTab()
{
    // --- 새로 추가한 "장비 제어" Action 그룹 ---
/*	
    if (ui->btnFetchStatus) {
        connect(ui->btnFetchStatus, &QPushButton::clicked, this, [this](){
            setActionMsg(ui, "장비 상태 스냅샷 요청");
            // TODO: presenter_->onClickFetchStatus();
            // 나중에 DeviceStatus 수집 후, 각 라벨/뷰 업데이트
        });
    }
*/


    if (ui->btnRefresh) {
        connect(ui->btnRefresh, &QPushButton::clicked, this, [this](){
            setActionMsg(ui, "화면 새로고침");
            // TODO: presenter_->onClickRefresh();  // 캐시 무효화 + 최신 스냅샷 표시
        });
    }

    if (ui->btnRestartCamera) {
        connect(ui->btnRestartCamera, &QPushButton::clicked, this, [this](){
            const auto ret = QMessageBox::question(
                this, tr("카메라 재시작"),
                tr("카메라 스트림을 재시작할까요? 진행 중인 인식이 잠시 중단됩니다."),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ret == QMessageBox::Yes) {
                setActionMsg(ui, "카메라 재시작 요청");
                // TODO: frService_->restartCamera();
            }
        });
    }

    if (ui->btnUnlockDoor) {
        connect(ui->btnUnlockDoor, &QPushButton::clicked, this, [this](){
            const auto ret = QMessageBox::warning(
                this, tr("도어 열기"),
                tr("도어를 수동으로 엽니다. 정말 진행할까요?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ret == QMessageBox::Yes) {
                setActionMsg(ui, "도어 열기 요청");
                // TODO: doorService_->requestUnlock();
            }
        });
    }

    if (ui->btnLockDoor) {
        connect(ui->btnLockDoor, &QPushButton::clicked, this, [this](){
            setActionMsg(ui, "도어 잠금 요청");
            // TODO: doorService_->requestLock();
        });
    }

    if (ui->btnRetrain) {
        connect(ui->btnRetrain, &QPushButton::clicked, this, [this](){
            const auto ret = QMessageBox::question(
                this, tr("재학습"),
                tr("인식기를 재학습할까요? (시간이 소요될 수 있습니다)"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ret == QMessageBox::Yes) {
                setActionMsg(ui, "재학습 요청");
                // TODO: frService_->retrainAsync();
            }
        });
    }

    if (ui->btnExportLogs) {
        connect(ui->btnExportLogs, &QPushButton::clicked, this, [this](){
            const QString path = QFileDialog::getSaveFileName(
                this, tr("로그 내보내기"), QDir::homePath() + "/doorlock_logs.txt",
                tr("Text Files (*.txt);;All Files (*)"));
            if (path.isEmpty()) return;

            setActionMsg(ui, "로그 내보내기 요청");
            // TODO: Logger::exportRecent(path);
            // 예시: 성공으로 가정
            setActionMsg(ui, tr("로그를 내보냈습니다: %1").arg(path));
        });
    }

    // 탭 가시성 이벤트(컨트롤 탭 보일 때만 갱신/폴링 시작하려면 여기서 훅)
    connect(ui->rightTabWidget, &QTabWidget::currentChanged, this, [this](int idx){
        const bool controlTabVisible = (ui->rightTabWidget->widget(idx) == ui->tabControl);
        if (controlTabVisible) {
            setActionMsg(ui, "Control 탭 활성화");
            // TODO: presenter_->onTabShown();
        } else {
            // TODO: presenter_->onTabHidden();
        }
    });
}

void MainWindow::setupUi() 
{
		// 1) Main Mindow setup
    ui->setupUi(this);
    setMinimumSize(WINDOW_MIN_WIDTH, WINDOW_MIN_HEIGHT);

		// 2) CameraLabel setup
    if (ui->cameraLabel) {
        ui->cameraLabel->setStyleSheet(CAM_LABEL_STYLE);
    }

		// 3) StandbyLabel setup
	  // 3-1) 경로 구성 (실행파일 기준 상대경로) 
		QString path = QString(IMAGES_PATH) + QString(STANDBY_IMAGE);


    // 3-2) 로드 확인
		QPixmap pm;
		if (pm.load(path)) {
				standbyOrig_ = pm; // 원본 보관
				ui->standbyLabel->setScaledContents(false);
				ui->standbyLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
				ui->standbyLabel->setMinimumSize(1,1);
				// 초기 1회 그리기
				ui->standbyLabel->setPixmap(standbyOrig_.scaled(
        ui->standbyLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
		}

		if (ui->stackedWidget && ui->standbyLabel) {
				ui->stackedWidget->setCurrentWidget(ui->standbyLabel);
		}

    applyStyles();
		setupUnlockOverlayLabel();
		connectSignals();
}
/*
void MainWindow::setupUi()
{
    // 1) 기본 창 설정
    ui->setupUi(this);
    setMinimumSize(WINDOW_MIN_WIDTH, WINDOW_MIN_HEIGHT);

    // 2) CameraLabel 스타일 (네 기존 스타일 유지)
    if (ui->cameraLabel) {
        ui->cameraLabel->setStyleSheet(CAM_LABEL_STYLE);
        // 라벨 자체는 레이아웃에 의해 확장 가능해야 함
        ui->cameraLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        ui->cameraLabel->setMinimumSize(1, 1);
        ui->cameraLabel->setAlignment(Qt::AlignCenter);
        // 실시간 프레임은 우리가 수동 스케일하므로 굳이 자동 스케일 필요 없음
        ui->cameraLabel->setScaledContents(false);
    }

    // 3) StandbyLabel — 핵심: sizeHint(픽스맵 크기)에 끌려가지 않게 설정
    if (ui->standbyLabel) {
        // pixmap이 .ui에 지정된 경우에도 라벨 확장을 막지 않도록:
        ui->standbyLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        ui->standbyLabel->setMinimumSize(1, 1);
        ui->standbyLabel->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        ui->standbyLabel->setAlignment(Qt::AlignCenter);
        // 자동 스케일은 끄고(우리가 고품질로 스케일), 필요시 true로 바꿔도 OK
        ui->standbyLabel->setScaledContents(false);

        // 3-1) 경로 구성 (실행파일 기준 상대경로) — 네가 정의한 상수 사용
        const QString path = QString(IMAGES_PATH) + QString(STANDBY_IMAGE);

        // 3-2) 로드 + 원본 보관
        QPixmap pm;
        bool ok = pm.load(path);
        if (!ok || pm.isNull()) {
            qWarning() << "[Standby] Load FAIL:" << path << " — .ui의 pixmap으로 폴백 시도";
            // .ui에 지정된 pixmap으로부터 원본 확보 (Qt5/Qt6 분기)
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
            QPixmap fromUi = ui->standbyLabel->pixmap();
#elif QT_VERSION >= QT_VERSION_CHECK(5,15,0)
            QPixmap fromUi = ui->standbyLabel->pixmap(Qt::ReturnByValue);
#else
            const QPixmap* p = ui->standbyLabel->pixmap();
            QPixmap fromUi = p ? *p : QPixmap();
#endif
            if (!fromUi.isNull()) {
                standbyOrig_ = fromUi;
            } else {
                qWarning() << "[Standby] .ui pixmap도 없음 — standby는 빈 화면일 수 있음";
            }
        } else {
            standbyOrig_ = pm;
        }

        // 3-3) 초기 1회 라벨 크기에 맞춰 고품질 스케일
        if (!standbyOrig_.isNull()) {
            const QSize target = ui->standbyLabel->size();
            ui->standbyLabel->setPixmap(
                standbyOrig_.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }

    // 4) 초기 화면은 standby로 강제 (프레임 들어오면 camera로 전환)
    if (ui->stackedWidget && ui->standbyLabel) {
        ui->stackedWidget->setCurrentWidget(ui->standbyLabel);
    }

    // 5) 나머지 네 기존 초기화
    applyStyles();
    setupUnlockOverlayLabel();
    connectSignals();

    // [옵션] 카메라 무프레임 감시 (쓰지 않으면 제거해도 됨)
    if (!cameraWatchdog_) {
        cameraWatchdog_ = new QTimer(this);
        cameraWatchdog_->setInterval(1000);
        connect(cameraWatchdog_, &QTimer::timeout, this, [this]{
            if (lastFrameMs_ == 0) return;
            if (QDateTime::currentMSecsSinceEpoch() - lastFrameMs_ > 3000) {
                if (ui->stackedWidget && ui->standbyLabel) {
                    ui->stackedWidget->setCurrentWidget(ui->standbyLabel);
                    firstFrameShown_ = false;
                }
            }
        });
        cameraWatchdog_->start();
    }
}
*/

void MainWindow::resizeEvent(QResizeEvent* e)
{
    QMainWindow::resizeEvent(e);

    // standby: 원본 기준으로 항상 고품질 재스케일
    if (ui->standbyLabel && !standbyOrig_.isNull()) {
        ui->standbyLabel->setPixmap(
            standbyOrig_.scaled(ui->standbyLabel->size(),
                                Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    // cameraLabel: 다음 프레임이 들어오며 갱신되지만,
    // 현재 픽스맵이 있다면 임시로 재스케일해서 깜빡임 줄이기
    if (ui->cameraLabel) {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        QPixmap pm = ui->cameraLabel->pixmap();
#elif QT_VERSION >= QT_VERSION_CHECK(5,15,0)
        QPixmap pm = ui->cameraLabel->pixmap(Qt::ReturnByValue);
#else
        const QPixmap* p = ui->cameraLabel->pixmap();
        QPixmap pm = p ? *p : QPixmap();
#endif
        if (!pm.isNull()) {
            ui->cameraLabel->setPixmap(
                pm.scaled(ui->cameraLabel->size(),
                          Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }
}



void MainWindow::setupUnlockOverlayLabel()
{
		qDebug() << "[MainWindow] setupUnlockOverlayLabel is called";
		unlockOverlayLabel = new QLabel(ui->cameraLabel);
		unlockOverlayLabel->setAlignment(Qt::AlignCenter);
		unlockOverlayLabel->setStyleSheet("background-color: rgba(0, 0, 0, 128);");
		unlockOverlayLabel->setVisible(false);
		unlockOverlayLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	
		updateUnlockOverlay();
}

void MainWindow::updateUnlockOverlay()
{
		qDebug() << "[MainWindow] updateUnlockOverlay is called";
		if (!unlockOverlayLabel || !ui->cameraLabel) return;

		QSize labelSize = ui->cameraLabel->size();
		unlockOverlayLabel->setGeometry(0, 0, labelSize.width(), labelSize.height());
		unlockOverlayLabel->setPixmap(QPixmap(OPEN_IMAGE).scaled(
						labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::showEvent(QShowEvent* event)
{
		qDebug() << "[MainWindow] showEvent is called";;
		QMainWindow::showEvent(event);
		updateUnlockOverlay();
}


void MainWindow::applyStyles() {
		qDebug() << "[MainWindow] appelyStyles is called";
    if (ui->statusbar) {
        ui->statusbar->setStyleSheet(STATUS_BAR_STYLE);
    }

    for (QPushButton* btn : buttonList()) {
        if (btn) {
						btn->setMinimumHeight(40);
						btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

						btn->setStyleSheet("");
            btn->setStyleSheet(BTN_STYLE);

						 auto *shadow = new QGraphicsDropShadowEffect();
						shadow->setBlurRadius(10);
						shadow->setXOffset(0);
						shadow->setYOffset(3);
						shadow->setColor(QColor(0, 0, 0, 60));
						btn->setGraphicsEffect(shadow);
        } else {
            LOG_WARN("Null value exist in buttonlist()");
        }
    }
}

void MainWindow::connectSignals() {
		qDebug() << "[MainWindow] connectSignals is called";
    auto safeConnect = [this](QPushButton* btn, auto slot, const QString& name) {
				if (!btn) {
						LOG_WARN(QString("Is not exist button: %1").arg(name));
						return;
				}
				if (!connect(btn, &QPushButton::clicked, this, slot)) {
						LOG_WARN(QString("Failed to connect button: %1").arg(name));
						showError("Button error", name + " Failed to connect button");
				}
    };


		safeConnect(ui->registerButton, [this]() { emit registerFaceRequested(); }, "User Registration");
    safeConnect(ui->showUsersList, [this]() { emit requestedShowUserList(); }, "User list");
		safeConnect(ui->showUserImages, [this]() { emit showUserImagesRequested(); }, "User Image"); 
		safeConnect(ui->resetButton, [this]() { if (QMessageBox::question(this, "사용자 초기화", "사용자를 초기화할까요?") == QMessageBox::Yes) { emit resetRequested(); } else { return; } }, "Reset");
		safeConnect(ui->ExitButton, [this]() { if (QMessageBox::question(this, "종료", "프로그램을 종료할까요?") == QMessageBox::Yes) QApplication::quit(); }, "Exit");
} 

QList<QPushButton*> MainWindow::buttonList() const
{
		return {
				ui->registerButton,
				ui->resetButton,
				ui->showUsersList,
				ui->showUserImages,
				ui->ExitButton
		};
}

UiState MainWindow::getCurrentUiState()
{
		return currentUiState;
}

void MainWindow::setCurrentUiState(UiState state)
{
		currentUiState = state;
}

void MainWindow::showUnlockOverlayLabel()
{
		unlockOverlayLabel->setVisible(true);
		showStatusMessage("Door was opend!");

		// 3초 후에 숨기고 상태 복귀
		QTimer::singleShot(3000, this, [this]() {
						unlockOverlayLabel->setVisible(false);
						cout << "setVisibel false" << endl;	
		});
}

RecognitionState MainWindow::getRecognitionState() 
{
		return currentRecognitionState;
}

void MainWindow::reset() {
		QMessageBox::information(this, "사용자 초기화", "초기화가 완료 됐습니다.");
		ui->statusbar->showMessage("모든 사용자 삭제됨.");
}

void MainWindow::showImagePreview(const QString& imagePath) 
{
		if (currentUiState != UiState::IDLE) return;
		currentUiState = UiState::PREVIEWING;

    QDialog* previewDialog = new QDialog(this);
		if (!previewDialog) {
				QMessageBox::information(this, "미리보기", "미리보기에 실패했습니다.");
				qDebug() << "[MW] Failed to allocate memory to previewDialog";
				return;
		}
		previewDialog->setAttribute(Qt::WA_DeleteOnClose); // auto memory delete
    previewDialog->setWindowTitle("미리보기");
    previewDialog->resize(500, 500);
		previewDialog->setStyleSheet("background-color: #1e1e1e; color: white;");

    QVBoxLayout* layout = new QVBoxLayout(previewDialog);
		if (!layout) {
				QMessageBox::information(this, "미리보기", "미리보기에 실패했습니다.");
				qDebug() << "[MW] Failed to allocate memory to Preview Layout";
				return;
		}

    QLabel* imageLabel = new QLabel();
		if (!imageLabel) {
				QMessageBox::information(this, "미리보기", "미리보기에 실패했습니다.");
				qDebug() << "[MW] Failed to allocate memory to  Preview imageLabel";
				return;
		}
    QPixmap pixmap(imagePath);
    imageLabel->setPixmap(pixmap.scaled(previewDialog->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    imageLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(imageLabel);

		QHBoxLayout* buttonLayout = new QHBoxLayout();
		QPushButton* deleteButton = new QPushButton("삭제");
		QPushButton* closeButton = new QPushButton("닫기");

		if (!buttonLayout || !deleteButton || !closeButton) {
				QMessageBox::information(this, "미리보기", "미리보기에 실패했습니다.");
				qDebug() << "[MW] Failed to allocate memory to  Preview delete, close or button layout";
				return;
		}

		deleteButton->setStyleSheet("background-color: #ff4c4c; color: white; padding: 6px;");
		closeButton->setStyleSheet("padding: 6px;");

		connect(deleteButton, &QPushButton::clicked, this, [=]() {
					if (QMessageBox::question(this, "삭제", imagePath + "파일을 삭제할까요?")) {
							emit deleteImageRequested(imagePath);
							qDebug() << "[MainWindow] Preview delete button called";
							previewDialog->accept();
							galleryDialog->accept();
					}
		});

		buttonLayout->addWidget(deleteButton);
		buttonLayout->addWidget(closeButton);
		layout->addLayout(buttonLayout);

		connect(previewDialog, &QDialog::finished, this, [=]() {
					currentUiState = UiState::IDLE;
		});

    previewDialog->exec();
}

QDialog* MainWindow::getGalleryDialog() const { return galleryDialog; }

// NOTE: galleryDialog는 QPointer로 단일 인스턴스 보장.
//				재호출 시 재사용/포커스만 주며, null/destroyed 안전 가드.
void MainWindow::showUserImageGallery(const QList<UserImage>& images) {
		qDebug()  << "[MainWindow] showUserImageGallery called with" << images.size() << "images";	

		if (galleryDialog) {
				if (galleryDialog->isVisible()) {
						galleryDialog->close();
				}
				galleryDialog->deleteLater();
				galleryDialog = nullptr;
		}

		galleryDialog = new QDialog(this);
		if (!galleryDialog) {
				qWarning() << "[MainWindow] Failed to create galleryDialog!";
				return;
		}

		galleryDialog->setWindowTitle("📸 사용자 이미지 갤러리");
    galleryDialog->resize(800, 600);
    galleryDialog->setStyleSheet("background-color: #1e1e1e; color: white;");


    QWidget* container = new QWidget();
		QGridLayout* gridLayout = new QGridLayout(container);
    gridLayout->setSpacing(10);

    int row = 0, col = 0;
    const int maxCols = 4;

    for (const auto& img : images) {
				QPixmap pixmap(img.filePath);
				if (pixmap.isNull()) continue;
				
        QVBoxLayout* cellLayout = new QVBoxLayout();
        QWidget* cellWidget = new QWidget();

				ClickableLabel* imgLabel = new ClickableLabel(img.filePath);
        imgLabel->setPixmap(pixmap.scaled(120, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        imgLabel->setFixedSize(130, 130);
        imgLabel->setAlignment(Qt::AlignCenter);
        imgLabel->setStyleSheet("border: 2px solid #444; border-radius: 6px;");
        imgLabel->setCursor(Qt::PointingHandCursor);

				connect(imgLabel, &ClickableLabel::clicked, this, &MainWindow::imageClicked);

        QLabel* nameLabel = new QLabel(img.userName);
        nameLabel->setAlignment(Qt::AlignCenter);
        nameLabel->setStyleSheet("font-size: 12px;");

        QPushButton* delBtn = new QPushButton("🗑️ 삭제");
        delBtn->setStyleSheet("background-color: #ff4c4c; color: white; border: none; padding: 4px;");
        connect(delBtn, &QPushButton::clicked, this, [=]() {
            if (QMessageBox::question(this, "삭제", img.filePath + " 파일을 삭제할까요?") == QMessageBox::Yes) {
                emit deleteImageRequested(img.filePath);
								galleryDialog->accept();
            }
        });

        cellLayout->addWidget(imgLabel);
        cellLayout->addWidget(nameLabel);
        cellLayout->addWidget(delBtn);
        cellWidget->setLayout(cellLayout);

        gridLayout->addWidget(cellWidget, row, col++);
        if (col >= maxCols) {
            col = 0;
            row++;
        }
    }

    QScrollArea* scrollArea = new QScrollArea(galleryDialog);
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(container);

    QVBoxLayout* mainLayout = new QVBoxLayout(galleryDialog);
    mainLayout->addWidget(scrollArea);

    QPushButton* closeBtn = new QPushButton("닫기", galleryDialog);
		if (!closeBtn) {
				qWarning() << "[MainWindow] close Button creation failed!";
		} else {
				closeBtn->setStyleSheet("padding: 6px 12px;");
				connect(closeBtn, &QPushButton::clicked, galleryDialog, &QDialog::accept);
				mainLayout->addWidget(closeBtn, 0, Qt::AlignCenter);
		}

		connect(galleryDialog, &QDialog::destroyed, this, [=]() {
					qDebug() << "[MainWindow] GalleryDialog destroyed";
					galleryDialog = nullptr;
		});

    galleryDialog->setLayout(mainLayout);
    galleryDialog->show();
}
void MainWindow::showUserList(const QStringList& users) 
{
		qDebug() << "[MainWindow] ShowUserList is called";

		if (users.isEmpty()) {
				QMessageBox::information(this, "사용자 목록", "등록된 사용자가 없습니다.");
		} else {
				 QMessageBox::information(this, "사용자 목록", users.join("\n"));
		}
}

void MainWindow::showErrorMessage(const QString& title, const QString& message)
{
		QMessageBox::critical(this, title, message);

}

void MainWindow::showInfo(const QString& title, const QString& message) {
    QMessageBox::information(this, title, message);
}

void MainWindow::showError(const QString& title, const QString& message) {
    QMessageBox::critical(this, title, message);
}


void MainWindow::showStatusMessage(const QString& msg)
{
		if (ui->statusbar) {
				ui->statusbar->showMessage(msg);
		}
}

MainWindow::~MainWindow() {
    delete ui;
}
