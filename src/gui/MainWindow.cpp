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

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) 
{
		qDebug() << "[MainWindow] constructor Created";
    qRegisterMetaType<RecognitionState>("RecognitionState");

		
		mainPresenter = new MainPresenter(this);
		if (!mainPresenter) {
				qDebug() << "[MainWindow] Failed to allocate memory"; 
				return;
		}

		mainPresenter->startAllServices();

		setupUi();
}

void MainWindow::setupUi() {
		qDebug() << "[MainWindow] setupUi is called";

    ui->setupUi(this);

		qDebug() << "[setupUi] Window minimum size: Width->" << WINDOW_MIN_WIDTH << ", Height->" << WINDOW_MIN_HEIGHT;
    setMinimumSize(WINDOW_MIN_WIDTH, WINDOW_MIN_HEIGHT);
    if (ui->cameraLabel) {
        ui->cameraLabel->setStyleSheet(CAM_LABEL_STYLE);
    }

    applyStyles();
		setupUnlockOverlayLabel();
		connectSignals();
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

void MainWindow::resizeEvent(QResizeEvent *event)
{
		qDebug() << "[MainWindow] resizeEvent is called";
		QMainWindow::resizeEvent(event);
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
    safeConnect(ui->clearButton, &MainWindow::onClearUsers, "Clean");
    safeConnect(ui->ExitButton, &MainWindow::onExitProgram, "Exit");

		
		/*
		connect(ui->showUserImages, &QPushButton::clicked, this, [=]() {
					qDebug() << "[MainWindow] User image button has been clicked";
					emit showUserImagesRequested();
		});
		*/
}

QList<QPushButton*> MainWindow::buttonList() const
{
		return {
				ui->registerButton,
				ui->clearButton,
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

void MainWindow::setRecognitionState(RecognitionState state) {
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
		qDebug() << "[MainWindow] onClearUsers is called";
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
		qDebug() << "[MainWindow] onExitProgram is called";
    if (QMessageBox::question(this, "종료", "프로그램을 종료할까요?") == QMessageBox::Yes)
        QApplication::quit();
}

void MainWindow::showImagePreview(const QString& imagePath) 
{
		qDebug() << "[MainWindow] showImagePreview is called";
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
		/*
		QStringList usersList;

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
		*/

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
				ui->statusbar->showMessage(msg, 3000);
		}
}

MainWindow::~MainWindow() {
    delete ui;
}
