#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QQueue>
#include <QMutex>
#include <QDateTime>
#include <QTimer>
#include <QThread>
#include "common_types.h"

class DatabaseManager : public QObject
{
    Q_OBJECT
public:
    static DatabaseManager& instance();

    bool initialize(const QString& mysqlHost = "", int mysqlPort = 3306,
                    const QString& mysqlDb = "", const QString& mysqlUser = "",
                    const QString& mysqlPwd = "");
    void startHeartbeat(int intervalSec = 30);
    bool isMySQLConnected() const { return (m_mysqlOk && m_mysqlDbConn.isOpen()); }
    void reconfigure(const QString& host, int port, const QString& dbName,
                     const QString& user, const QString& pwd);
    void setEnabledTags(const QStringList& tags);
    bool isTagEnabled(const QString& tagName) const;

    // 异步历史查询（线程安全）
    struct HistoryQueryRequest {
        QStringList tagNames;
        QDateTime from;
        QDateTime to;
        bool useAvg;
        int requestId;
    };
    struct HistoryResult {
        QString tagName;
        QVector<QDateTime> timestamps;
        QVector<double> values;
    };
    void queryHistoryAsync(const QStringList& tagNames, const QDateTime& from,
                           const QDateTime& to, bool useAvg, int requestId);

    // 添加数据点（线程安全）
    void addDataPoint(const DbDataPoint& point);

    //报警
    void addAlarm(const AlertInfo& alarm);
    QList<AlertInfo> getActiveAlarms();
    QList<AlertInfo> getAlarmHistory(const QDateTime& from, const QDateTime& to);
    void acknowledgeAlarm(int alarmId, const QString& user);
    void acknowledgeAlarms(const QList<int>& alarmIds, const QString& user);
    void clearAlarm(int alarmId);
    void setAlarmDbMaxSize(quint64 maxSizeMB = 500); // 设置最大大小（MB）

signals:
    void mysqlConnectionChanged(bool ok);
    void sqliteStorageWarning(const QString& message);
    void historyQueryResultReady(int requestId, const QList<HistoryResult>& results);
    void historyQueryRequested(const HistoryQueryRequest& req);
    void alarmAdded(const AlertInfo& alarm);

private slots:
    void processQueue();
    void checkMySQLConnection();
    void runMinuteAggregation();
    void runHourAggregation();
    void runDayAggregation();
    void cleanupOldData();
    void executeHistoryQuery(const HistoryQueryRequest& req);
    void delayedStart();
    void doOpenDatabases();

private:
    DatabaseManager(QObject* parent = nullptr);
    ~DatabaseManager();

    QSqlDatabase getActiveDb() const;
    bool openSQLite();
    bool createTables();
    bool insertBatchSQLite(const QVector<DbDataPoint>& batch);
    bool insertBatchMySQL(const QVector<DbDataPoint>& batch);
    void syncSQLiteToMySQL();
    void startAggregationTasks();
    bool aggregateTable(const QString& sourceTable, const QString& targetTable,
                        const QString& timeFormat, int groupMinutes,
                        const QDateTime& targetTime, int *insertedCount = nullptr);
    QDateTime getLastAggregationTime(const QString& granularity);
    void setLastAggregationTime(const QString& granularity, const QDateTime& time);
    QList<HistoryResult> queryHistoryInternal(const QStringList& tagNames,
                                              const QDateTime& from,
                                              const QDateTime& to,
                                              bool useAvg);
    void setMySQLOk(bool ok);

    bool m_initialized = false;
    QString m_mysqlHost;
    int m_mysqlPort;
    QString m_mysqlDb;
    QString m_mysqlUser;
    QString m_mysqlPwd;
    QSqlDatabase m_sqliteDb;
    QSqlDatabase m_mysqlDbConn;
    bool m_mysqlOk;
    QQueue<DbDataPoint> m_queue;
    mutable QMutex m_mutex;
    QStringList m_enabledTags;
    mutable QMutex m_tagMutex;
    QThread* m_workerThread;
    QTimer* m_queueTimer;
    QTimer *m_heartbeatTimer = nullptr;
    bool m_mysqlWasConnected = false;

private:
    QSqlDatabase m_alarmDb;          // 独立的报警数据库连接
    bool openAlarmDb();              // 打开/创建报警数据库
    void createAlarmTables();        // 创建报警表
    void checkAlarmDbSizeAndClean();   // 检查报警数据库大小并清理
    void cleanupOldAlarms(int keepCount = 10000); // 保留最近 N 条报警记录

    qint64 m_alarmWriteCount = 0;
    quint64 m_alarmDbMaxSize = 500 * 1024 * 1024; // 500 MB

};

#endif // DATABASEMANAGER_H