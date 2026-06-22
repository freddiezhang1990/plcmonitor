#include "application.h"
#include "databaseconfigdialog.h"
#include "logger.h"
#include "mainwindow.h"
#include "controller.h"
#include "commmanager.h"
#include "s7connector.h"
#include "modbustcpconnector.h"
#include "modbusrtuconnector.h"
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
        logger.setLogFile(logPath + "/plcmonitor.log");
        logger.setRotationPolicy(100 * 1024 * 1024, 5);  // 100MB，5个备份
        logger.setLogLevel(LogDebug);
        logger.startWorker();
        qDebug() << "日志系统工作线程已启动";
    });

    // 3. 加载配置文件
    QString configPath = "configs/plc_config.json";
    if (!QFile::exists(configPath)) {
        qCritical() << "配置文件不存在:" << configPath;
        return false;
    }

    // 加载 PLC 连接参数（旧配置中可能包含 ip, rack, slot 等，仍需要）
    if (!loadPLCConfig(configPath)) {
        qCritical() << "无法加载PLC配置文件，使用默认配置";
        QFile::rename(configPath, configPath + ".backup");
        savePLCConfig(configPath);
    }

    // 4. 创建数据模型（新版 PLCData）
    qDebug() << "创建 PLCData 数据模型...";
    m_dataModel = new PLCData(this);

    // 5.  加载设备配置
    if (!m_dataModel->loadFromJson("configs/devices.json")) {
        qCritical() << "加载设备配置失败，将使用空配置";
    } else {
        qDebug() << "设备配置加载成功，设备数量:" << m_dataModel->getAllDevices().size();
    }

    // ========== 新增：收集启用数据库的标签并设置 ==========
    QStringList enabledTagKeys;
    for (DeviceConfig* dev : m_dataModel->getAllDevices()) {
        // 递归遍历所有分组
        std::function<void(TagGroup*)> collect = [&](TagGroup* group) {
            for (const TagInfo& tag : group->tags) {
                if (tag.dbEnabled) {
                    enabledTagKeys.append(m_dataModel->makeTagKey(dev->deviceId, tag.name));
                }
            }
            for (TagGroup* sub : group->subGroups) {
                collect(sub);
            }
        };
        for (TagGroup* rootGroup : dev->rootGroups) {
            collect(rootGroup);
        }
    }
    DatabaseManager::instance().setEnabledTags(m_dataModel->getEnabledTagKeys());
    qDebug() << "初始化：已设置" << enabledTagKeys.size() << "个标签到数据库管理器";
    // =========================================

    // 6. 创建通讯管理器并注册协议工厂
    qDebug() << "创建 CommManager...";
    m_commManager = new CommManager(this);

    // 注册 S7 协议工厂
    m_commManager->registerFactory("S7", [](DeviceConfig *cfg, QObject *parent) -> IConnector* {
        return new S7Connector(cfg, parent);
    });
    qDebug() << "已注册 S7 协议工厂";

    // 注册 ModbusTCP 协议工厂
    m_commManager->registerFactory("ModbusTCP", [](DeviceConfig *cfg, QObject *parent) -> IConnector* {
        return new ModbusTcpConnector(cfg, parent);
    });
    qDebug() << "已注册 ModbusTCP 协议工厂";

    // 注册 ModbusRTU 协议工厂
    m_commManager->registerFactory("ModbusRTU", [](DeviceConfig *cfg, QObject *parent) -> IConnector* {
        return new ModbusRtuConnector(cfg, parent);
    });
    qDebug() << "已注册 ModbusRTU 协议工厂";

    // 7. 根据设备配置自动构建连接器（每个设备独立线程）
    qDebug() << "构建设备连接器...";
    m_commManager->buildConnectors(m_dataModel);
    qDebug() << "设备连接器构建完成，设备数量:" << m_commManager->deviceIds().size();

    // 8. 创建控制器
    qDebug() << "创建 Controller...";
    m_controller = new Controller(m_dataModel, m_commManager, this);

    // 9. 初始化数据库
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

    // 10. 初始化用户管理器
    if (!UserManager::instance().initialize()) {
        qCritical() << "用户管理器初始化失败";
        return false;
    }
    UserManager::instance().logout();

    // 11. 创建并初始化各个管理器
    qDebug() << "创建并初始化各个管理器...";
    m_realTimeModel = new RealTimeDataModel(this);
    qDebug() << "RealTimeDataModel 创建完成";
    m_chartManager = new ChartManager(this);
    qDebug() << "ChartManager 创建完成";
    m_trendManager = new TrendManager(this);
    qDebug() << "TrendManager 创建完成";
    m_processViewManager = new ProcessViewManager(m_dataModel, &UserManager::instance(), this);
    qDebug() << "ProcessViewManager 创建完成";

    // 12. 将管理器传递给控制器
    qDebug() << "将管理器传递给 Controller...";
    m_controller->setRealTimeModel(m_realTimeModel);
    m_controller->setChartManager(m_chartManager);
    m_controller->setTrendManager(m_trendManager);
    m_controller->setProcessViewManager(m_processViewManager);
    qDebug() << "管理器指针已设置到 Controller";

    // 13. 初始化控制器
    qDebug() << "开始初始化控制器...";
    bool initResult = m_controller->initialize();
    qDebug() << "controller->initialize() 返回:" << initResult;
    qDebug() << "controller->isInitialized() =" << m_controller->isInitialized();

    // 14. 创建主窗口
    m_mainWindow = new MainWindow(m_dataModel, m_commManager, nullptr);

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
    qDebug() << "管理器指针已设置到 MainWindow";

    // 17. 建立关键信号连接
    setupConnections();

    // 18. 显示主窗口
    m_mainWindow->show();

    // 19. 根据配置决定是否自动连接
    qDebug() << "=== 检查PLC自动连接配置 ===";
    qDebug() << "自动连接选项:" << m_plcConfig.autoConnect;
    qDebug() << "自动开始轮询:" << m_plcConfig.autoStartPolling;
    if (m_plcConfig.autoConnect && m_mainWindow) {
        // 优先使用设备配置中的连接参数
        QString connectIp = m_plcConfig.ip;   // 默认全局
        int connectRack = m_plcConfig.rack;
        int connectSlot = m_plcConfig.slot;
        auto devices = m_dataModel->getAllDevices();
        qDebug() << "自动连接时设备数量:" << devices.size();
        if (!devices.isEmpty()) {
            qDebug() << "第一个设备IP:" << devices.first()->ip;
            DeviceConfig* firstDev = devices.first();
            connectIp = firstDev->ip;
            connectRack = firstDev->rack;
            connectSlot = firstDev->slot;
            qDebug() << "使用设备配置中的连接参数:" << connectIp << "Rack:" << connectRack << "Slot:" << connectSlot;
        } else {
            qDebug() << "未找到设备配置，使用全局PLC参数:" << connectIp;
        }
        QTimer::singleShot(1000, this, [this, connectIp, connectRack, connectSlot]() {
            qDebug() << "=== 启动自动PLC连接 ===";
            emit m_mainWindow->connectRequested(connectIp, connectRack, connectSlot);
        });
    } else {
        qDebug() << "未启用自动连接，需要手动连接";
    }

    qDebug() << "=== Application 初始化完成 ===================";
    return true;
}

// 注意：loadTagsFromConfig 函数已删除，因为新架构使用 loadFromJson

bool Application::loadPLCConfig(const QString &configPath)
{
    // 与原来相同，只加载 PLC 连接参数和设备无关的配置（如 MySQL）
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
    PLCConfig newConfig = m_plcConfig;

    if (root.contains("plc") && root["plc"].isObject()) {
        QJsonObject plcObj = root["plc"].toObject();
        if (plcObj.contains("ip") && plcObj["ip"].isString())
            newConfig.ip = plcObj["ip"].toString();
        if (plcObj.contains("rack") && plcObj["rack"].isDouble())
            newConfig.rack = plcObj["rack"].toInt();
        if (plcObj.contains("slot") && plcObj["slot"].isDouble())
            newConfig.slot = plcObj["slot"].toInt();
        if (plcObj.contains("port") && plcObj["port"].isDouble())
            newConfig.port = plcObj["port"].toInt();
    }

    // 轮询配置
    if (root.contains("polling") && root["polling"].isObject()) {
        QJsonObject pollingObj = root["polling"].toObject();
        if (pollingObj.contains("auto_start") && pollingObj["auto_start"].isBool())
            newConfig.autoStartPolling = pollingObj["auto_start"].toBool();
        if (pollingObj.contains("interval") && pollingObj["interval"].isDouble()) {
            int interval = pollingObj["interval"].toInt();
            if (interval >= 100 && interval <= 10000)
                newConfig.pollingInterval = interval;
        }
    }

    // 连接配置
    if (root.contains("connection") && root["connection"].isObject()) {
        QJsonObject connObj = root["connection"].toObject();
        if (connObj.contains("auto_connect") && connObj["auto_connect"].isBool())
            newConfig.autoConnect = connObj["auto_connect"].toBool();
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

void Application::setupConnections()
{
    qDebug() << "=== 建立信号连接 ========================";

    if (!m_controller || !m_mainWindow || !m_commManager || !m_dataModel) {
        qCritical() << "❌ 关键组件未初始化:";
        return;
    }

    // 1. CommManager 信号已在 Controller::initialize() 中连接

    // 2. Controller -> MainWindow
    connect(m_controller, &Controller::tableDataUpdated,
            m_mainWindow, &MainWindow::onTableDataUpdated, Qt::QueuedConnection);
    connect(m_controller, &Controller::chartDataUpdated,
            m_mainWindow, &MainWindow::onChartDataUpdated, Qt::QueuedConnection);
    connect(m_controller, &Controller::processDataUpdated,
            m_mainWindow, &MainWindow::onProcessDataUpdated, Qt::QueuedConnection);
    connect(m_controller, &Controller::alertTriggered,
            m_mainWindow, &MainWindow::onAlertTriggered, Qt::QueuedConnection);

    // 3. ProcessViewManager 写操作请求 → Controller
    if (m_processViewManager) {
        connect(m_processViewManager, &ProcessViewManager::writeTagRequested,
                m_controller, &Controller::writeSingleTag);
    }

    // 3. MainWindow -> CommManager
    connect(m_mainWindow, &MainWindow::connectRequested,
            this, [this](const QString&, int, int) {
                qDebug() << "Application: connectRequested -> CommManager::connectAll()";
                if (m_commManager) m_commManager->connectAll();
            });

    connect(m_mainWindow, &MainWindow::disconnectRequested,
            this, [this]() {
                qDebug() << "Application: disconnectRequested -> CommManager::disconnectAll()";
                if (m_commManager) m_commManager->disconnectAll();
            });

    connect(m_mainWindow, &MainWindow::systemResumed, this, [this]() {
        qDebug() << "Application: system resumed, reconnecting devices";
        if (m_commManager) m_commManager->connectAll();
    });

    // 4. MainWindow -> Controller
    connect(m_mainWindow, &MainWindow::writeRequested,
            m_controller, &Controller::onWriteRequest, Qt::QueuedConnection);

    // 5. PLCData -> MainWindow
    connect(m_dataModel, &PLCData::tagValueChanged,
            m_mainWindow, &MainWindow::onTagValueChanged, Qt::QueuedConnection);

    // 6. 调试信号： CommManager 设备状态
    connect(m_commManager, &CommManager::deviceStateChanged,
            this, [](const QString &deviceId, PLCConnectionState state) {
                qDebug() << "[CommManager] Device" << deviceId << "state:" << plcConnectionStateToString(state);
            });
    connect(m_commManager, &CommManager::deviceConnected,
            this, [](const QString &deviceId) {
                qDebug() << "[CommManager] Device" << deviceId << "connected";
            });
    connect(m_commManager, &CommManager::deviceDisconnected,
            this, [](const QString &deviceId) {
                qDebug() << "[CommManager] Device" << deviceId << "disconnected";
            });

    // 7. 咎适配置到 MainWindow
    QTimer::singleShot(100, this, [this]() {
        emit plcConfigLoaded(m_plcConfig.ip, m_plcConfig.rack,
                             m_plcConfig.slot, m_plcConfig.port);
    });

    // 8. 连接报譚
    connect(m_controller, &Controller::saveAlarmToDatabase,
            &DatabaseManager::instance(), &DatabaseManager::addAlarm,
            Qt::QueuedConnection);

    qDebug() << "=== 所朢信号连接完成 =================";
}


void Application::cleanup()
{
    // 1. 断开所有连接
    if (m_commManager) {
        disconnect(m_commManager, nullptr, this, nullptr);
        disconnect(m_commManager, nullptr, m_mainWindow, nullptr);
    }
    if (m_controller) {
        disconnect(m_controller, nullptr, m_mainWindow, nullptr);
    }

    // 2. 断开所朢设备连接(（CommManager 内部管理线前生命期）
    if (m_commManager) {
        m_commManager->disconnectAll();
        qDebug() << "CommManager: All devices disconnected";
    }

    // 3. 删除主窗口
    if (m_mainWindow) {
        delete m_mainWindow;
        m_mainWindow = nullptr;
    }

    // 4. 清理对象（CommManager 析构函数会停止所有线程）
    if (m_commManager) {
        m_commManager->deleteLater();
    }
    if (m_controller) {
        m_controller->deleteLater();
    }
}
