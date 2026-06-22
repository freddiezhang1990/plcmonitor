#include "devicetreemodel.h"
#include <QDebug>
#include <QStack>

DeviceTreeModel::DeviceTreeModel(PLCData *plcData, QObject *parent)
    : QAbstractItemModel(parent), m_plcData(plcData), m_root(nullptr)
{
    rebuildModel();
}

DeviceTreeModel::~DeviceTreeModel()
{
    deleteNode(m_root);
}

void DeviceTreeModel::rebuildModel()
{
    beginResetModel();
    deleteNode(m_root);
    m_root = buildTreeFromData();
    endResetModel();
}

DeviceTreeModel::TreeNode* DeviceTreeModel::buildTreeFromData()
{
    auto *root = new TreeNode{NodeType::Root, QString(), nullptr, nullptr, nullptr};
    for (DeviceConfig *dev : m_plcData->getAllDevices()) {
        auto *devNode = new TreeNode{NodeType::Device, dev->deviceId, nullptr, nullptr, root};
        devNode->isCheckable = true;
        // 递归添加分组和标签
        std::function<void(TagGroup*, TreeNode*)> addGroup =
            [&](TagGroup *group, TreeNode *parentNode) {
                auto *groupNode = new TreeNode{NodeType::Group, QString(), group, nullptr, parentNode};
                groupNode->isCheckable = true;
                for (TagInfo &tag : group->tags) {
                    auto *tagNode = new TreeNode{NodeType::Tag, QString(), nullptr, &tag, groupNode};
                    tagNode->isCheckable = true;
                    tagNode->checkState = Qt::Unchecked;
                    groupNode->children.append(tagNode);
                }
                for (auto *sub : group->subGroups) {
                    addGroup(sub, groupNode);
                }
                parentNode->children.append(groupNode);
            };
        for (auto *group : dev->rootGroups) {
            addGroup(group, devNode);
        }
        root->children.append(devNode);
    }
    return root;
}

void DeviceTreeModel::deleteNode(TreeNode *node)
{
    if (!node) return;
    for (TreeNode *child : node->children)
        deleteNode(child);
    delete node;
}

QModelIndex DeviceTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!m_root) return QModelIndex();
    TreeNode *parentNode = parent.isValid() ? nodeFromIndex(parent) : m_root;
    if (row < 0 || row >= parentNode->children.size()) return QModelIndex();
    TreeNode *child = parentNode->children[row];
    return createIndex(row, column, child);
}

QModelIndex DeviceTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid()) return QModelIndex();
    TreeNode *node = static_cast<TreeNode*>(child.internalPointer());
    TreeNode *parentNode = node->parent;
    if (!parentNode || parentNode == m_root) return QModelIndex();
    TreeNode *grandParent = parentNode->parent;
    if (!grandParent) return QModelIndex();
    int row = grandParent->children.indexOf(parentNode);
    return createIndex(row, 0, parentNode);
}

int DeviceTreeModel::rowCount(const QModelIndex &parent) const
{
    if (!m_root) return 0;
    TreeNode *parentNode = parent.isValid() ? nodeFromIndex(parent) : m_root;
    return parentNode->children.size();
}

int DeviceTreeModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 3; // 名称, 地址, 数据类型
}

QVariant DeviceTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();
    TreeNode *node = nodeFromIndex(index);
    if (!node) return QVariant();

    if (role == Qt::DisplayRole) {
        // ... 原有代码保持不变 ...
        if (index.column() == 0) {
            switch (node->type) {
            case NodeType::Device: return m_plcData->getDevice(node->deviceId)->deviceName;
            case NodeType::Group: return node->group->groupName;
            case NodeType::Tag: return node->tag->name;
            default: return QString();
            }
        } else {
            if (node->type != NodeType::Tag) return QString();
            if (index.column() == 1) return node->tag->address;
            if (index.column() == 2) return tagDataTypeToString(node->tag->dataType);
            return QVariant();
        }
    }
    else     if (role == Qt::CheckStateRole && index.column() == 0) {
        TreeNode* node = nodeFromIndex(index);
        if (node->type == NodeType::Tag) {
            QString deviceId = getDeviceIdForNode(node);
            QString tagKey = m_plcData->makeTagKey(deviceId, node->tag->name);
            return m_plcData->isTagSelectedInView(m_currentView, tagKey) ? Qt::Checked : Qt::Unchecked;
        }
        else if (node->type == NodeType::Device || node->type == NodeType::Group) {
            QStringList tagKeys = getTagKeysUnderNode(node);
            if (tagKeys.isEmpty()) return Qt::Unchecked;
            int checked = 0;
            for (const QString& key : tagKeys) {
                if (m_plcData->isTagSelectedInView(m_currentView, key))
                    checked++;
            }
            if (checked == tagKeys.size()) return Qt::Checked;
            if (checked > 0) return Qt::PartiallyChecked;
            return Qt::Unchecked;
        }
    }
    return QVariant();
}

bool DeviceTreeModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.column() != 0 || role != Qt::CheckStateRole)
        return false;

    TreeNode* node = nodeFromIndex(index);
    Qt::CheckState state = static_cast<Qt::CheckState>(value.toInt());

    if (node->type == NodeType::Tag) {
        QString deviceId = getDeviceIdForNode(node);
        QString tagKey = m_plcData->makeTagKey(deviceId, node->tag->name);
        bool selected = (state == Qt::Checked);
        m_plcData->setTagSelectedInView(m_currentView, tagKey, selected);
        // 更新父节点显示
        updateParentCheckState(node);
        emit dataChanged(index, index, {Qt::CheckStateRole});
        return true;
    }
    else if (node->type == NodeType::Device || node->type == NodeType::Group) {
        bool selected = (state == Qt::Checked);
        setSelectionForSubtree(node, selected);
        // 更新当前节点及其父节点显示
        emit dataChanged(index, index, {Qt::CheckStateRole});
        QModelIndex parentIdx = index.parent();
        if (parentIdx.isValid())
            emit dataChanged(parentIdx, parentIdx, {Qt::CheckStateRole});
        // 子节点状态变化：由于 PLCData 会发射 viewSelectionChanged 信号，可以在此信任外部刷新
        // 或者遍历所有子标签索引发射 dataChanged（可选）
        return true;
    }

    return false;
}

Qt::ItemFlags DeviceTreeModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractItemModel::flags(index);
    TreeNode *node = nodeFromIndex(index);
    if (node && index.column() == 0) {
        // 设备、分组、标签节点都显示复选框（列0）
        if (node->type == NodeType::Device || node->type == NodeType::Group || node->type == NodeType::Tag) {
            f |= Qt::ItemIsUserCheckable;
        }
    }
    return f;
}

QVariant DeviceTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case 0: return "名称";
        case 1: return "地址";
        case 2: return "数据类型";
        default: return QVariant();
        }
    }
    return QVariant();
}

DeviceTreeModel::TreeNode* DeviceTreeModel::nodeFromIndex(const QModelIndex &index) const
{
    return static_cast<TreeNode*>(index.internalPointer());
}

void DeviceTreeModel::setCurrentView(ViewType view)
{
    m_currentView = view;
 //   loadSelectionsFromView(view);   // 加载该视图的选中状态到树中
}

void DeviceTreeModel::loadCurrentViewSelections()
{
    loadSelectionsFromView(m_currentView);
}

void DeviceTreeModel::applySelectionsToView()
{
    /*/ 收集树中所有勾选的标签键（当前树的勾选状态）
    QStringList selectedKeys;
    collectCheckedTags(m_root, selectedKeys);
 //   qDebug() << "applySelectionsToView: currentView =" << static_cast<int>(m_currentView);
    qDebug() << "applySelectionsToView: view=" << (int)m_currentView << "keys=" << selectedKeys;

    // 清除当前视图的旧选择，并逐个添加新选择
    m_plcData->clearViewSelection(m_currentView);
    for (const QString &key : selectedKeys) {
        m_plcData->setTagSelectedInView(m_currentView, key, true);
    }
    */
    // 禁用：避免崩溃
    qDebug() << "applySelectionsToView is temporarily disabled";
    return;
}

void DeviceTreeModel::loadSelectionsFromView(ViewType view)
{
    // 遍历所有标签节点，发射 dataChanged 以刷新显示（复选框状态从 PLCData 实时获取）
    std::function<void(TreeNode*)> refresh = [&](TreeNode* node) {
        if (node->type == NodeType::Tag) {
            QModelIndex idx = createIndex(node->parent->children.indexOf(node), 0, node);
            emit dataChanged(idx, idx, {Qt::CheckStateRole});
        }
        for (TreeNode* child : node->children) refresh(child);
    };
    refresh(m_root);
}

QStringList DeviceTreeModel::getSelectedTagKeys() const
{
    QStringList keys;
    collectCheckedTags(m_root, keys);
    return keys;
}

void DeviceTreeModel::collectCheckedTags(TreeNode *node, QStringList &out) const
{
    if (node->type == NodeType::Tag && node->tag) {
        QString deviceId = getDeviceIdForNode(node);
        QString tagKey = m_plcData->makeTagKey(deviceId, node->tag->name);
        // 直接从 PLCData 查询当前视图的选中状态
        if (m_plcData->isTagSelectedInView(m_currentView, tagKey)) {
            out << tagKey;
        }
    }
    for (TreeNode *child : node->children)
        collectCheckedTags(child, out);
}

bool DeviceTreeModel::addDevice(DeviceConfig *device)
{
    m_plcData->addDevice(device);
    rebuildModel();
    return true;
}

bool DeviceTreeModel::addGroup(TreeNode *parentNode, const QString &groupName)
{
    if (!parentNode || (parentNode->type != NodeType::Device && parentNode->type != NodeType::Group))
        return false;
    // 根据父节点类型获取设备ID和父分组指针
    QString deviceId;
    TagGroup *parentGroup = nullptr;
    if (parentNode->type == NodeType::Device) {
        deviceId = parentNode->deviceId;
        parentGroup = nullptr;
    } else {
        deviceId = parentNode->parent->deviceId;
        parentGroup = parentNode->group;
    }
    DeviceConfig *dev = m_plcData->getDevice(deviceId);
    if (!dev) return false;
    auto *newGroup = new TagGroup(groupName);
    if (parentGroup)
        parentGroup->subGroups.append(newGroup);
    else
        dev->rootGroups.append(newGroup);
    // 更新索引
    m_plcData->rebuildIndex();
    emit dataChanged(QModelIndex(), QModelIndex());
    rebuildModel(); // 简单重建
    return true;
}

bool DeviceTreeModel::addTag(TreeNode *parentGroupNode, const TagInfo &tag)
{
    if (!parentGroupNode || parentGroupNode->type != NodeType::Group) return false;
    QString deviceId = parentGroupNode->parent->deviceId;
    DeviceConfig *dev = m_plcData->getDevice(deviceId);
    if (!dev) return false;
    // 找到对应的分组
    std::function<bool(TagGroup*)> findAndAdd = [&](TagGroup *group) -> bool {
        if (group == parentGroupNode->group) {
            group->tags.append(tag);
            return true;
        }
        for (auto *sub : group->subGroups) {
            if (findAndAdd(sub)) return true;
        }
        return false;
    };
    bool ok = false;
    for (auto *group : dev->rootGroups) {
        if (findAndAdd(group)) { ok = true; break; }
    }
    if (ok) {
        m_plcData->rebuildIndex();
        rebuildModel();
    }
    return ok;
}

bool DeviceTreeModel::updateNode(TreeNode *node, const QVariant &newValue)
{
    // 根据节点类型更新数据，此函数暂不实现完整逻辑，留给对话框
    Q_UNUSED(node);
    Q_UNUSED(newValue);
    return false;
}

bool DeviceTreeModel::removeNode(TreeNode *node)
{
    if (!node || node->type == NodeType::Root) return false;
    // 移除节点及其关联数据
    if (node->type == NodeType::Device) {
        m_plcData->removeDevice(node->deviceId);
    } else if (node->type == NodeType::Group) {
        // 从父分组或设备根组中删除该分组指针
        TreeNode *parentNode = node->parent;
        if (parentNode->type == NodeType::Device) {
            DeviceConfig *dev = m_plcData->getDevice(parentNode->deviceId);
            if (dev) {
                dev->rootGroups.removeOne(node->group);
                delete node->group;
            }
        } else if (parentNode->type == NodeType::Group) {
            parentNode->group->subGroups.removeOne(node->group);
            delete node->group;
        }
        m_plcData->rebuildIndex();
    } else if (node->type == NodeType::Tag) {
        // 从分组中删除标签
        TreeNode *groupNode = node->parent;
        if (groupNode && groupNode->group) {
            groupNode->group->tags.removeOne(*node->tag);
            m_plcData->rebuildIndex();
        }
    }
    rebuildModel();
    return true;
}

QStringList DeviceTreeModel::getTagKeysUnderNode(TreeNode* node) const
{
    QStringList keys;
    if (!node) return keys;

    QStack<TreeNode*> stack;
    stack.push(node);
    while (!stack.isEmpty()) {
        TreeNode* cur = stack.pop();
        if (cur->type == NodeType::Tag && cur->tag) {
            // 获取设备ID（可能需要向上查找，或者每个节点都存储deviceId）
            QString deviceId = getDeviceIdForNode(cur);
            keys.append(m_plcData->makeTagKey(deviceId, cur->tag->name));
        }
        for (TreeNode* child : cur->children)
            stack.push(child);
    }
    return keys;
}

void DeviceTreeModel::updateParentCheckState(TreeNode* node)
{
    TreeNode* parent = node->parent;
    if (!parent || (parent->type != NodeType::Device && parent->type != NodeType::Group))
        return;

    QModelIndex parentIdx = indexForNode(parent);
    if (parentIdx.isValid())
        emit dataChanged(parentIdx, parentIdx, {Qt::CheckStateRole});

    // 继续向上更新
    updateParentCheckState(parent);
}

void DeviceTreeModel::setSelectionForSubtree(TreeNode* node, bool selected)
{
    if (!node) return;

    if (node->type == NodeType::Tag && node->tag) {
        QString deviceId = getDeviceIdForNode(node);
        QString tagKey = m_plcData->makeTagKey(deviceId, node->tag->name);
        m_plcData->setTagSelectedInView(m_currentView, tagKey, selected);
    } else {
        for (TreeNode* child : node->children) {
            setSelectionForSubtree(child, selected);
        }
    }
}

QString DeviceTreeModel::getDeviceIdForNode(TreeNode* node) const
{
    if (!node) return QString();
    // 向上查找，直到找到类型为 Device 的节点
    TreeNode* current = node;
    while (current) {
        if (current->type == NodeType::Device) {
            return current->deviceId;
        }
        current = current->parent;
    }
    return QString();
}

QModelIndex DeviceTreeModel::indexForNode(TreeNode* node) const
{
    if (!node || node == m_root) return QModelIndex();

    // 递归查找父节点索引
    QModelIndex parentIdx = indexForNode(node->parent);
    if (!parentIdx.isValid() && node->parent != m_root)
        return QModelIndex();

    int row = node->parent->children.indexOf(node);
    if (row < 0) return QModelIndex();

    return createIndex(row, 0, node);
}