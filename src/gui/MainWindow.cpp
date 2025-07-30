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

#include "services/FaceRecognitionService.hpp"

#include "presenter/UserImagePresenter.hpp"  // ✅ 여기서는 실제 정의가 필요함
#include "presenter/DoorSensorPresenter.hpp"
#include "presenter/FaceSensorPresenter.hpp"
#include "presenter/FaceRecognitionPresenter.hpp"

#include "presenter/MainPresenter.hpp"


//#define DEBUG



MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) 
{
		qDebug() << "[MainWindow] Created";
    qRegisterMetaType<RecognitionState>("RecognitionState");

		MainPresenter* mainPresenter;
		mainPresenter = new MainPresenter(this);
		mainPresenter->startAllServices();

		setupUi();
}

void MainWindow::setupUi() {
    ui->setupUi(this);
    setMinimumSize(WINDOW_MIN_WIDTH, WINDOW_MIN_HEIGHT);
    if (ui->cameraLabel) {
        ui->cameraLabel->setStyleSheet(CAM_LABEL_STYLE);
    }

		setupUnlockOverlayLabel();
		setupButtonLayout();

    // ▶ 스타일 적용
    applyStyles();

    // ▶ 버튼 클릭 시그널 연결
		connectSignals();
}

void MainWindow::setupButtonLayout()
{
		QHBoxLayout* buttonLayout = new QHBoxLayout();
		for (QPushButton* btn : buttonList()) {
				if (btn) {
						btn->setMinimumHeight(40);
						btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

						btn->setStyleSheet("");
						btn->setStyleSheet(BTN_STYLE);

						auto* shadow = new QGraphicsDropShadowEffect();
						shadow->setBlurRadius(10);
						shadow->setXOffset(0);
						shadow->setYOffset(3);
						shadow->setColor(QColor(0, 0, 0, 60));
						btn->setGraphicsEffect(shadow);
				}
		}
}

void MainWindow::setupUnlockOverlayLabel()
{
		cout << "Setup unlock overlay label!!" << endl;
		unlockOverlayLabel = new QLabel(ui->cameraLabel);
		unlockOverlayLabel->setAlignment(Qt::AlignCenter);
		unlockOverlayLabel->setStyleSheet("background-color: rgba(0, 0, 0, 128);");
		unlockOverlayLabel->setVisible(false);
		unlockOverlayLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	
		updateUnlockOverlay();
}

void MainWindow::updateUnlockOverlay()
{
		if (!unlockOverlayLabel || !ui->cameraLabel) return;

		QSize labelSize = ui->cameraLabel->size();
		unlockOverlayLabel->setGeometry(0, 0, labelSize.width(), labelSize.height());
		unlockOverlayLabel->setPixmap(QPixmap(OPEN_IMAGE).scaled(
						labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::showEvent(QShowEvent* event)
{
		QMainWindow::showEvent(event);
		updateUnlockOverlay();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
		QMainWindow::resizeEvent(event);
		updateUnlockOverlay();
}

void MainWindow::applyStyles() {
    if (ui->statusbar) {
        ui->statusbar->setStyleSheet(STATUS_BAR_STYLE);
    }

    for (QPushButton* btn : buttonList()) {
        if (btn) {
						btn->setStyleSheet("");
            btn->setStyleSheet(BTN_STYLE);

						 auto *shadow = new QGraphicsDropShadowEffect();
						shadow->setBlurRadius(10);
						shadow->setXOffset(0);
						shadow->setYOffset(3);
						shadow->setColor(QColor(0, 0, 0, 60));
						btn->setGraphicsEffect(shadow);
        } else {
            LOG_WARN("buttonList() 내부에 null 버튼 존재");
        }
    }
}

void MainWindow::connectSignals() {
    auto safeConnect = [this](QPushButton* btn, auto slot, const QString& name) {
				if (!btn) {
						LOG_WARN(QString("버튼 없음: %1").arg(name));
						return;
				}
				if (!connect(btn, &QPushButton::clicked, this, slot)) {
						LOG_WARN(QString("연결 실패: %1").arg(name));
						showError("버튼 오류", name + " 연결 실패");
				}

    };


		safeConnect(ui->registerButton, [this]() { emit registerFaceRequested(); }, "사용자 등록");
    safeConnect(ui->clearButton, &MainWindow::onClearUsers, "초기화");
    safeConnect(ui->btnShowUsers, &MainWindow::onShowUserList, "사용자 목록");
    safeConnect(ui->ExitButton, &MainWindow::onExitProgram, "프로그램 종료");

		
		connect(ui->showUserImages, &QPushButton::clicked, this, [=]() {
					qDebug() << "[MainWindow] 사용자 이미지 버튼 클릭됨";
					emit showUserImagesRequested();
		});
}

QList<QPushButton*> MainWindow::buttonList() const
{
		return {
				ui->registerButton,
				ui->clearButton,
				ui->btnShowUsers,
				ui->showUserImages,
				ui->ExitButton
		};
}

/*
void MainWindow::onRegisterFace() {
		if (currentUiState != UiState::IDLE) return;
		currentUiState = UiState::REGISTERING;


    QString name = QInputDialog::getText(this, "사용자 등록", "이름을 입력하세요:");
		currentUiState = UiState::IDLE;

    if (name.isEmpty()) return;

#ifdef DEBUG
		qDebug() << "Thread: " << faceRecognitionService->thread()->isRunning();
		qDebug() << "faceRecognitionService pointer: : " << faceRecognitionService;
		qDebug() << "Is QObject alive? " << !faceRecognitionService->parent();
		qDebug() << "QObject name:  " << faceRecognitionService->objectName();
		qDebug() << "FaceRecognitionService thread: " << faceRecognitionService->thread();
		qDebug() << "Main thread: " << QCoreApplication::instance()->thread();
#endif

		if (faceRecognitionService) {
				QMetaObject::invokeMethod(faceRecognitionService, [this, name]() {
						if (this->faceRecognitionService) 
								this->faceRecognitionService->startRegistering(name);
				}, Qt::QueuedConnection);
		} else {
				qDebug() << "[Error] faceRecognitionService is null!";
		}
}
*/

UiState MainWindow::getCurrentUiState()
{
		return currentUiState;
}

void MainWindow::setCurrentUiState(UiState state)
{
		currentUiState = state;
}

void MainWindow::setRecognitionState(RecognitionState state) {
		cout << "Set recog state!!" << endl;
    switch (state) {
        case RecognitionState::IDLE:
            ui->statusbar->showMessage("대기 중...");
            break;
        case RecognitionState::DETECTING_PERSON:
            ui->statusbar->showMessage("사람 감지됨. 얼굴 인식 준비 중...");
            break;
        case RecognitionState::RECOGNIZING_FACE:
            ui->statusbar->showMessage("얼굴 인식 중...");
            break;
        case RecognitionState::REGISTERING:
            //ui->statusbar->showMessage(QString("'%1' 사용자 등록 중...").arg(faceRecognitionService->getUserName()));
            break;
				case RecognitionState::DUPLICATEDFACE:
						ui->statusbar->showMessage(QString("이미 등록된 얼굴입니다..."));
						QMessageBox::information(this, "Information", "이미 등록된 얼굴입니다...");
						break;
        case RecognitionState::UNLOCKED:
            ui->statusbar->showMessage("문이 열렸습니다!");
					  unlockOverlayLabel->setVisible(true);

						// 3초 후에 숨기고 상태 복귀
						QTimer::singleShot(3000, this, [this]() {
								unlockOverlayLabel->setVisible(false);
								cout << "setVisibel true" << endl;	
						});
						setRecognitionState(RecognitionState::IDLE);
						break;
    }
}

RecognitionState MainWindow::getRecognitionState() 
{
		return currentRecognitionState;
}

void MainWindow::onClearUsers() {
    // 사용자 파일 삭제 코드 생략
		if (QMessageBox::question(this, "사용자 초기화", "사용자를 초기화할까요?") == QMessageBox::Yes) {
				QDir dir(USER_FACES_DIR);
				dir.removeRecursively();

				QFile::remove(FACE_MODEL_FILE);
				QFile::remove(USER_LABEL_FILE);
	
				ui->statusbar->showMessage("모든 사용자 삭제됨.");

				emit clearUserRequested();
		}
		else {
			return;
		}
}

void MainWindow::onExitProgram() {
    if (QMessageBox::question(this, "종료", "프로그램을 종료할까요?") == QMessageBox::Yes)
        QApplication::quit();
}


/*
void MainWindow::onShowUserImages() {
    QDir imageDir(USER_FACES_DIR);
    if (!imageDir.exists()) {
        QMessageBox::warning(this, "오류", "등록된 이미지 디렉토리를 찾을 수 없습니다.");
        return;
    }

    QStringList imageFiles = imageDir.entryList(QStringList() << "*.png" << "*.jpg", QDir::Files);
    if (imageFiles.isEmpty()) {
        QMessageBox::information(this, "정보", "등록된 사용자 이미지가 없습니다.");
        return;
    }

    QDialog* galleryDialog = new QDialog(this);
    galleryDialog->setWindowTitle("📸 사용자 이미지 갤러리");
    galleryDialog->resize(800, 600);
    galleryDialog->setStyleSheet("background-color: #1e1e1e; color: white;");

    QWidget* container = new QWidget();
    QGridLayout* gridLayout = new QGridLayout(container);
    gridLayout->setSpacing(10);

    int row = 0, col = 0;
    const int maxCols = 4;

    for (const QString& fileName : imageFiles) {
        QString fullPath = imageDir.filePath(fileName);
        QPixmap pixmap(fullPath);
        if (pixmap.isNull()) continue;

        QVBoxLayout* cellLayout = new QVBoxLayout();
        QWidget* cellWidget = new QWidget();

        // ✅ ClickableLabel 사용
        ClickableLabel* imgLabel = new ClickableLabel(fullPath);
        imgLabel->setPixmap(pixmap.scaled(120, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        imgLabel->setFixedSize(130, 130);
        imgLabel->setAlignment(Qt::AlignCenter);
        imgLabel->setStyleSheet("border: 2px solid #444; border-radius: 6px;");
        connect(imgLabel, &ClickableLabel::clicked, this, &MainWindow::showImagePreview);

        QLabel* nameLabel = new QLabel(fileName.section('_', 2, 2).section('.', 0, 0));
        nameLabel->setAlignment(Qt::AlignCenter);
        nameLabel->setStyleSheet("font-size: 12px;");

        QPushButton* delBtn = new QPushButton("🗑️ 삭제");
        delBtn->setStyleSheet("background-color: #ff4c4c; color: white; border: none; padding: 4px;");
        connect(delBtn, &QPushButton::clicked, this, [=]() {
            if (QMessageBox::question(this, "삭제", fileName + " 파일을 삭제할까요?") == QMessageBox::Yes) {
                QFile::remove(fullPath);
                galleryDialog->accept();  // 다이얼로그 닫고 새로 열기
                onShowUserImages();
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

    QPushButton* closeBtn = new QPushButton("닫기");
    closeBtn->setStyleSheet("padding: 6px 12px;");
    connect(closeBtn, &QPushButton::clicked, galleryDialog, &QDialog::accept);
    mainLayout->addWidget(closeBtn, 0, Qt::AlignCenter);

    galleryDialog->setLayout(mainLayout);
    galleryDialog->exec();
}
*/

void MainWindow::showImagePreview(const QString& imagePath) 
{
		if (currentUiState != UiState::IDLE) return;
		currentUiState = UiState::PREVIEWING;

    QDialog* previewDialog = new QDialog(this);
		previewDialog->setAttribute(Qt::WA_DeleteOnClose); // auto memory delete
    previewDialog->setWindowTitle("미리보기");
    previewDialog->resize(500, 500);
		previewDialog->setStyleSheet("background-color: #1e1e1e; color: white;");

    QVBoxLayout* layout = new QVBoxLayout(previewDialog);

    QLabel* imageLabel = new QLabel();
    QPixmap pixmap(imagePath);
    imageLabel->setPixmap(pixmap.scaled(previewDialog->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    imageLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(imageLabel);

		QHBoxLayout* buttonLayout = new QHBoxLayout();
		QPushButton* deleteButton = new QPushButton("삭제");
		QPushButton* closeButton = new QPushButton("닫기");

		deleteButton->setStyleSheet("background-color: #ff4c4c; color: white; padding: 6px;");
		closeButton->setStyleSheet("padding: 6px;");

		connect(deleteButton, &QPushButton::clicked, this, [=]() {
					if (QMessageBox::question(this, "삭제", imagePath + "파일을 삭제할까요?")) {
							emit deleteImageRequested(imagePath);
							qDebug() << "[MainWindow] Preview delete button called";
							previewDialog->accept();
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

void MainWindow::showInfo(const QString& title, const QString& message) {
    QMessageBox::information(this, title, message);
}

void MainWindow::showError(const QString& title, const QString& message) {
    QMessageBox::critical(this, title, message);
}


void MainWindow::onShowUserList() 
{
		QString filePath = QString::fromStdString(USER_LABEL_FILE);
		QFile file(filePath);

		if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
				QMessageBox::warning(this, "오류",  "label.txt 파일을 열 수 없습니다.");
				return;
		}

		QStringList users;
		while (!file.atEnd()) {
				QByteArray line = file.readLine();
				QString str(line);
				QStringList parts = str.trimmed().split(' ');
				if (parts.size() >= 2) {
						users.append(parts[0] + ": " + parts[1]);
				}
		}

		if (users.isEmpty()) {
				QMessageBox::information(this, "사용자 목록", "등록된 사용자가 없습니다.");
		} else {
				 QMessageBox::information(this, "사용자 목록", users.join("\n"));
		}
}

void MainWindow::showDuplicateUserMessage()
{
		QMessageBox::information(this, "중복 사용자", "이미 등록된 얼굴입니다.");
}

void MainWindow::showErrorMessage(const QString& title, const QString& message)
{
		QMessageBox::critical(this, title, message);

}

void MainWindow::showStatusMessage(const QString& msg)
{
		if (ui->statusbar) {
				ui->statusbar->showMessage(msg, 3000);
		}
}

MainWindow::~MainWindow() {
    delete ui;
}
