// application.h
#ifndef APPLICATION_H
#define APPLICATION_H

#include <QApplication>
#include <QSharedMemory>
#include "common_types.h"

// 前向声明
class Logger;
class PLCData;
class CommManager;
class Controller;
class MainWindow;
class RealTimeDataModel;
class ChartManager;
class TrendManager;
class ProcessViewManager;

class Application : public QApplication
{
    Q_OBJECT

public:
    explicit Application(int &argc, char **argv);
    ~Application();

    static Application* instance() { return m_instance; }

    bool initialize();
    void cleanup();

    // 配置管理方法
    bool loadPLCConfig(const QString &configPath = "configs/plc_config.json");
    bool savePLCConfig(const QString &configPath = "configs/plc_config.json");
    bool validatePLCConfig(const PLCConfig &config);

    // 获取和设置配置
    PLCConfig getPLCConfig() const { return m_plcConfig; }
    void setPLCConfig(const PLCConfig &config) { m_plcConfig = config; }

    // 加载配置文件
 //   bool loadConfig();
 //   bool saveConfig();

    // 获取配置文件路径
    QString getConfigPath() const { return m_configPath; }
    void setConfigPath(const QString &path) { m_configPath = path; }

    // 获取核心组件
    PLCData* dataModel() const { return m_dataModel; }
    CommManager* commManager() const { return m_commManager; }
    MainWindow* getMainWindow() const { return m_mainWindow; }
    Controller* getController() const { return m_controller; }

    PLCConfig m_plcConfig;

signals:
    void configLoaded(const PLCConfig &config);
    void configSaved(const QString &path);
    void configError(const QString &error);
    void plcConfigLoaded(const QString& ip, int rack, int slot, int port);

private:
    void setupConnections();

private:
    static Application* m_instance;
    QSharedMemory m_sharedMemory;

    // 核心组件
    PLCData *m_dataModel = nullptr;
    CommManager *m_commManager = nullptr;
    MainWindow *m_mainWindow = nullptr;
    Controller* m_controller = nullptr;
    QString m_configPath = "configs/plc_config.json";

    RealTimeDataModel* m_realTimeModel = nullptr;
    ChartManager* m_chartManager = nullptr;
    TrendManager* m_trendManager = nullptr;
    ProcessViewManager* m_processViewManager = nullptr;

    bool m_initialized = false;
};

#endif // APPLICATION_H