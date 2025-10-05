#pragma once
#include <QString>

// 버튼 공통 스타일 (라운드, 마우스 반응 포함)
/*
const QString BTN_STYLE = R"(
    QPushButton {
        background-color: #007AFF;
        color: white;
        border-radius: 12px;
        padding: 8px 16px;
        font-size: 15px;
        font-weight: 500;
        border: none;
    }
    QPushButton:hover {
        background-color: #339CFF;
    }
    QPushButton:pressed {
        background-color: #005FCC;
    }
)";
const QString BTN_STYLE = R"(
QPushButton {
    background-color: #2B2B2B;
    color: #EAEAEA;
    border: 1px solid #3A3A3A;
    border-radius: 14px;
    padding: 10px 20px;
    font-size: 30px;
    font-family: 'Segoe UI', 'Apple SD Gothic Neo', sans-serif;
    font-weight: 500;
}

QPushButton:hover {
    background-color: #3A3A3A;
}

QPushButton:pressed {
    background-color: #1E1E1E;
}

QPushButton:disabled {
    background-color: #555555;
    color: #999999;
    border: 1px solid #444444;
}
)";

// 카메라 라벨 스타일 (라운드 + 연한 테두리)
const QString CAM_LABEL_STYLE = R"(
    QLabel {
        border: 2px solid #E0E0E0;
        border-radius: 10px;
        background-color: #FAFAFA;
    }
)";

// 상태바 스타일 (얇고 차분한 회색 + 중간 투명 느낌)
const QString STATUS_BAR_STYLE = R"(
    QStatusBar {
        background-color: #F2F2F2;
        color: #555555;
        font-size: 13px;
        padding-left: 10px;
    }
)";
*/


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

