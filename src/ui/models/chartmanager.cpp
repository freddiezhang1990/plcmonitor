#include "ChartManager.h"
#include <QDebug>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QFileDialog>
#include <QMessageBox>

ChartManager::ChartManager(QObject* parent)
    : QObject(parent)
    , m_customPlot(nullptr)
    , m_updateTimer(nullptr)
    , m_chartTimeRange(60)  // 默认60秒
    , m_lineWidth(2)
    , m_maxDataPoints(600)  // 默认最多600个点
    , m_isPlaying(true)
    , m_lastUpdateTime(0)
    , m_updateCount(0)
{
    qDebug() << "ChartManager initialized";

    // 初始化图表颜色
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

    m_startTime = QDateTime::currentDateTime();

    m_updateTimer = new QTimer(this);
    m_updateTimer->setSingleShot(true);
    connect(m_updateTimer, &QTimer::timeout, this, &ChartManager::updateChartUI);
}

ChartManager::~ChartManager()
{
    if (m_updateTimer) {
        m_updateTimer->stop();
        delete m_updateTimer;
        m_updateTimer = nullptr;
    }
    qDebug() << "ChartManager destroyed";
}

void ChartManager::setCustomPlot(QCustomPlot* customPlot)
{
    if (m_customPlot != customPlot) {
        m_customPlot = customPlot;

        if (m_customPlot) {
             // 设置图表基本属性
            m_customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
            m_customPlot->axisRect()->setupFullAxesBox(true);

            // 设置坐标轴标签
            m_customPlot->xAxis->setLabel("时间");
            m_customPlot->yAxis->setLabel("值");

            // 设置图例
            m_customPlot->legend->setVisible(true);
            m_customPlot->legend->setBrush(QBrush(QColor(255, 255, 255, 230)));

            // 设置网格
            m_customPlot->xAxis->grid()->setVisible(true);
            m_customPlot->yAxis->grid()->setVisible(true);

            qDebug() << "ChartManager: CustomPlot set successfully";

            // 如果已经有缓存的标签，现在应用它们
            if (!m_chartTags.isEmpty()) {
                qDebug() << "ChartManager: Applying cached chart tags";
                applyCachedChartTags();
            }
        } else {
            qWarning() << "ChartManager: CustomPlot is null!";
        }
    }
}


void ChartManager::setChartTags(const QStringList& tagKeys)
{
    if (m_chartTags == tagKeys) return;
    m_chartTags = tagKeys;
    qDebug() << "ChartManager: Set chart tags (tagKeys):" << tagKeys;

    if (!m_customPlot) {
        qWarning() << "ChartManager::setChartTags: m_customPlot is null, caching tags";
        return;
    }
    applyChartTagsNow();
}

void ChartManager::applyChartTagsNow()
{
    if (!m_customPlot) return;

    m_chartData.clear();
    m_timeData.clear();
    m_customPlot->clearGraphs();
    m_graphs.clear();

    // 重新创建图形并设置图例名称
    int graphIndex = 0;
    for (const QString& tagKey : m_chartTags) {
        QCPGraph* graph = m_customPlot->addGraph();
        m_graphs[tagKey] = graph;
        QColor color = getColorForGraph(graphIndex++);
        graph->setPen(QPen(color, m_lineWidth));
        graph->setName(getDisplayName(tagKey));   // 显示友好名称
        graph->setAdaptiveSampling(true);
    }
    emit chartTagsChanged(m_chartTags);
}

QString ChartManager::getDisplayName(const QString& tagKey) const
{
    // 解析 tagKey "deviceId/tagName"，返回 "[deviceName] tagName" 或直接返回 tagName
    int slash = tagKey.indexOf('/');
    if (slash == -1) return tagKey;
    QString deviceId = tagKey.left(slash);
    QString tagName = tagKey.mid(slash + 1);
    // 可调用 PLCData::getDevice(deviceId)->deviceName，但为了避免循环依赖，暂时只显示 deviceId
    // 若 PLCData 指针可用，可通过信号获取设备名称，此处简化
    return QString("[%1] %2").arg(deviceId, tagName);
}

void ChartManager::applyCachedChartTags()
{
    if (!m_customPlot) {
        qWarning() << "ChartManager::applyCachedChartTags: m_customPlot is null";
        return;
    }

    if (m_chartTags.isEmpty()) {
        return;
    }

    qDebug() << "ChartManager: Applying cached chart tags:" << m_chartTags;
    applyChartTagsNow();
}
QStringList ChartManager::getChartTags() const
{
    return m_chartTags;
}

void ChartManager::setChartTimeRange(int seconds)
{
    if (seconds > 0 && m_chartTimeRange != seconds) {
        m_chartTimeRange = seconds;
        // 假设最坏情况每秒 10 个点，缓存 2 倍时间范围的点数
        int estimatedPoints = seconds * 10 * 2;
        m_maxDataPoints = qMax(estimatedPoints, 10000);
        qDebug() << "时间范围:" << seconds << "秒，缓存点数上限:" << m_maxDataPoints;
        updateChartUI();
        emit chartTimeRangeChanged(seconds);
    }
}

void ChartManager::setChartLineWidth(int width)
{
    if (width > 0 && m_lineWidth != width) {
        m_lineWidth = width;

        // 更新所有图形的线条宽度
        for (QCPGraph* graph : m_graphs) {
            if (graph) {
                QPen pen = graph->pen();
                pen.setWidth(width);
                graph->setPen(pen);
            }
        }

        if (m_customPlot) {
            m_customPlot->replot();
        }
    }
}

void ChartManager::setChartBackground(const QColor& color)
{
    if (m_customPlot) {
        m_customPlot->setBackground(QBrush(color));
        m_customPlot->replot();
    }
}

void ChartManager::updateChartData(const QMap<QString, QVariant>& data)
{
    if (data.isEmpty() || !m_isPlaying) return;

    QDateTime now = QDateTime::currentDateTime();
    double currentTime = now.toMSecsSinceEpoch() / 1000.0;
    m_timeData.append(currentTime);

    // 清理过期数据
    if (m_chartTimeRange > 0) {
        double cutoffTime = currentTime - m_chartTimeRange;
        int removeCount = 0;
        while (removeCount < m_timeData.size() && m_timeData[removeCount] < cutoffTime)
            removeCount++;
        if (removeCount > 0) {
            m_timeData.remove(0, removeCount);
            for (auto& tagData : m_chartData)
                if (tagData.size() > removeCount) tagData.remove(0, removeCount);
                else tagData.clear();
        }
    }

    // 更新数据
    for (const QString& tagKey : m_chartTags) {
        if (!data.contains(tagKey)) continue;
        QVariant value = data[tagKey];
        if (!value.isValid()) continue;
        double doubleValue = value.toDouble();
        QVector<double>& tagData = m_chartData[tagKey];
        tagData.append(doubleValue);
        if (tagData.size() > m_maxDataPoints) tagData.removeFirst();
    }

    // 保持时间数据长度
    if (m_timeData.size() > m_maxDataPoints)
        m_timeData.remove(0, m_timeData.size() - m_maxDataPoints);

    if (!m_updateTimer->isActive())
        m_updateTimer->start(100);
}

void ChartManager::setViewVisible(bool visible) {
    if (m_isVisible != visible) {
        m_isVisible = visible;
        if (m_isVisible) {
            // 视图变为可见，启动定时器
            if (!m_updateTimer->isActive()) {
                m_updateTimer->start();
            }
        } else {
            // 视图变为不可见，停止定时器
            m_updateTimer->stop();
        }
    }
}

void ChartManager::refreshChartsNow()
{
    qDebug() << "ChartManager: Refreshing charts immediately";
    updateChartUI();
}

void ChartManager::clearChartData()
{
    m_chartData.clear();
    m_timeData.clear();

    if (m_customPlot) {
        m_customPlot->clearGraphs();
        m_graphs.clear();

        // 重置X轴范围
        m_customPlot->xAxis->setRange(0, m_chartTimeRange);
        m_customPlot->replot();
    }

    m_startTime = QDateTime::currentDateTime();
    emit chartCleared();
}

void ChartManager::playChart(bool play)
{
    m_isPlaying = play;
    emit chartPlayStateChanged(play);

    if (play && m_customPlot) {
        m_customPlot->replot();
    }
}

void ChartManager::exportChartData(const QString& filePath)
{
    if (m_timeData.isEmpty() || m_chartData.isEmpty()) {
        qWarning() << "ChartManager: No data to export";
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "ChartManager: Failed to open file for writing:" << filePath;
        return;
    }

    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");  // Qt5
#else
    out.setEncoding(QStringConverter::Utf8);  // Qt6
#endif

    // 写入表头
    out << "Time";
    for (const QString& tag : m_chartTags) {
        out << "," << tag;
    }
    out << "\n";

    // 写入数据
    for (int i = 0; i < m_timeData.size(); ++i) {
        out << m_timeData[i];

        for (const QString& tag : m_chartTags) {
            if (i < m_chartData[tag].size()) {
                out << "," << m_chartData[tag][i];
            } else {
                out << ","; // 空值
            }
        }
        out << "\n";
    }

    file.close();
    qDebug() << "ChartManager: Data exported to" << filePath;
}

void ChartManager::addChartTag(const QString& tagKey)
{
    if (!m_chartTags.contains(tagKey)) {
        m_chartTags.append(tagKey);
        qDebug() << "ChartManager: Added chart tag:" << tagKey;
        emit chartTagsChanged(m_chartTags);
    }
}

void ChartManager::removeChartTag(const QString& tagKey)
{
    if (m_chartTags.contains(tagKey)) {
        m_chartTags.removeAll(tagKey);
        m_chartData.remove(tagKey);
        m_graphs.remove(tagKey);
        qDebug() << "ChartManager: Removed chart tag:" << tagKey;
        emit chartTagsChanged(m_chartTags);
    }
}

void ChartManager::clearChartTags()
{
    if (!m_chartTags.isEmpty()) {
        m_chartTags.clear();
        m_chartData.clear();
        m_graphs.clear();
        qDebug() << "ChartManager: Cleared all chart tags";
        emit chartTagsChanged(m_chartTags);
    }
}

QMap<QString, QVector<double>> ChartManager::getChartData() const
{
    return m_chartData;
}

QVector<double> ChartManager::getTimeData() const
{
    return m_timeData;
}

int ChartManager::getDataCount() const
{
    return m_timeData.size();
}

// 私有方法实现
void ChartManager::updateChartUI()
{
    if (!m_isVisible || !m_customPlot) {
        return;
    }

    static QElapsedTimer timer;
    static qint64 lastWarningTime = 0;
    timer.start();

    // 1. 删除不再使用的图形
    QStringList existingTags = m_graphs.keys();
    for (const QString& tag : existingTags) {
        if (!m_chartTags.contains(tag) || !m_chartData.contains(tag)) {
            if (QCPGraph* graph = m_graphs.take(tag)) {
                m_customPlot->removeGraph(graph);
            }
        }
    }
    int startIdx = 0;
    // 2. 获取当前时间范围
    if (m_timeData.isEmpty()) {
        double currentTime = m_timeData.last();
        double startTime = currentTime - m_chartTimeRange;

        while (startIdx < m_timeData.size() && m_timeData[startIdx] < startTime)
            startIdx++;
        // 没有数据，直接重绘清空
        m_customPlot->replot();
        return;
    }
    const int MAX_POINTS_PER_GRAPH = 1000; // 最大显示点数

    int graphIndex = 0;
    for (const QString& tagKey : m_chartTags) {
        if (!m_chartData.contains(tagKey)) continue;
        const QVector<double>& valueData = m_chartData[tagKey];
        if (valueData.isEmpty()) continue;

        // 截取时间范围内的数据
        QVector<double> slicedTime = m_timeData.mid(startIdx);
        QVector<double> slicedValue = valueData.mid(startIdx);
        if (slicedTime.isEmpty()) continue;

        // 下采样
        if (slicedTime.size() > MAX_POINTS_PER_GRAPH) {
            QVector<double> sampledTime, sampledValue;
            double step = (double)slicedTime.size() / MAX_POINTS_PER_GRAPH;
            for (int i = 0; i < MAX_POINTS_PER_GRAPH; ++i) {
                int idx = qRound(i * step);
                if (idx >= slicedTime.size()) idx = slicedTime.size() - 1;
                sampledTime << slicedTime[idx];
                sampledValue << slicedValue[idx];
            }
            slicedTime = sampledTime;
            slicedValue = sampledValue;
        }

        QCPGraph* graph = m_graphs.value(tagKey, nullptr);
        if (!graph) {
            graph = m_customPlot->addGraph();
            m_graphs[tagKey] = graph;
            QColor color = getColorForGraph(graphIndex);
            graph->setPen(QPen(color, m_lineWidth));
            graph->setName(getDisplayName(tagKey));
        }
        graph->setData(slicedTime, slicedValue);
        graphIndex++;
    }

    // 3. 更新坐标轴
    updateChartXAxis();
    updateChartYAxis();

    // 4. 重绘图表
    m_customPlot->replot();

    qint64 elapsed = timer.elapsed();
    if (elapsed > 50) {
        qint64 currentTimeMs = QDateTime::currentMSecsSinceEpoch();
        if (currentTimeMs - lastWarningTime > 5000) {
            qWarning() << "ChartManager::updateChartUI: UI update took" << elapsed << "ms";
            lastWarningTime = currentTimeMs;
        }
    }
}

void ChartManager::updateChartXAxis()
{
    if (!m_customPlot) {
        qWarning() << "ChartManager: m_customPlot is null";
        return;
    }

    if (m_timeData.isEmpty()) {
        m_customPlot->xAxis->setRange(0, m_chartTimeRange);
        m_customPlot->replot();
        return;
    }

    // 获取当前时间范围
    double currentTime = m_timeData.last();
    double startTime = currentTime - m_chartTimeRange;

    // 如果数据不足指定时间范围，从第一个数据点开始
    if (m_timeData.first() > startTime) {
        startTime = m_timeData.first();
    }

    // 设置X轴范围
    m_customPlot->xAxis->setRange(startTime, currentTime);

    // 设置时间刻度格式（QCustomPlot 2.x版本）
    QSharedPointer<QCPAxisTickerDateTime> dateTimeTicker(new QCPAxisTickerDateTime);
    dateTimeTicker->setDateTimeFormat("hh:mm:ss");

    // 设置刻度间距（根据时间范围自动调整）
    if (m_chartTimeRange <= 60) {  // 1分钟内
        dateTimeTicker->setTickCount(6);
    } else if (m_chartTimeRange <= 300) {  // 5分钟内
        dateTimeTicker->setTickCount(6);
    } else if (m_chartTimeRange <= 1800) {  // 30分钟内
        dateTimeTicker->setTickCount(7);
    } else {  // 30分钟以上
        dateTimeTicker->setTickCount(8);
    }

    m_customPlot->xAxis->setTicker(dateTimeTicker);
    m_customPlot->xAxis->setLabel("时间");

    // 重新绘制图表
   // m_customPlot->replot();

}

void ChartManager::updateChartYAxis()
{
    if (!m_customPlot || m_chartData.isEmpty()) return;

    // 查找所有数据的最小值和最大值
    double minValue = std::numeric_limits<double>::max();
    double maxValue = std::numeric_limits<double>::lowest();

    for (const auto& data : m_chartData) {
        if (data.isEmpty()) continue;

        for (double value : data) {
            if (value < minValue) minValue = value;
            if (value > maxValue) maxValue = value;
        }
    }

    // 如果所有值都相同，设置一个合理的范围
    if (minValue == maxValue) {
        minValue -= 1.0;
        maxValue += 1.0;
    }

    // 添加一些边距
    double margin = (maxValue - minValue) * 0.1;
    m_customPlot->yAxis->setRange(minValue - margin, maxValue + margin);
}

QColor ChartManager::getColorForGraph(int index)
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