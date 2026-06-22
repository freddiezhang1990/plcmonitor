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
    QDateTime timestamp;                     // 时间戳
    QMap<QString, QVariant> tagValues;       // 各标签的值
    int id;                                  // 记录ID（用于排序）
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

    // 排序
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    // 数据管理接口
    void addDataRecord(const QMap<QString, QVariant> &tagData);
    void setVisibleTags(const QStringList &tags);
    void addTagColumn(const QString &tagName);
    void removeTagColumn(const QString &tagName);
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
    QVariant getValue(int row, const QString &tagName) const;

    // 标签管理
    QStringList allTags() const;
    QStringList visibleTags() const;
    bool isTagVisible(const QString &tagName) const;

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
    // 数据存储
    QVector<TimeSeriesRecord> m_records;
    mutable QRecursiveMutex m_dataMutex;

    // 配置
    int m_maxRecords = 1000;                    // 最大记录数
    bool m_showNewestFirst = true;              // 最新数据在上方
    int m_recordCounter = 0;                    // 记录计数器

    // 标签管理
    QStringList m_allTags;                      // 所有可用的标签
    QStringList m_visibleTags;                  // 当前显示的标签
    QMap<QString, int> m_tagColumnIndex;        // 标签名 -> 列索引映射

    // 辅助方法
    void cleanupOldRecords();
    int getColumnForTag(const QString &tagName) const;
    void rebuildColumnIndex();
};

#endif // REALTIMEDATAMODEL_H