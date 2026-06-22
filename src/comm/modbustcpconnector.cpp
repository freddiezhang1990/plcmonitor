// src/comm/modbustcpconnector.cpp — Modbus TCP 连接器实现
#include "modbustcpconnector.h"
#include "common_types.h"
#include "modbushelper.h"

#include <QDebug>
#include <QThread>
#include <QtNetwork/qhostaddress.h>

extern "C" {
#include "modbus.h"
}

ModbusTcpConnector::ModbusTcpConnector(DeviceConfig *deviceConfig, QObject *parent)
    : IConnector(deviceConfig ? deviceConfig->deviceId : QString(), parent)
    , m_deviceConfig(deviceConfig)
{
    qDebug() << "ModbusTcpConnector created for device:" << m_deviceId
             << "IP:" << (deviceConfig ? deviceConfig->ip : "N/A");
}

ModbusTcpConnector::~ModbusTcpConnector()
{
    qDebug() << "ModbusTcpConnector: Destroying" << m_deviceId;
    stopPolling();
    disconnectFromDevice();
    qDebug() << "ModbusTcpConnector: Destruction completed for" << m_deviceId;
}

bool ModbusTcpConnector::connectToDevice()
{
    if (!m_deviceConfig) {
        emit connectionError(m_deviceId, "DeviceConfig is null");
        return false;
    }

    QString ip = m_deviceConfig->ip;
    int port = m_deviceConfig->port > 0 ? m_deviceConfig->port : 502;

    if (ip.isEmpty()) {
        emit connectionError(m_deviceId, "IP address is empty");
        return false;
    }

    // 验证 IP 地址格式
    QHostAddress addr;
    if (!addr.setAddress(ip)) {
        emit connectionError(m_deviceId, "Invalid IP: " + ip);
        return false;
    }

    m_mutex.lock();
    PLCConnectionState current = m_connState;
    m_mutex.unlock();

    if (current == PLCConnectionState::CONNECTED ||
        current == PLCConnectionState::CONNECTING) {
        disconnectFromDevice();
        QThread::msleep(100);
    }

    updateState(PLCConnectionState::CONNECTING);
    emit statusMessage(m_deviceId, "Connecting to Modbus TCP: " + ip + ":" + QString::number(port));

    // 释放旧上下文
    if (m_ctx) {
        modbus_close(m_ctx);
        modbus_free(m_ctx);
        m_ctx = nullptr;
    }

    // 创建 TCP 上下文并连接
    m_ctx = modbus_new_tcp(ip.toUtf8().constData(), port);
    if (!m_ctx) {
        updateState(PLCConnectionState::CONNECTION_FAILED);
        emit connectionError(m_deviceId, "Failed to create Modbus TCP context");
        return false;
    }

    // 设置从站地址和响应超时
    modbus_set_slave(m_ctx, m_deviceConfig->modbusSlaveId);
    modbus_set_response_timeout(m_ctx, 0, 500000); // 500ms

    int rc = modbus_connect(m_ctx);
    if (rc == -1) {
        QString err = QString("Modbus TCP connect failed: %1").arg(modbus_strerror(errno));
        qWarning() << "ModbusTcpConnector [" << m_deviceId << "]:" << err;
        updateState(PLCConnectionState::CONNECTION_FAILED);
        emit connectionError(m_deviceId, err);
        modbus_free(m_ctx);
        m_ctx = nullptr;
        return false;
    }

    qDebug() << "ModbusTcpConnector [" << m_deviceId << "]: Connected to" << ip << ":" << port;
    updateState(PLCConnectionState::CONNECTED);
    emit statusMessage(m_deviceId, "Connected to Modbus TCP: " + ip + ":" + QString::number(port));
    emit connected(m_deviceId);
    return true;
}

void ModbusTcpConnector::disconnectFromDevice()
{
    m_mutex.lock();
    PLCConnectionState current = m_connState;
    m_mutex.unlock();

    if (current == PLCConnectionState::NOT_CONNECTED ||
        current == PLCConnectionState::DISCONNECTED) {
        return;
    }

    updateState(PLCConnectionState::DISCONNECTING);
    stopPolling();

    if (m_ctx) {
        modbus_close(m_ctx);
        modbus_free(m_ctx);
        m_ctx = nullptr;
    }

    updateState(PLCConnectionState::DISCONNECTED);
    emit statusMessage(m_deviceId, "Disconnected from Modbus TCP");
    emit disconnected(m_deviceId);
}

bool ModbusTcpConnector::isConnected() const
{
    QMutexLocker locker(&m_mutex);
    return m_connState == PLCConnectionState::CONNECTED;
}

PLCConnectionState ModbusTcpConnector::connectionState() const
{
    QMutexLocker locker(&m_mutex);
    return m_connState;
}

QList<IConnector::ReadResult> ModbusTcpConnector::readBatch(const QList<ReadRequest> &requests)
{
    QList<ReadResult> results;
    if (requests.isEmpty()) return results;

    // 缓存请求用于轮询
    m_pollRequests = requests;

    QMutexLocker locker(&m_mutex);
    if (!m_ctx || m_connState != PLCConnectionState::CONNECTED) {
        for (const ReadRequest &req : requests) {
            ReadResult r;
            r.tagKey = req.tagKey;
            r.success = false;
            r.error = "Not connected";
            results.append(r);
        }
        return results;
    }

    // 释放锁之前完成操作（libmodbus 不是线程安全的）
    for (const ReadRequest &req : requests) {
        ReadResult r;
        r.tagKey = req.tagKey;

        int fc, regAddr, count;
        if (!ModbusHelper::parseAddress(req.tagInfo.address, fc, regAddr, count)) {
            r.success = false;
            r.error = "Failed to parse Modbus address: " + req.tagInfo.address;
            results.append(r);
            continue;
        }

        // 分配缓冲区
        int bufSize = count;
        if (fc == 3 || fc == 4) bufSize = count * 2; // 寄存器 = 2 字节
        QByteArray buf(bufSize, 0);

        int rc = ModbusHelper::readRegisters(m_ctx, fc, regAddr, count, (uint8_t*)buf.data());
        if (rc < 0) {
            r.success = false;
            r.error = QString("Modbus read error: %1").arg(modbus_strerror(errno));
            results.append(r);
            continue;
        }

        // 解析数据
        if (fc == 1 || fc == 2) {
            // 线圈/离散输入已在 buf 中
            r.value = ModbusHelper::regsToVariant((const uint16_t*)buf.constData(), count, req.tagInfo.dataType);
        } else {
            r.value = ModbusHelper::regsToVariant((const uint16_t*)buf.constData(), count, req.tagInfo.dataType);
        }
        r.success = true;
        results.append(r);
    }

    return results;
}

bool ModbusTcpConnector::writeTag(const TagInfo &tag, const QVariant &value)
{
    if (!tag.writable) return false;

    QString tagKey = m_deviceId + "/" + tag.name;

    QMutexLocker locker(&m_mutex);
    if (!m_ctx || m_connState != PLCConnectionState::CONNECTED) {
        emit writeCompleted(tagKey, false, "Not connected");
        return false;
    }

    int fc, regAddr, count;
    if (!ModbusHelper::parseAddress(tag.address, fc, regAddr, count)) {
        QString err = "Failed to parse Modbus address: " + tag.address;
        emit writeCompleted(tagKey, false, err);
        return false;
    }

    QVector<uint16_t> regs = ModbusHelper::variantToRegs(value, tag.dataType);
    if (regs.isEmpty()) {
        emit writeCompleted(tagKey, false, "Failed to convert value to registers");
        return false;
    }

    int rc = -1;
    if (regs.size() == 1) {
        rc = ModbusHelper::writeRegister(m_ctx, fc, regAddr, regs[0]);
    } else {
        rc = ModbusHelper::writeRegisters(m_ctx, fc, regAddr, regs.size(), regs.constData());
    }

    if (rc < 0) {
        QString err = QString("Modbus write failed: %1").arg(modbus_strerror(errno));
        qWarning() << "ModbusTcpConnector [" << m_deviceId << "]: " << err;
        emit writeCompleted(tagKey, false, err);
        return false;
    }

    emit writeCompleted(tagKey, true, "OK");
    return true;
}

void ModbusTcpConnector::startPolling(int intervalMs)
{
    m_pollInterval = intervalMs;
    m_isPolling = true;
    qDebug() << "ModbusTcpConnector [" << m_deviceId << "]: Polling started, interval=" << intervalMs << "ms";
}

void ModbusTcpConnector::stopPolling()
{
    m_isPolling = false;
    if (m_pollTimer) {
        m_pollTimer->stop();
    }
    qDebug() << "ModbusTcpConnector [" << m_deviceId << "]: Polling stopped";
}

bool ModbusTcpConnector::isPolling() const
{
    return m_isPolling;
}

void ModbusTcpConnector::setAutoReconnect(bool enabled, int maxAttempts, int intervalMs)
{
    m_autoReconnect = enabled;
    m_maxReconnectAttempts = maxAttempts;
    m_reconnectInterval = intervalMs;
}

void ModbusTcpConnector::initInThread()
{
    qDebug() << "ModbusTcpConnector::initInThread() on thread:" << QThread::currentThread();

    // 创建轮询定时器
    if (!m_pollTimer) {
        m_pollTimer = new QTimer(this);
        m_pollTimer->setSingleShot(false);
        connect(m_pollTimer, &QTimer::timeout, this, &ModbusTcpConnector::onPollTimer);
    }

    // 创建重连定时器
    if (!m_reconnectTimer) {
        m_reconnectTimer = new QTimer(this);
        m_reconnectTimer->setSingleShot(true);
        connect(m_reconnectTimer, &QTimer::timeout, this, &ModbusTcpConnector::onReconnectTimer);
    }

    // 启动轮询
    if (m_isPolling && !m_pollTimer->isActive()) {
        m_pollTimer->start(m_pollInterval);
    }
}

void ModbusTcpConnector::onPollTimer()
{
    if (!m_isPolling) return;

    QMutexLocker locker(&m_mutex);
    if (!m_ctx || m_connState != PLCConnectionState::CONNECTED) {
        return;
    }

    // 读取缓存请求
    if (m_pollRequests.isEmpty()) {
        return; // 还没有任何 readBatch 调用
    }

    // 执行批量读取
    QMap<QString, QVariant> dataMap;
    for (const ReadRequest &req : m_pollRequests) {
        int fc, regAddr, count;
        if (!ModbusHelper::parseAddress(req.tagInfo.address, fc, regAddr, count)) {
            continue;
        }

        int bufSize = count;
        if (fc == 3 || fc == 4) bufSize = count * 2;
        QByteArray buf(bufSize, 0);

        int rc = ModbusHelper::readRegisters(m_ctx, fc, regAddr, count, (uint8_t*)buf.data());
        if (rc >= 0) {
            QVariant val = ModbusHelper::regsToVariant((const uint16_t*)buf.constData(), count, req.tagInfo.dataType);
            dataMap[req.tagKey] = val;
            emit tagValueUpdated(req.tagKey, val);
        }
    }

    if (!dataMap.isEmpty()) {
        emit batchDataUpdated(dataMap);
    }
}

void ModbusTcpConnector::onReconnectTimer()
{
    if (!m_autoReconnect) return;

    m_mutex.lock();
    PLCConnectionState state = m_connState;
    m_mutex.unlock();

    if (state == PLCConnectionState::CONNECTED ||
        state == PLCConnectionState::CONNECTING) {
        return;
    }

    if (m_currentAttempts >= m_maxReconnectAttempts) {
        qWarning() << "ModbusTcpConnector [" << m_deviceId
                    << "]: Max reconnect attempts reached (" << m_maxReconnectAttempts << ")";
        m_currentAttempts = 0;
        return;
    }

    m_currentAttempts++;
    qDebug() << "ModbusTcpConnector [" << m_deviceId
             << "]: Reconnect attempt" << m_currentAttempts << "/" << m_maxReconnectAttempts;

    if (connectToDevice()) {
        m_currentAttempts = 0;
    } else {
        m_reconnectTimer->start(m_reconnectInterval);
    }
}

void ModbusTcpConnector::updateState(PLCConnectionState newState)
{
    QMutexLocker locker(&m_mutex);
    if (m_connState == newState) return;
    qDebug() << "ModbusTcpConnector [" << m_deviceId << "]: State" << (int)m_connState << "->" << (int)newState;
    m_connState = newState;
    locker.unlock();
    emit connectionStateChanged(m_deviceId, newState);
}
