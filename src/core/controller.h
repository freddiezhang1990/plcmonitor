// controller.h
/*
 * 控制层的主要作用是协调各个模块之间的交互，它应该：

    接收PLC数据并分发给不同的视图
    处理用户操作并转发给PLC
    管理业务流程状态
    处理报警逻辑
    维护用户配置
*/
#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <QObject>
#include <QMap>
#include <QString>
#include <QVariant>
#include <qdir.h>
#include "commmanager.h"
#include "plcdata.h"
//#include "chartmanager.h"
#include "processviewmanager.h"
//#include "RealTimeDataModel.h"
#include "common_types.h"
#include "trendmanager.h"

class RealTimeDataModel;
class ChartManager;
class TrendManager;
class ProcessViewManager;

class Controller : public QObject
{
    Q_OBJECT

public:
    explicit Controller(PLCData* dataModel, CommManager* commManager, QObject* parent = nullptr);
    ~Controller();

    // 初始化
    bool initialize();
    bool initializeAlarm();
 //   bool initializeviewselection();
    bool isInitialized() const { return m_initialized; }  // 添加初始化检查方法

    // 数据分发
  //  void distributeData(const QMap<QString, QVariant>& data);

    // 写入功能接口
    bool writeSingleTag(const QString& tagName, const QVariant& value);
    bool writeBatchTags(const QMap<QString, QVariant>& tagValueMap);

    // 写入配置
    void setWriteRetryCount(int count) { m_writeRetryCount = count; }
    void setWriteRetryInterval(int ms) { m_writeRetryInterval = ms; }

    // 流程管理
    void updateProcessFlow(const QString& deviceId, const QVariant& value);
    void updateProcessStates(const QMap<QString, QVariant>& data);

    // 报警控制
    void setAlertConfig(const QString& tagName, double min, double max, AlertLevel level);
    void checkAlerts(const QMap<QString, QVariant>& data);

    void addChartTag(const QString& tagName);
    void removeChartTag(const QString& tagName);

    // 🆕 添加管理器设置方法
    void setRealTimeModel(RealTimeDataModel* model);
    void setChartManager(ChartManager* manager);
    void setTrendManager(TrendManager* manager);
    void setProcessViewManager(ProcessViewManager* manager);
    // 停止
    void stopController();

signals:
    // 数据更新信号
    void tableDataUpdated(const QMap<QString, QVariant>& data);
    void chartDataUpdated(const QMap<QString, QVariant>& data);
    void processDataUpdated(const QMap<QString, QVariant>& data);
    void trendDataUpdated(const QMap<QString, QVariant>& data);

    void requestImmediateUpdate();
    // 状态信号
    void connectionStateChanged(bool connected);
    void alertTriggered(const AlertInfo& alert);
    void processStateChanged(const QString& processName, ProcessState state);

    // 新增写入相关信号
    void singleWriteStarted(const QString& tagName, const QVariant& value);
    void singleWriteCompleted(const QString& tagName, bool success, const QString& error);
    void batchWriteStarted(const QMap<QString, QVariant>& tagValueMap);
    void batchWriteCompleted(const QMap<QString, bool>& results, const QString& error);
    void writeProgressChanged(int current, int total);
    void chartSelectionChanged(const QString& tagName, bool added);

    void statusMessage(const QString& message);

    // 标签选择变化信号 - 添加这4个信号
    void viewSelectionApplied(ViewType type, const QStringList& tags);
    void saveAlarmToDatabase(const AlertInfo& alarm);

public slots:
    // PLC数据更新处理
   // void onPLCDataUpdated(const QMap<QString, QVariant>& data);

    // 用户操作处理
    void onWriteRequest(const QString& tagName, const QVariant& value);
    void onSelectionChanged(ViewType type, const QStringList& tags);

    void onPLCWriteCompleted(const QString& tagName, bool success, const QString& error);
    void onPLCConnectionError(const QString& error);
    void onStatusMessage(const QString& message);
    void onTagSelectionChanged(ViewType type, const QStringList& tags);
    // 订阅管理
    void subscribeToView(ViewType type, const QStringList& tags, int interval = 0);
    void unsubscribeFromView(ViewType type);
    // 轮询调度
    void onUpdateTimer();
private slots:
    // 新增处理单个写入完成的槽
    void onSingleWriteCompleted(const QString& tagName, bool success, const QString& error);
 //   void onPLCThreadFinished();

private:
    // 优化读取逻辑
    void performOptimizedPolling();
    QStringList getAllSubscribedTags();
    QMap<int, QList<IConnector::ReadRequest>> getBatchReadRequests(const QStringList& tags);
    // 通过订阅分发数据
   // void distributeDataViaSubscriptions(const QMap<QString, QVariant>& data);
    // 分发数据到视图
    void distributeDataToViews(const QList<IConnector::ReadResult>& results);

    QMap<QString, QVariant> filterData(const QMap<QString, QVariant>& data, const QStringList& tags);

    // 私有辅助方法
    bool parseTagKey(const QString& tagKey, QString& deviceId, QString& tagName) const;
    bool writeTagToPLC(const QString& tagKey, const QVariant& value);
    void processNextBatchWrite();
    void finishBatchWrite();

    QString getViewTypeName(ViewType type) const;
    void updatePlcSubscription(ViewType type, const QStringList& tags);
    void logTagSelectionChange(ViewType type, const QStringList& tags);
    void updateViewImmediately(ViewType type);
    int getDefaultIntervalForView(ViewType type) const;
  //  void distributeDefaultViews(const QMap<QString, QVariant>& data);

    void verifyConnections();

private:
 //   QThreadPool* m_threadPool;
    PLCData* m_plcData;
    CommManager* m_commManager = nullptr;
    RealTimeDataModel* m_realTimeModel = nullptr;
    ChartManager* m_chartManager = nullptr;
    ProcessViewManager* m_processViewManager = nullptr;
    TrendManager* m_trendManager = nullptr;

    // 批量写入相关的成员变量
    QMap<QString, QVariant> m_pendingBatchWrites;
    QMap<QString, bool> m_batchWriteResults;
    int m_writeRetryCount = 3;
    int m_writeRetryInterval = 100; // 毫秒
    bool m_isBatchWriting = false;
    int m_currentBatchIndex = 0;
    int m_totalBatchItems = 0;

    // 报警配置
    QMap<QString, AlertConfig> m_alertConfigs;
    QList<AlertInfo> m_alertHistory;
    int m_nextAlertId = 1;
    QHash<QString, bool> m_alertActiveStates;
    QHash<QString, QDateTime> m_alertLastTriggerTime;

    ViewType m_currentView = ViewType::PROCESS_VIEW ;

     // 流程状态
    QMap<QString, ProcessState> m_processStates;

    QFile m_logFile;                           // 日志文件

    // 标签选择自动更新订阅
    bool m_autoUpdateSubscription = true;      // 自动更新PLC订阅标志

    // 添加停止标志
    QAtomicInteger<bool> m_stopping = false;
    bool m_initialized = false;                // 初始化状态标记

    // 订阅管理
    struct Subscription {
        ViewType viewType;
        QStringList tags;
        int updateInterval;  // ms
        QDateTime lastUpdate;
    };

    QMap<ViewType, Subscription> m_subscriptions;

    // 新增：定时器用于触发不同视图的更新
    QTimer* m_updateTimer;
    int m_updateCycle = 100;  // 基础更新周期 100ms


 //   QAtomicInt m_isProcessing;  // 0=空闲, 1=处理中
 //   QAtomicInt m_dataCount;     // 原子数据计数
};

#endif // CONTROLLER_H