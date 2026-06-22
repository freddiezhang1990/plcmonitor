#include "logger.h"
#include <QDateTime>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QMessageLogContext>
#include <cstdio>

static Logger *g_loggerInstance = nullptr;

Logger& Logger::instance()
{
    if (!g_loggerInstance) {
        g_loggerInstance = new Logger();
    }
    return *g_loggerInstance;
}

Logger::Logger()
    : m_stop(false)
    , m_logLevel(LogInfo)
    , m_maxFileSize(10 * 1024 * 1024)  // 10MB
    , m_maxBackupFiles(5)
    , m_pendingOpen(false)

{
    // 不在构造函数中启动线程
    // 线程将在startWorker()中启动
}

Logger::~Logger()
{
    {
        QMutexLocker locker(&m_mutex);
        m_stop = true;
    }
    m_workerThread.quit();
    m_workerThread.wait();

    if (m_logFile.isOpen()) {
        m_logStream.flush();
        m_logFile.close();
    }
}

void Logger::enqueueLogMessage(const QString &message) {
    // 1. 快速加锁
    m_queueMutex.lock();
    // 2. 将消息放入队列
    m_queue.enqueue(message);
    // 3. 唤醒工作线程
    m_queueCond.wakeOne();
    // 4. 快速解锁
    m_queueMutex.unlock();
    // 整个过程非常快，UI线程几乎感觉不到停顿
}

void Logger::startWorker() {
    if (!m_workerThread.isRunning()) {
        connect(&m_workerThread, &QThread::started,
                this, &Logger::workerLoop,
                Qt::DirectConnection);
        m_workerThread.start();
    }
}

void Logger::setLogFile(const QString &filePath)
{
    {
        QMutexLocker locker(&m_mutex);
        m_currentLogPath = filePath;
        m_pendingOpen = true;
    }
    // 在工作线程中执行实际的文件打开操作
    QMetaObject::invokeMethod(this, "doSetLogFile", Qt::QueuedConnection);
}

void Logger::doSetLogFile()
{
    QMutexLocker locker(&m_mutex);
    if (!m_pendingOpen) return;
    m_pendingOpen = false;

    // 关闭当前文件（如果有）
    if (m_logFile.isOpen()) {
        m_logStream.flush();
        m_logFile.close();
    }

    // 确保目录存在
    QDir().mkpath(QFileInfo(m_currentLogPath).absolutePath());

    // 尝试打开文件
    m_logFile.setFileName(m_currentLogPath);
    if (m_logFile.open(QIODevice::Append | QIODevice::Text)) {
        m_logStream.setDevice(&m_logFile);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        m_logStream.setCodec("UTF-8");
#endif
        // 检查是否需要立即轮转（文件大小已超过限制）
        if (m_logFile.size() >= m_maxFileSize) {
            locker.unlock();
            rotateLogFile();      // 在工作线程中执行轮转
            locker.relock();
            // 轮转后重新打开当前日志文件（rotateLogFile 中已处理）
        }
    } else {
        qWarning() << "Failed to open log file:" << m_currentLogPath;
    }
}

void Logger::setRotationPolicy(qint64 maxSizeBytes, int maxBackupFiles)
{
    QMutexLocker locker(&m_mutex);
    m_maxFileSize = maxSizeBytes;
    m_maxBackupFiles = maxBackupFiles;
}


void Logger::log(LogLevel level, const QString &message, const char *file, int line) {
    // 这个函数现在只负责格式化字符串，然后调用 enqueueLogMessage
    // 它不再直接操作 m_queue，从而避免了在工作线程和UI线程之间产生复杂的锁竞争

    if (level < m_logLevel) {
        return;
    }

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString levelStr;
    switch (level) {
    case LogDebug: levelStr = "DEBUG"; break;
    case LogInfo:  levelStr = "INFO";  break;
    case LogWarning: levelStr = "WARNING"; break;
    case LogError: levelStr = "ERROR"; break;
    default: levelStr = "LOG"; break;
    }

    QString formatted = QString("[%1] %2: %3").arg(timestamp, levelStr, message);
    if (file && line && file[0] != '\0') {
        QString fileName = QFileInfo(file).fileName();
        formatted.append(QString(" (%1:%2)").arg(fileName).arg(line));
    }

    // 核心：将格式化好的字符串交给无锁（或低锁竞争）的入队函数
    enqueueLogMessage(formatted);
}

void Logger::workerLoop() {
    while (true) {
        QString line;
        {
            // 1. 加锁，准备从队列取数据
            QMutexLocker locker(&m_queueMutex);

            // 2. 如果队列为空，就等待，避免CPU空转
            while (m_queue.isEmpty() && !m_stop) {
                m_queueCond.wait(&m_queueMutex, 100); // 等待100ms或直到被唤醒
            }

            if (m_stop && m_queue.isEmpty()) {
                break; // 退出循环
            }

            if (!m_queue.isEmpty()) {
                line = m_queue.dequeue(); // 取出队首消息
            }
        } // 3. locker 在此处析构，自动解锁。锁的持有时间非常短。

        // 4. 在锁外处理耗时的文件写入操作
        if (!line.isEmpty()) {
            writeToFile(line);
        }
    }
}

void Logger::writeToFile(const QString &line) {
    // 1. 执行文件写入操作
    // 注意：这里不再需要任何锁，因为此函数只在工作线程中被调用
    if (m_logFile.isOpen()) {
        m_logStream << line << Qt::endl;
        m_logStream.flush();
    } else {
        // 如果文件未打开，输出到标准错误
        fprintf(stderr, "%s\n", qPrintable(line));
        fflush(stderr);
    }
    // 2. 检查是否需要轮转文件
    // 我们只在检查文件大小时短暂加锁，避免在写入大块数据时持有锁
    bool needRotate = false;
    {
        QMutexLocker locker(&m_fileMutex);
        if (m_logFile.isOpen() && m_logFile.size() >= m_maxFileSize) {
            needRotate = true;
        }
    }

    // 3. 如果需要轮转，则执行轮转操作
    // rotateLogFile 内部会处理自己的加锁逻辑
    if (needRotate) {
        rotateLogFile();
    }
}

void Logger::rotateLogFile() {
    // 确保这里使用的是 m_fileMutex
    QMutexLocker locker(&m_fileMutex);

    if (!m_logFile.isOpen()) return;

    // 1. 关闭当前文件
    m_logStream.flush();
    m_logFile.close();

    // 2. 生成备份文件名并重命名
    QString backupName = generateBackupFileName();
    QString backupPath = QFileInfo(m_currentLogPath).absolutePath() + "/" + backupName;

    if (!QFile::rename(m_currentLogPath, backupPath)) {
        qWarning() << "Failed to rotate log file, cannot rename" << m_currentLogPath;
        // 恢复打开原文件
        m_logFile.setFileName(m_currentLogPath);
        m_logFile.open(QIODevice::Append | QIODevice::Text);
        m_logStream.setDevice(&m_logFile);
        return;
    }

    // 3. 重新打开新的当前日志文件
    m_logFile.setFileName(m_currentLogPath);
    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Cannot create new log file, logging to stderr";
    } else {
        m_logStream.setDevice(&m_logFile);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        m_logStream.setCodec("UTF-8");
#endif
    }

    // 4. 清理旧备份
    cleanupOldBackups();
}

void Logger::cleanupOldBackups()
{
    QDir dir(QFileInfo(m_currentLogPath).absolutePath());
    QString baseName = QFileInfo(m_currentLogPath).baseName();
    QStringList filters;
    filters << baseName + "_*.log";
    QFileInfoList backupFiles = dir.entryInfoList(filters, QDir::Files, QDir::Time);
    int count = backupFiles.size();
    for (int i = m_maxBackupFiles; i < count; ++i) {
        QFile::remove(backupFiles[i].absoluteFilePath());
    }
}

QString Logger::generateBackupFileName() const
{
    QString timeStr = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString baseName = QFileInfo(m_currentLogPath).baseName();
    return QString("%1_%2.log").arg(baseName, timeStr);
}

void logMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {

#ifndef QT_DEBUG
    if (type == QtDebugMsg || type == QtInfoMsg) {
        return;
    }
#endif
    // 1. 获取时间戳（精确到毫秒）
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");

    // 2. 获取日志级别
    QString levelStr;
    switch (type) {
    case QtDebugMsg:    levelStr = "DEBUG"; break;
    case QtInfoMsg:     levelStr = "INFO"; break;
    case QtWarningMsg:  levelStr = "WARNING"; break;
    case QtCriticalMsg: levelStr = "ERROR"; break;
    case QtFatalMsg:    levelStr = "FATAL"; break;
    default:            levelStr = "UNKNOWN"; break;
    }

    // 3. 格式化文件信息
    QString fileInfo;
    if (context.file && context.line > 0) {
        // 只取文件名，不要完整路径
        QString fileName = QString(context.file).split('/').last().split('\\').last();
        fileInfo = QString(" (%1:%2)").arg(fileName).arg(context.line);
    }

    // 4. 拼接最终格式
    QString formattedMsg = QString("[%1] %2: %3%4")
                               .arg(timestamp, levelStr, msg, fileInfo);

    // 5. 入队（使用我们之前优化过的无锁/低锁入队函数）
    Logger::instance().enqueueLogMessage(formattedMsg);
}

void Logger::installMessageHandler()
{
   // Logger& logger = instance();
    qInstallMessageHandler(logMessageHandler);
}


void Logger::cleanup()
{
    // 设置停止标志并唤醒工作线程
    {
        QMutexLocker locker(&m_mutex);
        m_stop = true;
        m_queueCond.wakeOne();   // 原 m_cond 替换为 m_queueCond
    }

    // 等待工作线程结束
    if (m_workerThread.isRunning()) {
        m_workerThread.quit();
        m_workerThread.wait(2000);
        if (m_workerThread.isRunning()) {
            m_workerThread.terminate();
            m_workerThread.wait();
        }
    }

    // 关闭日志文件
    if (m_logFile.isOpen()) {
        m_logStream.flush();
        m_logFile.close();
    }

    // 清理队列缓冲区
    {
        QMutexLocker locker(&m_queueMutex);
        m_queue.clear();
    }
}