// src/core/plcdata.cpp
#include "plcdata.h"
#include "modbushelper.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>

//解析函数
static bool parseS7Address(const QString &address, int &dbNumber, int &byteOffset, int &bitOffset, TagDataType &dataType)
{
    QRegularExpression regex(R"(DB(\d+)\.DB([XBWD])(\d+)(?:\.(\d+))?)");
    QRegularExpressionMatch match = regex.match(address);
    if (!match.hasMatch()) return false;
    dbNumber = match.captured(1).toInt();
    QString type = match.captured(2);
    byteOffset = match.captured(3).toInt();
    if (type == "X") {
        dataType = TagDataType::BOOL;
        bitOffset = (match.lastCapturedIndex() >= 4) ? match.captured(4).toInt() : -1;
    } else if (type == "B") {
        dataType = TagDataType::BYTE;
        bitOffset = -1;
    } else if (type == "W") {
        dataType = TagDataType::WORD;
        bitOffset = -1;
    } else if (type == "D") {
        dataType = TagDataType::DWORD;
        bitOffset = -1;
    } else {
        return false;
    }
    return true;
}

// ---------- 辅助函数：将 TagDataType 转为 JSON 字符串 ----------
static QString dataTypeToJsonString(TagDataType type) {
    switch (type) {
    case TagDataType::BOOL:   return "BOOL";
    case TagDataType::BYTE:   return "BYTE";
    case TagDataType::WORD:   return "WORD";
    case TagDataType::DWORD:  return "DWORD";
    case TagDataType::INT:    return "INT";
    case TagDataType::DINT:   return "DINT";
    case TagDataType::REAL:   return "REAL";
    case TagDataType::STRING: return "STRING";
    default: return "UNKNOWN";
    }
}

static TagDataType jsonStringToDataType(const QString &str) {
    return stringToTagDataType(str);
}

// ---------- 递归序列化 TagGroup ----------
static QJsonObject serializeTagGroup(const TagGroup *group) {
    QJsonObject obj;
    obj["name"] = group->groupName;
    QJsonArray tagsArray;
    for (const TagInfo &tag : group->tags) {
        QJsonObject tagObj;
        tagObj["name"] = tag.name;
        tagObj["address"] = tag.address;
        tagObj["dataType"] = dataTypeToJsonString(tag.dataType);
        tagObj["writable"] = tag.writable;
        tagObj["dbEnabled"] = tag.dbEnabled;
        if (!tag.description.isEmpty())
            tagObj["description"] = tag.description;
        if (tag.scalingFactor != 1.0)
            tagObj["scalingFactor"] = tag.scalingFactor;
        if (tag.offset != 0.0)
            tagObj["offset"] = tag.offset;
        if (!tag.unit.isEmpty())
            tagObj["unit"] = tag.unit;

        // 在 tagsArray 循环内，tagObj 后添加：
        QJsonObject alertObj;
        alertObj["minEnabled"] = tag.alertConfig.minEnabled;
        alertObj["minValue"] = tag.alertConfig.minValue;
        alertObj["maxEnabled"] = tag.alertConfig.maxEnabled;
        alertObj["maxValue"] = tag.alertConfig.maxValue;
        alertObj["level"] = static_cast<int>(tag.alertConfig.level);
        if (!tag.alertConfig.description.isEmpty())
            alertObj["description"] = tag.alertConfig.description;
        tagObj["alertConfig"] = alertObj;

        tagsArray.append(tagObj);
    }
    obj["tags"] = tagsArray;
    QJsonArray subGroupsArray;
    for (const TagGroup *sub : group->subGroups) {
        subGroupsArray.append(serializeTagGroup(sub));
    }
    obj["subGroups"] = subGroupsArray;
    return obj;
}

static TagGroup* deserializeTagGroup(const QJsonObject &obj) {
    TagGroup *group = new TagGroup(obj["name"].toString());
    QJsonArray tagsArray = obj["tags"].toArray();
    for (const QJsonValue &v : tagsArray) {
        QJsonObject tagObj = v.toObject();
        TagInfo tag;
        tag.name = tagObj["name"].toString();
        tag.address = tagObj["address"].toString();

        // 尝试解析地址（S7 格式 或 Modbus 格式）
        int db, byteOff, bitOff;
        TagDataType dt;
        int modbusFc, modbusRegAddr, modbusCount;
        bool s7Parsed   = parseS7Address(tag.address, db, byteOff, bitOff, dt);
        bool modbusParsed = ModbusHelper::parseAddress(tag.address, modbusFc, modbusRegAddr, modbusCount);

        if (s7Parsed) {
            tag.dbNumber = db;
            tag.byteOffset = byteOff;
            tag.bitOffset = bitOff;
            tag.dataType = dt;
        } else if (modbusParsed) {
            tag.dbNumber = 0;           // Modbus 不使用 DB
            tag.byteOffset = modbusRegAddr;
            tag.bitOffset = -1;
            // dataType 从 JSON 的 "dataType" 字段读取（下方已有逻辑）
        } else {
            qWarning() << "Failed to parse address:" << tag.address;
        }
        // 从 JSON 中读取其他属性（可覆盖自动解析的类型）
        if (tagObj.contains("dataType")) {
            tag.dataType = jsonStringToDataType(tagObj["dataType"].toString());
        }
        tag.writable = tagObj["writable"].toBool(false);
        tag.dbEnabled = tagObj["dbEnabled"].toBool(true);
        tag.description = tagObj["description"].toString();
        tag.scalingFactor = tagObj["scalingFactor"].toDouble(1.0);
        tag.offset = tagObj["offset"].toDouble(0.0);
        tag.unit = tagObj["unit"].toString();

        // 报警数据：
        if (tagObj.contains("alertConfig") && tagObj["alertConfig"].isObject()) {
            QJsonObject alertObj = tagObj["alertConfig"].toObject();
            tag.alertConfig.minEnabled = alertObj["minEnabled"].toBool(false);
            tag.alertConfig.minValue = alertObj["minValue"].toDouble(0.0);
            tag.alertConfig.maxEnabled = alertObj["maxEnabled"].toBool(false);
            tag.alertConfig.maxValue = alertObj["maxValue"].toDouble(0.0);
            tag.alertConfig.level = static_cast<AlertLevel>(alertObj["level"].toInt(0));
            tag.alertConfig.description = alertObj["description"].toString();
        }

        group->tags.append(tag);
    }
    QJsonArray subArray = obj["subGroups"].toArray();
    for (const QJsonValue &v : subArray) {
        group->subGroups.append(deserializeTagGroup(v.toObject()));
    }
    return group;
}

// ---------- PLCData 实现 ----------
PLCData::PLCData(QObject *parent) : QObject(parent)
{
}

PLCData::~PLCData()
{
    QMutexLocker locker(&m_mutex);
    qDeleteAll(m_devices);
}

void PLCData::addDevice(DeviceConfig *device)
{
    if (!device || device->deviceId.isEmpty()) return;
    {
        QMutexLocker locker(&m_mutex);
        for (DeviceConfig *d : m_devices) {
            if (d->deviceId == device->deviceId) {
                qWarning() << "Device already exists:" << device->deviceId;
                return;
            }
        }
        m_devices.append(device);
        // 重建索引（包含新设备）
    }
    rebuildIndex();
    emit deviceAdded(device->deviceId);
    emit dataChanged();
}

void PLCData::removeDevice(const QString &deviceId)
{
    DeviceConfig *removed = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        for (int i = 0; i < m_devices.size(); ++i) {
            if (m_devices[i]->deviceId == deviceId) {
                removed = m_devices.takeAt(i);
                break;
            }
        }
    }
    if (removed) {
        // 从值缓存中删除该设备的所有标签
        QString prefix = deviceId + "/";
        QMutableHashIterator<QString, QVariant> itVal(m_tagValues);
        while (itVal.hasNext()) {
            itVal.next();
            if (itVal.key().startsWith(prefix))
                itVal.remove();
        }
        // 从视图选择中删除
        for (auto it = m_viewSelections.begin(); it != m_viewSelections.end(); ++it) {
            QSet<QString> &set = it.value();
            QMutableSetIterator<QString> setIt(set);
            while (setIt.hasNext()) {
                if (setIt.next().startsWith(prefix))
                    setIt.remove();
            }
        }
        rebuildIndex(); // 重建索引（会自动排除已删除设备）
        delete removed;
        emit deviceRemoved(deviceId);
        emit dataChanged();
    }
}

DeviceConfig* PLCData::getDevice(const QString &deviceId) const
{
    QMutexLocker locker(&m_mutex);
    for (DeviceConfig *d : m_devices) {
        if (d->deviceId == deviceId) return d;
    }
    return nullptr;
}

QVector<DeviceConfig*> PLCData::getAllDevices() const
{
    QMutexLocker locker(&m_mutex);
    return m_devices;
}

QString PLCData::makeTagKey(const QString &deviceId, const QString &tagName) const
{
    return deviceId + "/" + tagName;
}

TagInfo* PLCData::findTag(const QString &deviceId, const QString &tagName)
{
    QString key = makeTagKey(deviceId, tagName);
    QMutexLocker locker(&m_mutex);
    return m_tagIndex.value(key, nullptr);
}

const TagInfo* PLCData::findTag(const QString &deviceId, const QString &tagName) const
{
    QString key = makeTagKey(deviceId, tagName);
    QMutexLocker locker(&m_mutex);
    return m_tagIndex.value(key, nullptr);
}

void PLCData::updateTagValue(const QString &deviceId, const QString &tagName, const QVariant &value)
{
    QString key = makeTagKey(deviceId, tagName);
    QVariant oldValue;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_tagIndex.contains(key)) {
            qWarning() << "Tag not found:" << key;
            return;
        }
        oldValue = m_tagValues.value(key);
        if (oldValue == value) return;
        m_tagValues[key] = value;
        // 同时更新 TagInfo 中的 value 字段（可选，保持一致性）
        TagInfo *tag = m_tagIndex[key];
        if (tag) tag->value = value;
    }
    emit tagValueChanged(deviceId, tagName, value);
}

QVariant PLCData::getTagValue(const QString &deviceId, const QString &tagName) const
{
    QString key = makeTagKey(deviceId, tagName);
    QMutexLocker locker(&m_mutex);
    return m_tagValues.value(key);
}

void PLCData::setTagDbEnabled(const QString &deviceId, const QString &tagName, bool enabled)
{
    QString key = makeTagKey(deviceId, tagName);
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        TagInfo *tag = m_tagIndex.value(key, nullptr);
        if (tag && tag->dbEnabled != enabled) {
            tag->dbEnabled = enabled;
            changed = true;
        }
    }
    if (changed) {
        emit tagDbEnabledChanged(deviceId, tagName, enabled);
    }
}

bool PLCData::isTagDbEnabled(const QString &deviceId, const QString &tagName) const
{
    QString key = makeTagKey(deviceId, tagName);
    QMutexLocker locker(&m_mutex);
    const TagInfo *tag = m_tagIndex.value(key, nullptr);
    return tag ? tag->dbEnabled : false;
}

void PLCData::setTagSelectedInView(ViewType view, const QString &tagKey, bool selected)
{
    QMutexLocker locker(&m_mutex);
    QSet<QString> &set = m_viewSelections[view];
    if (selected)
        set.insert(tagKey);
    else
        set.remove(tagKey);
    // 发射信号（锁外）
    locker.unlock();
    emit viewSelectionChanged(view, getSelectedTagKeysForView(view));
}

void PLCData::setViewSelection(ViewType view, const QStringList &tagKeys)
{
    clearViewSelection(view);
    for (const QString &key : tagKeys) {
        setTagSelectedInView(view, key, true);
    }
}

bool PLCData::isTagSelectedInView(ViewType view, const QString &tagKey) const
{
    QMutexLocker locker(&m_mutex);
    return m_viewSelections.value(view).contains(tagKey);
}

QStringList PLCData::getSelectedTagKeysForView(ViewType view) const
{
    QMutexLocker locker(&m_mutex);
    QSet<QString> set = m_viewSelections.value(view);
    return QStringList(set.begin(), set.end());
}

void PLCData::clearViewSelection(ViewType view)
{
    {
        QMutexLocker locker(&m_mutex);
        m_viewSelections.remove(view);
    }
    emit viewSelectionChanged(view, QStringList());
}

void PLCData::rebuildIndex()
{
    QMutexLocker locker(&m_mutex);
    m_tagIndex.clear();
    // 递归收集所有标签指针
    for (DeviceConfig *dev : m_devices) {
        for (TagGroup *group : dev->rootGroups) {
            // 递归遍历分组（使用栈或递归）
            std::function<void(TagGroup*)> collect = [&](TagGroup *g) {
                for (TagInfo &tag : g->tags) {
                    QString key = makeTagKey(dev->deviceId, tag.name);
                    m_tagIndex[key] = &tag;
                }
                for (TagGroup *sub : g->subGroups) {
                    collect(sub);
                }
            };
            collect(group);
        }
    }
    // 注意：m_tagValues 中的陈旧条目不会自动删除，但可以通过 updateTagValue 覆盖
}

void PLCData::beginStructureChange()
{
    // 可暂不实现，直接调用 rebuildIndex 即可
}

void PLCData::endStructureChange()
{
    rebuildIndex();
    emit dataChanged();
}

bool PLCData::loadFromJson(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCritical() << "Cannot open config file:" << filePath;
        return false;
    }
    QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        qCritical() << "JSON parse error:" << err.errorString();
        return false;
    }
    if (!doc.isObject()) {
        qCritical() << "Config file is not a JSON object";
        return false;
    }
    QJsonObject root = doc.object();

    // 清空现有数据
    {
        QMutexLocker locker(&m_mutex);
        qDeleteAll(m_devices);
        m_devices.clear();
        m_tagIndex.clear();
        m_tagValues.clear();
        m_viewSelections.clear();
    }

    // 解析 devices 数组
    if (root.contains("devices") && root["devices"].isArray()) {
        QJsonArray devArray = root["devices"].toArray();
        for (const QJsonValue &devVal : devArray) {
            QJsonObject devObj = devVal.toObject();
            DeviceConfig *dev = new DeviceConfig();
            dev->deviceId = devObj["id"].toString();
            if (dev->deviceId.isEmpty()) continue;
            dev->deviceName = devObj["name"].toString();
            dev->ip = devObj["ip"].toString();
            dev->rack = devObj["rack"].toInt();
            dev->slot = devObj["slot"].toInt();
            dev->port = devObj["port"].toInt();
            dev->pollingInterval = devObj["pollingInterval"].toInt(1000);

            // ---- 读取 Modbus 相关字段（向后兼容：缺省时 protocol="S7"） ----
            dev->protocol = devObj["protocol"].toString();
            if (dev->protocol.isEmpty()) dev->protocol = "S7";
            dev->modbusSlaveId = devObj["modbusSlaveId"].toInt(1);
            dev->serialPort = devObj["serialPort"].toString();
            dev->baudRate = devObj["baudRate"].toInt(9600);
            dev->dataBits = devObj["dataBits"].toInt(8);
            dev->stopBits = devObj["stopBits"].toInt(1);
            dev->parity = devObj["parity"].toString("N");

            // 解析 tagGroups
            if (devObj.contains("tagGroups") && devObj["tagGroups"].isArray()) {
                QJsonArray groupsArray = devObj["tagGroups"].toArray();
                for (const QJsonValue &grpVal : groupsArray) {
                    TagGroup *group = deserializeTagGroup(grpVal.toObject());
                    if (group) dev->rootGroups.append(group);
                }
            }
            {
                QMutexLocker locker(&m_mutex);
                m_devices.append(dev);
            }
        }
    }

    // 解析视图选择
    if (root.contains("viewSelections") && root["viewSelections"].isObject()) {
        QJsonObject selObj = root["viewSelections"].toObject();
        QMutexLocker locker(&m_mutex);
        for (auto it = selObj.begin(); it != selObj.end(); ++it) {
            ViewType view = static_cast<ViewType>(it.key().toInt());
            QJsonArray arr = it.value().toArray();
            QSet<QString> set;
            for (const QJsonValue &v : arr) {
                set.insert(v.toString());
            }
            m_viewSelections[view] = set;
        }
    }

    rebuildIndex();
    emit dataChanged();
    return true;
}

bool PLCData::saveToJson(const QString &filePath) const
{
    QJsonObject root;

    // 序列化 devices
    QJsonArray devicesArray;
    for (DeviceConfig *dev : m_devices) {
        QJsonObject devObj;
        devObj["id"] = dev->deviceId;
        devObj["name"] = dev->deviceName;
        devObj["ip"] = dev->ip;
        devObj["rack"] = dev->rack;
        devObj["slot"] = dev->slot;
        devObj["port"] = dev->port;
        devObj["pollingInterval"] = dev->pollingInterval;

        // ---- 写入 Modbus 相关字段 ----
        devObj["protocol"] = dev->protocol;
        devObj["modbusSlaveId"] = dev->modbusSlaveId;
        if (!dev->serialPort.isEmpty()) devObj["serialPort"] = dev->serialPort;
        devObj["baudRate"] = dev->baudRate;
        devObj["dataBits"] = dev->dataBits;
        devObj["stopBits"] = dev->stopBits;
        devObj["parity"] = dev->parity;

        QJsonArray groupsArray;
        for (TagGroup *group : dev->rootGroups) {
            groupsArray.append(serializeTagGroup(group));
        }
        devObj["tagGroups"] = groupsArray;
        devicesArray.append(devObj);
    }
    root["devices"] = devicesArray;

    // 序列化视图选择
    QJsonObject selObj;
    for (auto it = m_viewSelections.begin(); it != m_viewSelections.end(); ++it) {
        QString key = QString::number(static_cast<int>(it.key()));
        QJsonArray arr;
        for (const QString &tagKey : it.value()) {
            arr.append(tagKey);
        }
        selObj[key] = arr;
    }
    root["viewSelections"] = selObj;

    QJsonDocument doc(root);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCritical() << "Cannot write config file:" << filePath;
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QStringList PLCData::getAllTagNames() const
{
    QMutexLocker locker(&m_mutex);
    return m_tagIndex.keys();
}

bool PLCData::isDbEnabled(const QString &tagKey) const
{
    QMutexLocker locker(&m_mutex);
    TagInfo* tag = m_tagIndex.value(tagKey, nullptr);
    return tag ? tag->dbEnabled : false;
}

QStringList PLCData::getEnabledTagKeys() const
{
    QStringList keys;
    for (DeviceConfig* dev : m_devices) {
        std::function<void(TagGroup*)> collect = [&](TagGroup* group) {
            for (const TagInfo& tag : group->tags) {
                if (tag.dbEnabled) {
                    keys.append(makeTagKey(dev->deviceId, tag.name));
                }
            }
            for (TagGroup* sub : group->subGroups) {
                collect(sub);
            }
        };
        for (TagGroup* root : dev->rootGroups) {
            collect(root);
        }
    }
    return keys;
}