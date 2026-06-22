#include "TrendManager.h"
#include <QDebug>

TrendManager::TrendManager(QObject* parent)
    : QObject(parent)
{
    qDebug() << "TrendManager initialized";
}

TrendManager::~TrendManager()
{
    qDebug() << "TrendManager destroyed";
}

void TrendManager::setTrendTags(const QStringList& tagKeys)
{
    if (m_trendTags != tagKeys) {
        m_trendTags = tagKeys;
        qDebug() << "TrendManager: Set trend tags (tagKeys):" << tagKeys;
        emit trendTagsChanged(tagKeys);
    }
}

// 在 trendmanager.cpp 中添加以下方法

void TrendManager::setCustomPlot(QCustomPlot* customPlot)
{
    if (m_historyCustomPlot != customPlot) {
        m_historyCustomPlot = customPlot;

        if (m_historyCustomPlot) {
            // 设置历史图表基本属性
            m_historyCustomPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
            m_historyCustomPlot->axisRect()->setupFullAxesBox(true);

            // 设置坐标轴标签
            m_historyCustomPlot->xAxis->setLabel("时间");
            m_historyCustomPlot->yAxis->setLabel("值");

            // 设置图例
            m_historyCustomPlot->legend->setVisible(true);
            m_historyCustomPlot->legend->setBrush(QBrush(QColor(255, 255, 255, 230)));

            // 设置网格
            m_historyCustomPlot->xAxis->grid()->setVisible(true);
            m_historyCustomPlot->yAxis->grid()->setVisible(true);

            // 初始化颜色列表
            m_graphColors = {
                QColor(255, 0, 0),       // 红色
                QColor(0, 255, 0),       // 绿色
                QColor(0, 0, 255),       // 蓝色
                QColor(255, 255, 0),     // 黄色
                QColor(255, 0, 255),     // 紫色
                QColor(0, 255, 255),     // 青色
                QColor(255, 128, 0),     // 橙色
                QColor(128, 0, 255)      // 紫色
            };

            qDebug() << "TrendManager: History chart plot set successfully";
        }
    }
}

void TrendManager::setHistoryTimeRange(int seconds)
{
    if (seconds > 0 && m_historyTimeRange != seconds) {
        m_historyTimeRange = seconds;
        m_maxHistoryPoints = seconds;  // 假设每秒1个点

        // 如果数据超过新范围，截断数据
        QDateTime currentTime = QDateTime::currentDateTime();
        QDateTime cutoffTime = currentTime.addSecs(-m_historyTimeRange);

        for (auto& historyList : m_trendHistory) {
            // 移除超过时间范围的数据
            while (!historyList.isEmpty() && historyList.first().first < cutoffTime) {
                historyList.removeFirst();
            }
        }

        // 更新图表显示
        updateHistoryChartUI();
    }
}

void TrendManager::setHistoryLineWidth(int width)
{
    if (width > 0) {
        m_lineWidth = width;

        // 更新所有图形的线条宽度
        for (QCPGraph* graph : m_historyGraphs) {
            if (graph) {
                QPen pen = graph->pen();
                pen.setWidth(width);
                graph->setPen(pen);
            }
        }

        if (m_historyCustomPlot) {
            m_historyCustomPlot->replot();
        }
    }
}

void TrendManager::updateHistoryChart()
{
    qDebug() << "TrendManager: Updating history chart";
    updateHistoryChartUI();
}

void TrendManager::updateHistoryChartUI()
{
    if (!m_historyCustomPlot || m_trendTags.isEmpty()) {
        return;
    }

    // 清除现有图形
    m_historyCustomPlot->clearGraphs();
    m_historyGraphs.clear();

    QDateTime currentTime = QDateTime::currentDateTime();
    QDateTime startTime = currentTime.addSecs(-m_historyTimeRange);

    int graphIndex = 0;

    for (const QString& tagKey : m_trendTags) {
        if (m_trendHistory.contains(tagKey) && !m_trendHistory[tagKey].isEmpty()) {
            QCPGraph* graph = m_historyCustomPlot->addGraph();
            m_historyGraphs[tagKey] = graph;

            // 设置线条颜色
            QColor color = getColorForHistoryGraph(graphIndex++);
            graph->setPen(QPen(color, m_lineWidth));
            graph->setName(tagKey);

            // 准备数据
            QVector<double> xData, yData;
            const auto& history = m_trendHistory[tagKey];

            for (const auto& point : history) {
                if (point.first >= startTime) {
                    double timeValue = point.first.toMSecsSinceEpoch() / 1000.0;  // 转换为秒
                    double dataValue = point.second.toDouble();
                    xData.append(timeValue);
                    yData.append(dataValue);
                }
            }

            // 设置数据
            if (!xData.isEmpty() && !yData.isEmpty()) {
                graph->setData(xData, yData);
            }
        }
    }

    // 更新坐标轴
    setupHistoryChartAxes();

    // 重新绘制
    m_historyCustomPlot->replot();
    emit historyChartUpdated();
}

void TrendManager::setupHistoryChartAxes()
{
    if (!m_historyCustomPlot) return;

    QDateTime currentTime = QDateTime::currentDateTime();
    QDateTime startTime = currentTime.addSecs(-m_historyTimeRange);

    // 计算X轴范围
    double startX = startTime.toMSecsSinceEpoch() / 1000.0;
    double endX = currentTime.toMSecsSinceEpoch() / 1000.0;
    double xRange = endX - startX;

    if (xRange <= 0) {
        xRange = 1.0;
    }

    m_historyCustomPlot->xAxis->setRange(startX - xRange * 0.05, endX + xRange * 0.05);

    // 计算Y轴范围
    double minValue = std::numeric_limits<double>::max();
    double maxValue = std::numeric_limits<double>::lowest();

    for (const QString& tagKey : m_trendTags) {
        if (m_trendHistory.contains(tagKey)) {
            const auto& history = m_trendHistory[tagKey];
            for (const auto& point : history) {
                if (point.first >= startTime) {
                    double value = point.second.toDouble();
                    if (value < minValue) minValue = value;
                    if (value > maxValue) maxValue = value;
                }
            }
        }
    }

    // 如果所有值都相同，设置一个合理的范围
    if (minValue == maxValue) {
        minValue -= 1.0;
        maxValue += 1.0;
    }

    // 添加边距
    double margin = (maxValue - minValue) * 0.1;
    m_historyCustomPlot->yAxis->setRange(minValue - margin, maxValue + margin);
}

QColor TrendManager::getColorForHistoryGraph(int index)
{
    if (index >= 0 && index < m_graphColors.size()) {
        return m_graphColors[index];
    }

    // 如果颜色不够，生成随机颜色
    static bool seeded = false;
    if (!seeded) {
        srand(QTime::currentTime().msec());
        seeded = true;
    }

    return QColor(rand() % 256, rand() % 256, rand() % 256);
}

void TrendManager::exportHistoryData(const QString& filePath)
{
    if (m_trendHistory.isEmpty()) {
        qWarning() << "TrendManager: No history data to export";
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "TrendManager: Failed to open file for writing:" << filePath;
        return;
    }

    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");  // Qt5
#else
    out.setEncoding(QStringConverter::Utf8);  // Qt6
#endif

    // 写入表头
    out << "Timestamp";
    for (const QString& tagKey : m_trendTags) {
        out << "," << tagKey;
    }
    out << "\n";

    // 收集所有时间点
    QSet<QDateTime> allTimestamps;
    for (const auto& history : m_trendHistory) {
        for (const auto& point : history) {
            allTimestamps.insert(point.first);
        }
    }

    // 按时间排序
    QList<QDateTime> sortedTimestamps = allTimestamps.values();
    std::sort(sortedTimestamps.begin(), sortedTimestamps.end());

    // 写入数据
    for (const QDateTime& timestamp : sortedTimestamps) {
        out << timestamp.toString("yyyy-MM-dd hh:mm:ss.zzz");

        for (const QString& tagKey : m_trendTags) {
            if (m_trendHistory.contains(tagKey)) {
                const auto& history = m_trendHistory[tagKey];
                bool found = false;

                // 查找对应时间点的值
                for (const auto& point : history) {
                    if (point.first == timestamp) {
                        out << "," << point.second.toString();
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    out << ",";  // 空值
                }
            } else {
                out << ",";  // 空值
            }
        }
        out << "\n";
    }

    file.close();
    qDebug() << "TrendManager: History data exported to" << filePath;
}

QStringList TrendManager::getTrendTags() const
{
    return m_trendTags;
}

void TrendManager::updateTrendData(const QMap<QString, QVariant>& data)
{
    if (data.isEmpty()) return;
    QDateTime timestamp = QDateTime::currentDateTime();
    QMap<QString, QVariant> trendData;

    for (const QString& tagKey : m_trendTags) {
        if (data.contains(tagKey)) {
            QVariant value = data[tagKey];
            trendData[tagKey] = value;

            if (!m_trendHistory.contains(tagKey))
                m_trendHistory[tagKey] = QList<QPair<QDateTime, QVariant>>();
            m_trendHistory[tagKey].append(qMakePair(timestamp, value));
            if (m_trendHistory[tagKey].size() > m_maxHistoryPoints)
                m_trendHistory[tagKey].removeFirst();
            emit historyDataUpdated(tagKey, value, timestamp);
        }
    }
    if (!trendData.isEmpty())
        emit trendDataUpdated(trendData);
}

void TrendManager::refreshTrendsNow()
{
    qDebug() << "TrendManager: Refreshing trends immediately";

    // 发送最新的趋势数据
    if (!m_trendTags.isEmpty()) {
        QMap<QString, QVariant> latestData;

        for (const QString& tagKey : m_trendTags) {
            if (!m_trendHistory[tagKey].isEmpty()) {
                latestData[tagKey] = m_trendHistory[tagKey].last().second;
            }
        }

        if (!latestData.isEmpty()) {
            emit trendDataUpdated(latestData);
        }
    }
}

QList<QPair<QDateTime, QVariant>> TrendManager::getTrendHistory(const QString& tagName, int maxPoints) const
{
    if (m_trendHistory.contains(tagName)) {
        const auto& history = m_trendHistory[tagName];
        int startIndex = qMax(0, history.size() - maxPoints);
        return history.mid(startIndex);
    }
    return QList<QPair<QDateTime, QVariant>>();
}

void TrendManager::clearTrendData()
{
    m_trendHistory.clear();
    qDebug() << "TrendManager: Cleared all trend data";
}

void TrendManager::plotHistoryQueryResult(const QList<DatabaseManager::HistoryResult>& results)
{
    if (!m_historyCustomPlot) return;
    m_historyCustomPlot->clearGraphs();
    m_historyGraphs.clear();

    const int MAX_POINTS = 2000;
    QVector<QColor> colors = {Qt::red, Qt::blue, Qt::green, Qt::magenta, Qt::cyan, Qt::darkYellow, Qt::darkCyan, Qt::darkMagenta};
    double minY = std::numeric_limits<double>::max(), maxY = std::numeric_limits<double>::lowest();
    double minX = 0, maxX = 0;
    bool first = true;
    int colorIndex = 0;

    for (const auto& res : results) {
        if (res.timestamps.isEmpty()) continue;
        QVector<double> xData, yData;
        int total = res.timestamps.size();

        if (total > MAX_POINTS) {
            double step = (double)total / MAX_POINTS;
            for (int i = 0; i < MAX_POINTS; ++i) {
                int idx = qRound(i * step);
                if (idx >= total) idx = total - 1;
                double x = res.timestamps[idx].toSecsSinceEpoch();
                double y = res.values[idx];
                xData << x; yData << y;
                updateMinMax(x, y, first, minX, maxX, minY, maxY);
            }
        } else {
            for (int i = 0; i < total; ++i) {
                double x = res.timestamps[i].toSecsSinceEpoch();
                double y = res.values[i];
                xData << x; yData << y;
                updateMinMax(x, y, first, minX, maxX, minY, maxY);
            }
        }

        QCPGraph* graph = m_historyCustomPlot->addGraph();
        m_historyGraphs[res.tagName] = graph;   // res.tagName 已经是 tagKey
        graph->setPen(QPen(colors[colorIndex % colors.size()], m_lineWidth));
        graph->setName(getDisplayName(res.tagName));
        graph->setData(xData, yData);
        graph->setAdaptiveSampling(true);
        colorIndex++;
    }

    setupHistoryChartAxesForRange(minX, maxX, minY, maxY);
    m_historyCustomPlot->setNoAntialiasingOnDrag(true);
    m_historyCustomPlot->replot();
    emit historyChartUpdated();
}

QString TrendManager::getDisplayName(const QString& tagKey) const
{
    int slash = tagKey.indexOf('/');
    if (slash == -1) return tagKey;
    return QString("[%1] %2").arg(tagKey.left(slash), tagKey.mid(slash+1));
}

// 辅助函数，更新 min/max
void TrendManager::updateMinMax(double x, double y, bool& first,
                                double& minX, double& maxX,
                                double& minY, double& maxY)
{
    if (first) {
        minX = maxX = x;
        minY = maxY = y;
        first = false;
    } else {
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
    }
}

// 设置坐标轴范围（基于实际数据范围）
void TrendManager::setupHistoryChartAxesForRange(double minX, double maxX,
                                                 double minY, double maxY)
{
    if (!m_historyCustomPlot) return;

    if (maxX > minX) {
        double marginX = (maxX - minX) * 0.05;
        m_historyCustomPlot->xAxis->setRange(minX - marginX, maxX + marginX);
    } else {
        m_historyCustomPlot->xAxis->setRange(minX - 1, maxX + 1);
    }

    if (maxY > minY) {
        double marginY = (maxY - minY) * 0.1;
        m_historyCustomPlot->yAxis->setRange(minY - marginY, maxY + marginY);
    } else {
        m_historyCustomPlot->yAxis->setRange(minY - 1, maxY + 1);
    }

    // 设置 x 轴为时间格式
    QSharedPointer<QCPAxisTickerDateTime> dateTimeTicker(new QCPAxisTickerDateTime);
    double rangeSec = maxX - minX;
    if (rangeSec > 86400) {
        dateTimeTicker->setDateTimeFormat("MM-dd HH:mm");
    } else if (rangeSec > 3600) {
        dateTimeTicker->setDateTimeFormat("HH:mm");
    } else {
        dateTimeTicker->setDateTimeFormat("HH:mm:ss");
    }
    m_historyCustomPlot->xAxis->setTicker(dateTimeTicker);
    m_historyCustomPlot->xAxis->setLabel("时间");
    m_historyCustomPlot->yAxis->setLabel("值");
}