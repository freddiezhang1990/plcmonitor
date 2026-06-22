// realtimedatamodel.h
#ifndef REALTIMEDATAMODEL_H
#define REALTIMEDATAMODEL_H

#include <QAbstractTableModel>
#include <QDateTime>
#include <QMap>
#include <QVector>
#include <QString>
#include <QMutex>

struct TimeSeriesRecord {
    QDateTime timestamp;
    QMap<QString, QVariant> tagValues;   // key = tagKey ("deviceId/tagName")
    int id;
};

class RealTimeDataModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit RealTimeDataModel(QObject *parent = nullptr);
    ~RealTimeDataModel();

    // QAbstractTableModel 接口
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    // 数据管理
    void addDataRecord(const QMap<QString, QVariant> &tagData);  // tagData key = tagKey
    void setVisibleTags(const QStringList &tagKeys);            // 显示指定 tagKey 的列
    void addTagColumn(const QString &tagKey);
    void removeTagColumn(const QString &tagKey);
    void clearData();

    // 配置
    void setMaxRecords(int max);
    int maxRecords() const;
    void setShowNewestFirst(bool newestFirst);
    bool showNewestFirst() const;

    // 数据导出
    bool exportToCsv(const QString &filename) const;

    // 获取数据
    TimeSeriesRecord getRecord(int row) const;
    QVariant getValue(int row, const QString &tagKey) const;

    // 标签管理
    QStringList allTags() const;            // 所有 tagKey
    QStringList visibleTags() const;        // 当前显示的 tagKey
    bool isTagVisible(const QString &tagKey) const;

    // 数据统计
    int totalRecordCount() const;
    int visibleColumnCount() const;
    QDateTime getOldestTimestamp() const;
    QDateTime getNewestTimestamp() const;

signals:
    void dataAdded();
    void visibleTagsChanged();
    void dataCleared();

private:
    void cleanupOldRecords();
    int getColumnForTag(const QString &tagKey) const;
    void rebuildColumnIndex();
    QString getDisplayName(const QString &tagKey) const;   // 将 tagKey 转为表头显示名称

    QVector<TimeSeriesRecord> m_records;
    mutable QRecursiveMutex m_dataMutex;

    int m_maxRecords = 1000;
    bool m_showNewestFirst = true;
    int m_recordCounter = 0;

    QStringList m_allTags;                    // 所有出现过的 tagKey
    QStringList m_visibleTags;                // 当前显示的 tagKey
    QMap<QString, int> m_tagColumnIndex;     // tagKey -> 列索引 (从1开始)
};

#endif // REALTIMEDATAMODEL_H