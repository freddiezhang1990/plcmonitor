#ifndef LOGGER_H
#define LOGGER_H

#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <QThread>
#include <QQueue>
#include <QFile>
#include <QTextStream>

enum LogLevel {
    LogDebug = 0,
    LogInfo,
    LogWarning,
    LogError,
    LogOff
};

class Logger : public QObject
{
    Q_OBJECT
public:
    static Logger& instance();

    // 新增启动方法
    void startWorker();

    // 异步设置日志文件路径（不会阻塞主线程）
    void setLogFile(const QString &filePath);

    void setLogLevel(LogLevel level) { m_logLevel = level; }
    LogLevel logLevel() const { return m_logLevel; }

    // 异步设置轮转参数
    void setRotationPolicy(qint64 maxSizeBytes, int maxBackupFiles);

    void log(LogLevel level, const QString &message, const char *file = nullptr, int line = 0);
    void debug(const QString &msg) { log(LogDebug, msg); }
    void info(const QString &msg)  { log(LogInfo, msg); }
    void warning(const QString &msg) { log(LogWarning, msg); }
    void error(const QString &msg) { log(LogError, msg); }

    static void installMessageHandler();

    // 新增：一个极简的、用于高频调用的日志入队函数
    // 这个函数将被 qInstallMessageHandler 调用
    void enqueueLogMessage(const QString &message);

    // 添加析构辅助方法
    void cleanup();

    ~Logger();

private slots:
    void doSetLogFile();          // 在工作线程中执行实际的文件打开和初始化

private:
    Logger();
    void workerLoop();
    void writeToFile(const QString &line);
    void rotateLogFile();
    void cleanupOldBackups();
    QString generateBackupFileName() const;

    QMutex          m_mutex;

    QQueue<QString> m_queue;      // 日志消息队列
    QMutex          m_queueMutex;          // 专门保护队列的互斥锁
    QWaitCondition  m_queueCond;   // 用于通知工作线程
    QMutex          m_fileMutex;       // 【新增】专门保护文件写入和轮转状态
    QThread         m_workerThread;
    bool            m_stop = false;

    QFile           m_logFile;
    QTextStream     m_logStream;
    LogLevel        m_logLevel = LogInfo;

    QString         m_currentLogPath;
    qint64          m_maxFileSize = 100 * 1024 * 1024;
    int             m_maxBackupFiles = 5;

    bool            m_pendingOpen = false;   // 是否有待打开的文件

};

void logMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);

#endif // LOGGER_H