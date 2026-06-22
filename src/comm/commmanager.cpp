#include "commmanager.h"
#include "plcdata.h"
#include "s7connector.h"
#include <QDebug>

CommManager::CommManager(QObject *parent)
    : QObject(parent)
{
    qDebug() << "CommManager: Created";
}

CommManager::~CommManager()
{
    disconnectAll();
    for (auto thread : m_threads) {
        thread->quit();
        thread->wait(5000);
        delete thread;
    }
    qDeleteAll(m_connectors);
}

void CommManager::registerFactory(const QString &protocol, ConnectorFactory factory)
{
    m_factories[protocol] = factory;
    qDebug() << "CommManager: Registered factory for protocol" << protocol;
}

void CommManager::buildConnectors(PLCData *plcData)
{
    if (!plcData) { qWarning() << "CommManager::buildConnectors: plcData is null"; return; }

    const auto &devices = plcData->getAllDevices();
    for (auto *cfg : devices) {
        if (!cfg) continue;

        const QString &protocol = cfg->protocol.isEmpty() ? QString("S7") : cfg->protocol;
        if (!m_factories.contains(protocol)) {
            qWarning() << "CommManager: No factory registered for protocol" << protocol
                       << "(device:" << cfg->deviceId << ")";
            continue;
        }

        // 创建线程
        QThread *thread = new QThread(this);
        thread->setObjectName(QString("Comm_%1").arg(cfg->deviceId));
        m_threads[cfg->deviceId] = thread;

        // 创建连接器（parent = nullptr，稍后 moveToThread）
        IConnector *conn = m_factories[protocol](cfg, nullptr);
        if (!conn) {
            qWarning() << "CommManager: Factory failed to create connector for" << cfg->deviceId;
            delete thread;
            m_threads.remove(cfg->deviceId);
            continue;
        }

        // 移到工作线程
        conn->moveToThread(thread);

        // 连接信号
        connect(conn, &IConnector::connected, this, &CommManager::onConnectorConnected);
        connect(conn, &IConnector::disconnected, this, &CommManager::onConnectorDisconnected);
        connect(conn, &IConnector::connectionError, this, &CommManager::onConnectorError);
        connect(conn, &IConnector::connectionStateChanged, this, &CommManager::onConnectorStateChanged);
        connect(conn, &IConnector::tagValueUpdated, this, &CommManager::onTagValueUpdated);
        connect(conn, &IConnector::writeCompleted, this, &CommManager::onWriteCompleted);
        connect(conn, &IConnector::statusMessage, this, &CommManager::onStatusMessage);

        // batchDataUpdated 使用 lambda 添加 deviceId
        QString deviceId = cfg->deviceId;
        connect(conn, &IConnector::batchDataUpdated, this,
                [this, deviceId](const QMap<QString, QVariant> &data) {
                    emit batchDataUpdated(deviceId, data);
                }, Qt::QueuedConnection);

        m_connectors[cfg->deviceId] = conn;

        // 在工作线程中初始化定时器
        connect(thread, &QThread::started, conn, [conn]() {
            conn->initInThread();
        });

        thread->start();
        qDebug() << "CommManager: Built connector for" << cfg->deviceId << "protocol" << protocol;
    }
}

void CommManager::connectAll()
{
    for (auto it = m_connectors.begin(); it != m_connectors.end(); ++it) {
        QMetaObject::invokeMethod(it.value(), "connectToDevice", Qt::QueuedConnection);
    }
}

void CommManager::disconnectAll()
{
    for (auto it = m_connectors.begin(); it != m_connectors.end(); ++it) {
        QMetaObject::invokeMethod(it.value(), "disconnectFromDevice", Qt::QueuedConnection);
    }
}

QList<IConnector::ReadResult> CommManager::readBatch(
    const QString &deviceId, const QList<IConnector::ReadRequest> &requests)
{
    IConnector *conn = m_connectors.value(deviceId, nullptr);
    if (!conn) {
        qWarning() << "CommManager::readBatch: Unknown device" << deviceId;
        return {};
    }
    if (!conn->isConnected()) {
        qWarning() << "CommManager::readBatch: Device not connected" << deviceId;
        return {};
    }
    return conn->readBatch(requests);
}

bool CommManager::writeTag(const QString &deviceId, const TagInfo &tag, const QVariant &value)
{
    IConnector *conn = m_connectors.value(deviceId, nullptr);
    if (!conn) return false;
    return conn->writeTag(tag, value);
}

IConnector* CommManager::connector(const QString &deviceId) const
{
    return m_connectors.value(deviceId, nullptr);
}

QStringList CommManager::deviceIds() const
{
    return m_connectors.keys();
}

bool CommManager::isDeviceConnected(const QString &deviceId) const
{
    IConnector *conn = m_connectors.value(deviceId, nullptr);
    return conn ? conn->isConnected() : false;
}

// -------- 信号转发 slots --------
void CommManager::onConnectorConnected(const QString &deviceId)
{
    emit deviceConnected(deviceId);
}

void CommManager::onConnectorDisconnected(const QString &deviceId)
{
    emit deviceDisconnected(deviceId);
}

void CommManager::onConnectorError(const QString &deviceId, const QString &error)
{
    emit deviceError(deviceId, error);
}

void CommManager::onConnectorStateChanged(const QString &deviceId, PLCConnectionState state)
{
    emit deviceStateChanged(deviceId, state);
}

void CommManager::onBatchDataUpdated(const QMap<QString, QVariant> &data)
{
    // This is not directly usable (no deviceId), use the lambda connect instead
}

void CommManager::onTagValueUpdated(const QString &tagKey, const QVariant &value)
{
    emit tagValueUpdated(tagKey, value);
}

void CommManager::onWriteCompleted(const QString &tagKey, bool success, const QString &error)
{
    emit writeCompleted(tagKey, success, error);
}

void CommManager::onStatusMessage(const QString &deviceId, const QString &message)
{
    emit statusMessage(deviceId, message);
}