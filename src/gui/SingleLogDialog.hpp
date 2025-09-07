#pragma once
#include <QDialog>
#include <QVector>
#include "services/LogDtos.hpp"

class QTableView;
class QStandardItemModel;

enum class LogKind { Auth, System };

class SingleLogDialog : public QDialog {
    Q_OBJECT
public:
    explicit SingleLogDialog(LogKind kind, QWidget* parent = nullptr);

    void setAuthLogs(const QVector<AuthLog>& rows);       // kind == Auth 에서 사용
    void setSystemLogs(const QVector<SystemLog>& rows);   // kind == System 에서 사용

private:
    LogKind kind_;
    QTableView* table_ = nullptr;
    QStandardItemModel* model_ = nullptr;
    bool styleApplied_ = false;

    void applyLogTableStyle(int idCol, int userCol, int msgCol, int tsCol, int imgCol, int rowHeight);
    void setupHeaders();  // kind에 따라 헤더 구성
};

