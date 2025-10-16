#pragma once
#include <QString>
#include <QDialog>


// 버튼 스타일 — 모양은 유지, 크기/패딩/폰트 업
const QString BTN_STYLE = R"(
QPushButton {
    background-color: #2B2B2B;
    color: #EAEAEA;
    border: 1px solid #3A3A3A;
    border-radius: 16px;
    padding: 14px 28px;         /* ← 더 넉넉한 패딩 */
    font-size: 32px;            /* ← 글씨 크게 */
    font-family: 'Segoe UI', 'Apple SD Gothic Neo', 'Noto Sans CJK KR', sans-serif;
    font-weight: 600;           /* 살짝 굵게 */
}
QPushButton:hover { background-color: #3A3A3A; }
QPushButton:pressed { background-color: #1E1E1E; }
QPushButton:disabled {
    background-color: #555555;
    color: #999999;
    border: 1px solid #444444;
}
)";
// 버튼 스타일 — 모양은 유지, 크기/패딩/폰트 업
const QString BTN_STYLE_2 = R"(
QPushButton {
    background-color: #2B2B2B;
    color: #EAEAEA;
    border: 1px solid #3A3A3A;
    border-radius: 16px;
    padding: 14px 28px;         /* ← 더 넉넉한 패딩 */
    font-size: 23px;            /* ← 글씨 크게 */
    font-family: 'Segoe UI', 'Apple SD Gothic Neo', 'Noto Sans CJK KR', sans-serif;
    font-weight: 600;           /* 살짝 굵게 */
}
QPushButton:hover { background-color: #3A3A3A; }
QPushButton:pressed { background-color: #1E1E1E; }
QPushButton:disabled {
    background-color: #555555;
    color: #999999;
    border: 1px solid #444444;
}
)";

// 카메라 라벨 — 틀은 유지, 테두리 살짝 굵게, 배경을 더 중립 톤
const QString CAM_LABEL_STYLE = R"(
QLabel#cameraLabel {
    border: 2px solid #DADADA;
    border-radius: 12px;
    background-color: #F7F7F7;
}
)";

// 상태바 — 약간 더 크게
const QString STATUS_BAR_STYLE = R"(
QStatusBar {
    background-color: #F2F2F2;
    color: #555555;
    font-size: 16px;
    padding-left: 12px;
}
)";

// 입력 계열(라인에딧/콤보/스핀/텍스트에딧) — 높이와 폰트 업, 기존 톤 유지
const QString INPUT_STYLE = R"(
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QTextEdit {
    background: #FFFFFF;
    color: #222222;
    border: 1px solid #CFCFCF;
    border-radius: 10px;
    padding: 10px 14px;
    font-size: 26px;     /* ← 크게 */
    selection-background-color: #D0D4D9;
}
QComboBox::drop-down {
    border: none;
    width: 28px;
}
QComboBox QAbstractItemView {
    font-size: 24px;
}
)";

// 정보 박스(예: QGroupBox 타이틀) — 가독성만 업
const QString GROUPBOX_STYLE = R"(
QGroupBox {
    font-size: 22px;
    font-weight: 600;
    margin-top: 8px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 8px;
    padding: 2px 4px;
}
)";


// ===== 질의 박스(= QMessageBox) =====
const QString MESSAGEBOX_STYLE = R"(
QMessageBox {
    background: #FFFFFF;
    min-width: 360px;         /* 모바일에서 꽉 차 보이게 */
    border-radius: 12px;
}
QMessageBox QLabel {
    font-size: 26px;          /* 본문 크게 */
    color: #222222;
    line-height: 140%;
}
QMessageBox QPushButton {
    font-size: 26px;          /* 버튼 글자 크게 */
    padding: 12px 20px;       /* 터치 영역 확대 */
    min-height: 48px;
    min-width: 96px;
    border-radius: 12px;
    background: #F2F4F7;      /* 연한 회색 버튼 */
    color: #222222;
    border: 1px solid #CFD3D8;
}
QMessageBox QPushButton:hover {
    background: #E9EDF2;
}
QMessageBox QPushButton:pressed {
    background: #E1E6EC;
}
QMessageBox QPushButton:default {
    background: #2E6EF7;      /* 기본 버튼 강조 */
    color: #FFFFFF;
    border: 1px solid #2E6EF7;
}
)";

// ===== 인풋 박스(= QInputDialog 전용) =====
static const char* INPUT_DIALOG_STYLE = R"(
QInputDialog {
    background: #FFFFFF;
    min-width: 480px;
    min-height: 280px;
    border-radius: 16px;
    padding: 24px;
}

/* 입력창(QLineEdit) */
QLineEdit {
    background: #FAFAFA;
    color: #111111;
    border: 2px solid #C5C9CF;
    border-radius: 12px;
    padding: 14px 18px;
    font-size: 28px;            /* 터치 환경용 */
    selection-background-color: #D0D4D9;
}

/* 안내 텍스트 (QLabel) */
QInputDialog QLabel {
    font-size: 26px;
    color: #222222;
    line-height: 150%;
}

/* 버튼 */
QPushButton {
    font-size: 26px;
    padding: 16px 28px;
    min-height: 64px;
    min-width: 140px;
    border-radius: 14px;
    background: #F2F4F7;
    color: #222222;
    border: 1px solid #CFD3D8;
}
QPushButton:hover   { background: #E9EDF2; }
QPushButton:pressed { background: #E1E6EC; }
QPushButton:default {
    background: #2E6EF7;
    color: #FFFFFF;
    border: 1px solid #2E6EF7;
}
)";


// 갤러리 전용 스타일시트
static const char* GALLERY_DIALOG_STYLE = R"(
QDialog#GalleryDialog {
    background: #0f141a;      /* 다크 배경 */
    color: #eef2f6;           /* 본문 텍스트 */
    border-radius: 16px;
}

/* 스크롤/컨테이너 테두리 제거 */
QScrollArea { border: none; }
QWidget#GalleryContainer { background: transparent; }

/* 썸네일(ClickableLabel) */
QLabel[role="thumb"] {
    background: #111827;
    border: 2px solid #1f2937;
    border-radius: 12px;
}

/* 이름 라벨 */
QLabel[role="name"] {
    font-size: 18px;
    color: #cbd5e1;
    padding-top: 6px;
}

/* 삭제/닫기 버튼 */
QPushButton[role="delete"] {
    font-size: 16px;
    padding: 8px 12px;
    border-radius: 10px;
    background: #ef4444;
    color: #ffffff;
    border: none;
}
QPushButton[role="delete"]:hover  { background: #f87171; }
QPushButton[role="delete"]:pressed{ background: #dc2626; }

QPushButton[role="close"] {
    font-size: 18px;
    padding: 10px 18px;
    border-radius: 12px;
    background: #374151;
    color: #ffffff;
    border: none;
}
QPushButton[role="close"]:hover   { background: #4b5563; }
QPushButton[role="close"]:pressed { background: #1f2937; }

/* 그리드 안 셀 위젯(카드 느낌) */
QWidget[role="cell"] {
    background: transparent;
}
)";

/* 적용 헬퍼 */
inline void applyGalleryDialogStyle(QDialog* dlg) {
    if (!dlg) return;
    dlg->setObjectName("GalleryDialog");
    dlg->setStyleSheet(GALLERY_DIALOG_STYLE);
}


