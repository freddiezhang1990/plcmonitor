#include "databasemanager.h"
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlRecord>
#include <QDateTime>
#include <QRandomGenerator>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include <QThread>
#include <QElapsedTimer>
#include <qsqlquery.h>
#include <QFileInfo>

DatabaseManager& DatabaseManager::instance()
{
    static DatabaseManager instance;
    return instance;
}

DatabaseManager::DatabaseManager(QObject* parent)
    : QObject(parent)
    , m_mysqlOk(false)
{
    m_workerThread = new QThread(this);
    moveToThread(m_workerThread);
    m_workerThread->start();

    // 定时器创建和信号连接（这些可以在任何线程创建，但启动要在工作线程）
    m_queueTimer = new QTimer();
    m_queueTimer->moveToThread(m_workerThread);
    connect(m_queueTimer, &QTimer::timeout, this, &DatabaseManager::processQueue);

    m_heartbeatTimer = new QTimer();
    m_heartbeatTimer->moveToThread(m_workerThread);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &DatabaseManager::checkMySQLConnection);

    connect(this, &DatabaseManager::historyQueryRequested, this, &DatabaseManager::executeHistoryQuery, Qt::QueuedConnection);
}

DatabaseManager::~DatabaseManager()
{
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
    }

    if (m_alarmDb.isOpen())
        m_alarmDb.close();
    QSqlDatabase::removeDatabase("alarm_conn");
}

bool DatabaseManager::initialize(const QString& mysqlHost, int mysqlPort,
                                 const QString& mysqlDb, const QString& mysqlUser,
                                 const QString& mysqlPwd)
{
    if (m_initialized) return true;

    // 保存参数
    m_mysqlHost = mysqlHost;
    m_mysqlPort = mysqlPort;
    m_mysqlDb = mysqlDb;
    m_mysqlUser = mysqlUser;
    m_mysqlPwd = mysqlPwd;

    // 异步在工作线程中打开数据库
    QMetaObject::invokeMethod(this, "doOpenDatabases", Qt::QueuedConnection);
    return true;
}

void DatabaseManager::reconfigure(const QString& host, int port, const QString& dbName,
                                  const QString& user, const QString& pwd)
{
    // 必须在工作线程中调用，确保线程安全
    QMetaObject::invokeMethod(this, [=]() {
        if (m_mysqlDbConn.isOpen())
            m_mysqlDbConn.close();
        m_mysqlDbConn.setHostName(host);
        m_mysqlDbConn.setPort(port);
        m_mysqlDbConn.setDatabaseName(dbName);
        m_mysqlDbConn.setUserName(user);
        m_mysqlDbConn.setPassword(pwd);

        // MariaDB 连接选项 - 禁用 SSL
        QStringList options;
        options << "MYSQL_OPT_SSL_MODE=DISABLED";  // 尝试这个
        options << "MARIADB_OPT_SSL_MODE=DISABLED"; // MariaDB 专用
        options << "CLIENT_SSL=0";                   // 禁用客户端 SSL
        m_mysqlDbConn.setConnectOptions(options.join(";"));

        bool ok = m_mysqlDbConn.open();
        setMySQLOk(ok);
        if (ok) {
            qDebug() << "MySQL reconfigured and connected";
            createTables(); // 确保表存在
        } else {
            qWarning() << "MySQL reconfigure failed:" << m_mysqlDbConn.lastError().text();
        }
    }, Qt::QueuedConnection);
}

void DatabaseManager::doOpenDatabases()
{
    qDebug() << "doOpenDatabases running in thread:" << QThread::currentThread();

    // 注册元类型（只需一次，可在主线程注册，但这里也可）
    qRegisterMetaType<DatabaseManager::HistoryQueryRequest>("DatabaseManager::HistoryQueryRequest");
    qRegisterMetaType<QList<DatabaseManager::HistoryResult>>("QList<DatabaseManager::HistoryResult>");

    if (!openSQLite()) {
        qCritical() << "Failed to open SQLite database";
        emit sqliteStorageWarning("SQLite database open failed");
        return;
    }
    // 新增：打开独立的报警数据库
    if (!openAlarmDb()) {
        qWarning() << "Failed to open alarm database, alarms will not be recorded";
    }
    if (!m_mysqlHost.isEmpty() && !m_mysqlDb.isEmpty()) {
        m_mysqlDbConn = QSqlDatabase::addDatabase("QMYSQL", "mysql_conn");
        m_mysqlDbConn.setHostName(m_mysqlHost);
        m_mysqlDbConn.setPort(m_mysqlPort);
        m_mysqlDbConn.setDatabaseName(m_mysqlDb);
        m_mysqlDbConn.setUserName(m_mysqlUser);
        m_mysqlDbConn.setPassword(m_mysqlPwd);

        // MariaDB 连接选项 - 禁用 SSL
        QStringList options;
        options << "MYSQL_OPT_SSL_MODE=DISABLED";  // 尝试这个
        options << "MARIADB_OPT_SSL_MODE=DISABLED"; // MariaDB 专用
        options << "CLIENT_SSL=0";                   // 禁用客户端 SSL
        m_mysqlDbConn.setConnectOptions(options.join(";"));

        setMySQLOk(m_mysqlDbConn.open());
        if (m_mysqlOk) {
            qDebug() << "MySQL connected successfully";
        } else {
            qWarning() << "MySQL connection failed:" << m_mysqlDbConn.lastError().text();
        }
    } else {
        qDebug() << "MySQL not configured, using SQLite only.";
    }

    if (!createTables()) {
        qCritical() << "Failed to create database tables";
        return;
    }

    // 启动定时器（现在在工作线程中，安全）
    m_queueTimer->start(200);
    m_heartbeatTimer->start(30000);
    startAggregationTasks();

    m_initialized = true;
    qDebug() << "DatabaseManager initialization completed in worker thread";
}

bool DatabaseManager::openSQLite()
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dataDir);
    if (!dir.exists())
        dir.mkpath(".");
    QString dbPath = dataDir + "/plc_data.db";
    m_sqliteDb = QSqlDatabase::addDatabase("QSQLITE", "sqlite_conn");
    m_sqliteDb.setDatabaseName(dbPath);
    if (!m_sqliteDb.open()) {
        qCritical() << "Failed to open SQLite database:" << m_sqliteDb.lastError().text();
        return false;
    }
    qDebug() << "SQLite database opened at" << dbPath;
    return true;
}

bool DatabaseManager::openAlarmDb()
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dataDir);
    if (!dir.exists())
        dir.mkpath(".");
    QString dbPath = dataDir + "/alarm.db";

    m_alarmDb = QSqlDatabase::addDatabase("QSQLITE", "alarm_conn");
    m_alarmDb.setDatabaseName(dbPath);
    if (!m_alarmDb.open()) {
        qCritical() << "Failed to open alarm database:" << m_alarmDb.lastError().text();
        return false;
    }
    qDebug() << "Alarm database opened at" << dbPath;

    createAlarmTables();

    m_alarmDbMaxSize = 500 * 1024 * 1024; // 500 MB
    return true;
}

bool DatabaseManager::createTables()
{
    QSqlQuery query(m_sqliteDb);
    // 秒级表
    QString sql = R"(
        CREATE TABLE IF NOT EXISTS data_second (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            data_id INTEGER UNIQUE NOT NULL,
            tag_name TEXT NOT NULL,
            value REAL NOT NULL,
            quality INTEGER DEFAULT 0,
            timestamp DATETIME NOT NULL,
            synced INTEGER DEFAULT 0
        )
    )";
    if (!query.exec(sql)) {
        qCritical() << "Failed to create data_second table:" << query.lastError().text();
        return false;
    }
    query.exec("CREATE INDEX IF NOT EXISTS idx_sec_tag_time ON data_second(tag_name, timestamp)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_sec_time ON data_second(timestamp)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_sec_synced ON data_second(synced)");

    // 分钟表
    sql = R"(
        CREATE TABLE IF NOT EXISTS data_minute (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            tag_name TEXT NOT NULL,
            timestamp DATETIME NOT NULL,
            avg_value REAL,
            last_value REAL,
            quality INTEGER DEFAULT 0,
            num_samples INTEGER DEFAULT 0,
            UNIQUE(tag_name, timestamp)
        )
    )";
    if (!query.exec(sql)) {
        qCritical() << "Failed to create data_minute table:" << query.lastError().text();
        return false;
    }
    query.exec("CREATE INDEX IF NOT EXISTS idx_min_tag_time ON data_minute(tag_name, timestamp)");

    // 小时表
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS data_hour (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            tag_name TEXT NOT NULL,
            timestamp DATETIME NOT NULL,
            avg_value REAL,
            last_value REAL,
            quality INTEGER DEFAULT 0,
            num_samples INTEGER DEFAULT 0,
            UNIQUE(tag_name, timestamp)
        )
    )");
    query.exec("CREATE INDEX IF NOT EXISTS idx_hour_tag_time ON data_hour(tag_name, timestamp)");

    // 天表
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS data_day (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            tag_name TEXT NOT NULL,
            timestamp DATETIME NOT NULL,
            avg_value REAL,
            last_value REAL,
            quality INTEGER DEFAULT 0,
            num_samples INTEGER DEFAULT 0,
            UNIQUE(tag_name, timestamp)
        )
    )");
    query.exec("CREATE INDEX IF NOT EXISTS idx_day_tag_time ON data_day(tag_name, timestamp)");

    // 同步状态表
    query.exec("CREATE TABLE IF NOT EXISTS sync_state (key TEXT PRIMARY KEY, value TEXT)");

    // MySQL 表创建
    if (m_mysqlOk) {
        QSqlQuery mysqlQuery(m_mysqlDbConn);
        mysqlQuery.exec(R"(
            CREATE TABLE IF NOT EXISTS data_second (
                id BIGINT AUTO_INCREMENT PRIMARY KEY,
                data_id BIGINT UNSIGNED NOT NULL UNIQUE,
                tag_name VARCHAR(255) NOT NULL,
                value DOUBLE NOT NULL,
                quality TINYINT DEFAULT 0,
                timestamp DATETIME NOT NULL,
                INDEX idx_tag_time (tag_name, timestamp),
                INDEX idx_time (timestamp)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )");
        mysqlQuery.exec(R"(
            CREATE TABLE IF NOT EXISTS data_minute (
                id BIGINT AUTO_INCREMENT PRIMARY KEY,
                tag_name VARCHAR(255) NOT NULL,
                timestamp DATETIME NOT NULL,
                avg_value DOUBLE,
                last_value DOUBLE,
                quality TINYINT DEFAULT 0,
                num_samples INT DEFAULT 0,
                INDEX idx_tag_time (tag_name, timestamp),
                UNIQUE(tag_name, timestamp)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )");
        mysqlQuery.exec(R"(
            CREATE TABLE IF NOT EXISTS data_hour (
                id BIGINT AUTO_INCREMENT PRIMARY KEY,
                tag_name VARCHAR(255) NOT NULL,
                timestamp DATETIME NOT NULL,
                avg_value DOUBLE,
                last_value DOUBLE,
                quality TINYINT DEFAULT 0,
                num_samples INT DEFAULT 0,
                INDEX idx_tag_time (tag_name, timestamp),
                UNIQUE(tag_name, timestamp)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )");
        mysqlQuery.exec(R"(
            CREATE TABLE IF NOT EXISTS data_day (
                id BIGINT AUTO_INCREMENT PRIMARY KEY,
                tag_name VARCHAR(255) NOT NULL,
                timestamp DATETIME NOT NULL,
                avg_value DOUBLE,
                last_value DOUBLE,
                quality TINYINT DEFAULT 0,
                num_samples INT DEFAULT 0,
                INDEX idx_tag_time (tag_name, timestamp),
                UNIQUE(tag_name, timestamp)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )");
        if (mysqlQuery.lastError().isValid())
            qWarning() << "MySQL create table error:" << mysqlQuery.lastError().text();
    }
    return true;
}

void DatabaseManager::createAlarmTables()
{
    if (!m_alarmDb.isOpen())
        return;

    QSqlQuery query(m_alarmDb);
    bool ok = query.exec(R"(
        CREATE TABLE IF NOT EXISTS alarm_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            alarm_id INTEGER NOT NULL,
            tag_name TEXT NOT NULL,
            timestamp DATETIME NOT NULL,
            message TEXT,
            value REAL,
            level INTEGER,
            acknowledged BOOLEAN DEFAULT 0,
            ack_time DATETIME,
            ack_user TEXT,
            cleared BOOLEAN DEFAULT 0,
            clear_time DATETIME
        )
    )");
    if (!ok) {
        qWarning() << "Failed to create alarm_history table:" << query.lastError().text();
        return;
    }
    query.exec("CREATE INDEX IF NOT EXISTS idx_alarm_tag ON alarm_history(tag_name)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_alarm_time ON alarm_history(timestamp)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_alarm_cleared ON alarm_history(cleared)");
    qDebug() << "Alarm table ready";
}

void DatabaseManager::setEnabledTags(const QStringList& tags)
{
    QMutexLocker locker(&m_tagMutex);
    m_enabledTags = tags;
}

bool DatabaseManager::isTagEnabled(const QString& tagName) const
{
    QMutexLocker locker(&m_tagMutex);
    return m_enabledTags.contains(tagName);
}

void DatabaseManager::addDataPoint(const DbDataPoint& point)
{
    if (!isTagEnabled(point.tagName))
        return;
    QMutexLocker locker(&m_mutex);
    const int MAX_QUEUE_SIZE = 10000;
    if (m_queue.size() >= MAX_QUEUE_SIZE) {
        DbDataPoint discarded = m_queue.dequeue();
        qWarning() << "Database queue overflow, discarding oldest data point: tag="
                   << discarded.tagName << " value=" << discarded.value;
    }
    m_queue.enqueue(point);
}

void DatabaseManager::processQueue()
{
    if (m_queue.isEmpty())
        return;

    QVector<DbDataPoint> batch;
    int maxBatchSize = 500;
    {
        QMutexLocker locker(&m_mutex);
        while (!m_queue.isEmpty() && batch.size() < maxBatchSize) {
            batch.append(m_queue.dequeue());
        }
    }
    if (batch.isEmpty())
        return;

    bool mysqlSuccess = false;
    if (m_mysqlOk) {
        mysqlSuccess = insertBatchMySQL(batch);
        if (!mysqlSuccess) {
            qWarning() << "MySQL insert failed, fallback to SQLite";
        }
    }
    if (!mysqlSuccess) {
        insertBatchSQLite(batch);
    }
}

bool DatabaseManager::insertBatchSQLite(const QVector<DbDataPoint>& batch)
{
    if (batch.isEmpty()) return false;
    QSqlQuery query(m_sqliteDb);
    if (!m_sqliteDb.transaction()) {
        qWarning() << "Failed to start SQLite transaction";
        return false;
    }
    QString sql = "INSERT OR IGNORE INTO data_second (data_id, tag_name, value, quality, timestamp, synced) VALUES (?, ?, ?, ?, ?, 0)";
    query.prepare(sql);
    for (const DbDataPoint& p : batch) {
        query.addBindValue(p.dataId);
        query.addBindValue(p.tagName);
        query.addBindValue(p.value);
        query.addBindValue(p.quality);
        query.addBindValue(p.timestamp.toString(Qt::ISODate));
        query.exec();
    }
    if (!m_sqliteDb.commit()) {
        qWarning() << "Failed to commit SQLite transaction";
        return false;
    }
    return true;
}

bool DatabaseManager::insertBatchMySQL(const QVector<DbDataPoint>& batch)
{
    if (!m_mysqlOk || !m_mysqlDbConn.isOpen()) return false;
    if (batch.isEmpty()) return true;
    if (!m_mysqlDbConn.transaction()) {
        qWarning() << "MySQL transaction start failed";
        return false;
    }
    QSqlQuery query(m_mysqlDbConn);
    QString sql = "INSERT IGNORE INTO data_second (data_id, tag_name, value, quality, timestamp) VALUES (?, ?, ?, ?, ?)";
    if (!query.prepare(sql)) {
        qWarning() << "MySQL prepare failed:" << query.lastError().text();
        m_mysqlDbConn.rollback();
        return false;
    }
    for (const DbDataPoint& p : batch) {
        query.addBindValue(p.dataId);
        query.addBindValue(p.tagName);
        query.addBindValue(p.value);
        query.addBindValue(p.quality);
        query.addBindValue(p.timestamp.toString(Qt::ISODate));
        if (!query.exec()) {
            qWarning() << "MySQL insert error:" << query.lastError().text();
            m_mysqlDbConn.rollback();
            return false;
        }
    }
    if (!m_mysqlDbConn.commit()) {
        qWarning() << "MySQL commit failed, rolling back";
        m_mysqlDbConn.rollback();
        return false;
    }
    return true;
}

void DatabaseManager::startHeartbeat(int intervalSec) {
    if (!m_heartbeatTimer) {
        m_heartbeatTimer = new QTimer(this);
        connect(m_heartbeatTimer, &QTimer::timeout, this, &DatabaseManager::checkMySQLConnection);
    }
    m_heartbeatTimer->start(intervalSec * 1000);
    // 立即执行一次初始检测
    QMetaObject::invokeMethod(this, "checkMySQLConnection", Qt::QueuedConnection);
}

void DatabaseManager::checkMySQLConnection() {
    bool current = isMySQLConnected();
    if (current != m_mysqlWasConnected) {
        m_mysqlWasConnected = current;
        emit mysqlConnectionChanged(current);
        if (!current) {
            qWarning() << "MySQL连接丢失，切换到SQLite";
        } else {
            qInfo() << "MySQL已恢复，切回主存储";
        }
    }
}

void DatabaseManager::setMySQLOk(bool ok)
{
    if (m_mysqlOk == ok) return;
    m_mysqlOk = ok;
    emit mysqlConnectionChanged(ok);
    if (ok) {
        syncSQLiteToMySQL();
    }
}

void DatabaseManager::syncSQLiteToMySQL()
{
    if (!m_mysqlOk) return;

    QSqlQuery select(m_sqliteDb);
    select.prepare("SELECT id, data_id, tag_name, value, quality, timestamp FROM data_second WHERE synced=0 ORDER BY id LIMIT 1000");
    if (!select.exec()) {
        qWarning() << "Failed to select unsynced data:" << select.lastError().text();
        return;
    }

    QVector<DbDataPoint> batch;
    QVector<qint64> ids;
    while (select.next()) {
        DbDataPoint p;
        p.dataId = select.value(1).toLongLong();
        p.tagName = select.value(2).toString();
        p.value = select.value(3).toDouble();
        p.quality = select.value(4).toInt();
        p.timestamp = QDateTime::fromString(select.value(5).toString(), Qt::ISODate);
        batch.append(p);
        ids.append(select.value(0).toLongLong());
    }

    if (batch.isEmpty())
        return;

    if (insertBatchMySQL(batch)) {
        QString idList = QString::number(ids[0]);
        for (int i = 1; i < ids.size(); ++i) idList += "," + QString::number(ids[i]);
        QSqlQuery update(m_sqliteDb);
        update.prepare("UPDATE data_second SET synced=1 WHERE id IN (" + idList + ")");
        update.exec();
    } else {
        qWarning() << "MySQL sync failed, will retry later";
    }
}

void DatabaseManager::startAggregationTasks()
{
    QTimer* minuteTimer = new QTimer(this);
    minuteTimer->moveToThread(m_workerThread);
    connect(minuteTimer, &QTimer::timeout, this, &DatabaseManager::runMinuteAggregation);
    minuteTimer->start(60000);
    QMetaObject::invokeMethod(this, "runMinuteAggregation", Qt::QueuedConnection);

    qDebug() << "startAggregationTasks called";
    QTimer* hourTimer = new QTimer(this);
    hourTimer->moveToThread(m_workerThread);
    connect(hourTimer, &QTimer::timeout, this, &DatabaseManager::runHourAggregation);
    hourTimer->start(3600000);
    QTimer::singleShot(600000, this, &DatabaseManager::runHourAggregation);
    qDebug() << "Hour aggregation scheduled (periodic every 1 hour, first in 10 minutes)";

    QTimer* dayTimer = new QTimer(this);
    dayTimer->moveToThread(m_workerThread);
    connect(dayTimer, &QTimer::timeout, this, &DatabaseManager::runDayAggregation);
    dayTimer->start(86400000);
    QTimer::singleShot(86400000, this, &DatabaseManager::runDayAggregation);

    QTimer* cleanupTimer = new QTimer(this);
    cleanupTimer->moveToThread(m_workerThread);
    connect(cleanupTimer, &QTimer::timeout, this, &DatabaseManager::cleanupOldData);
    cleanupTimer->start(86400000);
}

void DatabaseManager::runMinuteAggregation()
{
    QDateTime now = QDateTime::currentDateTimeUtc();
    QDateTime currentMinute = QDateTime(now.date(), now.time().addSecs(-now.time().second()).addMSecs(-now.time().msec()));
    QDateTime cutoff = currentMinute.addSecs(-60);

    QDateTime last = getLastAggregationTime("minute");
    if (last.isNull()) {
        // 从秒表最早时间开始
        QSqlQuery query(getActiveDb());
        query.exec("SELECT MIN(timestamp) FROM data_second");
        if (query.next()) {
            QDateTime earliest = QDateTime::fromString(query.value(0).toString(), Qt::ISODate);
            if (earliest.isValid()) {
                // 对齐到分钟边界
                QDateTime aligned = QDateTime(earliest.date(), QTime(earliest.time().hour(), earliest.time().minute(), 0, 0));
                last = aligned;
            } else {
                last = cutoff.addSecs(-60);
            }
        } else {
            last = cutoff.addSecs(-60);
        }
    }

    while (last < cutoff) {
        QDateTime targetStart = last.addSecs(60);
        int inserted = 0;
        bool ok = aggregateTable("data_second", "data_minute", "yyyy-MM-dd hh:mm:00", 1, targetStart, &inserted);
        if (ok && inserted > 0) {
            setLastAggregationTime("minute", targetStart);
        }
        // 即使没有插入，也继续前进（避免死循环），但注意秒表数据可能缺失，通常不会
        last = targetStart;
    }
}

void DatabaseManager::runHourAggregation()
{
    QDateTime now = QDateTime::currentDateTimeUtc();
    QDateTime currentHour = QDateTime(now.date(), QTime(now.time().hour(), 0, 0));
    QDateTime cutoff = currentHour.addSecs(-3600); // 上一个完整小时

    QDateTime last = getLastAggregationTime("hour");
    if (last.isNull()) {
        // 从分钟表最早的小时边界开始
        QSqlQuery query(getActiveDb());
        query.exec("SELECT MIN(datetime(timestamp, 'start of hour')) FROM data_minute");
        if (query.next()) {
            QDateTime earliestHour = QDateTime::fromString(query.value(0).toString(), Qt::ISODate);
            if (earliestHour.isValid()) {
                last = earliestHour;
            } else {
                last = cutoff.addSecs(-3600);
            }
        } else {
            last = cutoff.addSecs(-3600);
        }
    }

    while (last < cutoff) {
        QDateTime targetStart = last.addSecs(3600);
        int inserted = 0;
        bool ok = aggregateTable("data_minute", "data_hour", "yyyy-MM-dd hh:00:00", 60, targetStart, &inserted);
        if (ok && inserted > 0) {
            setLastAggregationTime("hour", targetStart);
        }
        last = targetStart;
    }
}

void DatabaseManager::runDayAggregation()
{
    QDateTime now = QDateTime::currentDateTimeUtc();
    QDateTime currentDay = QDateTime(now.date(), QTime(0, 0, 0));
    QDateTime cutoff = currentDay.addDays(-1);

    QDateTime last = getLastAggregationTime("day");
    if (last.isNull()) {
        // 从小时表最早的天边界开始
        QSqlQuery query(getActiveDb());
        query.exec("SELECT MIN(datetime(timestamp, 'start of day')) FROM data_hour");
        if (query.next()) {
            QDateTime earliestDay = QDateTime::fromString(query.value(0).toString(), Qt::ISODate);
            if (earliestDay.isValid()) {
                last = earliestDay;
            } else {
                last = cutoff.addDays(-1);
            }
        } else {
            last = cutoff.addDays(-1);
        }
    }

    while (last < cutoff) {
        QDateTime targetStart = last.addDays(1);
        int inserted = 0;
        bool ok = aggregateTable("data_hour", "data_day", "yyyy-MM-dd 00:00:00", 1440, targetStart, &inserted);
        if (ok && inserted > 0) {
            setLastAggregationTime("day", targetStart);
        }
        last = targetStart;
    }
}

bool DatabaseManager::aggregateTable(const QString& sourceTable, const QString& targetTable,
                                     const QString& timeFormat, int groupMinutes,
                                     const QDateTime& targetTime, int *insertedCount)
{
    QString startStr = targetTime.toString(Qt::ISODate);
    QString endStr = targetTime.addSecs(groupMinutes * 60).toString(Qt::ISODate);

    // 根据源表选择字段名
    QString valueField, lastValueField, samplesField;
    if (sourceTable == "data_second") {
        valueField = "value";
        lastValueField = "value";
        samplesField = "1";
    } else {
        valueField = "avg_value";
        lastValueField = "last_value";
        samplesField = "num_samples";
    }

    // 子查询取最后一个值
    QString lastValueSub = QString(
                               "(SELECT %1 FROM %2 WHERE tag_name = t.tag_name AND timestamp >= '%3' AND timestamp < '%4' ORDER BY timestamp DESC LIMIT 1)"
                               ).arg(lastValueField, sourceTable, startStr, endStr);

    // 根据当前活跃数据库选择时间截断表达式和插入语句前缀
    bool isMysql = m_mysqlOk;
    QString datetimeExpr;
    QString insertPrefix;
    if (isMysql) {
        if (timeFormat == "yyyy-MM-dd hh:mm:00")
            datetimeExpr = "DATE_FORMAT(timestamp, '%Y-%m-%d %H:%i:00')";
        else if (timeFormat == "yyyy-MM-dd hh:00:00")
            datetimeExpr = "DATE_FORMAT(timestamp, '%Y-%m-%d %H:00:00')";
        else if (timeFormat == "yyyy-MM-dd 00:00:00")
            datetimeExpr = "DATE_FORMAT(timestamp, '%Y-%m-%d 00:00:00')";
        else
            datetimeExpr = "DATE_FORMAT(timestamp, '%Y-%m-%d %H:%i:00')";
        insertPrefix = "REPLACE INTO";
    } else {
        if (timeFormat == "yyyy-MM-dd hh:mm:00")
            datetimeExpr = "datetime(strftime('%Y-%m-%d %H:%M:00', timestamp))";
        else if (timeFormat == "yyyy-MM-dd hh:00:00")
            datetimeExpr = "datetime(strftime('%Y-%m-%d %H:00:00', timestamp))";
        else if (timeFormat == "yyyy-MM-dd 00:00:00")
            datetimeExpr = "datetime(strftime('%Y-%m-%d 00:00:00', timestamp))";
        else
            datetimeExpr = "datetime(strftime('%Y-%m-%d %H:%M:00', timestamp))";
        insertPrefix = "INSERT OR REPLACE INTO";
    }

    // 完整 SQL
    QString sql = QString(
                      "%1 %2 (tag_name, timestamp, avg_value, last_value, num_samples, quality)\n"
                      "SELECT \n"
                      "  tag_name,\n"
                      "  %3 as ts,\n"
                      "  AVG(%4),\n"
                      "  %5,\n"
                      "  SUM(%6),\n"
                      "  CASE WHEN SUM(CASE WHEN quality=0 THEN 1 ELSE 0 END) < SUM(%6)/2 THEN 1 ELSE 0 END\n"
                      "FROM %7 t\n"
                      "WHERE timestamp >= '%8' AND timestamp < '%9'\n"
                      "GROUP BY tag_name, ts"
                      ).arg(insertPrefix, targetTable, datetimeExpr, valueField, lastValueSub, samplesField, sourceTable, startStr, endStr);

    QSqlDatabase db = getActiveDb();
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        qWarning() << "Aggregation failed:" << query.lastError().text();
        if (insertedCount) *insertedCount = 0;
        return false;
    }
    int affected = query.numRowsAffected();
    if (insertedCount) *insertedCount = affected;
    return true;
}

QDateTime DatabaseManager::getLastAggregationTime(const QString& granularity)
{
    QSqlDatabase db = getActiveDb();
    QSqlQuery query(db);
    query.prepare("SELECT value FROM sync_state WHERE key = ?");
    query.addBindValue("agg_" + granularity);
    if (query.exec() && query.next()) {
        return QDateTime::fromString(query.value(0).toString(), Qt::ISODate);
    }
    return QDateTime();
}

void DatabaseManager::setLastAggregationTime(const QString& granularity, const QDateTime& time)
{
    QSqlDatabase db = getActiveDb();
    QSqlQuery query(db);
    query.prepare("INSERT OR REPLACE INTO sync_state (key, value) VALUES (?, ?)");
    query.addBindValue("agg_" + granularity);
    query.addBindValue(time.toString(Qt::ISODate));
    query.exec();
}

QList<DatabaseManager::HistoryResult> DatabaseManager::queryHistoryInternal(
    const QStringList& tagNames,
    const QDateTime& from,
    const QDateTime& to,
    bool useAvg)
{
    QList<HistoryResult> results;
    if (tagNames.isEmpty() || from >= to)
        return results;

    int days = from.daysTo(to);
    QString table;
    QString valueField;
    if (days <= 7) {
        table = "data_second";
        valueField = "value";
    } else if (days <= 30) {
        table = "data_minute";
        valueField = useAvg ? "avg_value" : "last_value";
    } else if (days <= 365) {
        table = "data_hour";
        valueField = useAvg ? "avg_value" : "last_value";
    } else {
        table = "data_day";
        valueField = useAvg ? "avg_value" : "last_value";
    }

    // 构建 IN 子句的命名占位符
    QStringList placeholders;
    for (int i = 0; i < tagNames.size(); ++i) {
        placeholders << QString(":tag%1").arg(i);
    }

    QString sql = QString("SELECT tag_name, timestamp, %1 FROM %2 WHERE tag_name IN (%3) AND timestamp BETWEEN :from AND :to ORDER BY timestamp")
                      .arg(valueField, table, placeholders.join(","));

    QSqlDatabase db = getActiveDb();
    QSqlQuery query(db);
    query.prepare(sql);

    for (int i = 0; i < tagNames.size(); ++i) {
        query.bindValue(placeholders[i], tagNames[i]);
    }
    query.bindValue(":from", from.toString(Qt::ISODate));
    query.bindValue(":to", to.toString(Qt::ISODate));

    if (!query.exec()) {
        qWarning() << "History query failed:" << query.lastError().text();
        return results;
    }

    QMap<QString, HistoryResult> resultMap;
    while (query.next()) {
        QString tag = query.value(0).toString();
        QDateTime ts = QDateTime::fromString(query.value(1).toString(), Qt::ISODate);
        double val = query.value(2).toDouble();
        resultMap[tag].tagName = tag;
        resultMap[tag].timestamps.append(ts);
        resultMap[tag].values.append(val);
    }
    results = resultMap.values();
    return results;
}

void DatabaseManager::queryHistoryAsync(const QStringList& tagNames, const QDateTime& from,
                                        const QDateTime& to, bool useAvg, int requestId)
{
    HistoryQueryRequest req{tagNames, from, to, useAvg, requestId};
    emit historyQueryRequested(req);
}

void DatabaseManager::executeHistoryQuery(const HistoryQueryRequest& req)
{
    QList<HistoryResult> results = queryHistoryInternal(req.tagNames, req.from, req.to, req.useAvg);
    emit historyQueryResultReady(req.requestId, results);
}

void DatabaseManager::cleanupOldData()
{
    QDateTime cutoff = QDateTime::currentDateTimeUtc().addDays(-7);
    QSqlQuery query(m_sqliteDb);
    query.prepare("DELETE FROM data_second WHERE timestamp < :cutoff");
    query.bindValue(":cutoff", cutoff.toString(Qt::ISODate));
    query.exec();

    cutoff = QDateTime::currentDateTimeUtc().addDays(-30);
    query.prepare("DELETE FROM data_minute WHERE timestamp < :cutoff");
    query.bindValue(":cutoff", cutoff.toString(Qt::ISODate));
    query.exec();

    cutoff = QDateTime::currentDateTimeUtc().addYears(-1);
    query.prepare("DELETE FROM data_hour WHERE timestamp < :cutoff");
    query.bindValue(":cutoff", cutoff.toString(Qt::ISODate));
    query.exec();

    // 天表不清理

    query.exec("VACUUM");

    QString dbPath = m_sqliteDb.databaseName();
    QFileInfo fi(dbPath);
    if (fi.size() > 300 * 1024 * 1024) {
        emit sqliteStorageWarning("SQLite database exceeds 300MB, consider manual cleanup");
    }
}
void DatabaseManager::delayedStart()
{
    qDebug() << "Delayed start - thread:" << QThread::currentThread();
    m_queueTimer->start(200);
    m_heartbeatTimer->start(30000);
    startAggregationTasks();
}

void DatabaseManager::addAlarm(const AlertInfo& alarm)
{
    if (!m_alarmDb.isOpen()) {
        qWarning() << "Alarm database not open, cannot save alarm";
        return;
    }
    QSqlQuery query(m_alarmDb);
    query.prepare("INSERT INTO alarm_history (alarm_id, tag_name, timestamp, message, value, level, acknowledged) "
                  "VALUES (?, ?, ?, ?, ?, ?, ?)");
    query.addBindValue(alarm.id);
    query.addBindValue(alarm.tagName);
    query.addBindValue(alarm.timestamp.toString(Qt::ISODate));
    query.addBindValue(alarm.message);
    query.addBindValue(alarm.value.toDouble());
    query.addBindValue(static_cast<int>(alarm.level));
    query.addBindValue(alarm.acknowledged ? 1 : 0);
    if (!query.exec())
        qWarning() << "Failed to add alarm:" << query.lastError().text();
    else
        emit alarmAdded(alarm);

    // 惰性检查：每 100 次写入检查一次大小
    if (++m_alarmWriteCount >= 100) {
        m_alarmWriteCount = 0;
        checkAlarmDbSizeAndClean();
    }
}

QList<AlertInfo> DatabaseManager::getActiveAlarms()
{
    QList<AlertInfo> alarms;
    if (!m_alarmDb.isOpen()) return alarms;
    QSqlQuery query(m_alarmDb);
    query.exec("SELECT * FROM alarm_history WHERE cleared=0 ORDER BY timestamp DESC");
    while (query.next()) {
        AlertInfo alarm;
        alarm.id = query.value("alarm_id").toInt();
        alarm.tagName = query.value("tag_name").toString();
        alarm.timestamp = QDateTime::fromString(query.value("timestamp").toString(), Qt::ISODate);
        alarm.message = query.value("message").toString();
        alarm.value = query.value("value").toDouble();
        alarm.level = static_cast<AlertLevel>(query.value("level").toInt());
        alarm.acknowledged = query.value("acknowledged").toBool();
        alarm.acknowledgeTime = QDateTime::fromString(query.value("ack_time").toString(), Qt::ISODate);
        alarm.operatorName = query.value("ack_user").toString();
        alarms.append(alarm);
    }
    return alarms;
}

void DatabaseManager::acknowledgeAlarm(int alarmId, const QString& user)
{
    if (!m_alarmDb.isOpen()) return;
    QSqlQuery query(m_alarmDb);
    query.prepare("UPDATE alarm_history SET acknowledged=1, ack_time=?, ack_user=? WHERE alarm_id=? AND cleared=0");
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    query.addBindValue(user);
    query.addBindValue(alarmId);
    if (!query.exec())
        qWarning() << "Failed to acknowledge alarm:" << query.lastError().text();
}

void DatabaseManager::clearAlarm(int alarmId)
{
    if (!m_alarmDb.isOpen()) return;
    QSqlQuery query(m_alarmDb);
    query.prepare("UPDATE alarm_history SET cleared=1, clear_time=? WHERE alarm_id=? AND cleared=0");
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    query.addBindValue(alarmId);
    if (!query.exec())
        qWarning() << "Failed to clear alarm:" << query.lastError().text();
}

QList<AlertInfo> DatabaseManager::getAlarmHistory(const QDateTime& from, const QDateTime& to)
{
    QList<AlertInfo> alarms;
    if (!m_alarmDb.isOpen()) return alarms;
    QSqlQuery query(m_alarmDb);
    query.prepare("SELECT * FROM alarm_history WHERE timestamp BETWEEN :from AND :to ORDER BY timestamp DESC");
    query.bindValue(":from", from.toString(Qt::ISODate));
    query.bindValue(":to", to.toString(Qt::ISODate));
    if (!query.exec()) return alarms;
    while (query.next()) {
        AlertInfo alarm;
        alarm.id = query.value("alarm_id").toInt();
        alarm.tagName = query.value("tag_name").toString();
        alarm.timestamp = QDateTime::fromString(query.value("timestamp").toString(), Qt::ISODate);
        alarm.message = query.value("message").toString();
        alarm.value = query.value("value").toDouble();
        alarm.level = static_cast<AlertLevel>(query.value("level").toInt());
        alarm.acknowledged = query.value("acknowledged").toBool();
        alarm.acknowledgeTime = QDateTime::fromString(query.value("ack_time").toString(), Qt::ISODate);
        alarm.operatorName = query.value("ack_user").toString();
        alarms.append(alarm);
    }
    return alarms;
}

void DatabaseManager::acknowledgeAlarms(const QList<int>& alarmIds, const QString& user)
{
    if (!m_alarmDb.isOpen() || alarmIds.isEmpty()) return;
    QString idList;
    for (int id : alarmIds) {
        if (!idList.isEmpty()) idList += ",";
        idList += QString::number(id);
    }
    QSqlQuery query(m_alarmDb);
    query.prepare(QString("UPDATE alarm_history SET acknowledged=1, ack_time=?, ack_user=? WHERE alarm_id IN (%1) AND cleared=0").arg(idList));
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    query.addBindValue(user);
    if (!query.exec())
        qWarning() << "Batch acknowledge failed:" << query.lastError().text();
}

void DatabaseManager::checkAlarmDbSizeAndClean()
{
    if (!m_alarmDb.isOpen()) return;
    QFileInfo fi(m_alarmDb.databaseName());
    if (fi.size() > m_alarmDbMaxSize) {
        qDebug() << "Alarm DB size" << fi.size() << "exceeds limit" << m_alarmDbMaxSize << ", cleaning...";
        cleanupOldAlarms(10000); // 保留最近 10000 条
    }
}

void DatabaseManager::cleanupOldAlarms(int keepCount)
{
    if (!m_alarmDb.isOpen()) return;
    // 获取需要保留的最早的 alarm_id（按 id 降序，因为 id 自增且时间有序）
    QSqlQuery query(m_alarmDb);
    query.prepare("SELECT alarm_id FROM alarm_history ORDER BY alarm_id DESC LIMIT 1 OFFSET :offset");
    query.bindValue(":offset", keepCount);
    if (query.exec() && query.next()) {
        int minIdToKeep = query.value(0).toInt();
        QSqlQuery deleteQuery(m_alarmDb);
        deleteQuery.prepare("DELETE FROM alarm_history WHERE alarm_id < :id");
        deleteQuery.bindValue(":id", minIdToKeep);
        if (deleteQuery.exec()) {
            int deleted = deleteQuery.numRowsAffected();
            qDebug() << "Cleaned up" << deleted << "old alarms";
            // 不执行 VACUUM，让 SQLite 自动重用空间
        } else {
            qWarning() << "Failed to clean old alarms:" << deleteQuery.lastError().text();
        }
    } else {
        qWarning() << "Failed to get alarm_id offset for cleanup:" << query.lastError().text();
    }
}

QSqlDatabase DatabaseManager::getActiveDb() const
{
    // 如果 MySQL 可用且连接打开，则使用 MySQL，否则使用 SQLite
    if (m_mysqlOk && m_mysqlDbConn.isOpen())
        return m_mysqlDbConn;
    else
        return m_sqliteDb;
}
