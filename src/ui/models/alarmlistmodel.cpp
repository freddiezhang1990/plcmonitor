#include "alarmlistmodel.h"
#include <QDateTime>
#include <qcolor.h>

AlarmListModel::AlarmListModel(QObject *parent) : QAbstractTableModel(parent) {}

int AlarmListModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_alarms.size();
}

int AlarmListModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return ColCount;
}

QVariant AlarmListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_alarms.size())
        return QVariant();
    const AlertInfo &alarm = m_alarms.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColId: return alarm.id;
        case ColTime: return alarm.timestamp.toString("yyyy-MM-dd HH:mm:ss");
        case ColTag: return alarm.tagName;
        case ColMessage: return alarm.message;
        case ColValue: return alarm.value.toDouble();
        case ColLevel: return alertLevelToString(alarm.level);
        case ColStatus: return alarm.acknowledged ? "已确认" : "未确认";
        default: return QVariant();
        }
    }
    else if (role == Qt::ForegroundRole) {
        // 已确认的报警统一使用灰色
        if (alarm.acknowledged) {
            return QColor(128, 128, 128); // 灰色
        }
        // 未确认的报警根据级别显示不同颜色
        switch (alarm.level) {
        case AlertLevel::INFO:    return QColor(0, 128, 0);    // 绿色
        case AlertLevel::WARNING: return QColor(255, 165, 0); // 橙色
        case AlertLevel::ERRORAlert:   return QColor(255, 0, 0);    // 红色
        case AlertLevel::CRITICAL:return QColor(128, 0, 128);  // 紫色
        default: return QVariant();
        }
    }
    else if (role == Qt::TextAlignmentRole) {
        if (index.column() == ColValue) return int(Qt::AlignRight | Qt::AlignVCenter);
    }
    return QVariant();
}

QVariant AlarmListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case ColId: return "ID";
        case ColTime: return "时间";
        case ColTag: return "标签";
        case ColMessage: return "消息";
        case ColValue: return "值";
        case ColLevel: return "级别";
        case ColStatus: return "状态";
        default: return QVariant();
        }
    }
    return QVariant();
}

void AlarmListModel::setAlarms(const QList<AlertInfo> &alarms)
{
    beginResetModel();
    m_alarms = alarms;
    if (m_alarms.size() > MAX_ALARMS) {
        m_alarms.erase(m_alarms.begin() + MAX_ALARMS, m_alarms.end());
    }
    endResetModel();
}

void AlarmListModel::addAlarm(const AlertInfo &alarm)
{
    beginInsertRows(QModelIndex(), 0, 0);
    m_alarms.prepend(alarm);
    while (m_alarms.size() > MAX_ALARMS) {
        m_alarms.removeLast();
    }
    endInsertRows();
}

void AlarmListModel::updateAcknowledged(int alarmId)
{
    for (int i = 0; i < m_alarms.size(); ++i) {
        if (m_alarms[i].id == alarmId && !m_alarms[i].acknowledged) {
            m_alarms[i].acknowledged = true;
            emit dataChanged(index(i, ColStatus), index(i, ColStatus));
            break;
        }
    }
}

void AlarmListModel::updateAcknowledgedBatch(const QList<int>& alarmIds)
{
    QSet<int> idSet = QSet<int>(alarmIds.begin(), alarmIds.end());
    for (int i = 0; i < m_alarms.size(); ++i) {
        if (idSet.contains(m_alarms[i].id) && !m_alarms[i].acknowledged) {
            m_alarms[i].acknowledged = true;
            emit dataChanged(index(i, ColStatus), index(i, ColStatus));
        }
    }
}