#include "configloader.h"
#include <QJsonParseError>
#include <QDebug>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <qjsonarray.h>

// 初始化静态单例指针
ConfigLoader* ConfigLoader::m_instance = nullptr;

ConfigLoader::ConfigLoader(const QString &configPath, QObject *parent)
    : QObject(parent), m_configPath(configPath)
{
    // 创建单例引用
    m_instance = this;

    ensureConfigDir();

    // 尝试加载配置文件
    if (!parseFile(m_configPath)) {
        qWarning() << "[CONFIG] Warning: Config file not found or invalid:" << m_configPath;
        // 注意：这里不再初始化默认 Tags，因为 Tags 应该由 UI 或外部 JSON 定义
        // 只初始化基础系统配置
        m_config.insert("app", QJsonObject{
                                   {"name", "PLCMonitor"},
                                   {"version", "1.0.0"}
                               });
        // 不插入 plc 或 tags，留给 UI 或外部写入
    }
}

ConfigLoader::~ConfigLoader()
{
    if (m_instance == this) {
        m_instance = nullptr;
    }
}

void ConfigLoader::ensureConfigDir()
{
    QFileInfo fi(m_configPath);
    QDir().mkpath(fi.absolutePath());
}

bool ConfigLoader::parseFile(const QString &path)
{
    QFile file(path);
    if (!file.exists()) {
        return false;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open config file:" << path;
        return false;
    }
    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "JSON parse error in" << path << ":" << parseError.errorString();
        return false;
    }
    if (!doc.isObject()) {
        qWarning() << "Config file is not a JSON object";
        return false;
    }
    m_config = doc.object();
    return true;
}

bool ConfigLoader::save()
{
    QMutexLocker locker(&m_mutex);
    QJsonDocument doc(m_config);
    QByteArray jsonData = doc.toJson(QJsonDocument::Indented);
    QFile file(m_configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "Failed to open config file for writing:" << m_configPath;
        return false;
    }
    qint64 written = file.write(jsonData);
    bool success = (written == jsonData.size());
    file.close();
    if (!success) {
        qWarning() << "Failed to write config file";
        return false;
    }
    return true;
}

bool ConfigLoader::reload(const QString &path)
{
    QString actualPath = path.isEmpty() ? m_configPath : path;
    QMutexLocker locker(&m_mutex);
    return parseFile(actualPath);
}

// ==================================================================
// 【核心修改】加载 Tags 列表
// ==================================================================
QVector<TagInfo> ConfigLoader::loadTagsFromConfig() const
{
    QMutexLocker locker(&m_mutex);
    QVector<TagInfo> tags;

    // 检查 JSON 中是否存在 "tags" 数组
    if (!m_config.contains("tags")) {
        qWarning() << "[CONFIG] 'tags' array not found in JSON.";
        return tags;
    }

    QJsonValue tagsValue = m_config.value("tags");
    if (!tagsValue.isArray()) {
        qWarning() << "[CONFIG] 'tags' is not an array!";
        return tags;
    }

    QJsonArray tagsArray = tagsValue.toArray();
    qDebug() << "[CONFIG] Parsing" << tagsArray.size() << "tags...";

    for (int i = 0; i < tagsArray.size(); ++i) {
        QJsonValue val = tagsArray.at(i);
        if (!val.isObject()) continue;

        QJsonObject obj = val.toObject();
        TagInfo tag; // 使用 plcdata.h 中定义的标准结构体

        // --- 映射字段 ---
        // 基础信息
        tag.name = obj.value("name").toString();
        tag.dataType = stringToTagDataType(obj.value("dataType").toString());

        // 读写权限
        tag.writable = obj.value("writable").toBool(false);

        // 地址信息 (关键映射)
        // 注意：如果 JSON 中没有这些字段，结构体的默认值会生效 (-1 或 false)
        tag.dbNumber = obj.value("dbNumber").toInt(-1);
        tag.byteOffset = obj.value("byteOffset").toInt(-1);
        tag.bitOffset = obj.value("bitOffset").toInt(-1);

        // --- 地址字符串生成 (可选) ---
        // 如果你的 UI 需要显示 "DB1.DBX0.0" 这样的地址，可以在这里生成
        // 否则留空，由 PLC 驱动根据 db/byte/bit 计算
        if (tag.dbNumber > 0) {
            if (tag.bitOffset >= 0 && tag.bitOffset <= 7) {
                tag.address = QString("DB%1.DBX%2.%3").arg(tag.dbNumber).arg(tag.byteOffset).arg(tag.bitOffset);
            } else {
                tag.address = QString("DB%1.DBB%2").arg(tag.dbNumber).arg(tag.byteOffset);
            }
        }

        // --- 校验 ---
        if (tag.name.isEmpty()) {
            qWarning() << "[CONFIG] Skip tag at index" << i << ": Missing name";
            continue;
        }
        if (tag.dbNumber <= 0) {
            qWarning() << "[CONFIG] Skip tag:" << tag.name << "- Invalid DB number";
            continue;
        }

        tags.append(tag);

        // 调试输出
        qDebug() << "[CONFIG] Loaded:" << tag.name
                 << "| Addr:" << tag.address
           //      << "| Type:" << tag.dataType
                 << "| Writable:" << tag.writable;
    }

    return tags;
}

// ==================================================================
// 通用模板实现 (Key-Value 读取)
// ==================================================================
// String
template<>
QString ConfigLoader::getValue<QString>(const QString &key, const QString &defaultValue) const {
    QMutexLocker locker(&m_mutex);
    auto it = m_config.find(key);
    if (it != m_config.end() && it.value().isString()) {
        return it.value().toString();
    }
    return defaultValue;
}

// Int
template<>
int ConfigLoader::getValue<int>(const QString &key, const int &defaultValue) const {
    QMutexLocker locker(&m_mutex);
    auto it = m_config.find(key);
    if (it != m_config.end() && it.value().isDouble()) { // JSON 数字都是 Double
        return static_cast<int>(it.value().toDouble());
    }
    return defaultValue;
}

// Double
template<>
double ConfigLoader::getValue<double>(const QString &key, const double &defaultValue) const {
    QMutexLocker locker(&m_mutex);
    auto it = m_config.find(key);
    if (it != m_config.end() && it.value().isDouble()) {
        return it.value().toDouble();
    }
    return defaultValue;
}

// Bool
template<>
bool ConfigLoader::getValue<bool>(const QString &key, const bool &defaultValue) const {
    QMutexLocker locker(&m_mutex);
    auto it = m_config.find(key);
    if (it != m_config.end() && it.value().isBool()) {
        return it.value().toBool();
    }
    return defaultValue;
}

// QVariant
template<>
QVariant ConfigLoader::getValue<QVariant>(const QString &key, const QVariant &defaultValue) const {
    QMutexLocker locker(&m_mutex);
    auto it = m_config.find(key);
    if (it != m_config.end()) {
        return it.value().toVariant();
    }
    return defaultValue;
}

// ==================================================================
// 通用模板实现 (Key-Value 写入)
// ==================================================================
// String
template<>
void ConfigLoader::setValue<QString>(const QString &key, const QString &value) {
    QMutexLocker locker(&m_mutex);
    m_config[key] = value;
}

// Int
template<>
void ConfigLoader::setValue<int>(const QString &key, const int &value) {
    QMutexLocker locker(&m_mutex);
    m_config[key] = value;
}

// Double
template<>
void ConfigLoader::setValue<double>(const QString &key, const double &value) {
    QMutexLocker locker(&m_mutex);
    m_config[key] = value;
}

// Bool
template<>
void ConfigLoader::setValue<bool>(const QString &key, const bool &value) {
    QMutexLocker locker(&m_mutex);
    m_config[key] = value;
}

// QVariant
template<>
void ConfigLoader::setValue<QVariant>(const QString &key, const QVariant &value) {
    QMutexLocker locker(&m_mutex);
    m_config[key] = QJsonValue::fromVariant(value);
}