#ifndef ALARMLISTMODEL_H
#define ALARMLISTMODEL_H

#include <QAbstractTableModel>
#include <QList>
#include "common_types.h"

class AlarmListModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        ColId,
        ColTime,
        ColTag,
        ColMessage,
        ColValue,
        ColLevel,
        ColStatus,
        ColCount
    };
    explicit AlarmListModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void setAlarms(const QList<AlertInfo> &alarms);
    void addAlarm(const AlertInfo &alarm);
    void updateAcknowledged(int alarmId);
    void updateAcknowledgedBatch(const QList<int>& alarmIds);

    static const int MAX_ALARMS = 1000;  // 最多显示1000条

private:
    QList<AlertInfo> m_alarms;
};

#endif