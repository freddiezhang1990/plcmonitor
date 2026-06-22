#include "historyquerydialog.h"
#include "plcdata.h"
#include <QVBoxLayout>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QLabel>

HistoryQueryDialog::HistoryQueryDialog(PLCData* plcData, const QStringList& preselectedTags, QWidget *parent)
    : QDialog(parent)
    , m_plcData(plcData)
{
    setWindowTitle("选择趋势标签");
    setModal(true);
    setupUI();
    populateTagList(preselectedTags);
}

HistoryQueryDialog::~HistoryQueryDialog() {}

void HistoryQueryDialog::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("选择要显示的标签（可多选）:"));

    m_tagList = new QListWidget;
    m_tagList->setSelectionMode(QAbstractItemView::MultiSelection);
    layout->addWidget(m_tagList);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    resize(450, 500);
}

void HistoryQueryDialog::populateTagList(const QStringList& preselectedTags)
{
    if (!m_plcData) return;
    m_tagList->clear();

    // 获取所有 tagKey（格式 "deviceId/tagName"）
    QStringList allTagKeys = m_plcData->getAllTagNames();
    for (const QString& tagKey : allTagKeys) {
        if (!m_plcData->isDbEnabled(tagKey))
            continue;

        // 解析设备ID和标签名
        QString deviceId, tagName;
        int slashPos = tagKey.indexOf('/');
        if (slashPos != -1) {
            deviceId = tagKey.left(slashPos);
            tagName = tagKey.mid(slashPos + 1);
        } else {
            // 兼容旧格式（理论上不会出现）
            tagName = tagKey;
        }

        // 获取设备名称用于显示
        QString displayText;
        DeviceConfig* dev = m_plcData->getDevice(deviceId);
        if (dev && !dev->deviceName.isEmpty()) {
            displayText = QString("[%1] %2").arg(dev->deviceName, tagName);
        } else if (dev) {
            displayText = QString("[%1] %2").arg(deviceId, tagName);
        } else {
            displayText = tagKey; // 降级显示
        }

        QListWidgetItem* item = new QListWidgetItem(displayText);
        item->setData(Qt::UserRole, tagKey); // 存储完整键
        m_tagList->addItem(item);

        if (preselectedTags.contains(tagKey)) {
            item->setSelected(true);
        }
    }
}

QStringList HistoryQueryDialog::getSelectedTags() const
{
    QStringList selected;
    for (QListWidgetItem* item : m_tagList->selectedItems()) {
        QString tagKey = item->data(Qt::UserRole).toString();
        if (!tagKey.isEmpty()) {
            selected << tagKey;
        } else {
            // 降级：使用显示文本（不推荐，但保留兼容）
            selected << item->text();
        }
    }
    return selected;
}