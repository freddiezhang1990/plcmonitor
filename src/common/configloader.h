#ifndef CONFIGLOADER_H
#define CONFIGLOADER_H
#include "plcdata.h"     // 包含 PLCTag 结构体定义
#include <QObject>
#include <QJsonObject>
#include <QMutex>
#include <QVector>


class ConfigLoader : public QObject
{
    Q_OBJECT

public:
    // 单例访问点（可选，方便全局调用）
    static ConfigLoader* instance() { return m_instance; }

    explicit ConfigLoader(const QString &configPath, QObject *parent = nullptr);
    ~ConfigLoader();

    // === 核心功能：加载 Tags ===
    // 这是关键函数，用于将 JSON 转换为 PLCTag 列表
    QVector<TagInfo> loadTagsFromConfig() const;

    // === 通用配置读写 (保留原有的 Key-Value 功能) ===
    // 用于读取 IP, Rack, Slot 等基础配置
    template<typename T>
    T getValue(const QString &key, const T &defaultValue = T()) const;

    template<typename T>
    void setValue(const QString &key, const T &value);

    bool save(); // 保存所有配置
    bool reload(const QString &path = QString());

private:
    // 内部实现
    void ensureConfigDir();
    bool parseFile(const QString &path);

    // 成员变量
    QString m_configPath;
    mutable QMutex m_mutex; // mutable 允许在 const 函数中加锁
    QJsonObject m_config;

    // 单例指针
    static ConfigLoader* m_instance;
};

// ==================================================================
// 模板函数特化声明 (必须放在头文件中)
// ==================================================================
// String
template<> QString ConfigLoader::getValue<QString>(const QString &key, const QString &defaultValue) const;
template<> void ConfigLoader::setValue<QString>(const QString &key, const QString &value);

// Int
template<> int ConfigLoader::getValue<int>(const QString &key, const int &defaultValue) const;
template<> void ConfigLoader::setValue<int>(const QString &key, const int &value);

// Double
template<> double ConfigLoader::getValue<double>(const QString &key, const double &defaultValue) const;
template<> void ConfigLoader::setValue<double>(const QString &key, const double &value);

// Bool
template<> bool ConfigLoader::getValue<bool>(const QString &key, const bool &defaultValue) const;
template<> void ConfigLoader::setValue<bool>(const QString &key, const bool &value);

// QVariant (通用类型)
template<> QVariant ConfigLoader::getValue<QVariant>(const QString &key, const QVariant &defaultValue) const;
template<> void ConfigLoader::setValue<QVariant>(const QString &key, const QVariant &value);

#endif // CONFIGLOADER_H