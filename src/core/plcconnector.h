// src/core/plcconnector.h
#ifndef PLCCONNECTOR_H
#define PLCCONNECTOR_H

#include <QObject>
#include <QTimer>
#include <QtGlobal>
#include <QMutex>
#include "plcdata.h"
#include <QRecursiveMutex>

#ifdef __cplusplus
extern "C" {
#endif
#include "snap7.h"
#ifdef __cplusplus
}
#endif

class PLCConnector : public QObject
{
    Q_OBJECT

public:
    // 批量读取请求结构
    struct BatchReadRequest {
        QString tagName;   // 实际应为 tagKey（格式 deviceId/tagName），存储完整键
        TagInfo tagInfo;
    };

    // 批量读取结果
    struct BatchReadResult {
        QString tagName;   // 实际应为 tagKey，存储完整键
        QVariant value;
        bool success;
        QString error;
    };

    // 读取分组
    struct ReadGroup {
        int dbNumber;
        int startByte;
        int size;
        QList<BatchReadRequest> requests;
    };

    explicit PLCConnector(PLCData *dataManager, QObject *parent = nullptr);
    ~PLCConnector();

    // 连接管理
    Q_INVOKABLE bool connectToPLC(const QString &ip, int rack = 0, int slot = 1);
    Q_INVOKABLE void disconnectFromPLC();
    ControllerState getControllerState() const;
    PLCConnectionState getPLCConnectionState() const;
    bool isConnected() const { return m_plcConnectionState == PLCConnectionState::CONNECTED; }
    bool isPolling() const;

    // 自动连接控制
    Q_INVOKABLE void setAutoConnect(bool enable, int intervalMs = 5000);
    void startAutoConnect();
    void stopAutoConnect();

    // 心跳检测控制
    void startHeartbeatCheck();
    void stopHeartbeatCheck();

    // 状态机更新（供内部使用）
    void updateControllerState(ControllerState newState);
    void updatePLCConnectionState(PLCConnectionState newState);

    // 自动重连控制
    void startAutoReconnect();
    void setAutoReconnect(bool enabled, int maxAttempts = 3, int interval = 3000);

    // 底层数据读写方法
    bool readDB(int dbNumber, int startByte, int size, quint8 *buffer);
    bool writeDB(int dbNumber, int startByte, int size, const quint8 *buffer);

    // 单点数据读写（基于 DB 地址）
    Q_INVOKABLE bool readBool(int dbNumber, int byteOffset, int bitOffset);
    Q_INVOKABLE float readReal(int dbNumber, int byteOffset);
    Q_INVOKABLE int readInt(int dbNumber, int byteOffset);
    Q_INVOKABLE quint32 readDWord(int dbNumber, int byteOffset);
    Q_INVOKABLE bool writeBool(int dbNumber, int byteOffset, int bitOffset, bool value);
    Q_INVOKABLE bool writeReal(int dbNumber, int byteOffset, float value);
    Q_INVOKABLE bool writeInt(int dbNumber, int byteOffset, int value);
    Q_INVOKABLE bool writeDWord(int dbNumber, int byteOffset, quint32 value);
    Q_INVOKABLE void setPollInterval(int intervalMs);

    // 批量读取接口（供 Controller 使用）
    QList<PLCConnector::BatchReadResult> readOptimizedBatchFromMap(
        const QMap<int, QList<BatchReadRequest>>& dbRequests);
    QList<BatchReadResult> readBatchTags(const QList<BatchReadRequest>& requests);
    QList<BatchReadResult> readOptimizedBatch(const QList<BatchReadRequest>& requests);
    QByteArray readRawData(int dbNumber, int startByte, int size);
    QMap<int, QList<BatchReadRequest>> groupByDB(const QList<BatchReadRequest>& requests);

    // ========== 已废弃的标签管理方法（新架构下不再使用） ==========

    // 设置使用订阅轮询模式（由 Controller 启用）
    void setUseSubscriptionPolling(bool enable);

public slots:
    void startPolling();      // 独立轮询已废弃，仅保留接口
    void stopPolling();
    void pollData();          // 独立轮询已废弃，仅保留接口
    void initTimers();        // 在工作线程中创建定时器（必须调用）

signals:
    void connected();
    void disconnected();
    void connectionError(const QString &error);
    void allDataUpdated(const QMap<QString, QVariant> &data);   // data key 为 tagKey
    void tagValueUpdated(const QString &tagName, const QVariant &value); // tagName 为 tagKey
    void writeCompleted(const QString &tagName, bool success, const QString &error = "");
    void controllerStateChanged(ControllerState newState);
    void plcConnectionStateChanged(PLCConnectionState newState);
    void errorOccurred(const QString &error, AlertLevel severity);
    void statusMessage(const QString &message);
    void pollingStateChanged(bool isPolling);
    void autoConnectStopped();

private slots:
    void onStateTimerTimeout();
    void startTimer(TimerType timerType, int interval);
    void stopTimer();

private:
    S7Object m_client;
    PLCData *m_dataManager;
    QList<TagInfo> m_tags;           // 不再使用，保留为了兼容性
    mutable QRecursiveMutex m_mutex;

    ControllerState m_controllerState = ControllerState::DISCONNECTED;
    QString m_ip;
    int m_rack;
    int m_slot;
    QMap<int, int> m_dbSizes;        // 存储 DB 块大小缓存

    QThread* m_plcThread = nullptr;
    PLCConnectionState m_plcConnectionState = PLCConnectionState::NOT_CONNECTED;
    bool m_connectionLostHandled = false;
    int m_maxReconnectAttempts = 3;
    int m_currentReconnectAttempts = 0;
    bool m_useSubscriptionPolling = false;

    QTimer *m_stateTimer = nullptr;
    TimerType m_currentTimerType = TimerType::NONE;

    int m_heartbeatInterval = 10000;
    int m_autoConnectInterval = 5000;
    int m_reconnectInterval = 3000;
    int m_pollInterval = 1000;

    void handleError(int errorCode, const QString &operation);
    QVariant readTagValue(const TagInfo &tag);
    bool writeTagValue(const TagInfo &tag, const QVariant &value);
    QList<ReadGroup> optimizeReadGroups(const QMap<int, QList<BatchReadRequest>>& dbGroups);
    BatchReadResult parseTagData(const BatchReadRequest& request, const QByteArray& data, int groupOffset);

    bool checkDBInfo(int dbNumber, int requiredSize);   // 废弃
    int getDataSizeForType(TagDataType dataType) const;
    QVariant getDefaultValueForType(TagDataType dataType) const;
    QVariant parsePLCData(const QByteArray &data, TagDataType dataType, int bitOffset = -1);

    void checkConnectionHealth();
    void onAutoConnectTimeout();
    void attemptReconnect();
    void handleConnectionLost();

    QString timerTypeToString(TimerType type);
    bool isValidStateTransition(ControllerState from, ControllerState to) const;

    bool validatePLCInfo();

    void testReadAddresses();  // 测试函数
};

#endif // PLCCONNECTOR_H