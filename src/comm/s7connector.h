#ifndef S7CONNECTOR_H
#define S7CONNECTOR_H

#include "iconnector.h"
#include "common_types.h"
#include <QTimer>
#include <QThread>
#include <QRecursiveMutex>

#ifdef __cplusplus
extern "C" {
#endif
#include "snap7.h"
#ifdef __cplusplus
}
#endif

// ==================================================
// S7Connector — Snap7 S7 协议实现
// 从原 PLCConnector 重构而来，继承 IConnector 抽象接口
// ==================================================
class S7Connector : public IConnector
{
    Q_OBJECT

public:
    // 读取分组（批量优化用）
    struct ReadGroup {
        int dbNumber;
        int startByte;
        int size;
        QList<ReadRequest> requests;
    };

    explicit S7Connector(DeviceConfig *deviceConfig, QObject *parent = nullptr);
    ~S7Connector() override;

    // -------- IConnector 接口实现 --------

    bool isConnected() const override;
    PLCConnectionState connectionState() const override;

    QList<ReadResult> readBatch(const QList<ReadRequest> &requests) override;
    bool writeTag(const TagInfo &tag, const QVariant &value) override;

    void startPolling(int intervalMs = 1000) override;
    void stopPolling() override;
    bool isPolling() const override;

    void setAutoReconnect(bool enabled, int maxAttempts = 3, int intervalMs = 3000) override;

    QString protocolName() const override { return "S7"; }
    void initInThread() override;

    // -------- S7 特有：原始 DB 块读写 --------
    bool readDB(int dbNumber, int startByte, int size, quint8 *buffer);
    bool writeDB(int dbNumber, int startByte, int size, const quint8 *buffer);

private slots:
    void onStateTimerTimeout();
    bool connectToDevice() override;
    void disconnectFromDevice() override;

private:
    // 批量读取优化
    QList<ReadGroup> buildReadGroups(const QList<ReadRequest> &requests);
    QMap<int, QList<ReadRequest>> groupByDB(const QList<ReadRequest> &requests);
    ReadResult parseTagData(const ReadRequest &req, const QByteArray &data, int groupOffset);
    QByteArray readRawData(int dbNumber, int startByte, int size);

    // 数据解析
    QVariant parsePLCData(const QByteArray &data, TagDataType dataType, int bitOffset = -1);
    QVariant getDefaultValue(TagDataType dataType) const;
    int getDataSize(TagDataType dataType) const;

    // 错误与状态
    void handleError(int errorCode, const QString &operation);
    void updateState(PLCConnectionState newState);

    // 连接健康与重连
    void checkConnectionHealth();
    void attemptReconnect();
    void handleConnectionLost();

    // 定时器
    void startTimer(TimerType type, int interval);
    void stopTimer();

    // 成员变量
    DeviceConfig *m_deviceConfig = nullptr;
    S7Object m_client = 0;
    mutable QRecursiveMutex m_mutex;

    PLCConnectionState m_connState = PLCConnectionState::NOT_CONNECTED;
    QMap<int, int> m_dbSizes;

    QTimer *m_stateTimer = nullptr;
    TimerType m_currentTimerType = TimerType::NONE;

    // 重连参数
    bool m_autoReconnect = true;
    int m_maxReconnectAttempts = 3;
    int m_currentAttempts = 0;
    int m_reconnectInterval = 3000;
    int m_pollInterval = 1000;
    int m_heartbeatInterval = 10000;
    bool m_isPolling = false;
    bool m_connectionLostHandled = false;
};

#endif // S7CONNECTOR_H