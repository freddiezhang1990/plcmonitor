#include "application.h"
#include "databaseconfigdialog.h"
#include "logger.h"
#include "mainwindow.h"
#include "controller.h"
#include "plcconnector.h"
#include "plcdata.h"
#include "common_types.h"
#include "databasemanager.h"
#include "usermanager.h"
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QThread>
#include <qhostaddress.h>
#include <QMessageBox>
#include "chartmanager.h"
#include "realtimedatamodel.h"
#include "trendmanager.h"
#include "processviewmanager.h"

Application* Application::m_instance = nullptr;

Application::Application(int &argc, char **argv)
    : QApplication(argc, argv)

{
    setApplicationName("PLCMonitor");
    setApplicationVersion("1.0.0");
    setOrganizationName("YourCompany");

    m_instance = this;
    // 初始化默认配置
    m_plcConfig.ip = "192.168.0.1";
    m_plcConfig.rack = 0;
    m_plcConfig.slot = 2;
    m_plcConfig.port = 102;
    m_plcConfig.autoConnect = false;
    m_plcConfig.autoStartPolling = false;
    m_plcConfig.pollingInterval = 1000;
}

Application::~Application()
{
    // 停止日志系统
    Logger::instance().cleanup();

    // 原有清理代码
    cleanup();

    if (m_instance == this) {
        m_instance = nullptr;
    }
}

bool Application::initialize()
{
    // 阶段1：立即安装消息处理器（会缓冲早期日志）
    Logger::installMessageHandler();  // 此时会缓冲，不会阻塞

    qDebug() << "=== Application 初始化开始 ===================";

    // 1. 检查单实例
    m_sharedMemory.setKey("PLCMonitor_Instance");
    if (m_sharedMemory.attach()) {
        qCritical() << "应用已在运行";
        return false;
    }
    if (!m_sharedMemory.create(1)) {
        qCritical() << "无法创建共享内存";
        return false;
    }

    // 2. 初始化日志系统但延迟启动
    Logger& logger = Logger::instance();
    QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(logPath);
    qDebug() << "日志目录:" << logPath;

    // 阶段2：延迟启动日志工作线程
    QTimer::singleShot(0, [&logger, logPath]() {
        // 设置日志文件参数
        logger.setLogFile(logPath + "/plcmonitor.log");
        logger.setRotationPolicy(100 * 1024 * 1024, 5);  // 100MB，5个备份
        logger.setLogLevel(LogDebug);

        // 启动工作线程
        logger.startWorker();

        qDebug() << "日志系统工作线程已启动";
    });

    // 3. 加载配置文件
    QString configPath = "configs/plc_config.json";
    if (!QFile::exists(configPath)) {
        qCritical() << "配置文件不存在:" << configPath;
        return false;
    }
    if (!loadPLCConfig(configPath)) {
        qCritical() << "无法加载PLC配置文件，使用默认配置";
        QFile::rename(configPath, configPath + ".backup");
        savePLCConfig(configPath);
    }

    QVector<TagInfo> tags = loadTagsFromConfig(configPath);
    if (tags.isEmpty()) {
        qWarning() << "配置文件中没有标签";
    } else {
        qDebug() << "加载" << tags.size() << "个标签";
    }

    // 4. 创建数据模型
    qDebug() << "创建 PLCData 数据模型...";
    m_dataModel = new PLCData(this);
    m_dataModel->setTags(tags);

    // 5. 创建 PLC 连接器（先不创建定时器）
    qDebug() << "创建 PLCConnector...";
    m_plcConnector = new PLCConnector(m_dataModel, nullptr);
    m_plcConnector->setTags(tags);

    // 6. 创建 PLC 工作线程并移动连接器
    qDebug() << "创建 PLC 工作线程...";
    m_plcThread = new QThread(this);
    m_plcConnector->moveToThread(m_plcThread);

    // 7. 启动工作线程（这样 initTimers 才能在正确线程执行）
    m_plcThread->start();

    // 8. 在工作线程中初始化定时器（关键修复）
    QMetaObject::invokeMethod(m_plcConnector, "initTimers", Qt::QueuedConnection);

    // 9. 创建控制器
    qDebug() << "创建 Controller...";
    m_controller = new Controller(m_dataModel, m_plcConnector, this);

    // 10. 初始化数据库（SQLite 必需，MySQL 尝试连接）
    qDebug() << "初始化数据库...";
    if (!DatabaseManager::instance().initialize(m_plcConfig.mysqlHost,
                                                m_plcConfig.mysqlPort,
                                                m_plcConfig.mysqlDatabase,
                                                m_plcConfig.mysqlUser,
                                                m_plcConfig.mysqlPassword)) {
        qCritical() << "数据库初始化失败，历史数据无法保存";
    } else {
        DatabaseManager::instance().startHeartbeat(30);
    }

    // ========== 新增：初始化用户管理器 ==========
    if (!UserManager::instance().initialize()) {
        qCritical() << "用户管理器初始化失败";
        return false;
    }
    // 在加载配置之后，创建任何 UI 之前
    UserManager::instance().logout();

    // 11. 创建并初始化各个管理器
    qDebug() << "创建并初始化各个管理器...";
    m_realTimeModel = new RealTimeDataModel(this);
    qDebug() << "  RealTimeDataModel 创建完成";
    m_chartManager = new ChartManager(this);
    qDebug() << "  ChartManager 创建完成";
    m_trendManager = new TrendManager(this);
    qDebug() << "  TrendManager 创建完成";
    m_processViewManager = new ProcessViewManager(this);
    qDebug() << "  ProcessViewManager 创建完成";

    // 12. 将管理器传递给控制器
    qDebug() << "将管理器传递给 Controller...";
    m_controller->setRealTimeModel(m_realTimeModel);
    m_controller->setChartManager(m_chartManager);
    m_controller->setTrendManager(m_trendManager);
    m_controller->setProcessViewManager(m_processViewManager);
    qDebug() << "  管理器指针已设置到 Controller";

    // 13. 初始化控制器
    qDebug() << "开始初始化控制器...";
    bool initResult = m_controller->initialize();
    qDebug() << "controller->initialize() 返回:" << initResult;
    qDebug() << "controller->isInitialized() =" << m_controller->isInitialized();

    // 14. 创建主窗口
    m_mainWindow = new MainWindow(m_dataModel, m_plcConnector, m_plcThread, nullptr);

    // 15. 在主窗口创建后检查 MySQL 连接状态并弹窗
    connect(&DatabaseManager::instance(), &DatabaseManager::mysqlConnectionChanged,
            this, [this](bool ok) {
                QString msg = ok ? "MySQL 已连接" : "MySQL 连接断开，使用本地 SQLite";
                qDebug() << msg;
            });

    bool mysqlConfigured = !m_plcConfig.mysqlHost.isEmpty() && !m_plcConfig.mysqlDatabase.isEmpty();
    bool mysqlConnected = DatabaseManager::instance().isMySQLConnected();
    if (mysqlConfigured && !mysqlConnected) {
        QMessageBox msgBox;
        msgBox.setWindowTitle("数据库连接提醒");
        msgBox.setText("MySQL 数据库连接失败，将使用本地 SQLite 存储。\n\n"
                       "是否现在配置 MySQL 连接参数？");
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::Yes);
        if (msgBox.exec() == QMessageBox::Yes) {
            if (UserManager::instance().hasPermission(UserRole::ENGINEER)) {
                DatabaseConfigDialog dlg(m_mainWindow);
                if (dlg.exec() == QDialog::Accepted) {
                    // 配置已保存并生效
                }
            } else {
                QMessageBox::warning(m_mainWindow, "权限不足",
                                     "您没有权限修改数据库配置，请联系工程师或管理员。");
            }
        }
    }

    // 16. 将管理器传递给主窗口
    qDebug() << "将管理器传递给 MainWindow...";
    m_mainWindow->setDataManagers(m_realTimeModel, m_chartManager,
                                  m_trendManager, m_processViewManager);
    qDebug() << "  管理器指针已设置到 MainWindow";

    // 17. 建立关键信号连接
    setupConnections();

    // 18. 安装消息处理器

    // 19. 显示主窗口
    m_mainWindow->show();

    // 20. 根据配置决定是否自动连接
    qDebug() << "=== 检查PLC自动连接配置 ===";
    qDebug() << "自动连接选项:" << m_plcConfig.autoConnect;
    qDebug() << "自动开始轮询:" << m_plcConfig.autoStartPolling;
    if (m_plcConfig.autoConnect && m_mainWindow) {
        QTimer::singleShot(1000, this, [this]() {
            qDebug() << "=== 启动自动PLC连接 ===";
            emit m_mainWindow->connectRequested(
                m_plcConfig.ip,
                m_plcConfig.rack,
                m_plcConfig.slot);
        });
    } else {
        qDebug() << "未启用自动连接，需要手动连接";
    }

    qDebug() << "=== Application 初始化完成 ===================";
    return true;
}

QVector<TagInfo> Application::loadTagsFromConfig(const QString &configPath)
{
    QVector<TagInfo> tags;

    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Cannot open config file:" << configPath;
        return tags;
    }

    QByteArray jsonData = file.readAll();
    file.close();

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "JSON parse error:" << error.errorString();
        return tags;
    }

    if (!doc.isObject()) {
        qWarning() << "Config file is not a JSON object";
        return tags;
    }

    QJsonObject root = doc.object();
    if (!root.contains("tags") || !root["tags"].isArray()) {
        qWarning() << "Config file does not contain 'tags' array";
        return tags;
    }

    QJsonArray tagsArray = root["tags"].toArray();
    for (const QJsonValue &value : tagsArray) {
        if (!value.isObject()) continue;

        QJsonObject obj = value.toObject();
        TagInfo tag;

        tag.name = obj["name"].toString();
        tag.dbNumber = obj["dbNumber"].toInt(1);
        tag.byteOffset = obj["byteOffset"].toInt(0);

        if (obj.contains("bitOffset")) {
            tag.bitOffset = obj["bitOffset"].toInt(-1);
        } else {
            tag.bitOffset = -1;
        }

        // 关键修正：使用 common_types.h 中的 stringToTagDataType 函数
        if (obj.contains("dataType")) {
            tag.dataType = stringToTagDataType(obj["dataType"].toString());
        } else {
            tag.dataType = TagDataType::BOOL;  // 默认值
        }

        tag.writable = obj.contains("writable") ? obj["writable"].toBool(false) : false;

        // 可选：解析其他字段
        if (obj.contains("description")) {
            tag.description = obj["description"].toString();
        }
        if (obj.contains("unit")) {
            tag.unit = obj["unit"].toString();
        }
        if (obj.contains("scalingFactor")) {
            tag.scalingFactor = obj["scalingFactor"].toDouble(1.0);
        }
        if (obj.contains("offset")) {
            tag.offset = obj["offset"].toDouble(0.0);
        }

        // 生成地址字符串
        QString dataTypeStr = tagDataTypeToString(tag.dataType);
        if (tag.dataType == TagDataType::BOOL && tag.bitOffset >= 0) {
            tag.address = QString("DB%1.DBX%2.%3")
            .arg(tag.dbNumber)
                .arg(tag.byteOffset)
                .arg(tag.bitOffset);
        } else if (tag.dataType == TagDataType::REAL || tag.dataType == TagDataType::DWORD) {
            tag.address = QString("DB%1.DBD%2")
            .arg(tag.dbNumber)
                .arg(tag.byteOffset);
        } else if (tag.dataType == TagDataType::INT || tag.dataType == TagDataType::WORD) {
            tag.address = QString("DB%1.DBW%2")
            .arg(tag.dbNumber)
                .arg(tag.byteOffset);
        } else if (tag.dataType == TagDataType::BYTE) {
            tag.address = QString("DB%1.DBB%2")
            .arg(tag.dbNumber)
                .arg(tag.byteOffset);
        } else {
            tag.address = QString("DB%1.Offset%2")
            .arg(tag.dbNumber)
                .arg(tag.byteOffset);
        }

        if (!tag.name.isEmpty()) {
            tags.append(tag);
            qDebug() << "Application: Loaded tag:" << tag.name
                     << "Address:" << tag.address
                     << "Type:" << dataTypeStr;
        }
    }
    return tags;
}

bool Application::loadPLCConfig(const QString &configPath)
{
    QString actualPath = configPath.isEmpty() ? m_configPath : configPath;

    qDebug() << "Application: Loading PLC config from:" << actualPath;

    QFile file(actualPath);
    if (!file.open(QIODevice::ReadOnly)) {
        QString error = QString("无法打开配置文件: %1").arg(file.errorString());
        qWarning() << error;
        emit configError(error);
        return false;
    }

    QByteArray configData = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument configDoc = QJsonDocument::fromJson(configData, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        QString error = QString("JSON解析错误: %1").arg(parseError.errorString());
        qWarning() << error;
        emit configError(error);
        return false;
    }

    if (!configDoc.isObject()) {
        QString error = "配置文件不是有效的JSON对象";
        qWarning() << error;
        emit configError(error);
        return false;
    }

    QJsonObject root = configDoc.object();

    // PLC配置
    PLCConfig newConfig = m_plcConfig; // 保留现有配置作为默认值

    if (root.contains("plc") && root["plc"].isObject()) {
        QJsonObject plcObj = root["plc"].toObject();

        if (plcObj.contains("ip") && plcObj["ip"].isString()) {
            newConfig.ip = plcObj["ip"].toString();
        }

        if (plcObj.contains("rack") && plcObj["rack"].isDouble()) {
            newConfig.rack = plcObj["rack"].toInt();
        }

        if (plcObj.contains("slot") && plcObj["slot"].isDouble()) {
            newConfig.slot = plcObj["slot"].toInt();
        }

        if (plcObj.contains("port") && plcObj["port"].isDouble()) {
            newConfig.port = plcObj["port"].toInt();
        }
    }

    // 轮询配置
    if (root.contains("polling") && root["polling"].isObject()) {
        QJsonObject pollingObj = root["polling"].toObject();

        if (pollingObj.contains("auto_start") && pollingObj["auto_start"].isBool()) {
            newConfig.autoStartPolling = pollingObj["auto_start"].toBool();
        }

        if (pollingObj.contains("interval") && pollingObj["interval"].isDouble()) {
            int interval = pollingObj["interval"].toInt();
            if (interval >= 100 && interval <= 10000) {
                newConfig.pollingInterval = interval;
            }
        }
    }

    // 连接配置
    if (root.contains("connection") && root["connection"].isObject()) {
        QJsonObject connObj = root["connection"].toObject();

        if (connObj.contains("auto_connect") && connObj["auto_connect"].isBool()) {
            newConfig.autoConnect = connObj["auto_connect"].toBool();
        }
    }

    // 数据库配置（MySQL）
    if (root.contains("database") && root["database"].isObject()) {
        QJsonObject dbObj = root["database"].toObject();
        if (dbObj.contains("mysql_host") && dbObj["mysql_host"].isString())
            newConfig.mysqlHost = dbObj["mysql_host"].toString();
        if (dbObj.contains("mysql_port") && dbObj["mysql_port"].isDouble())
            newConfig.mysqlPort = dbObj["mysql_port"].toInt();
        if (dbObj.contains("mysql_database") && dbObj["mysql_database"].isString())
            newConfig.mysqlDatabase = dbObj["mysql_database"].toString();
        if (dbObj.contains("mysql_user") && dbObj["mysql_user"].isString())
            newConfig.mysqlUser = dbObj["mysql_user"].toString();
        if (dbObj.contains("mysql_password") && dbObj["mysql_password"].isString())
            newConfig.mysqlPassword = dbObj["mysql_password"].toString();
    }

    // 验证配置
    if (!validatePLCConfig(newConfig)) {
        QString error = "配置验证失败";
        qWarning() << error;
        emit configError(error);
        return false;
    }

    m_plcConfig = newConfig;

    qDebug() << "Application: PLC Config loaded:";
    qDebug() << "  IP:" << m_plcConfig.ip;
    qDebug() << "  Rack:" << m_plcConfig.rack;
    qDebug() << "  Slot:" << m_plcConfig.slot;
    qDebug() << "  Port:" << m_plcConfig.port;
    qDebug() << "  Auto Connect:" << m_plcConfig.autoConnect;
    qDebug() << "  Auto Start Polling:" << m_plcConfig.autoStartPolling;
    qDebug() << "  Polling Interval:" << m_plcConfig.pollingInterval;

    emit configLoaded(m_plcConfig);
    return true;
}

bool Application::savePLCConfig(const QString &configPath)
{
    QString actualPath = configPath.isEmpty() ? m_configPath : configPath;

    // 确保配置目录存在
    QFileInfo fileInfo(actualPath);
    QDir configDir = fileInfo.dir();
    if (!configDir.exists() && !configDir.mkpath(".")) {
        QString error = QString("无法创建配置目录: %1").arg(configDir.path());
        qWarning() << error;
        emit configError(error);
        return false;
    }

    // 1. 读取现有配置（如果文件存在）
    QJsonObject root;
    QFile readFile(actualPath);
    if (readFile.open(QIODevice::ReadOnly)) {
        QByteArray data = readFile.readAll();
        readFile.close();
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(data, &error);
        if (error.error == QJsonParseError::NoError && doc.isObject()) {
            root = doc.object();
        } else {
            qWarning() << "Existing config parse error, will create new config.";
        }
    }

    // 2. 更新 PLC 配置部分
    QJsonObject plcObj;
    plcObj["ip"] = m_plcConfig.ip;
    plcObj["rack"] = m_plcConfig.rack;
    plcObj["slot"] = m_plcConfig.slot;
    plcObj["port"] = m_plcConfig.port;
    root["plc"] = plcObj;

    // 3. 更新轮询配置
    QJsonObject pollingObj;
    pollingObj["enabled"] = true;
    pollingObj["interval"] = m_plcConfig.pollingInterval;
    pollingObj["auto_start"] = m_plcConfig.autoStartPolling;
    root["polling"] = pollingObj;

    // 4. 更新连接配置
    QJsonObject connObj;
    connObj["auto_connect"] = m_plcConfig.autoConnect;
    connObj["timeout"] = 5000;      // 可根据需要保存实际超时值
    connObj["retry_count"] = 3;
    root["connection"] = connObj;

    // 5. 更新数据库配置（如果存在且需要）
    QJsonObject dbObj;
    dbObj["mysql_host"] = m_plcConfig.mysqlHost;
    dbObj["mysql_port"] = m_plcConfig.mysqlPort;
    dbObj["mysql_database"] = m_plcConfig.mysqlDatabase;
    dbObj["mysql_user"] = m_plcConfig.mysqlUser;
    dbObj["mysql_password"] = m_plcConfig.mysqlPassword;
    root["database"] = dbObj;

    // 6. 关键：不要修改 root 中的 "tags" 字段，它会自动保留

    // 7. 写入配置文件
    QFile writeFile(actualPath);
    if (!writeFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QString error = QString("无法写入配置文件: %1").arg(writeFile.errorString());
        qWarning() << error;
        emit configError(error);
        return false;
    }

    QJsonDocument configDoc(root);
    writeFile.write(configDoc.toJson(QJsonDocument::Indented));
    writeFile.close();

    emit configSaved(actualPath);
    return true;
}

bool Application::validatePLCConfig(const PLCConfig &config)
{
    // 验证IP地址格式
    QHostAddress address;
    if (!address.setAddress(config.ip)) {
        qWarning() << "Invalid IP address:" << config.ip;
        return false;
    }

    // 验证Rack和Slot范围
    if (config.rack < 0 || config.rack > 7) {
        qWarning() << "Invalid rack number:" << config.rack;
        return false;
    }

    if (config.slot < 0 || config.slot > 31) {
        qWarning() << "Invalid slot number:" << config.slot;
        return false;
    }

    // 验证端口范围
    if (config.port < 1 || config.port > 65535) {
        qWarning() << "Invalid port number:" << config.port;
        return false;
    }

    // 验证轮询间隔
    if (config.pollingInterval < 100 || config.pollingInterval > 10000) {
        qWarning() << "Invalid polling interval:" << config.pollingInterval;
        return false;
    }

    return true;
}

bool Application::loadConfig()
{
    return loadPLCConfig(m_configPath);
}

bool Application::saveConfig()
{
    return savePLCConfig(m_configPath);
}
// 添加连接设置方法
void Application::setupConnections()
{
    qDebug() << "=== 建立信号连接 ========================";

    // 先验证所有指针的有效性
    if (!m_controller || !m_mainWindow || !m_plcConnector || !m_dataModel) {
        qCritical() << "❌ 关键组件未初始化:";
        return;
    }


    // 1. PLCConnector -> Controller
    connect(m_plcConnector, &PLCConnector::connected,
            m_controller, [this]() { qDebug() << "PLC连接成功信号 -> Controller"; }, Qt::QueuedConnection);
    connect(m_plcConnector, &PLCConnector::disconnected,
            m_controller, [this]() { qDebug() << "PLC断开连接信号 -> Controller"; }, Qt::QueuedConnection);
    connect(m_plcConnector, &PLCConnector::allDataUpdated,
            m_controller, &Controller::onPLCDataUpdated, Qt::QueuedConnection);
    connect(m_plcConnector, &PLCConnector::connectionError,
            m_controller, &Controller::onPLCConnectionError, Qt::QueuedConnection);
    connect(m_plcConnector, &PLCConnector::writeCompleted,
            m_controller, &Controller::onPLCWriteCompleted, Qt::QueuedConnection);
    connect(m_plcConnector, &PLCConnector::statusMessage,
            m_controller, &Controller::onStatusMessage, Qt::QueuedConnection);
    connect(m_plcConnector, &PLCConnector::controllerStateChanged,
            m_mainWindow, &MainWindow::onConnectionStateChanged, Qt::QueuedConnection);

    // 2. Controller -> MainWindow
    connect(m_controller, &Controller::tableDataUpdated,
            m_mainWindow, &MainWindow::onTableDataUpdated, Qt::QueuedConnection);
    connect(m_controller, &Controller::chartDataUpdated,
            m_mainWindow, &MainWindow::onChartDataUpdated, Qt::QueuedConnection);
    connect(m_controller, &Controller::processDataUpdated,
            m_mainWindow, &MainWindow::onProcessDataUpdated, Qt::QueuedConnection);
    connect(m_controller, &Controller::alertTriggered,
            m_mainWindow, &MainWindow::onAlertTriggered, Qt::QueuedConnection);

    // 3. MainWindow -> PLCConnector
    bool connectRequestedConnected = connect(m_mainWindow, &MainWindow::connectRequested,
                                             m_plcConnector, &PLCConnector::connectToPLC,
                                             Qt::QueuedConnection);
    if (!connectRequestedConnected) {
        qDebug() << "⚠️ 直接连接失败，使用 lambda + invokeMethod";
        connect(m_mainWindow, &MainWindow::connectRequested,
                this, [this](const QString& ip, int rack, int slot) {
                    if (m_plcConnector) {
                        QMetaObject::invokeMethod(m_plcConnector, "connectToPLC",
                                                  Qt::QueuedConnection,
                                                  Q_ARG(QString, ip),
                                                  Q_ARG(int, rack),
                                                  Q_ARG(int, slot));
                    }
                });
    }

    connect(m_mainWindow, &MainWindow::disconnectRequested,
            m_plcConnector, &PLCConnector::disconnectFromPLC, Qt::QueuedConnection);

    connect(m_mainWindow, &MainWindow::systemResumed, this, [this]() {
        if (m_plcConnector && !m_plcConnector->isConnected()) {
            qDebug() << "Application: system resumed, triggering auto-reconnect";
            QMetaObject::invokeMethod(m_plcConnector, "startAutoReconnect", Qt::QueuedConnection);
        }
    });

    // 4. MainWindow -> Controller
    connect(m_mainWindow, &MainWindow::writeRequested,
            m_controller, &Controller::onWriteRequest, Qt::QueuedConnection);

    // 5. PLCData -> MainWindow
    connect(m_dataModel, &PLCData::tagValueChanged,
            m_mainWindow, &MainWindow::onTagValueChanged, Qt::QueuedConnection);

    // 6. TagManager -> Controller
    if (m_mainWindow && m_mainWindow->tagManager()) {
        connect(m_mainWindow->tagManager(), &TagManager::selectionApplied,
                m_controller, &Controller::onTagSelectionChanged, Qt::QueuedConnection);
    }

    // 7. 调试信号
    connect(m_plcConnector, &PLCConnector::controllerStateChanged,
            this, [](ControllerState state) { qDebug() << "[PLCConnector] Controller 状态变化:" << controllerStateToString(state); });
    connect(m_plcConnector, &PLCConnector::plcConnectionStateChanged,
            this, [](PLCConnectionState state) { qDebug() << "[PLCConnector] PLC 连接状态变化:" << plcConnectionStateToString(state); });

    // 8. 传递配置到 MainWindow
    QTimer::singleShot(100, this, [this]() {
        emit plcConfigLoaded(m_plcConfig.ip, m_plcConfig.rack,
                             m_plcConfig.slot, m_plcConfig.port);
    });

    qDebug() << "=== 所有信号连接完成 ====================";
}


void Application::cleanup()
{
    // 1. 断开所有连接，避免在销毁过程中收到信号
    if (m_plcConnector) {
        disconnect(m_plcConnector, nullptr, this, nullptr);
        disconnect(m_plcConnector, nullptr, m_mainWindow, nullptr);
    }
    if (m_controller) {
        disconnect(m_controller, nullptr, m_mainWindow, nullptr);
    }

    // 2. 停止 PLC 工作线程（不再接收数据）
    if (m_plcThread && m_plcThread->isRunning()) {
        // 先通知停止轮询和断开连接
        if (m_plcConnector) {
            QMetaObject::invokeMethod(m_plcConnector, "stopPolling", Qt::QueuedConnection);
            QMetaObject::invokeMethod(m_plcConnector, "disconnectFromPLC", Qt::QueuedConnection);
        }
        m_plcThread->quit();
        m_plcThread->wait();
        qDebug() << "PLC 工作线程已停止";
    }

    // 3. 删除主窗口（此时不再有信号发出）
    if (m_mainWindow) {
        delete m_mainWindow;
        m_mainWindow = nullptr;
    }

    // 4. 清理其他对象（可选）
    if (m_plcConnector) {
        m_plcConnector->deleteLater();
    }
    if (m_controller) {
        m_controller->deleteLater();
    }
}