#ifndef TAGCONFIGDIALOG_H
#define TAGCONFIGDIALOG_H

#include <QDialog>
#include <QVector>
#include "common_types.h"


QT_BEGIN_NAMESPACE
namespace Ui {
class TagConfigDialog;
}
QT_END_NAMESPACE

class PLCData;

// 前向声明模型类
class DeviceModel;
class TagTreeModel;

class TagConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TagConfigDialog(PLCData* plcData, QWidget *parent = nullptr);
    ~TagConfigDialog();

    // 获取/设置当前选中的设备ID
    QString currentDeviceId() const;
    void setCurrentDeviceId(const QString& id);

private slots:
    void on_btnAddDevice_clicked();
    void on_btnDelDevice_clicked();
    void on_btnAddGroup_clicked();
    void on_btnAddTag_clicked();
    void on_btnEditTag_clicked();
    void on_btnDelTag_clicked();
    void on_btnOk_clicked();
    void on_btnCancel_clicked();

    void on_deviceListView_clicked(const QModelIndex& index);
    void on_tagTreeView_clicked(const QModelIndex& index);

private:
    void setupModels();
    void loadDevices();
    void loadTagsForDevice(const QString& deviceId);

    Ui::TagConfigDialog *ui;
    PLCData* m_plcData;

    DeviceModel* m_deviceModel; // 管理左侧设备列表
    TagTreeModel* m_tagTreeModel; // 管理中间的标签树

    QString m_currentDeviceId;
};

#endif // TAGCONFIGDIALOG_H