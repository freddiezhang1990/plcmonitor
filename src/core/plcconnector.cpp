// src/core/plcconnector.cpp
#include "plcconnector.h"
#include <QDebug>
#include <QVariant>
#include <QtNetwork/qhostaddress.h>
#include <qdir.h>
#include <qelapsedtimer.h>
#include <qfile.h>
#include <qjsonarray.h>
#include <qjsondocument.h>
#include <qjsonobject.h>
#include <qjsonparseerror.h>
#include <qthread.h>

PLCConnector::PLCConnector(PLCData *dataManager, QObject *parent)
    : QObject(parent)
    , m_dataManager(dataManager)
    , m_client(0)
    , m_rack(0)
    , m_slot(1)
    , m_pollInterval(1000)
    , m_autoConnectInterval(5000)
    , m_heartbeatInterval(10000)
    , m_maxReconnectAttempts(3)
    , m_reconnectInterval(3000)
    , m_currentReconnectAttempts(0)
    , m_controllerState(ControllerState::DISCONNECTED)
    , m_plcConnectionState(PLCConnectionState::DISCONNECTED)
    , m_plcThread(new QThread(this))
    , m_currentTimerType(TimerType::NONE)
    , m_stateTimer(nullptr)
{
    qDebug() << "=== PLCConnector 构造函数被调用 ===";
    qDebug() << "当前线程:" << QThread::currentThread();
    qDebug() << "PLCData 指针:" << m_dataManager;

    m_client = Cli_Create();
    if (m_client) {
        qDebug() << "Snap7 客户端创建成功";
    } else {
        qCritical() << "无法创建 Snap7 客户端";
    }
}

PLCConnector::~PLCConnector()
{
    qDebug() << "PLCConnector: Starting destruction";

    stopAutoConnect();
    if (m_currentTimerType != TimerType::NONE)
        stopTimer();
    if (m_stateTimer) {
        qDebug() << "PLCConnector: Stopping state timer";
        m_stateTimer->stop();
        disconnect(m_stateTimer, nullptr, this, nullptr);
        delete m_stateTimer;
        m_stateTimer = nullptr;
    }

    if (m_client != 0) {
        qDebug() << "PLCConnector: Disconnecting from PLC";
        disconnectFromPLC();
        QElapsedTimer timer;
        timer.start();
        while (m_plcConnectionState == PLCConnectionState::CONNECTING ||
               m_plcConnectionState == PLCConnectionState::CONNECTED) {
            if (timer.elapsed() > 5000) {
                qWarning() << "PLCConnector: Timeout waiting for disconnection";
                break;
            }
            QThread::msleep(10);
        }
        Cli_Destroy(&m_client);
        m_client = 0;
        qDebug() << "PLCConnector: Client destroyed";
    }

    updatePLCConnectionState(PLCConnectionState::DISCONNECTED);
    qDebug() << "PLCConnector: Destruction completed";
}

// 在工作线程中初始化定时器
void PLCConnector::initTimers()
{
    qDebug() << "PLCConnector::initTimers() 执行线程:" << QThread::currentThread();
    if (!m_stateTimer) {
        m_stateTimer = new QTimer(this);
        m_stateTimer->setSingleShot(false);
        connect(m_stateTimer, &QTimer::timeout,
                this, &PLCConnector::onStateTimerTimeout,
                Qt::QueuedConnection);
        qDebug() << "PLCConnector 定时器已在工作线程创建";
    } else {
        qWarning() << "定时器已存在，跳过创建";
    }
}

// ========== 连接/断开 ==========
bool PLCConnector::connectToPLC(const QString &ip, int rack, int slot)
{
    // 验证参数...
    if (ip.isEmpty()) { emit connectionError("IP地址不能为空"); return false; }
    QHostAddress address;
    if (!address.setAddress(ip)) { emit connectionError("IP地址格式无效: " + ip); return false; }
    if (ip == "127.0.0.1" || ip == "localhost") { emit connectionError("本地回环地址不允许: " + ip); return false; }

    ControllerState currentState = getControllerState();
    if (currentState == ControllerState::CONNECTED || currentState == ControllerState::POLLING) {
        qDebug() << "已连接到PLC，先断开现有连接";
        disconnectFromPLC();
        QThread::msleep(100);
    }

    updatePLCConnectionState(PLCConnectionState::CONNECTING);
    updateControllerState(ControllerState::CONNECTING);
    emit statusMessage("正在连接到PLC: " + ip);

    if (m_client != 0) { Cli_Destroy(&m_client); m_client = 0; }
    m_client = Cli_Create();
    if (!m_client) {
        updatePLCConnectionState(PLCConnectionState::CONNECTION_FAILED);
        updateControllerState(ControllerState::STATE_ERROR);
        emit connectionError("创建S7客户端失败");
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    int result = Cli_ConnectTo(m_client, ip.toStdString().c_str(), rack, slot);
    qint64 elapsed = timer.elapsed();

    if (result != 0) {
        char errorText[256] = {0};
        Cli_ErrorText(result, errorText, sizeof(errorText)-1);
        QString errorMsg = QString("连接失败 (%1ms): %2 (IP: %3)").arg(elapsed).arg(QString::fromLocal8Bit(errorText)).arg(ip);
        qWarning() << errorMsg;
        updatePLCConnectionState(PLCConnectionState::CONNECTION_FAILED);
        updateControllerState(ControllerState::STATE_ERROR);
        emit connectionError(errorMsg);
        emit statusMessage("连接失败: " + QString(errorText));
        Cli_Destroy(&m_client);
        m_client = 0;
        return false;
    }

    m_ip = ip; m_rack = rack; m_slot = slot;
    qDebug() << "连接成功! 耗时:" << elapsed << "ms";
    stopAutoConnect();
    startHeartbeatCheck();
    updatePLCConnectionState(PLCConnectionState::CONNECTED);
    updateControllerState(ControllerState::CONNECTED);
    m_connectionLostHandled = false;
    emit statusMessage("成功连接到PLC: " + ip);
    emit connected();
    return true;
}

void PLCConnector::disconnectFromPLC()
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_controllerState == ControllerState::DISCONNECTED || m_controllerState == ControllerState::SHUTTING_DOWN)
            return;
    }
    updatePLCConnectionState(PLCConnectionState::DISCONNECTING);
    updateControllerState(ControllerState::SHUTTING_DOWN);
    emit statusMessage("正在断开PLC连接...");
    stopPolling();
    if (m_client) {
        int result = Cli_Disconnect(m_client);
        if (result != 0) {
            char errorText[256] = {0};
            Cli_ErrorText(result, errorText, sizeof(errorText)-1);
            qWarning() << "断开连接失败:" << errorText;
        }
    }
    updatePLCConnectionState(PLCConnectionState::DISCONNECTED);
    updateControllerState(ControllerState::DISCONNECTED);
    emit statusMessage("已断开PLC连接");
    emit disconnected();
    if (m_client) { Cli_Destroy(&m_client); m_client = 0; }
}

// ========== 读写操作（底层） ==========
bool PLCConnector::readDB(int dbNumber, int startByte, int size, quint8 *buffer)
{
    QMutexLocker locker(&m_mutex);
    if (m_plcConnectionState != PLCConnectionState::CONNECTED) {
        emit connectionError("Not connected to PLC");
        return false;
    }
    int result = Cli_DBRead(m_client, dbNumber, startByte, size, buffer);
    if (result != 0) {
        handleError(result, "DBRead");
        handleConnectionLost();
        return false;
    }
    return true;
}

bool PLCConnector::writeDB(int dbNumber, int startByte, int size, const quint8 *buffer)
{
    QMutexLocker locker(&m_mutex);
    if (m_plcConnectionState != PLCConnectionState::CONNECTED) {
        emit connectionError("Not connected to PLC");
        return false;
    }
    int result = Cli_DBWrite(m_client, dbNumber, startByte, size, const_cast<quint8*>(buffer));
    if (result != 0) {
        handleError(result, "DBWrite");
        return false;
    }
    return true;
}

bool PLCConnector::readBool(int dbNumber, int byteOffset, int bitOffset)
{
    QMutexLocker locker(&m_mutex);
    if (m_plcConnectionState != PLCConnectionState::CONNECTED) return false;
    quint8 buffer[1];
    if (readDB(dbNumber, byteOffset, 1, buffer))
        return (buffer[0] & (1 << bitOffset)) != 0;
    return false;
}

float PLCConnector::readReal(int dbNumber, int byteOffset)
{
    QMutexLocker locker(&m_mutex);
    if (m_plcConnectionState != PLCConnectionState::CONNECTED) return 0.0f;
    quint8 buffer[4];
    if (readDB(dbNumber, byteOffset, 4, buffer)) {
        float value;
        memcpy(&value, buffer, sizeof(float));
        return value;
    }
    return 0.0f;
}

int PLCConnector::readInt(int dbNumber, int byteOffset)
{
    QMutexLocker locker(&m_mutex);
    if (m_plcConnectionState != PLCConnectionState::CONNECTED) return 0;
    quint8 buffer[2];
    if (readDB(dbNumber, byteOffset, 2, buffer)) {
        int value = static_cast<int>(buffer[1]) | (static_cast<int>(buffer[0]) << 8);
        return value;
    }
    return 0;
}

quint32 PLCConnector::readDWord(int dbNumber, int byteOffset)
{
    QMutexLocker locker(&m_mutex);
    if (m_plcConnectionState != PLCConnectionState::CONNECTED) return 0;
    quint8 buffer[4];
    if (readDB(dbNumber, byteOffset, 4, buffer)) {
        quint32 value = (static_cast<quint32>(buffer[3]) << 24) |
                        (static_cast<quint32>(buffer[2]) << 16) |
                        (static_cast<quint32>(buffer[1]) << 8) |
                        static_cast<quint32>(buffer[0]);
        return value;
    }
    return 0;
}

bool PLCConnector::writeBool(int dbNumber, int byteOffset, int bitOffset, bool value)
{
    QString tagName = QString("DB%1.DBX%2.%3").arg(dbNumber).arg(byteOffset).arg(bitOffset);
    QMutexLocker locker(&m_mutex);
    if (m_plcConnectionState != PLCConnectionState::CONNECTED) {
        emit writeCompleted("", false, "Not connected to PLC");
        return false;
    }
    quint8 buffer[1];
    if (!readDB(dbNumber, byteOffset, 1, buffer)) return false;
    if (value) buffer[0] |= (1 << bitOffset);
    else buffer[0] &= ~(1 << bitOffset);
    bool success = writeDB(dbNumber, byteOffset, 1, buffer);
    emit writeCompleted(tagName, success, success ? "BOOL write successful" : "BOOL write failed");
    return success;
}

bool PLCConnector::writeReal(int dbNumber, int byteOffset, float value)
{
    QString tagName = QString("DB%1.DBX%2").arg(dbNumber).arg(byteOffset);
    QMutexLocker locker(&m_mutex);
    if (m_plcConnectionState != PLCConnectionState::CONNECTED) {
        emit writeCompleted("", false, "Not connected to PLC");
        return false;
    }
    quint8 buffer[4];
    memcpy(buffer, &value, sizeof(float));
    bool success = writeDB(dbNumber, byteOffset, 4, buffer);
    emit writeCompleted(tagName, success, success ? QString("REAL write successful: %1").arg(value) : "REAL write failed");
    return success;
}

bool PLCConnector::writeInt(int dbNumber, int byteOffset, int value)
{
    qDebug() << "=== writeInt ===" << "DB" << dbNumber << "Offset" << byteOffset << "Value" << value;
    QString tagName = QString("DB%1.DBX%2").arg(dbNumber).arg(byteOffset);
    QMutexLocker locker(&m_mutex);
    if (m_plcConnectionState != PLCConnectionState::CONNECTED) {
        emit writeCompleted(tagName, false, "Not connected to PLC");
        return false;
    }
    quint8 buffer[2];
    buffer[1] = static_cast<quint8>(value & 0xFF);
    buffer[0] = static_cast<quint8>((value >> 8) & 0xFF);
    bool success = writeDB(dbNumber, byteOffset, 2, buffer);
    emit writeCompleted(tagName, success, success ? QString("INT write successful: %1").arg(value) : "INT write failed");
    return success;
}

bool PLCConnector::writeDWord(int dbNumber, int byteOffset, quint32 value)
{
    QString tagName = QString("DB%1.DBX%2").arg(dbNumber).arg(byteOffset);
    QMutexLocker locker(&m_mutex);
    if (m_plcConnectionState != PLCConnectionState::CONNECTED) {
        emit writeCompleted("", false, "Not connected to PLC");
        return false;
    }
    quint8 buffer[4];
    buffer[3] = static_cast<quint8>(value & 0xFF);
    buffer[2] = static_cast<quint8>((value >> 8) & 0xFF);
    buffer[1] = static_cast<quint8>((value >> 16) & 0xFF);
    buffer[0] = static_cast<quint8>((value >> 24) & 0xFF);
    bool success = writeDB(dbNumber, byteOffset, 4, buffer);
    emit writeCompleted(tagName, success, success ? QString("DWORD write successful: %1").arg(value) : "DWORD write failed");
    return success;
}

// ========== 轮询相关（订阅模式下不再使用独立轮询） ==========
void PLCConnector::setUseSubscriptionPolling(bool enable)
{
    m_useSubscriptionPolling = enable;
    qDebug() << "PLCConnector: Subscription polling mode set to" << enable;
}

void PLCConnector::startPolling()
{
    qDebug() << "startPolling called but subscription mode is enabled, ignoring.";
    return;  // 订阅轮询模式下不启动独立轮询
}

void PLCConnector::stopPolling()
{
    if (m_currentTimerType == TimerType::POLLING) {
        stopTimer();
        if (m_controllerState == ControllerState::POLLING)
            updateControllerState(ControllerState::CONNECTED);
        emit pollingStateChanged(false);
    }
}

void PLCConnector::pollData()
{
    qDebug() << "pollData called but subscription mode is enabled, ignoring.";
    // 独立轮询已废弃，由 Controller 通过批量读取完成
}

// ========== 批量读取优化（供 Controller 使用） ==========
QList<PLCConnector::BatchReadResult> PLCConnector::readOptimizedBatch(
    const QList<BatchReadRequest>& requests)
{
    QList<BatchReadResult> results;
    if (requests.isEmpty()) return results;

    QMap<int, QList<BatchReadRequest>> dbGroups = groupByDB(requests);
    QList<ReadGroup> readGroups = optimizeReadGroups(dbGroups);
    for (const ReadGroup& group : readGroups) {
        QByteArray rawData = readRawData(group.dbNumber, group.startByte, group.size);
        if (rawData.isEmpty()) {
            for (const BatchReadRequest& req : group.requests) {
                BatchReadResult result;
                result.tagName = req.tagName;
                result.success = false;
                result.error = "批量读取失败";
                results.append(result);
            }
        } else {
            for (const BatchReadRequest& req : group.requests) {
                results.append(parseTagData(req, rawData, group.startByte));
            }
        }
    }
    return results;
}

QMap<int, QList<PLCConnector::BatchReadRequest>> PLCConnector::groupByDB(
    const QList<BatchReadRequest>& requests)
{
    QMap<int, QList<BatchReadRequest>> groups;
    for (const BatchReadRequest& req : requests)
        groups[req.tagInfo.dbNumber].append(req);
    return groups;
}

QList<PLCConnector::ReadGroup> PLCConnector::optimizeReadGroups(
    const QMap<int, QList<BatchReadRequest>>& dbGroups)
{
    QList<ReadGroup> optimizedGroups;
    for (auto it = dbGroups.begin(); it != dbGroups.end(); ++it) {
        int dbNumber = it.key();
        QList<BatchReadRequest> sorted = it.value();
        std::sort(sorted.begin(), sorted.end(),
                  [](const BatchReadRequest& a, const BatchReadRequest& b) {
                      return a.tagInfo.byteOffset < b.tagInfo.byteOffset;
                  });
        QList<ReadGroup> dbReadGroups;
        ReadGroup cur;
        for (const BatchReadRequest& req : sorted) {
            int size = getDataSizeForType(req.tagInfo.dataType);
            int end = req.tagInfo.byteOffset + size;
            if (cur.requests.isEmpty()) {
                cur.dbNumber = dbNumber;
                cur.startByte = req.tagInfo.byteOffset;
                cur.size = size;
                cur.requests.append(req);
            } else {
                int curEnd = cur.startByte + cur.size;
                if (req.tagInfo.byteOffset <= curEnd + 10) {
                    int newEnd = qMax(curEnd, end);
                    cur.size = newEnd - cur.startByte;
                    cur.requests.append(req);
                } else {
                    dbReadGroups.append(cur);
                    cur = ReadGroup();
                    cur.dbNumber = dbNumber;
                    cur.startByte = req.tagInfo.byteOffset;
                    cur.size = size;
                    cur.requests.append(req);
                }
            }
        }
        if (!cur.requests.isEmpty()) dbReadGroups.append(cur);
        optimizedGroups.append(dbReadGroups);
    }
    return optimizedGroups;
}

QByteArray PLCConnector::readRawData(int dbNumber, int startByte, int size)
{
    QMutexLocker locker(&m_mutex);
    if (!m_client || m_plcConnectionState != PLCConnectionState::CONNECTED) return QByteArray();
    QByteArray buffer(size, 0);
    int result = Cli_DBRead(m_client, dbNumber, startByte, size, buffer.data());
    if (result != 0) {
        char errorText[256] = {0};
        Cli_ErrorText(result, errorText, sizeof(errorText)-1);
        handleConnectionLost();
        qWarning() << "批量读取失败 DB" << dbNumber << "起始:" << startByte << "大小:" << size << "错误:" << errorText;
        return QByteArray();
    }
    return buffer;
}

PLCConnector::BatchReadResult PLCConnector::parseTagData(
    const BatchReadRequest& request, const QByteArray& data, int groupOffset)
{
    BatchReadResult result;
    result.tagName = request.tagName;
    const TagInfo& tag = request.tagInfo;
    int rel = tag.byteOffset - groupOffset;
    int size = getDataSizeForType(tag.dataType);
    if (rel < 0 || rel + size > data.size()) {
        result.success = false;
        result.error = QString("数据越界: 偏移=%1, 大小=%2, 数据大小=%3").arg(rel).arg(size).arg(data.size());
        return result;
    }
    QByteArray tagData = data.mid(rel, size);
    result.value = parsePLCData(tagData, tag.dataType, tag.bitOffset);
    result.success = result.value.isValid();
    if (!result.success) result.error = "数据解析失败";
    return result;
}

QList<PLCConnector::BatchReadResult> PLCConnector::readOptimizedBatchFromMap(
    const QMap<int, QList<BatchReadRequest>>& dbRequests)
{
    QList<BatchReadRequest> all;
    for (auto it = dbRequests.begin(); it != dbRequests.end(); ++it)
        all.append(it.value());
    return readOptimizedBatch(all);
}

bool PLCConnector::writeTagValue(const TagInfo &tag, const QVariant &value)
{
    if (!tag.writable) return false;
    switch (tag.dataType) {
    case TagDataType::BOOL: return writeBool(tag.dbNumber, tag.byteOffset, tag.bitOffset, value.toBool());
    case TagDataType::REAL: return writeReal(tag.dbNumber, tag.byteOffset, value.toFloat());
    case TagDataType::INT:  return writeInt(tag.dbNumber, tag.byteOffset, value.toInt());
    case TagDataType::DWORD:return writeDWord(tag.dbNumber, tag.byteOffset, value.toUInt());
    default: qWarning() << "Unsupported data type for writing:" << tagDataTypeToString(tag.dataType); return false;
    }
}

// ========== 辅助函数 ==========
void PLCConnector::handleError(int errorCode, const QString &operation)
{
    char errorText[256];
    Cli_ErrorText(errorCode, errorText, sizeof(errorText));
    QString errorMsg = QString("%1 failed: %2 (Error: %3)").arg(operation, QString::fromLocal8Bit(errorText)).arg(errorCode);
    qDebug() << "PLC Error:" << errorMsg;
    emit connectionError(errorMsg);
}

int PLCConnector::getDataSizeForType(TagDataType dataType) const
{
    switch (dataType) {
    case TagDataType::BOOL: case TagDataType::BYTE: return 1;
    case TagDataType::INT: case TagDataType::WORD: return 2;
    case TagDataType::DINT: case TagDataType::DWORD: case TagDataType::REAL: return 4;
    case TagDataType::STRING: return 1;
    default: return 1;
    }
}

QVariant PLCConnector::parsePLCData(const QByteArray &data, TagDataType dataType, int bitOffset)
{
    if (data.isEmpty()) return QVariant();
    switch (dataType) {
    case TagDataType::BOOL: {
        quint8 byte = static_cast<quint8>(data[0]);
        if (bitOffset >= 0 && bitOffset < 8)
            return QVariant((byte >> bitOffset) & 0x01);
        else
            return QVariant(byte != 0);
    }
    case TagDataType::REAL: {
        if (data.size() < 4) return QVariant();
        quint32 raw = (static_cast<quint8>(data[0]) << 24) | (static_cast<quint8>(data[1]) << 16) |
                      (static_cast<quint8>(data[2]) << 8) | static_cast<quint8>(data[3]);
        float f;
        memcpy(&f, &raw, sizeof(float));
        return QVariant(f);
    }
    case TagDataType::INT: {
        if (data.size() < 2) return QVariant();
        qint16 val = (static_cast<quint8>(data[0]) << 8) | static_cast<quint8>(data[1]);
        return QVariant(static_cast<int>(val));
    }
    case TagDataType::DWORD: {
        if (data.size() < 4) return QVariant();
        quint32 val = (static_cast<quint8>(data[0]) << 24) | (static_cast<quint8>(data[1]) << 16) |
                      (static_cast<quint8>(data[2]) << 8) | static_cast<quint8>(data[3]);
        return QVariant(static_cast<unsigned int>(val));
    }
    case TagDataType::WORD: {
        if (data.size() < 2) return QVariant();
        quint16 val = (static_cast<quint8>(data[0]) << 8) | static_cast<quint8>(data[1]);
        return QVariant(static_cast<unsigned int>(val));
    }
    case TagDataType::BYTE: {
        if (data.size() < 1) return QVariant();
        return QVariant(static_cast<unsigned int>(static_cast<quint8>(data[0])));
    }
    default:
        qWarning() << "Unknown data type:" << tagDataTypeToString(dataType);
        return QVariant();
    }
}

QVariant PLCConnector::getDefaultValueForType(TagDataType dataType) const
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

QVariant PLCConnector::readTagValue(const TagInfo &tag)
{
    if (!m_client || m_plcConnectionState != PLCConnectionState::CONNECTED) {
        qWarning() << "Cannot read tag: not connected";
        return getDefaultValueForType(tag.dataType);
    }
    int size = getDataSizeForType(tag.dataType);
    QByteArray buffer(size, 0);
    int result = Cli_DBRead(m_client, tag.dbNumber, tag.byteOffset, size, buffer.data());
    if (result != 0) {
        char errorText[256] = {0};
        Cli_ErrorText(result, errorText, sizeof(errorText)-1);
        qWarning() << "DBRead failed for tag" << tag.name << ": " << errorText;
        return getDefaultValueForType(tag.dataType);
    }
    return parsePLCData(buffer, tag.dataType, tag.bitOffset);
}

// ========== 状态管理 ==========
ControllerState PLCConnector::getControllerState() const
{
    QMutexLocker locker(&m_mutex);
    return m_controllerState;
}

PLCConnectionState PLCConnector::getPLCConnectionState() const
{
    QMutexLocker locker(&m_mutex);
    return m_plcConnectionState;
}

void PLCConnector::updateControllerState(ControllerState newState)
{
    ControllerState old = m_controllerState;
    if (old == newState) return;
    if (!isValidStateTransition(old, newState)) {
        qWarning() << "Invalid state transition from" << controllerStateToString(old)
        << "to" << controllerStateToString(newState);
        return;
    }
    {
        QMutexLocker locker(&m_mutex);
        m_controllerState = newState;
    }
    emit controllerStateChanged(newState);
}

void PLCConnector::updatePLCConnectionState(PLCConnectionState newState)
{
    PLCConnectionState old = m_plcConnectionState;
    if (old == newState) return;
    {
        QMutexLocker locker(&m_mutex);
        m_plcConnectionState = newState;
    }
    qDebug() << "PLC Connection State changed:" << plcConnectionStateToString(old) << "->" << plcConnectionStateToString(newState);
    emit plcConnectionStateChanged(newState);
    switch (newState) {
    case PLCConnectionState::CONNECTING: updateControllerState(ControllerState::CONNECTING); break;
    case PLCConnectionState::CONNECTED:  updateControllerState(ControllerState::CONNECTED); break;
    case PLCConnectionState::DISCONNECTING: updateControllerState(ControllerState::SHUTTING_DOWN); break;
    case PLCConnectionState::DISCONNECTED: updateControllerState(ControllerState::DISCONNECTED); break;
    case PLCConnectionState::CONNECTION_FAILED: updateControllerState(ControllerState::STATE_ERROR); break;
    case PLCConnectionState::CONNECTION_LOST: updateControllerState(ControllerState::DISCONNECTED); break;
    default: break;
    }
}

void PLCConnector::setPollInterval(int intervalMs)
{
    m_pollInterval = intervalMs;
    if (m_currentTimerType == TimerType::POLLING && m_stateTimer)
        m_stateTimer->setInterval(m_pollInterval);
}

// 自动重连相关
void PLCConnector::setAutoConnect(bool enable, int intervalMs)
{
    m_autoConnectInterval = intervalMs;
    if (enable) startTimer(TimerType::AUTO_CONNECT, intervalMs);
    else if (m_currentTimerType == TimerType::AUTO_CONNECT) stopTimer();
}

void PLCConnector::startAutoReconnect()
{
    m_currentReconnectAttempts = 0;
    startTimer(TimerType::RECONNECT, m_reconnectInterval);
}

void PLCConnector::stopAutoConnect()
{
    QMutexLocker locker(&m_mutex);
    if (m_currentTimerType == TimerType::AUTO_CONNECT) {
        stopTimer();
        qDebug() << "Auto-connect timer stopped";
    }
    m_currentReconnectAttempts = 0;
    emit autoConnectStopped();
}

void PLCConnector::startHeartbeatCheck()
{
    if (m_heartbeatInterval > 0)
        startTimer(TimerType::HEARTBEAT, m_heartbeatInterval);
}

void PLCConnector::stopHeartbeatCheck()
{
    if (m_currentTimerType == TimerType::HEARTBEAT) stopTimer();
}

void PLCConnector::handleConnectionLost()
{
    if (m_connectionLostHandled) return;
    m_connectionLostHandled = true;
    if (m_plcConnectionState == PLCConnectionState::CONNECTED) {
        updatePLCConnectionState(PLCConnectionState::CONNECTION_LOST);
        emit disconnected();
    }
    if (m_currentTimerType == TimerType::POLLING) stopTimer();
    startAutoReconnect();
}

bool PLCConnector::isPolling() const
{
    QMutexLocker locker(&m_mutex);
    return m_controllerState == ControllerState::POLLING;
}


// ========== 定时器 ==========
void PLCConnector::startTimer(TimerType timerType, int interval)
{
    if (!m_stateTimer) {
        qCritical() << "定时器未初始化，请先调用 initTimers()";
        return;
    }
    if (QThread::currentThread() != m_stateTimer->thread()) {
        m_currentTimerType = timerType;
        QMetaObject::invokeMethod(this, [this, timerType, interval]() {
            if (m_stateTimer->isActive()) m_stateTimer->stop();
            m_currentTimerType = timerType;
            m_stateTimer->setInterval(interval);
            m_stateTimer->start();
        }, Qt::QueuedConnection);
        return;
    }
    if (m_stateTimer->isActive()) m_stateTimer->stop();
    m_currentTimerType = timerType;
    m_stateTimer->setInterval(interval);
    m_stateTimer->start();
}

void PLCConnector::stopTimer()
{
    if (m_stateTimer && m_stateTimer->isActive()) {
        m_stateTimer->stop();
        qDebug() << "Timer stopped:" << timerTypeToString(m_currentTimerType);
        m_currentTimerType = TimerType::NONE;
    }
}

void PLCConnector::onStateTimerTimeout()
{
    switch (m_currentTimerType) {
    case TimerType::HEARTBEAT: checkConnectionHealth(); break;
    case TimerType::AUTO_CONNECT: onAutoConnectTimeout(); break;
    case TimerType::RECONNECT: attemptReconnect(); break;
    case TimerType::POLLING: pollData(); break;
    default: qWarning() << "定时器超时但未指定类型"; break;
    }
}

void PLCConnector::checkConnectionHealth()
{
    if (m_plcConnectionState != PLCConnectionState::CONNECTED) return;
    quint8 testByte = 0;
    int result = Cli_DBRead(m_client, 1, 0, 1, &testByte);
    if (result != 0) {
        qWarning() << "连接健康检查失败，连接可能已丢失";
        handleConnectionLost();
    }
}

void PLCConnector::onAutoConnectTimeout()
{
    qDebug() << "自动连接超时处理，尝试连接到PLC...";
    if (m_plcConnectionState == PLCConnectionState::NOT_CONNECTED)
        connectToPLC(m_ip, m_rack, m_slot);
    else if (m_currentTimerType == TimerType::AUTO_CONNECT)
        stopTimer();
}

void PLCConnector::attemptReconnect()
{
    static int backoffBase = 1000;
    if (m_currentReconnectAttempts == 0) backoffBase = 1000;
    qDebug() << "尝试重连 (" << (m_currentReconnectAttempts+1) << "次)";
    if (connectToPLC(m_ip, m_rack, m_slot)) {
        qInfo() << "自动重连成功";
        stopTimer();
        m_currentReconnectAttempts = 0;
        backoffBase = 1000;
        m_connectionLostHandled = false;
        emit connected();
        return;
    }
    m_currentReconnectAttempts++;
    int next = qMin(backoffBase * (1 << qMin(m_currentReconnectAttempts, 6)), 60000);
    backoffBase = next;
    qWarning() << "重连失败，下次尝试间隔:" << next << "ms";
    startTimer(TimerType::RECONNECT, next);
}

bool PLCConnector::isValidStateTransition(ControllerState from, ControllerState to) const
{
    static const QMap<ControllerState, QSet<ControllerState>> valid = {
                                                                        {ControllerState::DISCONNECTED,   {ControllerState::CONNECTING, ControllerState::STATE_ERROR, ControllerState::POLLING}},
                                                                        {ControllerState::CONNECTING,     {ControllerState::CONNECTED, ControllerState::DISCONNECTED, ControllerState::STATE_ERROR}},
                                                                        {ControllerState::CONNECTED,      {ControllerState::POLLING, ControllerState::DISCONNECTED, ControllerState::STATE_ERROR, ControllerState::SHUTTING_DOWN}},
                                                                        {ControllerState::POLLING,        {ControllerState::CONNECTED, ControllerState::DISCONNECTED, ControllerState::STATE_ERROR}},
                                                                        {ControllerState::STATE_ERROR,    {ControllerState::DISCONNECTED, ControllerState::CONNECTING}},
                                                                        {ControllerState::SHUTTING_DOWN,  {ControllerState::DISCONNECTED}},
                                                                        };
    return valid.value(from).contains(to);
}

QString PLCConnector::timerTypeToString(TimerType type)
{
    switch (type) {
    case TimerType::HEARTBEAT: return "HEARTBEAT";
    case TimerType::AUTO_CONNECT: return "AUTO_CONNECT";
    case TimerType::RECONNECT: return "RECONNECT";
    case TimerType::POLLING: return "POLLING";
    default: return "NONE";
    }
}

bool PLCConnector::checkDBInfo(int dbNumber, int requiredSize)
{
    Q_UNUSED(dbNumber); Q_UNUSED(requiredSize);
    qWarning() << "checkDBInfo is deprecated";
    return true;
}

bool PLCConnector::validatePLCInfo()
{
    qWarning() << "validatePLCInfo is deprecated";
    return true;
}