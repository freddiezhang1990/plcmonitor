#include "tagconfigdialog.h"
#include "ui_tagconfigdialog.h"
#include <QMessageBox>
#include <QInputDialog>
#include <QDebug>

// 临时的简单模型实现（实际项目中应单独写成 .cpp 文件）
#include <QAbstractListModel>
#include <QStandardItemModel>

// 仅用于左侧设备列表的简单模型
class DeviceModel : public QAbstractListModel {
public:
    explicit DeviceModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    void setDevices(const QVector<DeviceConfig>& devices) {
        beginResetModel();
        m_devices = devices;
        endResetModel();
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return m_devices.size();
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() >= m_devices.size()) return QVariant();
        if (role == Qt::DisplayRole) {
            return m_devices[index.row()].name;
        }
        return QVariant();
    }

    DeviceConfig deviceAt(int row) const {
        if (row < 0 || row >= m_devices.size()) return DeviceConfig();
        return m_devices[row];
    }

private:
    QVector<DeviceConfig> m_devices;
};

// 用于中间标签树的模型
class TagTreeModel : public QStandardItemModel {
public:
    explicit TagTreeModel(QObject* parent = nullptr) : QStandardItemModel(parent) {
        setColumnCount(2);
        setHeaderData(0, Qt::Horizontal, "标签名称");
        setHeaderData(1, Qt::Horizontal, "地址");
    }

    void loadTags(const QVector<DeviceConfig::TagGroup>& groups) {
        clear();
        setColumnCount(2);
        setHeaderData(0, Qt::Horizontal, "标签名称");
        setHeaderData(1, Qt::Horizontal, "地址");

        for (const auto& group : groups) {
            addGroupToModel(group, invisibleRootItem());
        }
    }

    // 递归添加组
    void addGroupToModel(const DeviceConfig::TagGroup& group, QStandardItem* parent) {
        QStandardItem* groupItem = new QStandardItem(group.name);
        groupItem->setEditable(false);
        groupItem->setData("group", Qt::UserRole); // 标记为组
        parent->appendRow({groupItem, new QStandardItem("")});

        // 添加组下的标签
        for (const auto& tag : group.tags) {
            QStandardItem* nameItem = new QStandardItem(tag.name);
            nameItem->setData("tag", Qt::UserRole);
            nameItem->setData(tag.name, Qt::UserRole + 1); // 存储标签名
            QStandardItem* addrItem = new QStandardItem(tag.address);
            parent->appendRow({nameItem, addrItem});
        }

        // 递归添加子组
        for (const auto& child : group.children) {
            addGroupToModel(child, groupItem);
        }
    }
};

TagConfigDialog::TagConfigDialog(PLCData *plcData, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::TagConfigDialog)
    , m_plcData(plcData)
{
    ui->setupUi(this);
    setWindowTitle("标签与设备配置");

    setupModels();
    loadDevices();

    // 连接按钮信号（如果使用 auto-connect，确保对象名正确）
    connect(ui->btnOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui->btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

TagConfigDialog::~TagConfigDialog()
{
    delete ui;
}

void TagConfigDialog::setupModels()
{
    m_deviceModel = new DeviceModel(this);
    ui->deviceListView->setModel(m_deviceModel);

    m_tagTreeModel = new TagTreeModel(this);
    ui->tagTreeView->setModel(m_tagTreeModel);
}

void TagConfigDialog::loadDevices()
{
    auto devices = m_plcData->getAllDevices();
    m_deviceModel->setDevices(devices);

    if (devices.size() > 0) {
        ui->deviceListView->setCurrentIndex(m_deviceModel->index(0, 0));
        m_currentDeviceId = devices[0].id;
        loadTagsForDevice(m_currentDeviceId);
    }
}

void TagConfigDialog::loadTagsForDevice(const QString &deviceId)
{
    auto device = m_plcData->getDevice(deviceId);
    m_tagTreeModel->loadTags(device.rootGroups);
}

void TagConfigDialog::on_deviceListView_clicked(const QModelIndex &index)
{
    if (!index.isValid()) return;
    auto device = m_deviceModel->deviceAt(index.row());
    m_currentDeviceId = device.id;
    loadTagsForDevice(m_currentDeviceId);
}

void TagConfigDialog::on_btnAddDevice_clicked()
{
    bool ok;
    QString name = QInputDialog::getText(this, "添加设备", "设备名称:", QLineEdit::Normal, "", &ok);
    if (ok && !name.isEmpty()) {
        DeviceConfig config;
        config.id = QUuid::createUuid().toString(); // 生成唯一ID
        config.name = name;
        config.ip = "192.168.0.1"; // 默认IP

        if (m_plcData->addDevice(config)) {
            loadDevices(); // 重新加载列表
        }
    }
}

void TagConfigDialog::on_btnDelDevice_clicked()
{
    if (m_currentDeviceId.isEmpty()) return;
    if (QMessageBox::question(this, "确认", "确定删除该设备及其所有标签？") == QMessageBox::Yes) {
        m_plcData->removeDevice(m_currentDeviceId);
        loadDevices();
    }
}

void TagConfigDialog::on_btnAddGroup_clicked()
{
    // 这里简化处理，实际应弹出输入框并更新 m_plcData
    QMessageBox::information(this, "功能", "创建组功能需结合具体业务逻辑实现");
}

void TagConfigDialog::on_btnAddTag_clicked()
{
    // 弹出编辑对话框
    bool ok;
    QString name = QInputDialog::getText(this, "添加标签", "标签名称:", QLineEdit::Normal, "", &ok);
    if (ok && !name.isEmpty()) {
        // 这里应该弹出一个详细的属性对话框，而不是简单的输入框
        TagInfo info;
        info.name = name;
        info.address = "DB1.DBD0"; // 默认地址

        // 获取当前设备
        auto device = m_plcData->getDevice(m_currentDeviceId);
        // 找到根组，添加标签
        if (!device.rootGroups.isEmpty()) {
            device.rootGroups[0].tags.append(info);
        } else {
            DeviceConfig::TagGroup group("默认组");
            group.tags.append(info);
            device.rootGroups.append(group);
        }

        m_plcData->updateDevice(device);
        loadTagsForDevice(m_currentDeviceId);
    }
}

void TagConfigDialog::on_btnEditTag_clicked()
{
    auto index = ui->tagTreeView->currentIndex();
    if (!index.isValid()) return;

    auto item = m_tagTreeModel->itemFromIndex(index);
    if (item->data(Qt::UserRole).toString() == "tag") {
        QString oldName = item->data(Qt::UserRole + 1).toString();
        bool ok;
        QString newName = QInputDialog::getText(this, "编辑标签", "新名称:", QLineEdit::Normal, oldName, &ok);
        if (ok && !newName.isEmpty()) {
            // 更新逻辑...
            // 1. 获取设备
            auto device = m_plcData->getDevice(m_currentDeviceId);
            // 2. 遍历树找到旧标签并更新
            // ... (遍历逻辑)
            // 3. 调用 m_plcData->updateDevice(device);

            loadTagsForDevice(m_currentDeviceId); // 简单刷新
        }
    }
}

void TagConfigDialog::on_btnDelTag_clicked()
{
    auto index = ui->tagTreeView->currentIndex();
    if (!index.isValid()) return;

    auto item = m_tagTreeModel->itemFromIndex(index);
    if (item->data(Qt::UserRole).toString() == "tag") {
        QString tagName = item->data(Qt::UserRole + 1).toString();
        if (QMessageBox::question(this, "确认", "确定删除标签?") == QMessageBox::Yes) {
            // 删除逻辑...
            auto device = m_plcData->getDevice(m_currentDeviceId);
            // ... 遍历移除 tagName
            m_plcData->updateDevice(device);
            loadTagsForDevice(m_currentDeviceId);
        }
    }
}

void TagConfigDialog::on_btnOk_clicked()
{
    // 保存所有配置
    // 这里可以触发 m_plcData 保存到文件，或者只是缓存状态
    accept();
}

void TagConfigDialog::on_btnCancel_clicked()
{
    reject();
}