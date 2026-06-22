// src/comm/modbusrtuconnector.cpp — Modbus RTU 连接器实现
#include "modbusrtuconnector.h"
#include "common_types.h"
#include "modbushelper.h"

#include <QDebug>
#include <QThread>

extern "C" {
#include "modbus.h"
#include "modbus-rtu.h"
}

ModbusRtuConnector::ModbusRtuConnector(DeviceConfig *deviceConfig, QObject *parent)
    : IConnector(deviceConfig ? deviceConfig->deviceId : QString(), parent)
    , m_deviceConfig(deviceConfig)
{
    qDebug() << "ModbusRtuConnector created for device:" << m_deviceId
             << "Port:" << (deviceConfig ? deviceConfig->serialPort : "N/A");
}

ModbusRtuConnector::~ModbusRtuConnector()
{
    qDebug() << "ModbusRtuConnector: Destroying" << m_deviceId;
    stopPolling();
    disconnectFromDevice();
    qDebug() << "ModbusRtuConnector: Destruction completed for" << m_deviceId;
}

bool ModbusRtuConnector::connectToDevice()
{
    if (!m_deviceConfig) {
        emit connectionError(m_deviceId, "DeviceConfig is null");
        return false;
    }

    QString port = m_deviceConfig->serialPort;
    if (port.isEmpty()) {
        emit connectionError(m_deviceId, "Serial port is empty");
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

    int baud    = m_deviceConfig->baudRate;
    int dataBits = m_deviceConfig->dataBits;
    int stopBits = m_deviceConfig->stopBits;
    char parity  = m_deviceConfig->parity.isEmpty() ? 'N' : m_deviceConfig->parity[0].toUpper().toLatin1();

    updateState(PLCConnectionState::CONNECTING);
    emit statusMessage(m_deviceId,
                       QString("Connecting to Modbus RTU: %1 %2,%3%4%5")
                           .arg(port).arg(baud).arg(dataBits).arg(parity).arg(stopBits));

    // 释放旧上下文
    if (m_ctx) {
        modbus_close(m_ctx);
        modbus_free(m_ctx);
        m_ctx = nullptr;
    }

    // 创建 RTU 上下文
    m_ctx = modbus_new_rtu(port.toUtf8().constData(), baud, parity, dataBits, stopBits);
    if (!m_ctx) {
        updateState(PLCConnectionState::CONNECTION_FAILED);
        emit connectionError(m_deviceId, "Failed to create Modbus RTU context");
        return false;
    }

    // 设置从站地址和响应超时（RTU 串口较慢，设 1 秒）
    modbus_set_slave(m_ctx, m_deviceConfig->modbusSlaveId);
    modbus_set_response_timeout(m_ctx, 1, 0); // 1 second

    int rc = modbus_connect(m_ctx);
    if (rc == -1) {
        QString err = QString("Modbus RTU connect failed: %1").arg(modbus_strerror(errno));
        qWarning() << "ModbusRtuConnector [" << m_deviceId << "]:" << err;
        updateState(PLCConnectionState::CONNECTION_FAILED);
        emit connectionError(m_deviceId, err);
        modbus_free(m_ctx);
        m_ctx = nullptr;
        return false;
    }

    qDebug() << "ModbusRtuConnector [" << m_deviceId << "]: Connected to" << port;
    updateState(PLCConnectionState::CONNECTED);
    emit statusMessage(m_deviceId, "Connected to Modbus RTU: " + port);
    emit connected(m_deviceId);
    return true;
}

void ModbusRtuConnector::disconnectFromDevice()
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
    emit statusMessage(m_deviceId, "Disconnected from Modbus RTU");
    emit disconnected(m_deviceId);
}

bool ModbusRtuConnector::isConnected() const
{
    QMutexLocker locker(&m_mutex);
    return m_connState == PLCConnectionState::CONNECTED;
}

PLCConnectionState ModbusRtuConnector::connectionState() const
{
    QMutexLocker locker(&m_mutex);
    return m_connState;
}

QList<IConnector::ReadResult> ModbusRtuConnector::readBatch(const QList<ReadRequest> &requests)
{
    QList<ReadResult> results;
    if (requests.isEmpty()) return results;

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

        int bufSize = count;
        if (fc == 3 || fc == 4) bufSize = count * 2;
        QByteArray buf(bufSize, 0);

        int rc = ModbusHelper::readRegisters(m_ctx, fc, regAddr, count, (uint8_t*)buf.data());
        if (rc < 0) {
            r.success = false;
            r.error = QString("Modbus RTU read error: %1").arg(modbus_strerror(errno));
            results.append(r);
            continue;
        }

        r.value = ModbusHelper::regsToVariant((const uint16_t*)buf.constData(), count, req.tagInfo.dataType);
        r.success = true;
        results.append(r);
    }

    return results;
}

bool ModbusRtuConnector::writeTag(const TagInfo &tag, const QVariant &value)
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
        emit writeCompleted(tagKey, false, "Failed to parse Modbus address: " + tag.address);
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
        QString err = QString("Modbus RTU write failed: %1").arg(modbus_strerror(errno));
        qWarning() << "ModbusRtuConnector [" << m_deviceId << "]: " << err;
        emit writeCompleted(tagKey, false, err);
        return false;
    }

    emit writeCompleted(tagKey, true, "OK");
    return true;
}

void ModbusRtuConnector::startPolling(int intervalMs)
{
    m_pollInterval = intervalMs;
    m_isPolling = true;
    qDebug() << "ModbusRtuConnector [" << m_deviceId << "]: Polling started, interval=" << intervalMs << "ms";
}

void ModbusRtuConnector::stopPolling()
{
    m_isPolling = false;
    if (m_pollTimer) {
        m_pollTimer->stop();
    }
    qDebug() << "ModbusRtuConnector [" << m_deviceId << "]: Polling stopped";
}

bool ModbusRtuConnector::isPolling() const
{
    return m_isPolling;
}

void ModbusRtuConnector::setAutoReconnect(bool enabled, int maxAttempts, int intervalMs)
{
    m_autoReconnect = enabled;
    m_maxReconnectAttempts = maxAttempts;
    m_reconnectInterval = intervalMs;
}

void ModbusRtuConnector::initInThread()
{
    qDebug() << "ModbusRtuConnector::initInThread() on thread:" << QThread::currentThread();

    if (!m_pollTimer) {
        m_pollTimer = new QTimer(this);
        m_pollTimer->setSingleShot(false);
        connect(m_pollTimer, &QTimer::timeout, this, &ModbusRtuConnector::onPollTimer);
    }

    if (!m_reconnectTimer) {
        m_reconnectTimer = new QTimer(this);
        m_reconnectTimer->setSingleShot(true);
        connect(m_reconnectTimer, &QTimer::timeout, this, &ModbusRtuConnector::onReconnectTimer);
    }

    if (m_isPolling && !m_pollTimer->isActive()) {
        m_pollTimer->start(m_pollInterval);
    }
}

void ModbusRtuConnector::onPollTimer()
{
    if (!m_isPolling) return;

    QMutexLocker locker(&m_mutex);
    if (!m_ctx || m_connState != PLCConnectionState::CONNECTED) {
        return;
    }

    if (m_pollRequests.isEmpty()) return;

    QMap<QString, QVariant> dataMap;
    for (const ReadRequest &req : m_pollRequests) {
        int fc, regAddr, count;
        if (!ModbusHelper::parseAddress(req.tagInfo.address, fc, regAddr, count)) continue;

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

void ModbusRtuConnector::onReconnectTimer()
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
        qWarning() << "ModbusRtuConnector [" << m_deviceId
                    << "]: Max reconnect attempts reached (" << m_maxReconnectAttempts << ")";
        m_currentAttempts = 0;
        return;
    }

    m_currentAttempts++;
    qDebug() << "ModbusRtuConnector [" << m_deviceId
             << "]: Reconnect attempt" << m_currentAttempts << "/" << m_maxReconnectAttempts;

    if (connectToDevice()) {
        m_currentAttempts = 0;
    } else {
        m_reconnectTimer->start(m_reconnectInterval);
    }
}

void ModbusRtuConnector::updateState(PLCConnectionState newState)
{
    QMutexLocker locker(&m_mutex);
    if (m_connState == newState) return;
    qDebug() << "ModbusRtuConnector [" << m_deviceId << "]: State" << (int)m_connState << "->" << (int)newState;
    m_connState = newState;
    locker.unlock();
    emit connectionStateChanged(m_deviceId, newState);
}
