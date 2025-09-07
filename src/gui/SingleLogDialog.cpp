#include "SingleLogDialog.hpp"
#include <QTableView>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QPixmap>
#include <QDebug>

SingleLogDialog::SingleLogDialog(LogKind kind, QWidget* parent)
: QDialog(parent), kind_(kind)
{
    table_ = new QTableView(this);
    model_ = new QStandardItemModel(this);
    table_->setModel(model_);

    auto* root = new QVBoxLayout(this);
    root->addWidget(table_);
    setLayout(root);
    resize(1000, 640);

    setupHeaders(); // 헤더만 먼저 세팅
}

void SingleLogDialog::setupHeaders() {
    if (kind_ == LogKind::Auth) {
        setWindowTitle(tr("Auth Logs"));
        model_->setHorizontalHeaderLabels({"ID","User","Message","Timestamp","Image"});
    } else {
        setWindowTitle(tr("System Logs"));
        model_->setHorizontalHeaderLabels({"ID","Level","Tag","Message","Timestamp"});
    }
}

void SingleLogDialog::applyLogTableStyle(int idCol, int userCol, int msgCol, int tsCol, int imgCol, int rowHeight)
{
    auto* hv = table_->horizontalHeader();
    auto* vv = table_->verticalHeader();
    if (!hv || !vv) return;

    hv->setStretchLastSection(false);
    hv->setSectionResizeMode(idCol,   QHeaderView::ResizeToContents);
    hv->setSectionResizeMode(userCol, QHeaderView::ResizeToContents);
    hv->setSectionResizeMode(msgCol,  QHeaderView::Stretch);
    hv->setSectionResizeMode(tsCol,   QHeaderView::ResizeToContents);
    hv->setSectionResizeMode(imgCol,  QHeaderView::ResizeToContents);

    vv->setDefaultSectionSize(rowHeight);
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSortingEnabled(true);
    table_->sortByColumn(idCol, Qt::DescendingOrder);
}

void SingleLogDialog::setAuthLogs(const QVector<AuthLog>& rows)
{
    Q_ASSERT(kind_ == LogKind::Auth);
    model_->removeRows(0, model_->rowCount());

    for (const auto& r : rows) {
        QList<QStandardItem*> line;
        line << new QStandardItem(QString::number(r.id));
        line << new QStandardItem(r.userName);
        line << new QStandardItem(r.message);
        line << new QStandardItem(r.timestamp.toString("yyyy-MM-dd HH:mm:ss"));

        auto* imgItem = new QStandardItem;
        if (!r.imageBlob.isEmpty()) {
            QPixmap pm; pm.loadFromData(r.imageBlob);
            imgItem->setData(pm.scaled(64,48,Qt::KeepAspectRatio,Qt::SmoothTransformation),
                             Qt::DecorationRole);
        }
        line << imgItem;
        model_->appendRow(line);
    }

    if (!styleApplied_) {
        // ID=0, User=1, Message=2, Timestamp=3, Image=4
        applyLogTableStyle(/*id*/0, /*user*/1, /*msg*/2, /*ts*/3, /*img*/4, /*rowHeight*/56);
        styleApplied_ = true;
    }
}

void SingleLogDialog::setSystemLogs(const QVector<SystemLog>& rows)
{
    Q_ASSERT(kind_ == LogKind::System);
    model_->removeRows(0, model_->rowCount());

    for (const auto& r : rows) {
        QList<QStandardItem*> line;
        line << new QStandardItem(QString::number(r.id));
        line << new QStandardItem(QString::number(r.level));
        line << new QStandardItem(r.tag);
        line << new QStandardItem(r.message);
        line << new QStandardItem(r.timestamp.toString("yyyy-MM-dd HH:mm:ss"));
        model_->appendRow(line);
    }

    if (!styleApplied_) {
        // ID=0, Level=1, Tag=2, Message=3, Timestamp=4  (이미지 컬럼 없음 → imgCol=4로 둬도 OK)
        applyLogTableStyle(/*id*/0, /*user*/1, /*msg*/3, /*ts*/4, /*img*/4, /*rowHeight*/28);
        styleApplied_ = true;
    }
}

