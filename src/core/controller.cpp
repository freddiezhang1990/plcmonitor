// controller.cpp
#include "controller.h"
#include "chartmanager.h"
//#include "processviewmanager.h"
#include "realtimedatamodel.h"
#include "databasemanager.h"
#include <QDebug>
#include <QTimer>
#include <qthread.h>
#include <QSettings>      // 用于 saveTagSelectionsToConfig() 等方法
#include <QFile>         // 用于日志文件操作
#include <QTextStream>
// 添加 QtConcurrent 头文件
#include <QtConcurrent/QtConcurrent>
#include <QRandomGenerator>
// 如果 QtConcurrent 不可用，可以使用 QFuture
#include <QFuture>

Controller::Controller(PLCData* dataModel, CommManager* commManager, QObject* parent)
    : QObject(parent)
    ,  m_plcData(dataModel)   // 修复：使用传入的dataModel
    , m_commManager(commManager)
    , m_isBatchWriting(false)
    , m_currentBatchIndex(0)
    , m_totalBatchItems(0)
    , m_nextAlertId(0)
    , m_writeRetryCount(3)
    , m_writeRetryInterval(50)
    , m_initialized(false)
    , m_autoUpdateSubscription(true)  // 新增初始化
{

    // 连接信号
 //  initialize();  //在application中完成
    initializeAlarm();

    // 验证连接
 //   verifyConnections();
}

Controller::~Controller()
{
    // 清理资源

}

bool Controller::initializeAlarm()
{
    if (!m_plcData) {
        qWarning() << "Controller::initializeAlarm: m_plcData is null";
        return false;
    }

    int totalDevices = m_plcData->getAllDevices().size();
    qDebug() << "=== initializeAlarm: 开始加载报警配置，设备数量:" << totalDevices;

    int loadedCount = 0;
    for (DeviceConfig* dev : m_plcData->getAllDevices()) {
        if (!dev) continue;
        qDebug() << "  设备:" << dev->deviceId << "(" << dev->deviceName << ")";
        std::function<void(TagGroup*)> collect = [&](TagGroup* group) {
            if (!group) return;
            for (const TagInfo& tag : group->tags) {
                bool hasAlert = (tag.alertConfig.minEnabled || tag.alertConfig.maxEnabled);
                qDebug() << "    标签:" << tag.name
                         << "minEnabled=" << tag.alertConfig.minEnabled
                         << "minValue=" << tag.alertConfig.minValue
                         << "maxEnabled=" << tag.alertConfig.maxEnabled
                         << "maxValue=" << tag.alertConfig.maxValue;
                if (hasAlert) {
                    QString tagKey = m_plcData->makeTagKey(dev->deviceId, tag.name);
                    m_alertConfigs[tagKey] = tag.alertConfig;
                    loadedCount++;
                    qDebug() << "      -> 已加载报警配置，tagKey:" << tagKey;
                }
            }
            for (TagGroup* sub : group->subGroups) collect(sub);
        };
        for (TagGroup* root : dev->rootGroups) collect(root);
    }

    qDebug() << "=== initializeAlarm: 共加载" << loadedCount << "个报警配置，m_alertConfigs 大小:" << m_alertConfigs.size();
    return true;
}

bool Controller::initialize()
{
    qDebug() << "=== Controller::initialize() 开始 ===";

    if (m_initialized) {
        qDebug() << "Controller 已经初始化，跳过";
        return true;
    }

    qDebug() << "检查 m_dataModel:" << (void*)m_plcData;
    qDebug() << "检查 m_commManager:" << (void*)m_commManager;

    if (!m_plcData) {
        qCritical() << "❌ Controller初始化失败: dataModel 为 nullptr";
        return false;
    }

    if (!m_commManager) {
        qCritical() << "❌ Controller初始化失败: commManager 为 nullptr";
        return false;
    }

    qDebug() << "✓ 数据模型和连接器检查通过";

    // ===== 1. 连接 CommManager 信号（原有） =====
    bool allConnected = true;

    bool connected2 = connect(m_commManager, &CommManager::writeCompleted,
                              this, &Controller::onPLCWriteCompleted, Qt::QueuedConnection);
    if (!connected2) {
        qWarning() << "无法连接writeCompleted信号";
        allConnected = false;
    }

    bool connected3 = connect(m_commManager, &CommManager::deviceConnected,
                              this, [this](const QString &deviceId) {
                                  qDebug() << "PLC设备连接成功:" << deviceId;
                                  // 不再调用 startPolling()，因为订阅模式会处理
                                  // m_commManager->startPolling();
                              }, Qt::QueuedConnection);
    if (!connected3) {
        qWarning() << "无法连接connected信号";
        allConnected = false;
    }

    bool connected4 = connect(m_commManager, &CommManager::deviceDisconnected,
                              this, [this](const QString &deviceId) {
                                  qDebug() << "PLC设备连接断开:" << deviceId;
                              }, Qt::QueuedConnection);
    if (!connected4) {
        qWarning() << "无法连接disconnected信号";
        allConnected = false;
    }

    bool connected5 = connect(m_commManager, &CommManager::deviceError,
                              this, [this](const QString &deviceId, const QString &error) {
                                  qWarning() << "PLC设备错误[" << deviceId << "]:" << error;
                                  emit statusMessage("PLC连接错误: " + error);
                              }, Qt::QueuedConnection);
    if (!connected5) {
        qWarning() << "无法连接connectionError信号";
        allConnected = false;
    }

    bool connected6 = connect(m_commManager, &CommManager::statusMessage,
                              this, [this](const QString &deviceId, const QString &message) {
                                  emit statusMessage(message);
                              }, Qt::QueuedConnection);
    if (!connected6) {
        qWarning() << "无法连接statusMessage信号";
        allConnected = false;
    }

    if (!allConnected) {
        qCritical() << "Controller信号连接失败";
        return false;
    }

    // ===== 2. 创建订阅定时器（从 initializeviewselection 合并） =====
    m_updateTimer = new QTimer(this);
    m_updateCycle = 1000;
    m_updateTimer->setInterval(m_updateCycle);
    connect(m_updateTimer, &QTimer::timeout, this, &Controller::onUpdateTimer);

    // ===== 3. CommManager 已就绪，使用订阅轮询模式（从 initializeviewselection 合并） =====
    if (m_commManager) {
        // m_commManager already manages polling via readBatch()
    }

    // ===== 4. 连接 PLCData 的视图选择信号并同步初始选择（从 initializeviewselection 合并） =====
    if (m_plcData) {
        connect(m_plcData, &PLCData::viewSelectionChanged,
                this, &Controller::onTagSelectionChanged, Qt::QueuedConnection);

        // 主动同步当前所有视图的选中标签
        for (int i = 0; i < static_cast<int>(ViewType::VIEW_COUNT); ++i) {
            ViewType type = static_cast<ViewType>(i);
            QStringList tags = m_plcData->getSelectedTagKeysForView(type);
            if (!tags.isEmpty()) {
                onTagSelectionChanged(type, tags);
            }
        }
    }
    // ===== 6. 标记为已初始化 =====
    m_initialized = true;

    qDebug() << "=== Controller::initialize() 完成 ===";
    return true;
}

// 在 controller.cpp 文件末尾添加
void Controller::onWriteRequest(const QString& tagKey, const QVariant& value)
{
    qDebug() << "接收到写入请求:" << tagKey << "=" << value;

    // 调用单个标签写入方法
    writeSingleTag(tagKey, value);
}

void Controller::onSelectionChanged(ViewType type, const QStringList& tags)
{
    qDebug() << "视图选择改变:" << static_cast<int>(type) << tags;

    // ✅ 统一通过 onTagSelectionChanged 处理
    onTagSelectionChanged(type, tags);
}


// ✅ 添加通用的数据过滤方法
QMap<QString, QVariant> Controller::filterData(const QMap<QString, QVariant>& data, const QStringList& tags)
{
    QMap<QString, QVariant> filtered;
    for (const QString& tag : tags) {
        if (data.contains(tag)) {
            filtered[tag] = data[tag];
        }
    }
    return filtered;
}

// 订阅视图
// 订阅视图
void Controller::subscribeToView(ViewType type, const QStringList& tags, int interval)
{
    if (tags.isEmpty()) {
        qDebug() << "[Controller] 标签列表为空，不创建订阅";
        return;
    }

    // 检查是否已存在订阅
    if (m_subscriptions.contains(type)) {
        qDebug() << "[Controller] 更新现有订阅:" << getViewTypeName(type);
        Subscription& sub = m_subscriptions[type];
        sub.tags = tags;
        sub.updateInterval = (interval > 0) ? interval : getDefaultIntervalForView(type);
        sub.lastUpdate = QDateTime::currentDateTime();
    } else {
        qDebug() << "[Controller] 创建新订阅:" << getViewTypeName(type);
        Subscription sub;
        sub.viewType = type;
        sub.tags = tags;
        sub.updateInterval = (interval > 0) ? interval : getDefaultIntervalForView(type);
        sub.lastUpdate = QDateTime::currentDateTime();

        m_subscriptions[type] = sub;
    }

    qDebug() << "订阅视图" << getViewTypeName(type)
             << "标签数:" << tags.size()
             << "更新间隔:" << m_subscriptions[type].updateInterval << "ms";

    // 启动更新定时器（如果未启动）
    if (!m_updateTimer->isActive()) {
        qDebug() << "启动订阅定时器，间隔:" << m_updateCycle << "ms";
        m_updateTimer->start(m_updateCycle);
    }

    // 当前读取的标签
    updatePlcSubscription(type, tags);
}

void Controller::unsubscribeFromView(ViewType viewType)
{
    auto it = m_subscriptions.find(viewType);
    if (it != m_subscriptions.end()) {
        QStringList tags = it->tags;   // 复制一份，避免日志时访问可能已失效的数据
        m_subscriptions.erase(it);
        qDebug() << "[Controller] Unsubscribed from view type:"
                 << static_cast<int>(viewType)
                 << "with tags:" << tags;
    }
}

// 获取所有订阅的标签（去重）
QStringList Controller::getAllSubscribedTags()
{
    QSet<QString> allTags;

    for (const Subscription& sub : m_subscriptions) {
        for (const QString& tag : sub.tags) {
            allTags.insert(tag);
        }
    }

    return allTags.values();
}

// controller.cpp
int Controller::getDefaultIntervalForView(ViewType type) const
{
    switch (type) {
    case ViewType::TABLE_VIEW:
        return 1000;    // 表格视图：100ms，最频繁更新
    case ViewType::CHART_VIEW:
        return 1000;    // 图表视图：200ms
    case ViewType::PROCESS_VIEW:
        return 500;    // 工艺视图：500ms
    case ViewType::TREND_VIEW:
        return 1000;   // 趋势视图：1000ms
    default:
        return 1000;   // 默认：1000ms
    }
}

// controller.cpp 中实现
void Controller::setRealTimeModel(RealTimeDataModel* model)
{
    qDebug() << "[Controller] 设置实时数据模型:" << (model ? "有效指针" : "nullptr");
    m_realTimeModel = model;
}

void Controller::setChartManager(ChartManager* manager)
{
    qDebug() << "[Controller] 设置图表管理器:" << (manager ? "有效指针" : "nullptr");
    m_chartManager = manager;
}

void Controller::setTrendManager(TrendManager* manager)
{
    qDebug() << "[Controller] 设置趋势管理器:" << (manager ? "有效指针" : "nullptr");
    m_trendManager = manager;
}

void Controller::setProcessViewManager(ProcessViewManager* manager)
{
    qDebug() << "[Controller] 设置工艺流程管理器:" << (manager ? "有效指针" : "nullptr");
    m_processViewManager = manager;
}

// 生成批量读取请求
QMap<int, QList<IConnector::ReadRequest>>
Controller::getBatchReadRequests(const QStringList& tagKeys)
{
    QMap<int, QList<IConnector::ReadRequest>> requests;

    for (const QString& tagKey : tagKeys) {
        // 解析 tagKey 获取 deviceId 和 tagName
        int slashPos = tagKey.indexOf('/');
        if (slashPos == -1) {
            qWarning() << "Invalid tagKey format:" << tagKey;
            continue;
        }
        QString deviceId = tagKey.left(slashPos);
        QString tagName = tagKey.mid(slashPos + 1);

        TagInfo* tag = m_plcData->findTag(deviceId, tagName);
        if (!tag) {
            qWarning() << "Tag not found for tagKey:" << tagKey;
            continue;
        }

        IConnector::ReadRequest request;
        request.tagKey  = tagKey;      // 存储 tagKey
        request.tagInfo = *tag;        // 拷贝 TagInfo 值
        requests[tag->dbNumber].append(request);
    }
    return requests;
}

// 定时器触发更新
void Controller::onUpdateTimer()
{
    static int timerCount = 0;
    timerCount++;

    if (!m_commManager) {
        return;
    }
    // Check if any device is connected
    bool anyConnected = false;
    for (const QString& id : m_commManager->deviceIds()) {
        if (m_commManager->isDeviceConnected(id)) {
            anyConnected = true;
            break;
        }
    }
    if (!anyConnected) {
        return;
    }

    if (m_subscriptions.isEmpty()) {
        if (timerCount % 10 == 0) {
            qDebug() << "定时器触发但无订阅，当前订阅数:" << m_subscriptions.size();
        }
        return;
    }

    QDateTime now = QDateTime::currentDateTime();
    bool needUpdate = false;

    // 检查是否有任何订阅需要更新
    for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
        if (it->lastUpdate.msecsTo(now) >= it->updateInterval) {
            needUpdate = true;
            break;
        }
    }

    if (needUpdate) {
        performOptimizedPolling();  // 读取所有订阅标签的并集，分发到所有视图
        // 更新所有订阅的最后更新时间
        for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
            it->lastUpdate = now;
        }
    }
}

// 执行优化轮询
void Controller::performOptimizedPolling()
{
    if (!m_commManager) {
        return;
    }
    // Check if any device is connected
    bool anyConnected = false;
    for (const QString& id : m_commManager->deviceIds()) {
        if (m_commManager->isDeviceConnected(id)) {
            anyConnected = true;
            break;
        }
    }
    if (!anyConnected) {
        return;
    }

    QStringList allTags = getAllSubscribedTags();
    if (allTags.isEmpty()) {
        return;
    }

    // 按设备分组请求，并通过 CommManager 按设备读取
    QMap<int, QList<IConnector::ReadRequest>> requestsByDB =
        getBatchReadRequests(allTags);

    // 将所有 DB 分组的请求按 deviceId 重新分组
    QMap<QString, QList<IConnector::ReadRequest>> requestsByDevice;
    for (auto it = requestsByDB.begin(); it != requestsByDB.end(); ++it) {
        for (const auto& req : it.value()) {
            int slashPos = req.tagKey.indexOf('/');
            if (slashPos != -1) {
                QString devId = req.tagKey.left(slashPos);
                requestsByDevice[devId].append(req);
            }
        }
    }

    // 逐设备读取
    QList<IConnector::ReadResult> results;
    for (auto it = requestsByDevice.begin(); it != requestsByDevice.end(); ++it) {
        QList<IConnector::ReadResult> deviceResults =
            m_commManager->readBatch(it.key(), it.value());
        results.append(deviceResults);
    }

    distributeDataToViews(results);
}

// 分发数据到视图
void Controller::distributeDataToViews(const QList<IConnector::ReadResult>& results)
{
    QMap<QString, QVariant> allData;   // key = tagKey
    int successCount = 0;

    for (const IConnector::ReadResult& result : results) {
        if (result.success && result.value.isValid()) {
            allData[result.tagKey] = result.value;   // result.tagName 已经是 tagKey
            successCount++;
            // 更新 PLCData 中的标签值（通过 tagKey 解析设备ID和标签名）
            if (m_plcData) {
                QString tagKey = result.tagKey;
                int slashPos = tagKey.indexOf('/');
                if (slashPos != -1) {
                    QString deviceId = tagKey.left(slashPos);
                    QString tagName = tagKey.mid(slashPos + 1);
                    m_plcData->updateTagValue(deviceId, tagName, result.value);
                } else {
                    // 兼容旧格式（不应出现）
                    qWarning() << "Invalid tagKey format:" << tagKey;
                }
            }
            // 数据库记录
            if (m_plcData && m_plcData->isDbEnabled(result.tagKey)) {
                DbDataPoint point;
                quint64 ms = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
                point.dataId = (ms << 20) | (QRandomGenerator::global()->generate() & 0xFFFFF);
                point.tagName = result.tagKey;   // 存储 tagKey
                point.value = result.value.toDouble();
                point.quality = 0;
                point.timestamp = QDateTime::currentDateTimeUtc();
                DatabaseManager::instance().addDataPoint(point);
            }
        } else {
            qWarning() << "标签读取失败:" << result.tagKey << "错误:" << result.error;
        }
    }

    if (successCount == 0) return;

    // ========== 新增：报警检测 ==========
    checkAlerts(allData);
    // =================================

    // 分发数据到各个视图
    for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
        ViewType viewType = it.key();
        const Subscription& sub = it.value();

        // 提取该视图需要的标签数据（tagKey 列表）
        QMap<QString, QVariant> viewData;
        for (const QString& tagKey : sub.tags) {
            if (allData.contains(tagKey)) {
                viewData[tagKey] = allData[tagKey];
            }
        }

        if (!viewData.isEmpty()) {
            switch (viewType) {
            case ViewType::TABLE_VIEW:
                emit tableDataUpdated(viewData);
                break;
            case ViewType::CHART_VIEW:
                emit chartDataUpdated(viewData);
                break;
            case ViewType::PROCESS_VIEW:
                emit processDataUpdated(viewData);
                break;
            case ViewType::TREND_VIEW:
                emit trendDataUpdated(viewData);
                break;
            default:
                break;
            }
        }
    }

    // 更新统计信息
    static int totalUpdates = 0;
    totalUpdates++;
    if (totalUpdates % 10 == 0) {
        // qDebug() << "优化轮询统计: 总标签" << allTags.size()
        //          << "成功" << successCount
        //          << "失败" << (results.size() - successCount);
    }
}

bool Controller::parseTagKey(const QString& tagKey, QString& deviceId, QString& tagName) const
{
    int slash = tagKey.indexOf('/');
    if (slash == -1) {
        qWarning() << "Invalid tagKey format:" << tagKey;
        return false;
    }
    deviceId = tagKey.left(slash);
    tagName = tagKey.mid(slash + 1);
    return true;
}

// 实际的PLC写入方法（内部使用）—— 通过 CommManager 路由到正确的设备连接器
bool Controller::writeTagToPLC(const QString& tagKey, const QVariant& value)
{
    if (!m_commManager) {
        qWarning() << "写入失败: CommManager 未初始化";
        return false;
    }

    // 解析 tagKey "deviceId/tagName"
    int slashPos = tagKey.indexOf('/');
    if (slashPos == -1) {
        qWarning() << "无效的 tagKey 格式:" << tagKey;
        return false;
    }
    QString deviceId = tagKey.left(slashPos);
    QString tagName = tagKey.mid(slashPos + 1);

    // 检查设备连接状态
    if (!m_commManager->isDeviceConnected(deviceId)) {
        qWarning() << "写入失败: 设备" << deviceId << "未连接";
        return false;
    }

    // 获取标签信息（从 PLCData 中通过设备ID和标签名查找）
    if (!m_plcData) {
        qWarning() << "PLCData 为空";
        return false;
    }
    TagInfo* tagInfo = m_plcData->findTag(deviceId, tagName);
    if (!tagInfo || tagInfo->name.isEmpty()) {
        qWarning() << "标签未找到:" << tagKey;
        return false;
    }

    // 通过 CommManager 写入，自动路由到正确的设备连接器
    bool success = m_commManager->writeTag(deviceId, *tagInfo, value);
    if (!success) {
        qWarning() << "写入失败:" << tagKey << "值:" << value;
    }
    return success;
}

// 单个标签写入 - 带有重试机制
bool Controller::writeSingleTag(const QString& tagKey, const QVariant& value)
{
    if (!m_commManager) {
        emit singleWriteCompleted(tagKey, false, "CommManager未初始化");
        return false;
    }
    if (value.isNull() || !value.isValid()) {
        emit singleWriteCompleted(tagKey, false, "写入值无效");
        return false;
    }

    qDebug() << "开始写入单个标签:" << tagKey << "=" << value;
    emit singleWriteStarted(tagKey, value);

    bool success = false;
    QString errorMsg;
    for (int retry = 0; retry < m_writeRetryCount && !success; ++retry) {
        if (retry > 0) {
            qDebug() << "写入重试 (" << retry << "/" << m_writeRetryCount << "):" << tagKey;
            QThread::msleep(m_writeRetryInterval);
        }
        success = writeTagToPLC(tagKey, value);   // 调用新版本
        if (!success) {
            errorMsg = QString("写入失败 (尝试 %1/%2)").arg(retry + 1).arg(m_writeRetryCount);
        }
    }

    if (success) {
        qDebug() << "写入成功:" << tagKey << "=" << value;
        emit singleWriteCompleted(tagKey, true, "");
        // 触发数据轮询
        QTimer::singleShot(50, this, [this]() {
            performOptimizedPolling();
        });
    } else {
        qWarning() << "写入失败:" << tagKey << "-" << errorMsg;
        emit singleWriteCompleted(tagKey, false, errorMsg);
    }
    return success;
}

// 批量标签写入
bool Controller::writeBatchTags(const QMap<QString, QVariant>& tagValueMap)
{
    if (!m_commManager) {
        qWarning() << "无法写入: CommManager未初始化";
        emit batchWriteCompleted(QMap<QString, bool>(), "CommManager未初始化");
        return false;
    }

    if (tagValueMap.isEmpty()) {
        qWarning() << "批量写入: 标签列表为空";
        emit batchWriteCompleted(QMap<QString, bool>(), "标签列表为空");
        return false;
    }

    if (m_isBatchWriting) {
        qWarning() << "批量写入正在进行中，请等待完成";
        emit batchWriteCompleted(QMap<QString, bool>(), "批量写入正在进行中");
        return false;
    }

    qDebug() << "开始批量写入" << tagValueMap.size() << "个标签";
    emit batchWriteStarted(tagValueMap);

    // 设置批量写入状态
    m_isBatchWriting = true;
    m_pendingBatchWrites = tagValueMap;
    m_batchWriteResults.clear();
    m_currentBatchIndex = 0;
    m_totalBatchItems = tagValueMap.size();

    // 开始批量写入处理
    processNextBatchWrite();

    return true;
}

// 处理下一个批量写入项
void Controller::processNextBatchWrite()
{
    if (m_currentBatchIndex >= m_totalBatchItems) {
        finishBatchWrite();
        return;
    }

    // 获取当前要写入的标签 (tagKey -> value)
    auto it = m_pendingBatchWrites.constBegin();
    for (int i = 0; i < m_currentBatchIndex && it != m_pendingBatchWrites.constEnd(); ++i) {
        ++it;
    }
    if (it == m_pendingBatchWrites.constEnd()) {
        finishBatchWrite();
        return;
    }

    QString currentTagKey = it.key();
    QVariant currentValue = it.value();

    emit writeProgressChanged(m_currentBatchIndex, m_totalBatchItems);

    qDebug() << "批量写入进度: (" << (m_currentBatchIndex + 1) << "/" << m_totalBatchItems
             << ") 标签:" << currentTagKey;

    // 直接调用 writeTagToPLC，传递 tagKey
    bool success = writeTagToPLC(currentTagKey, currentValue);

    // 记录结果
    m_batchWriteResults[currentTagKey] = success;

    // 处理下一个
    m_currentBatchIndex++;
    QTimer::singleShot(20, this, &Controller::processNextBatchWrite);
}

// 批量写入完成处理
void Controller::finishBatchWrite()
{
    m_isBatchWriting = false;

    // 统计结果
    int successCount = 0;
    int failCount = 0;

    for (bool result : m_batchWriteResults) {
        if (result) successCount++;
        else failCount++;
    }

    QString resultSummary = QString("批量写入完成: 成功 %1 个, 失败 %2 个")
                                .arg(successCount).arg(failCount);

    qDebug() << resultSummary;

    // 发送完成信号
    emit batchWriteCompleted(m_batchWriteResults, resultSummary);

    // 清除临时数据
    m_pendingBatchWrites.clear();

    // 如果至少有一个写入成功，触发数据更新
    if (successCount > 0) {
        QTimer::singleShot(100, this, [this]() {
            performOptimizedPolling();
        });
    }
}

// 处理单个写入完成信号
void Controller::onSingleWriteCompleted(const QString& tagName, bool success, const QString& error)
{
    // 这里可以处理写入完成后的额外逻辑
    // 比如记录日志、更新UI状态等

    qDebug() << "写入完成 - 标签:" << tagName
             << "结果:" << (success ? "成功" : "失败")
             << "错误:" << error;
}

void Controller::checkAlerts(const QMap<QString, QVariant>& data)
{
    QDateTime now = QDateTime::currentDateTime();
    for (auto it = data.begin(); it != data.end(); ++it) {
        QString tagKey = it.key();
        QVariant value = it.value();
        if (!m_alertConfigs.contains(tagKey)) continue;

        const AlertConfig& config = m_alertConfigs[tagKey];
        bool isActive = false;
        QString message;
        AlertLevel level = config.level;

        double val = value.toDouble();
        if (config.minEnabled && val < config.minValue) {
            isActive = true;
            message = QString("%1 低于下限 %2 (当前 %3)").arg(tagKey).arg(config.minValue).arg(val);
        } else if (config.maxEnabled && val > config.maxValue) {
            isActive = true;
            message = QString("%1 超过上限 %2 (当前 %3)").arg(tagKey).arg(config.maxValue).arg(val);
        }

        bool previousState = m_alertActiveStates.value(tagKey, false);
        QDateTime lastTrigger = m_alertLastTriggerTime.value(tagKey);
        int debounceMs = 1000; // 可配置

        if (isActive && !previousState && (!lastTrigger.isValid() || lastTrigger.msecsTo(now) >= debounceMs)) {
            // 触发报警
            AlertInfo alert;
            alert.id = ++m_nextAlertId;
            alert.timestamp = now;
            alert.tagName = tagKey;
            alert.message = message;
            alert.value = value;
            alert.level = level;
            alert.acknowledged = false;
            emit alertTriggered(alert);
            emit saveAlarmToDatabase(alert);
            m_alertActiveStates[tagKey] = true;
            m_alertLastTriggerTime[tagKey] = now;
        } else if (!isActive && previousState) {
            // 恢复报警
            AlertInfo recovery;
            recovery.id = ++m_nextAlertId;
            recovery.timestamp = now;
            recovery.tagName = tagKey;
            recovery.message = QString("%1 恢复正常，当前值 %2").arg(tagKey).arg(val);
            recovery.level = AlertLevel::INFO;
            emit alertTriggered(recovery);
            emit saveAlarmToDatabase(recovery);
            m_alertActiveStates[tagKey] = false;
        }
    }
}


void Controller::updateProcessStates(const QMap<QString, QVariant>& data)
{
    // 根据关键设备数据更新流程状态
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        QString tagName = it.key();
        QVariant value = it.value();

        // 根据具体业务逻辑更新流程状态
        if (tagName.startsWith("MOTOR_")) {
            bool isRunning = value.toBool();
            QString motorId = tagName.mid(6); // 去掉"MOTOR_"前缀

            if (m_processStates.contains(motorId)) {
                ProcessState oldState = m_processStates[motorId];
                ProcessState newState = isRunning ? ProcessState::RUNNING : ProcessState::STOPPED;

                if (oldState != newState) {
                    m_processStates[motorId] = newState;
                    emit processStateChanged(motorId, newState);
                }
            }
        }
    }
}

void Controller::addChartTag(const QString& tagName)
{
    if (!m_plcData) {
        qWarning() << "PLCData 为空，无法添加图表标签";
        return;
    }

    // ✅ 通过 PLCData 管理
    QStringList currentTags = m_plcData->getSelectedTagKeysForView(ViewType::CHART_VIEW);
    if (!currentTags.contains(tagName)) {
        currentTags.append(tagName);
        m_plcData->setViewSelection(ViewType::CHART_VIEW, currentTags);
        qDebug() << "图表标签添加成功:" << tagName;

        emit chartSelectionChanged(tagName, true);
    }
}

void Controller::removeChartTag(const QString& tagName)
{
    if (!m_plcData) {
        qWarning() << "PLCData 为空，无法移除图表标签";
        return;
    }

    // ✅ 通过 PLCData 管理
    QStringList currentTags = m_plcData->getSelectedTagKeysForView(ViewType::CHART_VIEW);
    if (currentTags.contains(tagName)) {
        currentTags.removeAll(tagName);
        m_plcData->setViewSelection(ViewType::CHART_VIEW, currentTags);
        qDebug() << "图表标签移除成功:" << tagName;

        emit chartSelectionChanged(tagName, false);
    }
}

// controller.cpp
void Controller::stopController()
{
    qDebug() << "Controller::stopController() 被调用";

    // 1. 停止轮询
    if (m_commManager) {
        qDebug() << "停止轮询...";
        // stopPolling managed by CommManager internally

        // 可以添加状态检查
     //   if (m_commManager->isPolling()) {
      //      qWarning() << "轮询可能没有立即停止，等待中...";
     //   }
    }

    // 2. 断开连接
    if (m_commManager) {
        qDebug() << "断开PLC连接...";
        m_commManager->disconnectAll();

        if (disconnect()) {
            qDebug() << "PLC连接已成功断开";
        } else {
            qWarning() << "断开PLC连接失败";
        }
    }

    // 3. 清理实时数据

    // 4. 清除选择状态

    // 5. 停止批量写入
    if (m_isBatchWriting) {
        qDebug() << "中止批量写入操作...";
        m_isBatchWriting = false;
        m_pendingBatchWrites.clear();
        m_batchWriteResults.clear();
    }

    // 6. 断开连接
    if (m_plcData) {
        m_plcData->clearViewSelection(ViewType::TABLE_VIEW);
        m_plcData->clearViewSelection(ViewType::CHART_VIEW);
        m_plcData->clearViewSelection(ViewType::PROCESS_VIEW);
        m_plcData->clearViewSelection(ViewType::TREND_VIEW);
    }

    if (m_commManager) {
        disconnect(m_commManager, nullptr, this, nullptr);
    }

    // 7. 重置状态变量
    m_currentBatchIndex = 0;
    m_totalBatchItems = 0;
    m_nextAlertId = 0;

    qDebug() << "Controller 停止完成";
}

// 在Controller中添加信号连接验证
void Controller::verifyConnections()
{
    // 验证CommManager信号
    if (!m_commManager) {
        qCritical() << "CommManager is null";
        return;
    }

    // 检查信号是否存在
    const QMetaObject* meta = m_commManager->metaObject();

    QStringList requiredSignals = {
        "deviceConnected(QString)",
        "deviceDisconnected(QString)",
        "deviceError(QString,QString)",
        "writeCompleted(QString,bool,QString)",
        "batchDataUpdated(QString,QMap<QString,QVariant>)"
    };

    for (const QString& signal : requiredSignals) {
        int index = meta->indexOfSignal(QMetaObject::normalizedSignature(signal.toLatin1()));
        if (index < 0) {
            qWarning() << "Missing signal in CommManager:" << signal;
        } else {
            qDebug() << "Signal found:" << signal;
        }
    }

    // 检查连接状态

}

void Controller::onTagSelectionChanged(ViewType type, const QStringList& tagKeys)
{
    // 1. 保存选择到 PLCData 模型（如果未保存，PLCData 应该已经保存，这里可选）
    if (m_plcData) {
       // m_plcData->setViewSelection(type, tagKeys);   // 可能已经由外部设置，此处不需要重复
    } else {
        qWarning() << "[Controller] PLCData 模型为空，无法保存选择";
    }

    // 2. 创建或更新订阅（传入 tagKey 列表）
    if (!tagKeys.isEmpty()) {
        int interval = getDefaultIntervalForView(type);
        subscribeToView(type, tagKeys, interval);
    } else {
        unsubscribeFromView(type);
    }

    // 3. 通知相关的视图模型更新显示（使用 tagKey 列表）
    switch (type) {
    case ViewType::TABLE_VIEW:
        if (m_realTimeModel) {
            // RealTimeDataModel 需要 setVisibleTags(tagKeys)
            m_realTimeModel->setVisibleTags(tagKeys);
        }
        break;
    case ViewType::CHART_VIEW:
        if (m_chartManager) {
            m_chartManager->setChartTags(tagKeys);
        }
        break;
    case ViewType::PROCESS_VIEW:
        if (m_processViewManager) {
            m_processViewManager->setProcessTags(tagKeys);
        }
        break;
    case ViewType::TREND_VIEW:
        if (m_trendManager) {
            m_trendManager->setTrendTags(tagKeys);
        }
        break;
    default:
        break;
    }

    // 4. 发送视图选择完成信号
    emit viewSelectionApplied(type, tagKeys);
}



// 辅助方法：获取视图类型名称
QString Controller::getViewTypeName(ViewType type) const
{
    switch (type) {
    case ViewType::TABLE_VIEW: return "TableView";
    case ViewType::CHART_VIEW: return "ChartView";
    case ViewType::PROCESS_VIEW: return "ProcessView";
    case ViewType::TREND_VIEW: return "TrendView";
    default: return "UnknownView";
    }
}

// 辅助方法：更新 PLC 订阅
void Controller::updatePlcSubscription(ViewType type, const QStringList& tags)
{
    if (!m_commManager) {
        qWarning() << "CommManager 为空，无法更新订阅";
        return;
    }

    // 合并所有视图的订阅标签
    QStringList allTags = getAllSubscribedTags();
    qDebug() << "[Controller] 所有订阅标签:" << allTags;

    if (allTags.isEmpty()) {
        qDebug() << "[Controller] 无订阅标签，停止定时器";
        m_updateTimer->stop();
        return;
    }

    // 根据视图类型设置不同的更新频率
    int updateInterval = 1000; // 默认1秒

    switch (type) {
    case ViewType::TABLE_VIEW:
        updateInterval = 100;  // 100ms
        break;
    case ViewType::CHART_VIEW:
        updateInterval = 200;  // 200ms
        break;
    case ViewType::PROCESS_VIEW:
        updateInterval = 500;  // 500ms
        break;
    case ViewType::TREND_VIEW:
        updateInterval = 1000; // 1000ms
        break;
    default:
        updateInterval = 1000;
        break;
    }

    // 更新订阅列表
  //  m_updateCycle = updateInterval;
  //  m_updateTimer->setInterval(updateInterval);

    if (!m_updateTimer->isActive()) {
        m_updateTimer->start();
    }

    qDebug() << "[Controller] 更新订阅: 视图=" << getViewTypeName(type)
             << "，标签数=" << tags.size()
             << "，总标签数=" << allTags.size()
             << "，更新间隔=" << updateInterval << "ms";
}

// 辅助方法：记录操作日志
void Controller::logTagSelectionChange(ViewType type, const QStringList& tags)
{
    QString logMessage = QString("View[%1]: Tag selection changed. Selected tags: %2")
    .arg(getViewTypeName(type))
        .arg(tags.join(", "));

    qDebug() << "[Controller Log]" << logMessage;

    // 如果需要，可以记录到文件
    if (m_logFile.isOpen()) {
        QTextStream stream(&m_logFile);
        stream << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
               << " " << logMessage << "\n";
    }
}

// 辅助方法：立即更新视图
void Controller::updateViewImmediately(ViewType type)
{
    // 根据视图类型调用相应的更新方法
    switch (type) {
    case ViewType::TABLE_VIEW:
        if (m_realTimeModel) {
            emit requestImmediateUpdate();
        }
        break;

    case ViewType::CHART_VIEW:
        if (m_chartManager) {
            m_chartManager->refreshChartsNow();
        }
        break;

    case ViewType::PROCESS_VIEW:
        if (m_processViewManager) {
            m_processViewManager->refreshViewNow();
        }
        break;

    case ViewType::TREND_VIEW:
        if (m_trendManager) {
            m_trendManager->refreshTrendsNow();
        }
        break;

    default:
        break;
    }
}

void Controller::onPLCWriteCompleted(const QString& tagName, bool success, const QString& error)
{
    qDebug() << "PLC Write Completed:"
             << "Tag:" << tagName
             << "Success:" << success
             << "Error:" << error;

    // 可以在这里处理写入完成后的逻辑
    if (success) {
        qInfo() << "Tag" << tagName << "written successfully";
    } else {
        qWarning() << "Failed to write tag" << tagName << ":" << error;
    }
}

void Controller::onPLCConnectionError(const QString& error)
{
    qCritical() << "PLC Connection Error:" << error;

    // 通知UI显示连接错误
    emit statusMessage("PLC连接错误: " + error);

    // 可以在这里尝试重新连接或其他错误处理
    if (m_commManager) {
  //      m_commManager->handleConnectionError(error);
    }
}

void Controller::onStatusMessage(const QString& message)
{
    qDebug() << "Status Message:" << message;

    // 如果需要，可以转发给UI
    emit statusMessage(message);
}

// 缺少的函数实现
void Controller::updateProcessFlow(const QString& deviceId, const QVariant& value)
{
    qDebug() << "更新流程设备:" << deviceId << "值:" << value;
    // TODO: 实现具体的流程更新逻辑
    // 可以根据deviceId和value更新流程状态
}

void Controller::setAlertConfig(const QString& tagName, double min, double max, AlertLevel level)
{
    AlertConfig config;
    config.minEnabled = !qIsNaN(min);
    config.maxEnabled = !qIsNaN(max);
    config.minValue = min;
    config.maxValue = max;
    config.level = level;

    m_alertConfigs[tagName] = config;
    qDebug() << "设置报警配置:" << tagName
             << "min:" << min << "max:" << max
             << "level:" << static_cast<int>(level);
}