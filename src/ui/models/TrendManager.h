#ifndef TRENDMANAGER_H
#define TRENDMANAGER_H

#include <QObject>
#include <QStringList>
#include <QMap>
#include <QVariant>
#include <QDateTime>
#include <QPair>
#include "qcustomplot.h"
#include "databasemanager.h"

class TrendManager : public QObject
{
    Q_OBJECT

public:
    explicit TrendManager(QObject* parent = nullptr);
    ~TrendManager();

    void setCustomPlot(QCustomPlot* customPlot);
    void setTrendTags(const QStringList& tagKeys);   // tagKey 列表
    QStringList getTrendTags() const;

    void setHistoryTimeRange(int seconds);
    void setHistoryLineWidth(int width);

    void updateTrendData(const QMap<QString, QVariant>& data);  // data key = tagKey
    void updateHistoryChart();
    void refreshTrendsNow();
    void clearTrendData();

    QList<QPair<QDateTime, QVariant>> getTrendHistory(const QString& tagKey, int maxPoints = 1000) const;
    void exportHistoryData(const QString& filePath);
    void plotHistoryQueryResult(const QList<DatabaseManager::HistoryResult>& results);  // results 中 tagName 已是 tagKey

signals:
    void trendDataUpdated(const QMap<QString, QVariant>& data);
    void trendTagsChanged(const QStringList& tagKeys);
    void historyChartUpdated();
    void historyDataUpdated(const QString& tagKey, const QVariant& value, const QDateTime& timestamp);

private:
    void updateHistoryChartUI();
    QColor getColorForHistoryGraph(int index);
    void setupHistoryChartAxes();
    void updateMinMax(double x, double y, bool& first, double& minX, double& maxX, double& minY, double& maxY);
    void setupHistoryChartAxesForRange(double minX, double maxX, double minY, double maxY);
    QString getDisplayName(const QString& tagKey) const;   // 显示友好名称

    QCustomPlot* m_historyCustomPlot = nullptr;
    QMap<QString, QList<QPair<QDateTime, QVariant>>> m_trendHistory;  // key = tagKey
    QMap<QString, QCPGraph*> m_historyGraphs;                         // key = tagKey
    QStringList m_trendTags;                                           // tagKey 列表

    int m_historyTimeRange = 3600;
    int m_maxHistoryPoints = 3600;
    int m_lineWidth = 2;
    QList<QColor> m_graphColors;
};

#endif // TRENDMANAGER_H