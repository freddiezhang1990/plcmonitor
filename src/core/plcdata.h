// src/core/plcdata.h
#ifndef PLCDATA_H
#define PLCDATA_H

#include <QObject>
#include <QVector>
#include <QHash>
#include <QVariant>
#include <QStringList>
#include <QMutex>
#include "common_types.h"

class PLCData : public QObject
{
    Q_OBJECT
public:
    explicit PLCData(QObject *parent = nullptr);
    ~PLCData();

    // ========== 设备管理 ==========
    void addDevice(DeviceConfig *device);
    void removeDevice(const QString &deviceId);
    DeviceConfig* getDevice(const QString &deviceId) const;
    QVector<DeviceConfig*> getAllDevices() const;

    // ========== 标签查找（扁平索引） ==========
    // 索引 key 格式：deviceId + "/" + tagName
    TagInfo* findTag(const QString &deviceId, const QString &tagName);
    const TagInfo* findTag(const QString &deviceId, const QString &tagName) const;
    QString makeTagKey(const QString &deviceId, const QString &tagName) const;

    // ========== 标签值缓存 ==========
    void updateTagValue(const QString &deviceId, const QString &tagName, const QVariant &value);
    QVariant getTagValue(const QString &deviceId, const QString &tagName) const;

    // ========== 标签 DB 记录标志 ==========
    void setTagDbEnabled(const QString &deviceId, const QString &tagName, bool enabled);
    bool isTagDbEnabled(const QString &deviceId, const QString &tagName) const;
    QStringList getEnabledTagKeys() const;

    // 获取所有 tagKey（格式 "deviceId/tagName"）列表
    QStringList getAllTagNames() const;
    // 通过 tagKey 判断该标签是否启用数据库记录
    bool isDbEnabled(const QString &tagKey) const;

    // ========== 视图选择管理 ==========
    // 视图选择基于全局（不区分设备），选择项为 "deviceId/tagName"
    void setTagSelectedInView(ViewType view, const QString &tagKey, bool selected);
    void setViewSelection(ViewType view, const QStringList &tagKeys);
    bool isTagSelectedInView(ViewType view, const QString &tagKey) const;
    QStringList getSelectedTagKeysForView(ViewType view) const;
    void clearViewSelection(ViewType view);
    // ========== 数据修改辅助（供 UI 模型使用） ==========
    void rebuildIndex();   // 重建扁平索引（结构变化后调用）—— 改为公有
    // ========== 数据修改信号（供 UI 模型同步） ==========
    // 设备层级变化
    void beginStructureChange();   // 批量修改前调用，可暂不实现简单版本
    void endStructureChange();     // 批量修改后重建索引

    // 持久化
    bool loadFromJson(const QString &filePath);
    bool saveToJson(const QString &filePath) const;

signals:
    // 设备变化
    void deviceAdded(const QString &deviceId);
    void deviceRemoved(const QString &deviceId);
    void deviceChanged(const QString &deviceId);   // 设备属性（如名称、IP）变化

    // 标签值变化
    void tagValueChanged(const QString &deviceId, const QString &tagName, const QVariant &value);

    // 标签 DB 标志变化
    void tagDbEnabledChanged(const QString &deviceId, const QString &tagName, bool enabled);

    // 视图选择变化
    void viewSelectionChanged(ViewType view, const QStringList &tagKeys);

    // 整体数据变化（结构变化后触发，用于刷新 UI）
    void dataChanged();

private:
    // 内部辅助
    void recursiveCollectTags(TagGroup *group, QVector<TagInfo*> &out); // 递归收集标签指针
    void recursiveDeleteGroups(QVector<TagGroup*> &groups);              // 递归删除分组

    // 数据容器
    QVector<DeviceConfig*> m_devices;
    // 扁平索引，key = deviceId + "/" + tagName
    QHash<QString, TagInfo*> m_tagIndex;
    // 标签值缓存（独立于 TagInfo，便于线程安全）
    QHash<QString, QVariant> m_tagValues;
    // 视图选择
    QHash<ViewType, QSet<QString>> m_viewSelections;

    mutable QMutex m_mutex;   // 保护上述容器
};

#endif // PLCDATA_H