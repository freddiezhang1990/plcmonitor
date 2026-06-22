#ifndef COMMMANAGER_H
#define COMMMANAGER_H

#include <QObject>
#include <QMap>
#include <QThread>
#include <functional>
#include "iconnector.h"
#include "common_types.h"

class PLCData;
class DeviceConfig;

class CommManager : public QObject
{
    Q_OBJECT

public:
    using ConnectorFactory = std::function<IConnector*(DeviceConfig*, QObject*)>;
    explicit CommManager(QObject *parent = nullptr);
    ~CommManager() override;

    void registerFactory(const QString &protocol, ConnectorFactory factory);
    void buildConnectors(PLCData *plcData);
    void connectAll();
    void disconnectAll();
    QList<IConnector::ReadResult> readBatch(
        const QString &deviceId,
        const QList<IConnector::ReadRequest> &requests);
    bool writeTag(const QString &deviceId, const TagInfo &tag, const QVariant &value);
    IConnector* connector(const QString &deviceId) const;
    QStringList deviceIds() const;
    bool isDeviceConnected(const QString &deviceId) const;

signals:
    void deviceConnected(const QString &deviceId);
    void deviceDisconnected(const QString &deviceId);
    void deviceError(const QString &deviceId, const QString &error);
    void deviceStateChanged(const QString &deviceId, PLCConnectionState state);
    void batchDataUpdated(const QString &deviceId, const QMap<QString, QVariant> &data);
    void tagValueUpdated(const QString &tagKey, const QVariant &value);
    void writeCompleted(const QString &tagKey, bool success, const QString &error);
    void statusMessage(const QString &deviceId, const QString &message);

private slots:
    void onConnectorConnected(const QString &deviceId);
    void onConnectorDisconnected(const QString &deviceId);
    void onConnectorError(const QString &deviceId, const QString &error);
    void onConnectorStateChanged(const QString &deviceId, PLCConnectionState state);
    void onBatchDataUpdated(const QMap<QString, QVariant> &data);
    void onTagValueUpdated(const QString &tagKey, const QVariant &value);
    void onWriteCompleted(const QString &tagKey, bool success, const QString &error);
    void onStatusMessage(const QString &deviceId, const QString &message);

private:
    QMap<QString, IConnector*> m_connectors;
    QMap<QString, QThread*>    m_threads;
    QMap<QString, ConnectorFactory> m_factories;
};

#endif