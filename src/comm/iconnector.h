#ifndef ICONNECTOR_H
#define ICONNECTOR_H

#include <QObject>
#include <QVariant>
#include "common_types.h"

// ==================================================
// 通讯抽象接口
// 所有协议连接器（S7Connector, ModbusTcpConnector, ModbusRtuConnector）继承此接口
// ==================================================
class IConnector : public QObject
{
    Q_OBJECT

public:
    // 读取请求（适配器传入）
    struct ReadRequest {
        QString tagKey;         // 格式: deviceId/tagName
        TagInfo tagInfo;
    };

    // 读取结果
    struct ReadResult {
        QString tagKey;
        QVariant value;
        bool success = false;
        QString error;
    };

    explicit IConnector(const QString &deviceId, QObject *parent = nullptr)
        : QObject(parent), m_deviceId(deviceId) {}
    ~IConnector() override = default;

    // -------- 连接管理 --------
    virtual bool isConnected() const = 0;
    virtual PLCConnectionState connectionState() const = 0;

    // -------- 数据读写 --------
    virtual QList<ReadResult> readBatch(const QList<ReadRequest> &requests) = 0;
    virtual bool writeTag(const TagInfo &tag, const QVariant &value) = 0;

    // -------- 轮询控制 --------
    virtual void startPolling(int intervalMs = 1000) = 0;
    virtual void stopPolling() = 0;
    virtual bool isPolling() const = 0;

    // -------- 重连参数 --------
    virtual void setAutoReconnect(bool enabled, int maxAttempts = 3, int intervalMs = 3000) = 0;

    // -------- 标识 --------
    QString deviceId() const { return m_deviceId; }
    virtual QString protocolName() const = 0;

    // 在工作线程中初始化（必须在 moveToThread 后调用）
    virtual void initInThread() = 0;

signals:
    void connected(const QString &deviceId);
    void disconnected(const QString &deviceId);
    void connectionError(const QString &deviceId, const QString &error);
    void connectionStateChanged(const QString &deviceId, PLCConnectionState state);
    void tagValueUpdated(const QString &tagKey, const QVariant &value);
    void batchDataUpdated(const QMap<QString, QVariant> &data);
    void writeCompleted(const QString &tagKey, bool success, const QString &error);
    void errorOccurred(const QString &deviceId, const QString &error, AlertLevel severity);
    void statusMessage(const QString &deviceId, const QString &message);
public slots:
    // 连接管理（异步调用专用）
    virtual bool connectToDevice() = 0;
    virtual void disconnectFromDevice() = 0;

protected:
    QString m_deviceId;
};

#endif // ICONNECTOR_H