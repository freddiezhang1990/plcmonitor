#ifndef DEVICETREEMODEL_H
#define DEVICETREEMODEL_H

#include <QAbstractItemModel>
#include "common_types.h"
#include "plcdata.h"

class DeviceTreeModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum NodeType { Root, Device, Group, Tag };
    struct TreeNode {
        NodeType type;
        QString deviceId;               // 仅当 type == Device
        TagGroup *group; // 仅当 type == Group
        TagInfo *tag;                  // 仅当 type == Tag
        TreeNode *parent = nullptr;
        QList<TreeNode*> children;
        Qt::CheckState checkState = Qt::Unchecked;
        bool isCheckable = false;
    };

    explicit DeviceTreeModel(PLCData *plcData, QObject *parent = nullptr);
    ~DeviceTreeModel();

    void rebuildModel();                          // 从 PLCData 重建树
    void setCurrentView(ViewType view);          // 新增：切换当前视图
    void loadCurrentViewSelections();
    void applySelectionsToView();                // 改为无参，应用当前视图  // 将树的勾选状态应用到指定视图
    void loadSelectionsFromView(ViewType view);  // 从视图加载勾选状态到树

    // 获取选中的标签键列表（deviceId/tagName）
    QStringList getSelectedTagKeys() const;

    // 节点操作
    bool addDevice(DeviceConfig *device);
    bool addGroup(TreeNode *parentNode, const QString &groupName);
    bool addTag(TreeNode *parentGroupNode, const TagInfo &tag);
    bool updateNode(TreeNode *node, const QVariant &newValue);
    bool removeNode(TreeNode *node);

    // QAbstractItemModel 接口
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    TreeNode* nodeFromIndex(const QModelIndex &index) const;
    void deleteNode(TreeNode *node);
    TreeNode* buildTreeFromData();
    void collectCheckedTags(TreeNode *node, QStringList &out) const;

    // 获取节点下所有标签的 tagKey 列表
    QStringList getTagKeysUnderNode(TreeNode *node) const;
    // 更新父节点的勾选状态（递归向上）
    void updateParentCheckState(TreeNode *node);
    // 递归设置节点下所有标签的视图选择状态
    void setSelectionForSubtree(TreeNode *node, bool selected);
    QString getDeviceIdForNode(TreeNode* node) const;
    QModelIndex indexForNode(TreeNode* node) const;

    PLCData *m_plcData;
    TreeNode *m_root;

    ViewType m_currentView = ViewType::TABLE_VIEW;   // 当前视图
};

#endif // DEVICETREEMODEL_H