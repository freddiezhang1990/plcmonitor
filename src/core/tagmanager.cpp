#include "tagmanager.h"
#include "plcdata.h"

TagManager::TagManager(PLCData *plcData, QObject *parent)
    : QObject(parent), m_plcData(plcData)
{
    // 连接 PLCData 的信号，以便转发
    connect(m_plcData, &PLCData::viewSelectionChanged, this, &TagManager::viewSelectionChanged);
}

bool TagManager::addDevice(const DeviceConfig &config)
{
    return m_plcData->addDevice(config);
}

bool TagManager::updateDevice(const DeviceConfig &config)
{
    return m_plcData->updateDevice(config);
}

bool TagManager::removeDevice(const QString &deviceId)
{
    return m_plcData->removeDevice(deviceId);
}

QVariant TagManager::getValue(const QString &deviceId, const QString &tagName) const
{
    auto tags = m_plcData->getTagsByDevice(deviceId);
    for (const auto& tag : tags) {
        if (tag.name == tagName) {
            return tag.value;
        }
    }
    return QVariant();
}

void TagManager::setValue(const QString &deviceId, const QString &tagName, const QVariant &value)
{
    // 实际上，写入值通常通过通信层，这里可以留空或触发写入请求
    // 如果只是更新本地显示，调用：
    // m_plcData->updateTagValue(deviceId, tagName, value);
    // 但通常是由轮询线程读取，所以这里可能不需要实现逻辑
}

void TagManager::setViewSelection(const QString &deviceId, ViewType view, const QStringList &tagNames)
{
    m_plcData->setViewSelection(deviceId, view, tagNames);
}

QStringList TagManager::getViewSelection(const QString &deviceId, ViewType view) const
{
    return m_plcData->getViewSelection(deviceId, view);
}