#ifndef DEVICECONFIGDIALOG_H
#define DEVICECONFIGDIALOG_H

#include <QDialog>
#include <qabstractitemmodel.h>
#include "common_types.h"
#include "plcdata.h"

namespace Ui {
class DeviceConfigDialog;
}

class DeviceTreeModel;

class DeviceConfigDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DeviceConfigDialog(PLCData *plcData, QWidget *parent = nullptr);
    ~DeviceConfigDialog();

    void setCurrentView(ViewType view);

signals:
    void tagDatabaseEnabledChanged();  // 标签的 dbEnabled 状态可能有变化

private slots:
    void onViewChanged(int index);
    void onApply();
    void onSave();
    void onClose();
    void onAddDevice();
    void onEditDevice();
    void onDeleteDevice();
    void onAddGroup();
    void onAddTag();
    void onEditTag();
    void onDeleteTag();
    void onTreeSelectionChanged(const QModelIndex &current);

private:
    void loadSelectionsForCurrentView();
    void updatePropertyPanel(const QModelIndex &index);
    void showDeviceProperties(DeviceConfig *dev);
    void showGroupProperties(TagGroup *group);
    void showTagProperties(TagInfo *tag);
    void clearPropertyPanel();
    void setTagPropertiesEditable(bool editable);  // 控制属性页控件是否可编辑


    PLCData *m_plcData;
    DeviceTreeModel *m_treeModel;
    Ui::DeviceConfigDialog *ui;   // 改为指针成员
    TagInfo* m_currentTag = nullptr;
    bool m_tagEditMode = false;                   // 是否处于编辑模式
    QPersistentModelIndex m_currentTagIndex;       // 当前正在编辑的标签树节点索引
};

#endif // DEVICECONFIGDIALOG_H