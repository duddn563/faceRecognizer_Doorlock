#include "StyledMsgBox.hpp"
#include <QPushButton>

// 이 스타일은 해당 QMessageBox 인스턴스에만 적용됩니다.
static const char* kMessageBoxStyle = R"(
QMessageBox {
    background: #FFFFFF;
    min-width: 360px;
    border-radius: 12px;
}
QMessageBox QLabel {
    font-size: 26px;
    color: #222222;
    line-height: 140%;
}
QMessageBox QPushButton {
    font-size: 26px;
    padding: 12px 20px;
    min-height: 48px;
    min-width: 96px;
    border-radius: 12px;
    background: #F2F4F7;
    color: #222222;
    border: 1px solid #CFD3D8;
}
QMessageBox QPushButton:hover   { background: #E9EDF2; }
QMessageBox QPushButton:pressed { background: #E1E6EC; }
QMessageBox QPushButton:default {
    background: #2E6EF7;
    color: #FFFFFF;
    border: 1px solid #2E6EF7;
}
)";

namespace StyledMsgBox {

static QMessageBox::StandardButton execBox(
    QWidget* parent, QMessageBox::Icon icon,
    const QString& title, const QString& text,
    QMessageBox::StandardButtons buttons,
    QMessageBox::StandardButton def)
{
    QMessageBox box(parent);
    box.setIcon(icon);
    box.setWindowTitle(title);
    box.setText(text);
    box.setStandardButtons(buttons);
    box.setDefaultButton(def);
    box.setStyleSheet(QString::fromUtf8(kMessageBoxStyle));
    box.setMinimumWidth(360);
    return static_cast<QMessageBox::StandardButton>(box.exec());
}

QMessageBox::StandardButton question(
    QWidget* parent, const QString& title, const QString& text,
    QMessageBox::StandardButtons buttons, QMessageBox::StandardButton def)
{
    return execBox(parent, QMessageBox::Question, title, text, buttons, def);
}

QMessageBox::StandardButton information(
    QWidget* parent, const QString& title, const QString& text,
    QMessageBox::StandardButtons buttons, QMessageBox::StandardButton def)
{
    return execBox(parent, QMessageBox::Information, title, text, buttons, def);
}

QMessageBox::StandardButton warning(
    QWidget* parent, const QString& title, const QString& text,
    QMessageBox::StandardButtons buttons, QMessageBox::StandardButton def)
{
    return execBox(parent, QMessageBox::Warning, title, text, buttons, def);
}


} // namespace StyledMsgBox

