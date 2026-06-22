#ifndef CHARTMANAGER_H
#define CHARTMANAGER_H

#include <QObject>
#include <QStringList>
#include <QMap>
#include <QVariant>
#include <QVector>
#include <QDateTime>
#include "qcustomplot.h"

class ChartManager : public QObject
{
    Q_OBJECT

public:
    explicit ChartManager(QObject* parent = nullptr);
    ~ChartManager();

    // 图表设置
    void setCustomPlot(QCustomPlot* customPlot);
    void setChartTags(const QStringList& tagKeys);   // tagKey 格式 "deviceId/tagName"
    QStringList getChartTags() const;                // 返回 tagKey 列表

    // 图表样式设置
    void setChartTimeRange(int seconds);
    void setChartLineWidth(int width);
    void setChartBackground(const QColor& color);

    // 图表操作
    void updateChartData(const QMap<QString, QVariant>& data);  // data 的 key 为 tagKey
    void refreshChartsNow();
    void clearChartData();
    void playChart(bool play);
    void exportChartData(const QString& filePath);

    bool isPlaying() const { return m_isPlaying; }

    // 标签管理
    void addChartTag(const QString& tagKey);
    void removeChartTag(const QString& tagKey);
    void clearChartTags();

    // 获取图表数据
    QMap<QString, QVector<double>> getChartData() const;
    QVector<double> getTimeData() const;
    int getDataCount() const;
    QCustomPlot* getCustomPlot() const { return m_customPlot; }

signals:
    void chartDataUpdated(const QMap<QString, QVariant>& data);
    void chartTagsChanged(const QStringList& tagKeys);
    void chartCleared();
    void chartTimeRangeChanged(int seconds);
    void chartPlayStateChanged(bool playing);

public slots:
    void setViewVisible(bool visible);

private:
    void updateChartUI();
    void updateChartXAxis();
    void updateChartYAxis();
    QColor getColorForGraph(int index);
    void applyCachedChartTags();
    void applyChartTagsNow();
    QString getDisplayName(const QString& tagKey) const;   // 将 tagKey 转为显示名称 "[设备名] 标签名"

    QCustomPlot* m_customPlot = nullptr;
    QMap<QString, QVector<double>> m_chartData;   // key = tagKey
    QVector<double> m_timeData;
    QStringList m_chartTags;                      // tagKey 列表

    int m_chartTimeRange = 30;
    int m_maxDataPoints = 600;
    int m_lineWidth = 2;
    bool m_isPlaying = true;
    QDateTime m_startTime;
    QList<QColor> m_graphColors;
    QMap<QString, QCPGraph*> m_graphs;            // key = tagKey
    QTimer* m_updateTimer;
    qint64 m_lastUpdateTime = 0;
    int m_updateCount = 0;
    const int MAX_UPDATES_PER_SECOND = 30;
    bool m_isVisible = true;
};

#endif // CHARTMANAGER_H