// realtimedatamodel.cpp
#include "realtimedatamodel.h"
#include <QFile>
#include <QTextStream>
#include <algorithm>

RealTimeDataModel::RealTimeDataModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

RealTimeDataModel::~RealTimeDataModel()
{
    QMutexLocker locker(&m_dataMutex);
    m_records.clear();
}

int RealTimeDataModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;

    QMutexLocker locker(&m_dataMutex);
    return m_records.size();
}

int RealTimeDataModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;

    QMutexLocker locker(&m_dataMutex);
    // 第一列是时间戳，后面是各个标签
    return 1 + m_visibleTags.size();
}

QVariant RealTimeDataModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return QVariant();

    QMutexLocker locker(&m_dataMutex);

    int row = index.row();
    int col = index.column();

    if (row < 0 || row >= m_records.size())
        return QVariant();

    const TimeSeriesRecord &record = m_records[row];

    // 第一列是时间戳
    if (col == 0) {
        return record.timestamp.toString("yyyy-MM-dd HH:mm:ss.zzz");
    }

    // 获取对应的标签名
    int tagIndex = col - 1;
    if (tagIndex >= 0 && tagIndex < m_visibleTags.size()) {
        QString tagName = m_visibleTags[tagIndex];
        if (record.tagValues.contains(tagName)) {
            return record.tagValues[tagName];
        }
    }

    return QVariant();
}

QVariant RealTimeDataModel::headerData(int section, Qt::Orientation orientation,
                                       int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();

    if (orientation == Qt::Horizontal) {
        QMutexLocker locker(&m_dataMutex);

        if (section == 0) {
            return tr("时间戳");
        }

        int tagIndex = section - 1;
        if (tagIndex >= 0 && tagIndex < m_visibleTags.size()) {
            return m_visibleTags[tagIndex];
        }
    } else if (orientation == Qt::Vertical) {
        // 显示行号
        return QString::number(section + 1);
    }

    return QVariant();
}

void RealTimeDataModel::sort(int column, Qt::SortOrder order)
{
    QMutexLocker locker(&m_dataMutex);

    if (m_records.isEmpty()) return;

    beginResetModel();

    if (column == 0) {  // 按时间戳排序
        std::sort(m_records.begin(), m_records.end(),
                  [order](const TimeSeriesRecord &a, const TimeSeriesRecord &b) {
                      if (order == Qt::AscendingOrder) {
                          return a.timestamp < b.timestamp;
                      } else {
                          return a.timestamp > b.timestamp;
                      }
                  });
    }

    endResetModel();
}

void RealTimeDataModel::addDataRecord(const QMap<QString, QVariant> &tagData)
{
    if (tagData.isEmpty()) return;

    QMutexLocker locker(&m_dataMutex);

    // 更新时间戳
    QDateTime timestamp = QDateTime::currentDateTime();

    // 创建新记录
    TimeSeriesRecord newRecord;
    newRecord.timestamp = timestamp;
    newRecord.tagValues = tagData;
    newRecord.id = ++m_recordCounter;

    // 更新所有标签列表
    for (auto it = tagData.constBegin(); it != tagData.constEnd(); ++it) {
        if (!m_allTags.contains(it.key())) {
            m_allTags.append(it.key());
        }
    }

    // 添加记录
    int insertPosition = m_showNewestFirst ? 0 : m_records.size();

    beginInsertRows(QModelIndex(), insertPosition, insertPosition);
    m_records.insert(insertPosition, newRecord);
    endInsertRows();

    // 清理旧记录
    if (m_maxRecords > 0 && m_records.size() > m_maxRecords) {
        cleanupOldRecords();
    }

    locker.unlock();
    emit dataAdded();
}

void RealTimeDataModel::cleanupOldRecords()
{
    int removeCount = m_records.size() - m_maxRecords;
    if (removeCount <= 0) return;

    if (m_showNewestFirst) {
        // 移除最旧的数据（在末尾）
        beginRemoveRows(QModelIndex(), m_maxRecords, m_records.size() - 1);
        m_records.remove(m_maxRecords, removeCount);
        endRemoveRows();
    } else {
        // 移除最旧的数据（在开头）
        beginRemoveRows(QModelIndex(), 0, removeCount - 1);
        m_records.remove(0, removeCount);
        endRemoveRows();
    }
}

void RealTimeDataModel::setVisibleTags(const QStringList &tags)
{
    QMutexLocker locker(&m_dataMutex);

    m_visibleTags = tags;
    rebuildColumnIndex();

    locker.unlock();

    // 通知视图列数已改变
    beginResetModel();
    endResetModel();

    emit visibleTagsChanged();
}

void RealTimeDataModel::addTagColumn(const QString &tagName)
{
    if (m_visibleTags.contains(tagName))
        return;

    QMutexLocker locker(&m_dataMutex);

    m_visibleTags.append(tagName);
    rebuildColumnIndex();

    locker.unlock();

    // 通知视图有列被添加
    int newColumn = 1 + m_visibleTags.size() - 1;  // 时间戳列之后
    beginInsertColumns(QModelIndex(), newColumn, newColumn);
    endInsertColumns();

    emit visibleTagsChanged();
}

void RealTimeDataModel::removeTagColumn(const QString &tagName)
{
    int index = m_visibleTags.indexOf(tagName);
    if (index == -1) return;

    QMutexLocker locker(&m_dataMutex);

    m_visibleTags.removeAt(index);
    rebuildColumnIndex();

    locker.unlock();

    // 通知视图有列被移除
    int removeColumn = 1 + index;  // 时间戳列之后
    beginRemoveColumns(QModelIndex(), removeColumn, removeColumn);
    endRemoveColumns();

    emit visibleTagsChanged();
}

void RealTimeDataModel::rebuildColumnIndex()
{
    m_tagColumnIndex.clear();
    for (int i = 0; i < m_visibleTags.size(); ++i) {
        m_tagColumnIndex[m_visibleTags[i]] = 1 + i;  // 列索引 = 1（时间戳）+ i
    }
}

void RealTimeDataModel::setMaxRecords(int max)
{
    QMutexLocker locker(&m_dataMutex);
    m_maxRecords = max;

    // 如果当前记录数超过新的最大值，立即清理旧记录
    if (m_maxRecords > 0 && m_records.size() > m_maxRecords) {
        cleanupOldRecords();
    }

    qDebug() << "设置最大记录数:" << m_maxRecords;
}

void RealTimeDataModel::setShowNewestFirst(bool newestFirst)
{
    QMutexLocker locker(&m_dataMutex);

    if (m_showNewestFirst != newestFirst) {
        m_showNewestFirst = newestFirst;

        // 如果已存在数据，需要重新排序
        if (!m_records.isEmpty()) {
            // 触发视图重新排序
            beginResetModel();
            // 如果需要，可以在这里重新排序m_records
            endResetModel();
        }
    }

    qDebug() << "设置显示顺序 - 最新数据在上方:" << m_showNewestFirst;
}

// 清理数据
void RealTimeDataModel::clearData()
{
    QMutexLocker locker(&m_dataMutex);

    if (!m_records.isEmpty()) {
        beginResetModel();
        m_records.clear();
        m_recordCounter = 0;
        endResetModel();

        emit dataCleared();
    }
}

// 导出到CSV
bool RealTimeDataModel::exportToCsv(const QString &filename) const
{
    QMutexLocker locker(&m_dataMutex);

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "无法打开文件写入:" << filename;
        return false;
    }

    QTextStream out(&file);

    // 写入表头
    out << "时间戳";
    for (const QString &tag : m_visibleTags) {
        out << "," << tag;
    }
    out << "\n";

    // 写入数据
    for (const TimeSeriesRecord &record : m_records) {
        out << record.timestamp.toString("yyyy-MM-dd HH:mm:ss.zzz");

        for (const QString &tag : m_visibleTags) {
            out << ",";
            if (record.tagValues.contains(tag)) {
                out << record.tagValues[tag].toString();
            }
        }
        out << "\n";
    }

    file.close();
    return true;
}