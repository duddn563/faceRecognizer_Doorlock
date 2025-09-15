#pragma once
#include <QDialog>
#include <QStackedWidget>
#include <QPushButton>
#include <QFrame>

class DevInfoDialog : public QDialog {
    Q_OBJECT
public:
    explicit DevInfoDialog(QWidget* parent = nullptr);
    ~DevInfoDialog() override = default;

    // 각 페이지 위젯 핸들을 돌려줘서 나중에 데이터 뿌릴 때 쓸 수 있게 해둠
    QWidget* basicPage() const { return basicPage_; }
    QWidget* netPage()   const { return netPage_; }
    QWidget* cpuPage()   const { return cpuPage_; }
    QWidget* memPage()   const { return memPage_; }

private:
    enum Page { Basic = 0, Net, Cpu, Mem };
    void switchTo(Page p);

    // 왼쪽 네비게이션 버튼
    QPushButton* btnBasic_ = nullptr;
    QPushButton* btnNet_   = nullptr;
    QPushButton* btnCpu_   = nullptr;
    QPushButton* btnMem_   = nullptr;

    // 오른쪽 스택
    QStackedWidget* stack_ = nullptr;

    // 각 페이지(초기엔 placeholder, 이후 실제 위젯으로 교체 가능)
    QWidget* basicPage_ = nullptr;
    QWidget* netPage_   = nullptr;
    QWidget* cpuPage_   = nullptr;
    QWidget* memPage_   = nullptr;
};

