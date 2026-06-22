#ifndef TAGMANAGER_H
#define TAGMANAGER_H

#include <QObject>
#include "common_types.h" // 包含 ViewType 等定义

class PLCData; // 前向声明

class TagManager : public QObject
{
    Q_OBJECT
public:
    explicit TagManager(PLCData* plcData, QObject *parent = nullptr);

    // --- 设备管理 ---
    bool addDevice(const DeviceConfig& config);
    bool updateDevice(const DeviceConfig& config);
    bool removeDevice(const QString& deviceId);

    // --- 标签读写 ---
    QVariant getValue(const QString& deviceId, const QString& tagName) const;
    void setValue(const QString& deviceId, const QString& tagName, const QVariant& value);

    // --- 视图配置 ---
    void setViewSelection(const QString& deviceId, ViewType view, const QStringList& tagNames);
    QStringList getViewSelection(const QString& deviceId, ViewType view) const;

signals:
    // 可以转发信号，也可以让 UI 直接连接 PLCData
    void viewSelectionChanged(const QString& deviceId, ViewType view);

private:
    PLCData* m_plcData;
};

#endif // TAGMANAGER_H