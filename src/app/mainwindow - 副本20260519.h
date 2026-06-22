// src/app/mainwindow.h
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QSet>
#include <QDateTime>
#include <QChart>
#include <QChartView>
#include <QLineSeries>
#include <QThread>
#include <QLabel>
#include <QSystemTrayIcon>
#include <qlistwidget.h>
#include "application.h"
#include "plcdata.h"
#include "plcconnector.h"
#include "controller.h"  // 添加 Controller 头文件
#include "common_types.h"  // 添加公共类型头文件
#include "realtimedatamodel.h"  // 添加实时数据模型头文件
#include "ui_mainwindow.h"

#ifdef QT_CHARTS_LIB
#include <QtCharts>
#endif

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(PLCData *dataModel,
                        PLCConnector *plcConnector,
                        QThread *plcThread,
                        QWidget *parent = nullptr);
    ~MainWindow();
public slots:
    void onTableDataUpdated(const QMap<QString, QVariant>& data);
    void onChartDataUpdated(const QMap<QString, QVariant>& data);
    void onProcessDataUpdated(const QMap<QString, QVariant>& data);
    void onConnectionStateChanged(bool connected);
    void onAlertTriggered(const AlertInfo& alert);
    void onTagValueChanged(const QString& tagName, const QVariant& value);

signals:
    void connectRequested(const QString& ip, int rack, int slot);
    void disconnectRequested();
    void startPollingRequested();
    void stopPollingRequested();
    void writeRequested(const QString& tagName, const QVariant& value);

private slots:
    // 导航和基本UI
    void onNavigationItemClicked(QListWidgetItem *item);
    void onConnectButtonClicked(bool checked);
    void onStartPollButtonClicked(bool checked);
    void onWriteButtonClicked();
    void onExportButtonClicked();
    void onSettingsButtonClicked();
    void onAddChartButtonClicked();
    void onSearchTextChanged(const QString &text);
    void onTimeRangeChanged(int index);
    void onAddTagButtonClicked();

    // Controller 相关信号槽
    void onControllerDataUpdated(const QMap<QString, QVariant> &data);
    void onControllerStateChanged(ControllerState newState);
    void onWriteCompleted(const QString &tagName, bool success, const QString &error);
    void onBatchWriteCompleted(const QMap<QString, bool>& results, const QString& error);
    void onWriteProgressChanged(int current, int total);
    void onPLCDataUpdated(const QMap<QString, QVariant> &data);

    // 实时数据表格
    void onTagSelected(const QString &tagName, bool selected);
    void exportRealTimeData();

    // 系统功能
    void onTakeScreenshot();
    void onExportData();
    void onImportConfig();
    void onAbout();
    void onQuit();

    // UI 主题
    void onThemeToggled(bool dark);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    // 初始化方法
  //  void setupUI();
    void setupConnections();
    void setupTableView();
    void setupChartView();
    void setupStatusBar();
    void setupMenuBar();
    void setupShortcuts();

    void setupController();  // 初始化 Controller

    // 主题控制
    void applyDarkTheme();
    void applyLightTheme();
    bool loadStyleSheet(const QString &fileName);
    void setupThemeMenu();

    // UI状态更新
    void updateUIState(ControllerState state);
    void updateConnectionStatus(bool connected);
    void updatePollingStatus();
    void updateStatusBarInfo();

    // 数据操作
    void showWriteValueDialog(const TagInfo &tag, const QVariant &currentValue);
    void writeValueToPLC(const TagInfo &tag, const QVariant &value);
    void writeMultipleValues(const QMap<QString, QVariant> &tagValues);
    void addTagToChart(const QString &tagName, const QString &displayName = "");

    // 数据处理
    void processDataForTable(const QMap<QString, QVariant> &data);
    void processDataForChart(const QMap<QString, QVariant> &data);
    void processDataForRealTime(const QMap<QString, QVariant> &data);
    void updateDataInTable(const QString &tagName, const QVariant &value);
    void updateDataInChart(const QString &tagName, const QVariant &value);
    void updateDataInRealTimeTable(const QString &tagName, const QVariant &value);

    // 工具方法
    void addStatusBarWidgets();
    void showNotification(const QString &title, const QString &message,
                          QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information);
    void updateChartAxis();
    void updateRealTimeData(const QString& tagName, const QVariant& value);
    void updateChartData(const QString& tagName, const QVariant& value);
    void checkAlarmCondition(const QString& tagName, const QVariant& value);

    // 配置管理
    void saveWindowState();
    void restoreWindowState();
    void saveSettings();
    void loadSettings();
    void cleanupResources();

    // 自动连接
    void autoConnectPLC();
    void autoStartPolling();

    // 视图管理
    void setupViews();
    void saveViewSelections();
    void loadViewSelections();
    void updateViewSelection(ViewType viewType, const QString& tagName, bool selected);
    void syncViewSelection(ViewType sourceView, ViewType targetView, const QStringList& tagNames);

    // 工具方法
    QString getViewName(ViewType viewType) const;
    void saveSelectionsToFile(const QString& fileName);
    void loadSelectionsFromFile(const QString& fileName);


private:
    // 核心组件
    Ui::MainWindow *ui;
    PLCData *m_tagModel = nullptr;
    PLCConnector *m_plcConnector = nullptr;
    Controller *m_controller = nullptr;  // Controller 实例
    QThread *m_plcThread = nullptr;
    RealTimeDataModel* m_realTimeDataModel = nullptr;


    // UI组件
    QChart *m_chart = nullptr;
    QChartView *m_chartView = nullptr;
    QMap<QString, QLineSeries*> m_chartSeries;

    // 状态栏组件
    QLabel *m_statusLabel = nullptr;
    QLabel *m_ipLabel = nullptr;
    QLabel *m_pollingLabel = nullptr;
    QLabel *m_tagCountLabel = nullptr;
    QLabel *m_memoryLabel = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_alertLabel = nullptr;  // 报警状态标签
    // 进度对话框
    QProgressDialog *m_writeProgressDialog = nullptr;

    // 系统托盘
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;

    // 主题相关
    bool m_isDarkTheme = true;
    QAction* m_darkThemeAction = nullptr;
    QAction* m_lightThemeAction = nullptr;

    // 状态标志
    ControllerState m_currentState = ControllerState::DISCONNECTED;
    QTimer* m_connectTimer;

    // 数据管理
    QSet<QString> m_selectedTags;  // 选择的标签
    // 添加Application引用
    Application *m_app = nullptr;
    PLCConfig m_plcConfig;

    // 性能监控
    int m_updateCount = 0;
    qint64 m_lastUpdateTime = 0;
    double m_updateFrequency = 0.0;

    // 报警管理
    QList<AlertInfo> m_activeAlerts;
    int m_alertCount = 0;

    QSet<QString> m_selectedTags[VIEW_COUNT];  // 不同视图的标签选择
    QSet<QString> m_processViewTags;           // 流程图视图专用标签集
};

#endif // MAINWINDOW_H