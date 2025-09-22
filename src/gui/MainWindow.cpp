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
#include <QFont>
#include <QStandardItemModel>        
#include <QSortFilterProxyModel>    

#include "gui/LogTab.hpp"
#include "gui/SingleLogDialog.hpp"
#include "gui/DevInfoDialog.hpp"
#include "gui/DevInfoTab.hpp"

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

void MainWindow::closeEvent(QCloseEvent* e)
{
	if (devInfoDlg_) {
		devInfoDlg_->close();
		devInfoDlg_->deleteLater();
		devInfoDlg_ = nullptr;
	}

	e->accept();
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) 
{
    qRegisterMetaType<RecognitionState>("RecognitionState");

    setupUi();

	// 로그 정보 탭
	logTab = new LogTab(this);
	ui->rightTabWidget->addTab(logTab, tr("Logs"));	

	// 디바이스 정보 탭
	devInfoDlg_ = new DevInfoDialog(this);
	ui->rightTabWidget->addTab(devInfoDlg_, tr("Information"));

#ifdef DEBUG
	qDebug() << "[MW] logTab ptr=" << logTab
			 << "parent=" << logTab->parent()
			 << "idx=" << ui->rightTabWidget->indexOf(logTab)
			 << "devInfoTab ptr=" <, devInfoTab
			 << "parent=" << devInfoTab->parent()
			 << "idx=" << ui->rightTabWidget->indexOf(devInfoTab)
			 << "inThread=" << QThread::currentThread();
			
#endif
	

	auto safeConnectSig = [this](auto* sender, auto signal, auto slot, const QString& name) {
    	if (!sender) { 
			LOG_WARN(QString("Is not exist sender: %1").arg(name)); 
		return; }
    	QObject::disconnect(sender, signal, this, nullptr);  // 중복 제거
    	QMetaObject::Connection c = QObject::connect(sender, signal, this, std::move(slot));
    	if (!c) {
        	LOG_WARN(QString("Failed to connect signal: %1").arg(name));
        	showError("Signal error", name + " Failed to connect signal");
    	} else {
        	qDebug() << "[safeConnectSig] connected:" << name;
    	}
	};


	// "장비 정보 보기"
	safeConnectSig(devInfoTab, &DevInfoTab::showDevInfo, [this] {
		qDebug() << "[MainWindow] clieck DevInfo btn";	
		if (!devInfoDlg_) {
			devInfoDlg_ = new DevInfoDialog(this);
		}

		// 중앙에 위치시키고 모달 실행 (원하면 show()로 모델리스)
		devInfoDlg_->exec();
	}, "DevInfo");

	// "인증 로그 보기"
	safeConnectSig(logTab, &LogTab::showAuthLogs, [this]{
    	QVector<AuthLog> rows; int total=0;
    	if (!mainPresenter || !mainPresenter->db_) { showError("Logs","서비스 준비 안됨"); return; }
    	if (!mainPresenter->db_->selectAuthLogs(0, 200, "", &rows, &total)) {
        	showError("Logs","인증 로그 조회 실패"); return;
    	}

    	SingleLogDialog dlg(LogKind::Auth, this);
    	dlg.setWindowTitle(tr("Auth Logs (%1/%2)").arg(rows.size()).arg(total));
    	dlg.setAuthLogs(rows);
    	dlg.setWindowModality(Qt::ApplicationModal);
    	dlg.exec();
	}, "AuthLogs");

	// "시스템 로그 보기"
	safeConnectSig(logTab, &LogTab::showSysLogs, [this]{
    	QVector<SystemLog> rows; int total=0;
    	if (!mainPresenter || !mainPresenter->db_) { showError("Logs","서비스 준비 안됨"); return; }
    	if (!mainPresenter->db_->selectSystemLogs(0, 200, 0, "", "", &rows, &total)) {
        	showError("Logs","시스템 로그 조회 실패"); return;
    	}

    	SingleLogDialog dlg(LogKind::System, this);
    	dlg.setWindowTitle(tr("System Logs (%1/%2)").arg(rows.size()).arg(total));
    	dlg.setSystemLogs(rows);
    	dlg.setWindowModality(Qt::ApplicationModal);
    	dlg.exec();
	}, "SysLogs");



    mainPresenter = new MainPresenter(this, this);
    if (!mainPresenter) {
        qDebug() << "[MainWindow] Failed to allocate memory"; 
        return;
    }
    mainPresenter->startAllServices();

     // 초기 로딩
    mainPresenter->requestAuthPage(authPage, pageSize, "");
    mainPresenter->requestSystemPage(sysPage, pageSize, 0, "", "");

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
				emit CamRestart();
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

void MainWindow::PresentCamRestart()
{
	setActionMsg(ui, "카메라 재시작 완료");
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
	
		//updateUnlockOverlay();
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
		//updateUnlockOverlay();
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
            //LOG_WARN("Null value exist in buttonlist()");
				
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
    	auto c = QObject::connect(btn, &QPushButton::clicked, this, slot);
    	if (!c) {
        	LOG_WARN(QString("Failed to connect button: %1").arg(name));
        	showError("Button error", name + " Failed to connect button");
    	}
	};


	safeConnect(ui->registerButton,[this]() { emit registerFaceRequested(); }, "User Registration");
	safeConnect(ui->showUsersList, [this]() { emit requestedShowUserList(); }, "User list");
	safeConnect(ui->showUserImages, [this]() { emit showUserImagesRequested(); }, "User Image"); 
	safeConnect(ui->resetButton, [this]() { 
		if (QMessageBox::question(this, "사용자 초기화", "사용자를 초기화할까요?") == QMessageBox::Yes) { 
			emit resetRequested(); 
		} 
		else { 
			return; 
		} 
	}, "Reset");
	safeConnect(ui->ExitButton, [this]() { 
		if (QMessageBox::question(this, "종료", "프로그램을 종료할까요?") == QMessageBox::Yes) 
			QApplication::quit(); 
	}, "Exit");
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
        qDebug() << "[MAINWINDOW] call reset";
		//QMessageBox::information(this, "사용자 초기화", "초기화가 완료 됐습니다.");
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
    // 상태바 기본 폰트 가져오기
    int fontSize = 16;

    // 상태바 기본 폰트 가져오기
    QFont font = ui->statusbar->font();

    // 글씨 크기 설정 (기본값: 16pt)
    ui->statusbar->setMinimumHeight(fontSize + 30);  // 상태바 높이 확보
    ui->statusbar->layout()->setContentsMargins(0, 0, 0, 0);

    ui->statusbar->setStyleSheet(QString("QStatusBar QLabel { font-size: %1pt; }").arg(fontSize));  // 글꼴 강제
    font.setPointSize(fontSize);        // 내부 폰트 변경 setStyleSteet와 같이 써야 함
    ui->statusbar->setFont(font);

    if (ui->statusbar) {
        ui->statusbar->showMessage(msg);
    }
}


MainWindow::~MainWindow() {
    delete ui;
}
