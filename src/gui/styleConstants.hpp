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
*/
const QString BTN_STYLE = R"(
QPushButton {
    background-color: #2B2B2B;
    color: #EAEAEA;
    border: 1px solid #3A3A3A;
    border-radius: 14px;
    padding: 10px 20px;
    font-size: 16px;
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

