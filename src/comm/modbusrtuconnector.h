// src/comm/modbusrtuconnector.h — Modbus RTU 连接器
#ifndef MODBUSRTUCONNECTOR_H
#define MODBUSRTUCONNECTOR_H

#include "iconnector.h"
#include <QTimer>
#include <QMutex>
#include <cstdint>

class DeviceConfig;

class ModbusRtuConnector : public IConnector
{
    Q_OBJECT
public:
    explicit ModbusRtuConnector(DeviceConfig *deviceConfig, QObject *parent = nullptr);
    ~ModbusRtuConnector() override;

    bool isConnected() const override;
    PLCConnectionState connectionState() const override;
    QList<ReadResult> readBatch(const QList<ReadRequest> &requests) override;
    bool writeTag(const TagInfo &tag, const QVariant &value) override;
    void startPolling(int intervalMs = 1000) override;
    void stopPolling() override;
    bool isPolling() const override;
    void setAutoReconnect(bool enabled, int maxAttempts = 3, int intervalMs = 3000) override;
    QString protocolName() const override { return "ModbusRTU"; }
    void initInThread() override;
public slots:
    bool connectToDevice() override;
    void disconnectFromDevice() override;
private slots:
    void onPollTimer();
    void onReconnectTimer();

private:
    void updateState(PLCConnectionState newState);

    DeviceConfig *m_deviceConfig = nullptr;
    struct _modbus *m_ctx = nullptr;
    PLCConnectionState m_connState = PLCConnectionState::NOT_CONNECTED;
    QTimer *m_pollTimer      = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    mutable QMutex m_mutex;
    QList<ReadRequest> m_pollRequests;
    int m_pollInterval      = 1000;
    bool m_isPolling         = false;
    bool m_autoReconnect     = true;
    int  m_maxReconnectAttempts = 3;
    int  m_currentAttempts   = 0;
    int  m_reconnectInterval = 3000;
};

#endif // MODBUSRTUCONNECTOR_H
