#include "s7connector.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QtNetwork/qhostaddress.h>
#include <algorithm>

S7Connector::S7Connector(DeviceConfig *deviceConfig, QObject *parent)
    : IConnector(deviceConfig->deviceId, parent)
    , m_deviceConfig(deviceConfig)
    , m_client(0)
    , m_pollInterval(deviceConfig->pollingInterval)
    , m_connState(PLCConnectionState::NOT_CONNECTED)
    , m_currentTimerType(TimerType::NONE)
    , m_stateTimer(nullptr)
{
    qDebug() << "S7Connector created for device:" << m_deviceId;
    m_client = Cli_Create();
    if (m_client) {
        qDebug() << "S7Connector: Snap7 client created for" << m_deviceId;
    } else {
        qCritical() << "S7Connector: Failed to create Snap7 client for" << m_deviceId;
    }
}

S7Connector::~S7Connector()
{
    qDebug() << "S7Connector: Destroying" << m_deviceId;
    if (m_stateTimer) {
        m_stateTimer->stop();
        disconnect(m_stateTimer, nullptr, this, nullptr);
        delete m_stateTimer;
        m_stateTimer = nullptr;
    }
    if (m_client != 0) {
        disconnectFromDevice();
        QElapsedTimer timer;
        timer.start();
        while (m_connState == PLCConnectionState::CONNECTING ||
               m_connState == PLCConnectionState::CONNECTED) {
            if (timer.elapsed() > 5000) {
                qWarning() << "S7Connector: Timeout waiting for disconnection";
                break;
            }
            QThread::msleep(10);
        }
        Cli_Destroy(&m_client);
        m_client = 0;
    }
    updateState(PLCConnectionState::DISCONNECTED);
    qDebug() << "S7Connector: Destruction completed for" << m_deviceId;
}

bool S7Connector::connectToDevice()
{
    const QString &ip = m_deviceConfig->ip;
    int rack = m_deviceConfig->rack;
    int slot = m_deviceConfig->slot;

    if (ip.isEmpty()) { emit connectionError(m_deviceId, "IP address is empty"); return false; }
    QHostAddress address;
    if (!address.setAddress(ip)) { emit connectionError(m_deviceId, "Invalid IP: " + ip); return false; }

    if (m_connState == PLCConnectionState::CONNECTED ||
        m_connState == PLCConnectionState::CONNECTING) {
        disconnectFromDevice();
        QThread::msleep(100);
    }

    updateState(PLCConnectionState::CONNECTING);
    emit statusMessage(m_deviceId, "Connecting to PLC: " + ip);

    if (m_client != 0) { Cli_Destroy(&m_client); m_client = 0; }
    m_client = Cli_Create();
    if (!m_client) {
        updateState(PLCConnectionState::CONNECTION_FAILED);
        emit connectionError(m_deviceId, "Failed to create S7 client");
        return false;
    }

    QElapsedTimer timer; timer.start();
    int result = Cli_ConnectTo(m_client, ip.toStdString().c_str(), rack, slot);
    qint64 elapsed = timer.elapsed();

    if (result != 0) {
        char errorText[256] = {0};
        Cli_ErrorText(result, errorText, sizeof(errorText)-1);
        QString err = QString("Connect failed (%1ms): %2").arg(elapsed).arg(QString::fromLocal8Bit(errorText));
        qWarning() << "S7Connector [" << m_deviceId << "]:" << err;
        updateState(PLCConnectionState::CONNECTION_FAILED);
        emit connectionError(m_deviceId, err);
        Cli_Destroy(&m_client); m_client = 0;
        return false;
    }

    qDebug() << "S7Connector [" << m_deviceId << "]: Connected! Duration:" << elapsed << "ms";
    updateState(PLCConnectionState::CONNECTED);
    m_connectionLostHandled = false;
    emit statusMessage(m_deviceId, "Connected to PLC: " + ip);
    emit connected(m_deviceId);
    startTimer(TimerType::HEARTBEAT, m_heartbeatInterval);
    return true;
}

void S7Connector::disconnectFromDevice()
{
    if (m_connState == PLCConnectionState::NOT_CONNECTED ||
        m_connState == PLCConnectionState::DISCONNECTED) return;

    updateState(PLCConnectionState::DISCONNECTING);
    emit statusMessage(m_deviceId, "Disconnecting from PLC...");
    stopPolling();

    if (m_client) {
        Cli_Disconnect(m_client);
    }

    updateState(PLCConnectionState::DISCONNECTED);
    emit statusMessage(m_deviceId, "Disconnected from PLC");
    emit disconnected(m_deviceId);

    if (m_client) { Cli_Destroy(&m_client); m_client = 0; }
}

bool S7Connector::isConnected() const
{
    QMutexLocker locker(&m_mutex);
    return m_connState == PLCConnectionState::CONNECTED;
}

PLCConnectionState S7Connector::connectionState() const
{
    QMutexLocker locker(&m_mutex);
    return m_connState;
}

QList<IConnector::ReadResult> S7Connector::readBatch(const QList<ReadRequest> &requests)
{
    QList<ReadResult> results;
    if (requests.isEmpty()) return results;

    QList<ReadGroup> readGroups = buildReadGroups(requests);
    for (const ReadGroup &group : readGroups) {
        QByteArray rawData = readRawData(group.dbNumber, group.startByte, group.size);
        if (rawData.isEmpty()) {
            for (const ReadRequest &req : group.requests) {
                ReadResult r; r.tagKey = req.tagKey; r.success = false;
                r.error = "Batch read failed"; results.append(r);
            }
        } else {
            for (const ReadRequest &req : group.requests)
                results.append(parseTagData(req, rawData, group.startByte));
        }
    }
    return results;
}

bool S7Connector::writeTag(const TagInfo &tag, const QVariant &value)
{
    if (!tag.writable) return false;
    QString tagKey = m_deviceId + "/" + tag.name;

    QMutexLocker locker(&m_mutex);
    if (m_connState != PLCConnectionState::CONNECTED) {
        emit writeCompleted(tagKey, false, "Not connected"); return false;
    }

    int db = tag.dbNumber, off = tag.byteOffset;
    bool ok = false;
    quint8 buf[4];

    switch (tag.dataType) {
    case TagDataType::BOOL:
        if (!readDB(db, off, 1, buf)) { emit writeCompleted(tagKey, false, "Read-modify-write failed"); return false; }
        if (value.toBool()) buf[0] |= (1 << tag.bitOffset);
        else buf[0] &= ~(1 << tag.bitOffset);
        ok = writeDB(db, off, 1, buf); break;
    case TagDataType::REAL: { float f = value.toFloat(); memcpy(buf, &f, 4); ok = writeDB(db, off, 4, buf); break; }
    case TagDataType::INT:  { int v = value.toInt(); buf[0]=(quint8)(v>>8); buf[1]=(quint8)(v&0xFF); ok = writeDB(db, off, 2, buf); break; }
    case TagDataType::DWORD:{ quint32 v = value.toUInt(); buf[0]=(quint8)(v>>24); buf[1]=(quint8)(v>>16); buf[2]=(quint8)(v>>8); buf[3]=(quint8)(v&0xFF); ok = writeDB(db, off, 4, buf); break; }
    default: emit writeCompleted(tagKey, false, "Unsupported type"); return false;
    }
    emit writeCompleted(tagKey, ok, ok ? "OK" : "Failed");
    return ok;
}

void S7Connector::startPolling(int intervalMs) { m_pollInterval = intervalMs; m_isPolling = true; }
void S7Connector::stopPolling() { m_isPolling = false; if (m_currentTimerType == TimerType::POLLING) stopTimer(); }
bool S7Connector::isPolling() const { return m_isPolling; }
void S7Connector::setAutoReconnect(bool enabled, int maxAttempts, int intervalMs)
{ m_autoReconnect = enabled; m_maxReconnectAttempts = maxAttempts; m_reconnectInterval = intervalMs; }

void S7Connector::initInThread()
{
    qDebug() << "S7Connector::initInThread() on thread:" << QThread::currentThread();
    if (!m_stateTimer) {
        m_stateTimer = new QTimer(this);
        m_stateTimer->setSingleShot(false);
        connect(m_stateTimer, &QTimer::timeout, this, &S7Connector::onStateTimerTimeout, Qt::QueuedConnection);
    }
}

bool S7Connector::readDB(int dbNumber, int startByte, int size, quint8 *buffer)
{
    QMutexLocker locker(&m_mutex);
    if (m_connState != PLCConnectionState::CONNECTED) { emit connectionError(m_deviceId, "Not connected"); return false; }
    int r = Cli_DBRead(m_client, dbNumber, startByte, size, buffer);
    if (r != 0) { handleError(r, "DBRead"); handleConnectionLost(); return false; }
    return true;
}

bool S7Connector::writeDB(int dbNumber, int startByte, int size, const quint8 *buffer)
{
    QMutexLocker locker(&m_mutex);
    if (m_connState != PLCConnectionState::CONNECTED) { emit connectionError(m_deviceId, "Not connected"); return false; }
    int r = Cli_DBWrite(m_client, dbNumber, startByte, size, const_cast<quint8*>(buffer));
    if (r != 0) { handleError(r, "DBWrite"); return false; }
    return true;
}

QMap<int, QList<IConnector::ReadRequest>> S7Connector::groupByDB(const QList<ReadRequest> &requests)
{
    QMap<int, QList<ReadRequest>> groups;
    for (const ReadRequest &req : requests) groups[req.tagInfo.dbNumber].append(req);
    return groups;
}

QList<S7Connector::ReadGroup> S7Connector::buildReadGroups(const QList<ReadRequest> &requests)
{
    QMap<int, QList<ReadRequest>> dbGroups = groupByDB(requests);
    QList<ReadGroup> optimizedGroups;

    for (auto it = dbGroups.begin(); it != dbGroups.end(); ++it) {
        int dbNumber = it.key();
        QList<ReadRequest> sorted = it.value();
        std::sort(sorted.begin(), sorted.end(),
                  [](const ReadRequest &a, const ReadRequest &b) {
                      return a.tagInfo.byteOffset < b.tagInfo.byteOffset; });

        QList<ReadGroup> dbReadGroups;
        ReadGroup cur;
        for (const ReadRequest &req : sorted) {
            int sz = getDataSize(req.tagInfo.dataType);
            int end = req.tagInfo.byteOffset + sz;
            if (cur.requests.isEmpty()) {
                cur.dbNumber = dbNumber; cur.startByte = req.tagInfo.byteOffset;
                cur.size = sz; cur.requests.append(req);
            } else {
                int curEnd = cur.startByte + cur.size;
                if (req.tagInfo.byteOffset <= curEnd + 10) {
                    cur.size = qMax(curEnd, end) - cur.startByte;
                    cur.requests.append(req);
                } else {
                    dbReadGroups.append(cur); cur = ReadGroup();
                    cur.dbNumber = dbNumber; cur.startByte = req.tagInfo.byteOffset;
                    cur.size = sz; cur.requests.append(req);
                }
            }
        }
        if (!cur.requests.isEmpty()) dbReadGroups.append(cur);
        optimizedGroups.append(dbReadGroups);
    }
    return optimizedGroups;
}

QByteArray S7Connector::readRawData(int dbNumber, int startByte, int size)
{
    QMutexLocker locker(&m_mutex);
    if (!m_client || m_connState != PLCConnectionState::CONNECTED) return QByteArray();
    QByteArray buffer(size, 0);
    int r = Cli_DBRead(m_client, dbNumber, startByte, size, buffer.data());
    if (r != 0) {
        char err[256]={0}; Cli_ErrorText(r, err, sizeof(err)-1);
        handleConnectionLost();
        qWarning() << "S7Connector [" << m_deviceId << "]: Batch read failed DB" << dbNumber << err;
        return QByteArray();
    }
    return buffer;
}


IConnector::ReadResult S7Connector::parseTagData(const ReadRequest &req, const QByteArray &data, int groupOffset)
{
    ReadResult r; r.tagKey = req.tagKey;
    const TagInfo &t = req.tagInfo;
    int rel = t.byteOffset - groupOffset;
    int sz = getDataSize(t.dataType);
    if (rel < 0 || rel + sz > data.size()) {
        r.success = false; r.error = QString::fromUtf8("Data out of bounds"); return r;
    }
    QByteArray td = data.mid(rel, sz);
    r.value = parsePLCData(td, t.dataType, t.bitOffset);
    r.success = r.value.isValid();
    if (!r.success) r.error = QString::fromUtf8("Data parse failed");
    return r;
}

QVariant S7Connector::parsePLCData(const QByteArray &data, TagDataType dataType, int bitOffset)
{
    if (data.isEmpty()) return QVariant();
    switch (dataType) {
    case TagDataType::BOOL: {
        quint8 b = static_cast<quint8>(data[0]);
        if (bitOffset >= 0 && bitOffset < 8) return QVariant((b >> bitOffset) & 0x01);
        return QVariant(b != 0);
    }
    case TagDataType::REAL: {
        if (data.size() < 4) return QVariant();
        quint32 raw = (static_cast<quint8>(data[0]) << 24) | (static_cast<quint8>(data[1]) << 16)
                    | (static_cast<quint8>(data[2]) << 8) | static_cast<quint8>(data[3]);
        float f; memcpy(&f, &raw, sizeof(float)); return QVariant(f);
    }
    case TagDataType::INT: {
        if (data.size() < 2) return QVariant();
        qint16 v = (static_cast<quint8>(data[0]) << 8) | static_cast<quint8>(data[1]);
        return QVariant(static_cast<int>(v));
    }
    case TagDataType::DWORD: {
        if (data.size() < 4) return QVariant();
        quint32 v = (static_cast<quint8>(data[0]) << 24) | (static_cast<quint8>(data[1]) << 16)
                  | (static_cast<quint8>(data[2]) << 8) | static_cast<quint8>(data[3]);
        return QVariant(static_cast<unsigned int>(v));
    }
    case TagDataType::WORD: {
        if (data.size() < 2) return QVariant();
        quint16 v = (static_cast<quint8>(data[0]) << 8) | static_cast<quint8>(data[1]);
        return QVariant(static_cast<unsigned int>(v));
    }
    case TagDataType::BYTE: {
        if (data.size() < 1) return QVariant();
        return QVariant(static_cast<unsigned int>(static_cast<quint8>(data[0])));
    }
    default: return QVariant();
    }
}

QVariant S7Connector::getDefaultValue(TagDataType dataType) const
{
    switch (dataType) {
    case TagDataType::BOOL: return false;
    case TagDataType::BYTE: case TagDataType::INT: case TagDataType::WORD:
    case TagDataType::DINT: case TagDataType::DWORD: return 0;
    case TagDataType::REAL: return 0.0f;
    case TagDataType::STRING: return QString();
    default: return QVariant();
    }
}

int S7Connector::getDataSize(TagDataType dataType) const
{
    switch (dataType) {
    case TagDataType::BOOL: case TagDataType::BYTE: return 1;
    case TagDataType::INT: case TagDataType::WORD: return 2;
    case TagDataType::DINT: case TagDataType::DWORD: case TagDataType::REAL: return 4;
    default: return 1;
    }
}

void S7Connector::handleError(int errorCode, const QString &operation)
{
    char err[256]; Cli_ErrorText(errorCode, err, sizeof(err));
    QString msg = QString("%1 failed: %2 (Error: %3)")
        .arg(operation, QString::fromLocal8Bit(err)).arg(errorCode);
    qDebug() << "S7Connector [" << m_deviceId << "]:" << msg;
    emit connectionError(m_deviceId, msg);
}

void S7Connector::updateState(PLCConnectionState newState)
{
    PLCConnectionState old = m_connState;
    if (old == newState) return;
    { QMutexLocker l(&m_mutex); m_connState = newState; }
    qDebug() << "S7Connector [" << m_deviceId << "]: State "
             << plcConnectionStateToString(old) << "->" << plcConnectionStateToString(newState);
    emit connectionStateChanged(m_deviceId, newState);
}

void S7Connector::checkConnectionHealth()
{
    if (m_connState != PLCConnectionState::CONNECTED) return;
    quint8 b; if (Cli_DBRead(m_client, 1, 0, 1, &b) != 0) handleConnectionLost();
}

void S7Connector::handleConnectionLost()
{
    if (m_connectionLostHandled) return;
    m_connectionLostHandled = true;
    if (m_connState == PLCConnectionState::CONNECTED) {
        updateState(PLCConnectionState::CONNECTION_LOST);
        emit disconnected(m_deviceId);
    }
    if (m_currentTimerType == TimerType::POLLING) stopTimer();
    if (m_autoReconnect) {
        m_currentAttempts = 0;
        startTimer(TimerType::RECONNECT, m_reconnectInterval);
    }
}

void S7Connector::attemptReconnect()
{
    static int backoff = 1000;
    if (m_currentAttempts == 0) backoff = 1000;
    qDebug() << "S7Connector [" << m_deviceId << "]: Reconnect attempt" << (m_currentAttempts+1);
    if (connectToDevice()) {
        qInfo() << "S7Connector [" << m_deviceId << "]: Auto-reconnect succeeded";
        stopTimer(); m_currentAttempts = 0; backoff = 1000; m_connectionLostHandled = false;
        return;
    }
    m_currentAttempts++;
    if (m_currentAttempts >= m_maxReconnectAttempts) {
        qWarning() << "S7Connector [" << m_deviceId << "]: Max reconnect attempts reached";
        stopTimer(); emit connectionError(m_deviceId, QString::fromUtf8("Max reconnect attempts reached"));
        return;
    }
    int next = qMin(backoff * (1 << qMin(m_currentAttempts, 6)), 60000);
    backoff = next;
    startTimer(TimerType::RECONNECT, next);
}

void S7Connector::startTimer(TimerType type, int interval)
{
    if (!m_stateTimer) { qCritical() << "S7Connector: Timer not initialized!"; return; }
    if (QThread::currentThread() != m_stateTimer->thread()) {
        QMetaObject::invokeMethod(this, [this,type,interval](){
            if(m_stateTimer->isActive()) m_stateTimer->stop();
            m_currentTimerType=type; m_stateTimer->setInterval(interval); m_stateTimer->start();
        }, Qt::QueuedConnection);
        return;
    }
    if(m_stateTimer->isActive()) m_stateTimer->stop();
    m_currentTimerType=type; m_stateTimer->setInterval(interval); m_stateTimer->start();
}

void S7Connector::stopTimer()
{
    if(m_stateTimer && m_stateTimer->isActive()) { m_stateTimer->stop(); m_currentTimerType=TimerType::NONE; }
}

void S7Connector::onStateTimerTimeout()
{
    switch (m_currentTimerType) {
    case TimerType::HEARTBEAT: checkConnectionHealth(); break;
    case TimerType::RECONNECT: attemptReconnect(); break;
    case TimerType::POLLING: break;
    default: break;
    }
}
