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
#include "gui/LedWidget.h"

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

	// MainWindow 생성자 등 UI 초기화 시
	ui->cameraLabel->setScaledContents(false); // 왜곡 방지
	ui->cameraLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	ui->cameraLabel->setMinimumSize(0, 0); // 원하는 최소 크기로 늘리기

	if (auto *lay = ui->centralwidget->layout())
		lay->setContentsMargins(0,0,0,0);


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

	onBleStateChanged(States::BleState::Idle);
	onDoorStateChanged(States::DoorState::Locked);


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

static inline const char* toBleText(States::BleState s) 
{
    switch (s) {
 		case States::BleState::Idle:         return "Idle";
    	case States::BleState::Scanning:     return "Advertising / Scanning";
    	case States::BleState::Connected:    return "Connected";
    	case States::BleState::Disconnected: return "Disconnected";
    }
    return "Unknown";
}

// ====== 상태 갱신 ======
void MainWindow::onBleStateChanged(States::BleState s)
{
	qDebug().noquote() << "[onBleStateChanged] slot in:" << int(s);
    using M = LedWidget::Mode;
    if (!ui || !ui->bleLed) return;

    switch (s) {
		case States::BleState::Idle:         ui->bleLed->setMode(M::Off);   break;
		case States::BleState::Scanning:     ui->bleLed->setMode(M::Blue);  break;
		case States::BleState::Connected:    ui->bleLed->setMode(M::Green); break;
		case States::BleState::Disconnected: ui->bleLed->setMode(M::Red);   break;
    }

	/*
	const char *msg = toBleText(s);
	ui->bleLed->setToolTip(QStringLiteral("BLE: %1").arg(QString::fromUtf8(msg)));

	if (auto lbl = ui->bleStatusLabel) {
		lbl->setText(QString::fromUtf8(msg));
	}

	qDebug().noquote() << "[onBleStateChanged] BLE = " << msg;
	*/
}

void MainWindow::onDoorStateChanged(States::DoorState s)
{
    using M = LedWidget::Mode;
    if (!ui || !ui->doorLed) return;

    ui->doorLed->setMode(s == States::DoorState::Open ? M::Green : M::Red);
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
            const auto ret = StyledMsgBox::question(
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
            const auto ret = StyledMsgBox::warning(
                this, tr("도어 열기"),
                tr("도어를 수동으로 엽니다. 정말 진행할까요?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ret == QMessageBox::Yes) {
                setActionMsg(ui, "도어 열기 요청");
                // TODO: doorService_->requestUnlock();
				emit doorOpen();
            }
        });
    }

    if (ui->btnLockDoor) {
        connect(ui->btnLockDoor, &QPushButton::clicked, this, [this](){
            setActionMsg(ui, "도어 잠금 요청");
            // TODO: doorService_->requestLock();
			emit doorClose();
        });
    }

    if (ui->btnRetrain) {
        connect(ui->btnRetrain, &QPushButton::clicked, this, [this](){
            const auto ret = StyledMsgBox::question(
                this, tr("재학습"),
                tr("인식기를 재학습할까요? (시간이 소요될 수 있습니다)"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ret == QMessageBox::Yes) {
                setActionMsg(ui, "재학습 요청");
				emit retrainRecog();
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

	ui->rightTabWidget->setStyleSheet(
			    "QTabBar::tab { height: 48px; padding: 10px 18px; font-size:16pt; }"
				"QTabBar::tab:selected { font-weight:600; }"
	);

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

void MainWindow::PresentCamRestart(const QString& msg)
{
	setActionMsg(ui, msg);
}

void MainWindow::PresentDoorOpen(const QString& msg)
{
	setActionMsg(ui, msg); 
}
void MainWindow::PresentDoorClose(const QString& msg)
{
	setActionMsg(ui, msg); 
}

void MainWindow::PresentRetrainRecog(const QString& msg)
{
	setActionMsg(ui, msg);
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


	applyStyles();
	connectSignals();
}

void MainWindow::applyStyles() {
		qDebug() << "[MainWindow] appelyStyles is called";
    if (ui->statusbar) {
        ui->statusbar->setStyleSheet(STATUS_BAR_STYLE);
    }

    for (QPushButton* btn : buttonList()) {
        if (btn) {
						btn->setMinimumHeight(100);
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
		if (StyledMsgBox::question(this, "사용자 초기화", "사용자를 초기화할까요?") == QMessageBox::Yes) { 
			emit resetRequested(); 
		} 
		else { 
			return; 
		} 
	}, "Reset");
	safeConnect(ui->ExitButton, [this]() { 
		if (StyledMsgBox::question(this, "종료", "프로그램을 종료할까요?") == QMessageBox::Yes) 
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
				ui->ExitButton,

				ui->btnRefresh,
				ui->btnRestartCamera,
				ui->btnUnlockDoor,
				ui->btnLockDoor,
				ui->btnRetrain,
				ui->btnExportLogs
				
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
			StyledMsgBox::information(this, "미리보기", "미리보기에 실패했습니다.");
			qDebug() << "[MW] Failed to allocate memory to previewDialog";
			return;
	}
	previewDialog->setAttribute(Qt::WA_DeleteOnClose); // auto memory delete
    previewDialog->setWindowTitle("미리보기");
    previewDialog->resize(500, 500);
	previewDialog->setStyleSheet("background-color: #1e1e1e; color: white;");

    QVBoxLayout* layout = new QVBoxLayout(previewDialog);
	if (!layout) {
		StyledMsgBox::information(this, "미리보기", "미리보기에 실패했습니다.");
		qDebug() << "[MW] Failed to allocate memory to Preview Layout";
		return;
	}

    QLabel* imageLabel = new QLabel();
	if (!imageLabel) {
			StyledMsgBox::information(this, "미리보기", "미리보기에 실패했습니다.");
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
		StyledMsgBox::information(this, "미리보기", "미리보기에 실패했습니다.");
		qDebug() << "[MW] Failed to allocate memory to  Preview delete, close or button layout";
		return;
	}

	deleteButton->setStyleSheet("background-color: #ff4c4c; color: white; padding: 6px;");
	closeButton->setStyleSheet("padding: 6px;");

	connect(deleteButton, &QPushButton::clicked, this, [=]() {
					if (StyledMsgBox::question(this, "삭제", imagePath + "파일을 삭제할까요?")) {
							emit deleteImageRequested(imagePath);
							qDebug() << "[MainWindow] Preview delete button called";
							previewDialog->accept();
							galleryDialog->accept();
					}
		});

	connect(closeButton, &QPushButton::clicked, this, [=]() {
			if (previewDialog) {
				previewDialog->accept();
				qDebug() << "[MainWindow] Preview dialog closed";
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
    qDebug() << "[MainWindow] showUserImageGallery called with" << images.size() << "images";

    if (galleryDialog) {
        if (galleryDialog->isVisible()) galleryDialog->close();
        galleryDialog->deleteLater();
        galleryDialog = nullptr;
    }

    galleryDialog = new QDialog(this);
    if (!galleryDialog) {
        qWarning() << "[MainWindow] Failed to create galleryDialog!";
        return;
    }

    // 스타일 적용(다이얼로그 전역)
    applyGalleryDialogStyle(galleryDialog);
    galleryDialog->setWindowTitle(QStringLiteral("사용자 이미지 갤러리"));
    galleryDialog->resize(920, 680);

    // 컨테이너 + 그리드
    QWidget* container = new QWidget();
    container->setObjectName("GalleryContainer");

    QGridLayout* gridLayout = new QGridLayout(container);
    gridLayout->setContentsMargins(16, 16, 16, 16);
    gridLayout->setHorizontalSpacing(16);
    gridLayout->setVerticalSpacing(16);

    int row = 0, col = 0;
    const int maxCols = 4;         // 필요 시 반응형으로 계산해도 됨
    const int thumb = 160;         // 썸네일 한 변(픽셀)

    for (const auto& img : images) {
        QPixmap pixmap(img.filePath);
        if (pixmap.isNull()) continue;

        // 셀(카드)
        auto* cellWidget = new QWidget();
        cellWidget->setProperty("role", "cell");

        auto* cellLayout = new QVBoxLayout(cellWidget);
        cellLayout->setContentsMargins(6, 6, 6, 6);
        cellLayout->setSpacing(6);

        // 썸네일
        auto* imgLabel = new ClickableLabel(img.filePath);
        imgLabel->setProperty("role", "thumb");
        imgLabel->setPixmap(pixmap.scaled(thumb, thumb, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        imgLabel->setFixedSize(thumb + 8, thumb + 8); // 테두리 포함 여유
        imgLabel->setAlignment(Qt::AlignCenter);
        imgLabel->setCursor(Qt::PointingHandCursor);
        connect(imgLabel, &ClickableLabel::clicked, this, &MainWindow::imageClicked);

        // 이름
        auto* nameLabel = new QLabel(img.userName);
        nameLabel->setProperty("role", "name");
        nameLabel->setAlignment(Qt::AlignCenter);
		nameLabel->setStyleSheet("font-size: 14px; color: black; font-weight: 600;");

        // 삭제 버튼
        auto* delBtn = new QPushButton(QStringLiteral("삭제"));
        delBtn->setProperty("role", "delete");
        connect(delBtn, &QPushButton::clicked, this, [=]() {
            if (StyledMsgBox::question(this, QStringLiteral("삭제"),
                                       img.filePath + QStringLiteral(" 파일을 삭제할까요?"))
                == QMessageBox::Yes) {
                emit deleteImageRequested(img.filePath);
                galleryDialog->accept();
            }
        });

        // 조립
        cellLayout->addWidget(imgLabel, 0, Qt::AlignCenter);
        cellLayout->addWidget(nameLabel);
        cellLayout->addWidget(delBtn);
        cellWidget->setLayout(cellLayout);

        gridLayout->addWidget(cellWidget, row, col++);
        if (col >= maxCols) { col = 0; ++row; }
    }

    // 스크롤 영역
    auto* scrollArea = new QScrollArea(galleryDialog);
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(container);

    // 닫기 버튼
    auto* closeBtn = new QPushButton(QStringLiteral("닫기"), galleryDialog);
    closeBtn->setProperty("role", "close");
    connect(closeBtn, &QPushButton::clicked, galleryDialog, &QDialog::accept);

    // 메인 레이아웃
    auto* mainLayout = new QVBoxLayout(galleryDialog);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);
    mainLayout->addWidget(scrollArea);
    mainLayout->addWidget(closeBtn, 0, Qt::AlignCenter);

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
				StyledMsgBox::information(this, "사용자 목록", "등록된 사용자가 없습니다.");
		} else {
				 StyledMsgBox::information(this, "사용자 목록", users.join("\n"));
		}
}


void MainWindow::showErrorMessage(const QString& title, const QString& message)
{
	QMessageBox::critical(this, title, message);
}

void MainWindow::showInfo(const QString& title, const QString& message)
{
	StyledMsgBox::information(this, title, message);
}

void MainWindow::showError(const QString& title, const QString& message) {
    StyledMsgBox::warning(this, title, message);
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
