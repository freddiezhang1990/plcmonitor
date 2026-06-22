#include "deviceconfigdialog.h"
#include "devicetreemodel.h"
#include "ui_DeviceConfigDialog.h"
#include <QMessageBox>
#include <QInputDialog>
#include <QDebug>

DeviceConfigDialog::DeviceConfigDialog(PLCData *plcData, QWidget *parent)
    : QDialog(parent), m_plcData(plcData), ui(new Ui::DeviceConfigDialog)
{
    ui->setupUi(this);

    ui->applyBtn->setVisible(false);

    // 初始化树模型
    m_treeModel = new DeviceTreeModel(m_plcData, this);
    ui->treeView->setModel(m_treeModel);
    ui->treeView->expandAll();
    ui->treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->treeView->setSelectionMode(QAbstractItemView::SingleSelection);

    ui->viewCombo->setItemData(0, static_cast<int>(ViewType::TABLE_VIEW));
    ui->viewCombo->setItemData(1, static_cast<int>(ViewType::CHART_VIEW));
    ui->viewCombo->setItemData(2, static_cast<int>(ViewType::PROCESS_VIEW));
    ui->viewCombo->setItemData(3, static_cast<int>(ViewType::TREND_VIEW));

    // 报警阈值范围设置（假设为双精度浮点数）
    if (auto *minSpin = qobject_cast<QDoubleSpinBox*>(ui->spinMinValue)) {
        minSpin->setRange(-1e6, 1e6);
        minSpin->setDecimals(2);
    } else {
        // 如果是 QSpinBox，则设置整数范围
        ui->spinMinValue->setRange(-1000000, 1000000);
    }
    if (auto *maxSpin = qobject_cast<QDoubleSpinBox*>(ui->spinMaxValue)) {
        maxSpin->setRange(-1e6, 1e6);
        maxSpin->setDecimals(2);
    } else {
        ui->spinMinValue->setRange(-1000000, 1000000);
    }

    // 报警级别下拉框存储用户数据
    ui->m_comboAlarmLevel->setItemData(0, static_cast<int>(AlertLevel::INFO));
    ui->m_comboAlarmLevel->setItemData(1, static_cast<int>(AlertLevel::WARNING));
    ui->m_comboAlarmLevel->setItemData(2, static_cast<int>(AlertLevel::ERRORAlert));
    ui->m_comboAlarmLevel->setItemData(3, static_cast<int>(AlertLevel::CRITICAL));

    // 连接信号
    connect(ui->viewCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DeviceConfigDialog::onViewChanged);
    connect(ui->applyBtn, &QPushButton::clicked, this, &DeviceConfigDialog::onApply);
    connect(ui->saveBtn, &QPushButton::clicked, this, &DeviceConfigDialog::onSave);
    connect(ui->closeBtn, &QPushButton::clicked, this, &DeviceConfigDialog::onClose);
    connect(ui->addDeviceBtn, &QPushButton::clicked, this, &DeviceConfigDialog::onAddDevice);
    connect(ui->editDeviceBtn, &QPushButton::clicked, this, &DeviceConfigDialog::onEditDevice);
    connect(ui->deleteDeviceBtn, &QPushButton::clicked, this, &DeviceConfigDialog::onDeleteDevice);
    connect(ui->addGroupBtn, &QPushButton::clicked, this, &DeviceConfigDialog::onAddGroup);
    connect(ui->addTagBtn, &QPushButton::clicked, this, &DeviceConfigDialog::onAddTag);
    connect(ui->editTagBtn, &QPushButton::clicked, this, &DeviceConfigDialog::onEditTag);
    connect(ui->deleteTagBtn, &QPushButton::clicked, this, &DeviceConfigDialog::onDeleteTag);
    connect(ui->treeView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &DeviceConfigDialog::onTreeSelectionChanged);

    // 当模型被重置（如添加/删除设备、分组等）时，自动重新展开
    connect(m_treeModel, &QAbstractItemModel::modelReset, this, [this]() {
        ui->treeView->expandAll();
    });

    // 初始加载视图选择
    loadSelectionsForCurrentView();
    onViewChanged(0);

    // 初始时属性页控件为只读状态
    setTagPropertiesEditable(false);
    ui->editTagBtn->setText("编辑标签");
}

DeviceConfigDialog::~DeviceConfigDialog()
{
    delete ui;
}

void DeviceConfigDialog::setCurrentView(ViewType view)
{
    int idx = ui->viewCombo->findData(static_cast<int>(view));
    if (idx >= 0) ui->viewCombo->setCurrentIndex(idx);
}

void DeviceConfigDialog::onViewChanged(int index)
{
    Q_UNUSED(index);
    int idx = ui->viewCombo->currentIndex();
    ViewType view = static_cast<ViewType>(idx);
    m_treeModel->setCurrentView(view);           // 仅设置当前视图
    m_treeModel->loadCurrentViewSelections();    // 加载该视图的选择到树中
}

void DeviceConfigDialog::loadSelectionsForCurrentView()
{
    int idx = ui->viewCombo->currentIndex();
    ViewType view = static_cast<ViewType>(idx);   // 直接使用索引
    m_treeModel->loadSelectionsFromView(view);
}

void DeviceConfigDialog::onApply()
{
    // 确保模型内部的当前视图与下拉框同步
   // int idx = ui->viewCombo->currentIndex();
  //  ViewType view = static_cast<ViewType>(idx);
  //  m_treeModel->setCurrentView(view);   // 先同步当前视图
    m_treeModel->applySelectionsToView(); // 无参调用
    QMessageBox::information(this, "应用", QString("已将选择应用到“%1”视图").arg(ui->viewCombo->currentText()));
}

void DeviceConfigDialog::onSave()  // 实际作为“确定”按钮
{
    // 如果处于编辑模式，先应用当前标签的修改
    if (m_tagEditMode && m_currentTag) {
        // 保存当前编辑的标签修改
        m_currentTag->name = ui->tagNameEdit->text();
        m_currentTag->address = ui->addressEdit->text();
        m_currentTag->dataType = stringToTagDataType(ui->dataTypeCombo->currentText());
        m_currentTag->writable = ui->writableCheckBox->isChecked();
        m_currentTag->dbEnabled = ui->dbEnabledCheckBox->isChecked();
        m_currentTag->description = ui->descriptionEdit->text();
        m_currentTag->scalingFactor = ui->scalingSpinBox->value();
        m_currentTag->offset = ui->offsetSpinBox->value();
        m_currentTag->unit = ui->unitEdit->text();
        // 报警配置
        m_currentTag->alertConfig.minEnabled = ui->chkMinAlarm->isChecked();
        m_currentTag->alertConfig.minValue = ui->spinMinValue->value();
        m_currentTag->alertConfig.maxEnabled = ui->chkMaxAlarm->isChecked();
        m_currentTag->alertConfig.maxValue = ui->spinMaxValue->value();
        m_currentTag->alertConfig.level = static_cast<AlertLevel>(
            ui->m_comboAlarmLevel->currentData().toInt());
        // 刷新树模型
        m_treeModel->rebuildModel();
        // 退出编辑模式
        setTagPropertiesEditable(false);
        ui->editTagBtn->setText("编辑标签");
        m_tagEditMode = false;
        m_currentTagIndex = QPersistentModelIndex();
    }

    // 保存设备属性页中显示的设备配置（如果当前显示了设备属性）
    if (ui->propertyStack->currentWidget() == ui->propertyStack->widget(1)) {
        QModelIndex idx = ui->treeView->currentIndex();
        if (idx.isValid()) {
            auto *node = static_cast<DeviceTreeModel::TreeNode*>(idx.internalPointer());
            if (node->type == DeviceTreeModel::Device) {
                DeviceConfig *dev = m_plcData->getDevice(node->deviceId);
                if (dev) {
                    dev->ip = ui->ipEdit->text();
                    dev->rack = ui->rackSpinBox->value();
                    dev->slot = ui->slotSpinBox->value();
                    dev->port = ui->portSpinBox->value();
                    dev->pollingInterval = ui->pollingIntervalSpinBox->value();
                    // 协议相关字段
                    switch (ui->protocolCombo->currentIndex()) {
                        case 1:  dev->protocol = "ModbusTCP"; break;
                        case 2:  dev->protocol = "ModbusRTU"; break;
                        default: dev->protocol = "S7"; break;
                    }
                    dev->modbusSlaveId = ui->slaveIdSpinBox->value();
                }
            }
        }
    }

    if (m_plcData->saveToJson("configs/devices.json")) {
        emit tagDatabaseEnabledChanged();  // 通知外部同步
        accept();  // 关闭对话框并返回 QDialog::Accepted
    } else {
        QMessageBox::warning(this, "失败", "保存配置失败");
    }
}

void DeviceConfigDialog::onClose()
{
    reject();
}

void DeviceConfigDialog::onAddDevice()
{
    // 弹出简单输入框获取设备ID和名称
    bool ok;
    QString devId = QInputDialog::getText(this, "添加设备", "设备ID:", QLineEdit::Normal, "", &ok);
    if (!ok || devId.isEmpty()) return;
    QString devName = QInputDialog::getText(this, "添加设备", "设备名称:", QLineEdit::Normal, devId, &ok);
    if (!ok) return;
    auto *dev = new DeviceConfig();
    dev->deviceId = devId;
    dev->deviceName = devName;
    // 默认连接参数
    dev->protocol = "S7";       // 默认 S7 协议
    dev->ip = "192.168.0.1";
    dev->rack = 0;
    dev->slot = 1;
    dev->port = 102;
    dev->pollingInterval = 1000;
    m_treeModel->addDevice(dev);
}

void DeviceConfigDialog::onEditDevice()
{
    QModelIndex idx = ui->treeView->currentIndex();
    if (!idx.isValid()) return;
    auto *node = static_cast<DeviceTreeModel::TreeNode*>(idx.internalPointer());
    if (node->type != DeviceTreeModel::Device) return;
    DeviceConfig *dev = m_plcData->getDevice(node->deviceId);
    if (!dev) return;
    // 弹出编辑对话框
    bool ok;
    QString newName = QInputDialog::getText(this, "编辑设备", "设备名称:", QLineEdit::Normal, dev->deviceName, &ok);
    if (ok && !newName.isEmpty()) {
        dev->deviceName = newName;
        m_treeModel->rebuildModel();
    }
}

void DeviceConfigDialog::onDeleteDevice()
{
    QModelIndex idx = ui->treeView->currentIndex();
    if (!idx.isValid()) return;
    auto *node = static_cast<DeviceTreeModel::TreeNode*>(idx.internalPointer());
    if (node->type != DeviceTreeModel::Device) return;
    if (QMessageBox::question(this, "确认删除", QString("确定要删除设备 %1 吗？").arg(node->deviceId))
        == QMessageBox::Yes) {
        m_treeModel->removeNode(node);
    }
}

void DeviceConfigDialog::onAddGroup()
{
    QModelIndex idx = ui->treeView->currentIndex();
    if (!idx.isValid()) return;
    auto *node = static_cast<DeviceTreeModel::TreeNode*>(idx.internalPointer());
    if (node->type != DeviceTreeModel::Device && node->type != DeviceTreeModel::Group) return;
    bool ok;
    QString groupName = QInputDialog::getText(this, "添加分组", "分组名称:", QLineEdit::Normal, "", &ok);
    if (ok && !groupName.isEmpty()) {
        m_treeModel->addGroup(node, groupName);
    }
}

void DeviceConfigDialog::onAddTag()
{
    QModelIndex idx = ui->treeView->currentIndex();
    if (!idx.isValid()) return;
    auto *node = static_cast<DeviceTreeModel::TreeNode*>(idx.internalPointer());
    if (node->type != DeviceTreeModel::Group) return;
    bool ok;
    QString tagName = QInputDialog::getText(this, "添加标签", "标签名:", QLineEdit::Normal, "", &ok);
    if (!ok || tagName.isEmpty()) return;
    QString address = QInputDialog::getText(this, "添加标签", "地址:", QLineEdit::Normal, "", &ok);
    if (!ok) return;
    TagInfo tag;
    tag.name = tagName;
    tag.address = address;
    tag.dataType = TagDataType::REAL; // 默认
    tag.writable = true;
    tag.dbEnabled = true;
    m_treeModel->addTag(node, tag);
}

void DeviceConfigDialog::onEditTag()
{
    QModelIndex idx = ui->treeView->currentIndex();
    if (!idx.isValid()) {
        QMessageBox::warning(this, "提示", "请先选择一个标签");
        return;
    }
    auto *node = static_cast<DeviceTreeModel::TreeNode*>(idx.internalPointer());
    if (node->type != DeviceTreeModel::Tag) {
        QMessageBox::warning(this, "提示", "请选择一个标签节点");
        return;
    }

    if (!m_tagEditMode) {
        // 进入编辑模式
        setTagPropertiesEditable(true);
        ui->editTagBtn->setText("保存修改");
        m_tagEditMode = true;
        m_currentTagIndex = idx;  // 记录正在编辑的标签索引
    } else {
        // 保存修改
        if (m_currentTag) {
            // 从界面读取新值
            m_currentTag->name = ui->tagNameEdit->text();
            m_currentTag->address = ui->addressEdit->text();
            m_currentTag->dataType = stringToTagDataType(ui->dataTypeCombo->currentText());
            m_currentTag->writable = ui->writableCheckBox->isChecked();
            m_currentTag->dbEnabled = ui->dbEnabledCheckBox->isChecked();
            m_currentTag->description = ui->descriptionEdit->text();
            m_currentTag->scalingFactor = ui->scalingSpinBox->value();
            m_currentTag->offset = ui->offsetSpinBox->value();
            m_currentTag->unit = ui->unitEdit->text();

            // ========== 新增：保存报警配置 ==========
            m_currentTag->alertConfig.minEnabled = ui->chkMinAlarm->isChecked();
            m_currentTag->alertConfig.minValue = ui->spinMinValue->value();
            m_currentTag->alertConfig.maxEnabled = ui->chkMaxAlarm->isChecked();
            m_currentTag->alertConfig.maxValue = ui->spinMaxValue->value();
            m_currentTag->alertConfig.level = static_cast<AlertLevel>(
                ui->m_comboAlarmLevel->currentData().toInt());

            // 刷新树模型显示（标签名可能变化）
            m_treeModel->rebuildModel();

            QMessageBox::information(this, "成功", "标签属性已更新，请记得保存配置文件");
        }
        // 退出编辑模式
        setTagPropertiesEditable(false);
        ui->editTagBtn->setText("编辑标签");
        m_tagEditMode = false;
        m_currentTagIndex = QPersistentModelIndex();
    }
}

void DeviceConfigDialog::onDeleteTag()
{
    QModelIndex idx = ui->treeView->currentIndex();
    if (!idx.isValid()) return;
    auto *node = static_cast<DeviceTreeModel::TreeNode*>(idx.internalPointer());
    if (node->type != DeviceTreeModel::Tag) return;
    if (QMessageBox::question(this, "确认删除", QString("确定要删除标签 %1 吗？").arg(node->tag->name))
        == QMessageBox::Yes) {
        m_treeModel->removeNode(node);
    }
}

void DeviceConfigDialog::onTreeSelectionChanged(const QModelIndex &current)
{
    if (m_tagEditMode) {
        QMessageBox::warning(this, "提示", "请先保存当前标签的修改（点击「保存修改」按钮）");
        // 恢复之前的选中（需要保存上一个有效索引）
        if (m_currentTagIndex.isValid())
            ui->treeView->setCurrentIndex(m_currentTagIndex);
        return;
    }
    updatePropertyPanel(current);
}

void DeviceConfigDialog::updatePropertyPanel(const QModelIndex &index)
{
    if (!index.isValid()) {
        clearPropertyPanel();
        return;
    }
    auto *node = static_cast<DeviceTreeModel::TreeNode*>(index.internalPointer());
    switch (node->type) {
    case DeviceTreeModel::Device:
        if (auto *dev = m_plcData->getDevice(node->deviceId))
            showDeviceProperties(dev);
        break;
    case DeviceTreeModel::Group:
        if (node->group)
            showGroupProperties(node->group);
        break;
    case DeviceTreeModel::Tag:
        if (node->tag)
            showTagProperties(node->tag);
        break;
    default:
        clearPropertyPanel();
        break;
    }
}

void DeviceConfigDialog::showDeviceProperties(DeviceConfig *dev)
{
    ui->propertyStack->setCurrentWidget(ui->propertyStack->widget(1)); // 设备属性页索引1
    ui->deviceIdEdit->setText(dev->deviceId);
    ui->deviceNameEdit->setText(dev->deviceName);
    ui->ipEdit->setText(dev->ip);
    ui->rackSpinBox->setValue(dev->rack);
    ui->slotSpinBox->setValue(dev->slot);
    ui->portSpinBox->setValue(dev->port);
    ui->pollingIntervalSpinBox->setValue(dev->pollingInterval);

    // ---- 协议相关字段 ----
    if (dev->protocol == "ModbusTCP")
        ui->protocolCombo->setCurrentIndex(1);
    else if (dev->protocol == "ModbusRTU")
        ui->protocolCombo->setCurrentIndex(2);
    else
        ui->protocolCombo->setCurrentIndex(0); // 默认 S7
    ui->slaveIdSpinBox->setValue(dev->modbusSlaveId);
}

void DeviceConfigDialog::showGroupProperties(TagGroup *group)
{
    ui->propertyStack->setCurrentWidget(ui->propertyStack->widget(2)); // 分组属性页索引2
    ui->groupNameEdit->setText(group->groupName);
    ui->groupPathLabel->setText("...");

    ui->groupTagList->clear();
    for (const TagInfo &tag : group->tags) {
        ui->groupTagList->addItem(tag.name + " (" + tag.address + ")");
    }
}

void DeviceConfigDialog::setTagPropertiesEditable(bool editable)
{
    // 定义编辑模式下的背景色
    const QString editBgColor = "#FFF8DC";  // 淡黄色（香槟色），可根据喜好调整
    const QString normalBgColor = "";       // 空字符串表示使用默认背景（或系统主题）

    auto setWidgetStyle = [&](QWidget* widget) {
        if (!widget) return;
        if (editable) {
            widget->setStyleSheet("background-color: " + editBgColor + ";");
        } else {
            widget->setStyleSheet("");  // 恢复默认
        }
    };

    // 设置各个控件的样式
    setWidgetStyle(ui->tagNameEdit);
    setWidgetStyle(ui->addressEdit);
    setWidgetStyle(ui->dataTypeCombo);
    setWidgetStyle(ui->writableCheckBox);
    setWidgetStyle(ui->dbEnabledCheckBox);
    setWidgetStyle(ui->descriptionEdit);
    setWidgetStyle(ui->scalingSpinBox);
    setWidgetStyle(ui->offsetSpinBox);
    setWidgetStyle(ui->unitEdit);
    setWidgetStyle(ui->chkMinAlarm);
    setWidgetStyle(ui->spinMinValue);
    setWidgetStyle(ui->chkMaxAlarm);
    setWidgetStyle(ui->spinMaxValue);
    setWidgetStyle(ui->m_comboAlarmLevel);


    // 如果希望组合框下拉列表也变色，可以设置，但样式表可能影响子控件，简单设置即可
    // 对于复选框，背景色可能不明显，可以设置其他效果，例如加边框
    if (editable) {
        QString checkBoxStyle = "QCheckBox { background-color: " + editBgColor + "; }";
        ui->writableCheckBox->setStyleSheet(checkBoxStyle);
        ui->dbEnabledCheckBox->setStyleSheet(checkBoxStyle);
    } else {
        ui->writableCheckBox->setStyleSheet("");
        ui->dbEnabledCheckBox->setStyleSheet("");
    }

    // 最后启用/禁用控件（确保可操作性）
    ui->tagNameEdit->setEnabled(editable);
    ui->addressEdit->setEnabled(editable);
    ui->dataTypeCombo->setEnabled(editable);
    ui->writableCheckBox->setEnabled(editable);
    ui->dbEnabledCheckBox->setEnabled(editable);
    ui->descriptionEdit->setEnabled(editable);
    ui->scalingSpinBox->setEnabled(editable);
    ui->offsetSpinBox->setEnabled(editable);
    ui->unitEdit->setEnabled(editable);

    // 报警配置区域
    ui->chkMinAlarm->setEnabled(editable);
    ui->spinMinValue->setEnabled(editable);
    ui->chkMaxAlarm->setEnabled(editable);
    ui->spinMaxValue->setEnabled(editable);
    ui->m_comboAlarmLevel->setEnabled(editable);
}

void DeviceConfigDialog::showTagProperties(TagInfo *tag)
{
    m_currentTag = tag;
    ui->propertyStack->setCurrentWidget(ui->propertyStack->widget(3));
    // 填充当前值
    ui->tagNameEdit->setText(tag->name);
    ui->addressEdit->setText(tag->address);
    int idx = ui->dataTypeCombo->findText(tagDataTypeToString(tag->dataType));
    if (idx >= 0) ui->dataTypeCombo->setCurrentIndex(idx);
    ui->writableCheckBox->setChecked(tag->writable);
    ui->dbEnabledCheckBox->setChecked(tag->dbEnabled);
    ui->descriptionEdit->setText(tag->description);
    ui->scalingSpinBox->setValue(tag->scalingFactor);
    ui->offsetSpinBox->setValue(tag->offset);
    ui->unitEdit->setText(tag->unit);

    // 报警配置
    ui->chkMinAlarm->setChecked(tag->alertConfig.minEnabled);
    ui->spinMinValue->setValue(tag->alertConfig.minValue);
    ui->chkMaxAlarm->setChecked(tag->alertConfig.maxEnabled);
    ui->spinMaxValue->setValue(tag->alertConfig.maxValue);
    int levelIdx = ui->m_comboAlarmLevel->findData(static_cast<int>(tag->alertConfig.level));
    if (levelIdx >= 0)
        ui->m_comboAlarmLevel->setCurrentIndex(levelIdx);
    else
        ui->m_comboAlarmLevel->setCurrentIndex(0); // 默认信息

    // 确保处于只读浏览模式
    if (m_tagEditMode) {
        // 如果之前处于编辑模式，强制退出（放弃未保存的修改）
        m_tagEditMode = false;
        ui->editTagBtn->setText("编辑标签");
    }
    setTagPropertiesEditable(false);
}

void DeviceConfigDialog::clearPropertyPanel()
{
    ui->propertyStack->setCurrentIndex(0); // 空白页
}