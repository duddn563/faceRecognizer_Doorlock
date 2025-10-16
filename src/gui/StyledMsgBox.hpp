#pragma once
#include <QMessageBox>
#include <QString>

namespace StyledMsgBox {

// 메시지박스 인스턴스에만 스타일을 적용해 보여주는 question()
QMessageBox::StandardButton question(
    QWidget* parent,
    const QString& title,
    const QString& text,
    QMessageBox::StandardButtons buttons = QMessageBox::Yes | QMessageBox::No,
    QMessageBox::StandardButton defaultButton = QMessageBox::No);

// 필요하면 정보/경고도 같은 방식으로 확장 가능
QMessageBox::StandardButton information(
    QWidget* parent, const QString& title, const QString& text,
    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton defaultButton = QMessageBox::Ok);

QMessageBox::StandardButton warning(
    QWidget* parent, const QString& title, const QString& text,
    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton defaultButton = QMessageBox::Ok);

} // namespace StyledMsgBox

