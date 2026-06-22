// mainwindow.h
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QVariant>
#include <QTimer>
#include <QDateTime>
#include <qlistwidget.h>
#include "qcustomplot.h"
#include "common_types.h"
#include "realtimedatamodel.h"
#include "databasemanager.h"
#include "usermanager.h"
#include "logindialog.h"
#include "usermanagedialog.h"
#include "connectionconfigdialog.h"
#include "alarmlistmodel.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

// 前向声明
class PLCData;
class CommManager;
class Controller;
class ChartManager;
class TrendManager;
class ProcessViewManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(PLCData* dataModel, CommManager* commManager, QWidget* parent = nullptr);
    ~MainWindow();

    // 公共接口
    void updateConnectionStatus(ControllerState state, const QString& message = "");
    void updateSystemTime();

    // 设置数据管理器
    void setDataManagers(RealTimeDataModel* realTimeModel,
                         ChartManager* chartManager,
                         TrendManager* trendManager,
                         ProcessViewManager* processViewManager);

public slots:
    // 数据更新槽函数
    void onTableDataUpdated(const QMap<QString, QVariant>& data);
    void onChartDataUpdated(const QMap<QString, QVariant>& data);
    void onProcessDataUpdated(const QMap<QString, QVariant>& data);
    void onChartTagsChanged(const QStringList& tags);
    void onChartCleared();

    // PLC连接状态
    void onConnectionStateChanged(ControllerState state);
    void onPLCConfigLoaded(const QString& ip, int rack, int slot, int port);

    // 标签值变化
    void onTagValueChanged(const QString& tagName, const QVariant& value);

    // 告警处理
    void onAlertTriggered(const AlertInfo& alert);

    // 数据库状态变化
    void onDatabaseStatusChanged(bool mysqlConnected);

    // 写入结果反馈
    void onWriteResult(const QString& tagName, bool success, const QString& error = "");

private slots:
    // UI事件槽函数
    void onConnectButtonClicked();
    void onRefreshProcessClicked();
    void onWriteValueClicked();
    void onExportDataClicked();
    void onSearchTextChanged(const QString& text);
    void onNavigationItemClicked(QListWidgetItem* item);
    void onTabChanged(int index);

    // 设备配置与切换
    void onDeviceConfigClicked();                     // 打开设备配置对话框
    void onDeviceComboChanged(int index);             // 设备下拉框切换

    // 图表控制
    void onChartPlayPauseClicked();
    void onChartTimeRangeChanged(int index);
    void onSaveChartClicked();
    void onRealtimeTableSelectionChanged(const QItemSelection &selected,
                                         const QItemSelection &deselected);

    // 历史数据查询
    void onSelectTrendTags();
    void onQueryHistoryClicked();
    void onExportHistoryClicked();
    void onHistoryQueryResultReady(int requestId, const QList<DatabaseManager::HistoryResult>& results);

    // 菜单动作
    void onExportActionTriggered();
    void onExitActionTriggered();
    void onViewActionTriggered(QAction* action);
    void onShowStatusBarToggled(bool checked);
    void onAboutActionTriggered();
    void onUserManualActionTriggered();

    // 定时器槽
    void onUpdateTimerTimeout();
    void onSystemTimerTimeout();

    // 登录相关
    void onLoginStatusClicked();
    void updateLoginStatusButton();
    //同步数据库启用标签
    void syncDatabaseEnabledTags();

protected:
    void showEvent(QShowEvent *event) override;

private:
    // 初始化函数
    void setupUI();
    void setupConnections();
    void connectDataSignals();
    void setupCharts();
    void setuptrend();
    void applyCurrentViewSelection();                 // 应用当前视图的标签选择

    // 主题管理
    void setupThemes();
    void applyDarkTheme();
    void applyLightTheme();
    void applyAppleTheme();
    void onThemeMenuTriggered(bool checked = false);
    void loadThemeSettings();
    void saveThemeSettings();
    bool loadStyleSheet(const QString &fileName);
    void setupThemeMenu();

    // 用户管理
    void setupUserMenu();
    void updateUIPermissions();
    void onUserLoggedIn(const QString& username);
    void onUserLoggedOut();

    // 工程师连接处理
    void handleEngineerConnectAction(ControllerState currentState);
    void showConnectionConfigDialog();

    // 工具函数
    void updateStatusBar();
    void updateDatabaseStatus(bool mysqlConnected);
    void updateTagCounts();
    void showStatusMessage(const QString& message, int timeout = 3000);
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result);
    bool savePLCConfig();

    // 辅助函数
    ViewType mapTabIndexToViewType(int index) const;
    void bindModelsToViews();
    void performOneTimeInitialization();
    void refreshDeviceComboBox();

    // 图表相关
    void clearChart();
    void exportHistoryResultsToCsv(const QList<DatabaseManager::HistoryResult>& results, const QString& fileName);

    // 工艺流程
    void refreshProcessView();
    //报警
    void setupAlarmCenter();
    void onAcknowledgeAlarm();

    // 数据导出
    void exportCurrentViewData(ViewType viewType);
    void exportToCsv(const QMap<QString, QVariant>& data, const QString& filename);

    // 成员变量
    Ui::MainWindow* ui;
    PLCData* m_plcData;
    CommManager* m_commManager;
    Controller* m_controller;

    // 数据管理器
    RealTimeDataModel* m_realTimeModel = nullptr;
    ChartManager* m_chartManager = nullptr;
    TrendManager* m_trendManager = nullptr;
    ProcessViewManager* m_processViewManager = nullptr;
    AlarmListModel *m_alarmModel = nullptr;
    QTableView *m_alarmView = nullptr;

    // 当前设备ID
    QString m_currentDeviceId;

    // 状态管理
    ControllerState m_connectionState;
    QTimer* m_updateTimer;
    QTimer* m_systemTimer;
    bool m_chartPlaying;

    // 数据缓存
    QMap<QString, QVariant> m_lastTableData;
    QMap<QString, QVariant> m_lastChartData;
    QMap<QString, QVariant> m_lastProcessData;

    // PLC连接参数
    QString m_currentPLCIP = "192.168.0.1";
    int m_currentPLCRack = 0;
    int m_currentPLCSlot = 2;
    int m_currentPLCPort = 102;
    int m_PLCTimeout = 3000;
    int m_pollingInterval = 1000;
    int m_chartTimeRange;

    // 历史查询
    int m_currentHistoryRequestId;
    QList<DatabaseManager::HistoryResult> m_lastHistoryResults;

    // 主题
    QString m_currentTheme;
    QMenu* m_themeMenu;
    QAction* m_actionDarkTheme;
    QAction* m_actionLightTheme;
    QAction* m_actionmacTheme;
    QAction *m_actionLogin;
    QAction *m_actionLogout;
    QAction *m_actionUserManage;

    // 登录对话框指针
    LoginDialog* m_loginDialog = nullptr;

    QCustomPlot* m_realTimePlot;
    QCustomPlot* m_historyPlot;

signals:
    void connectRequested(const QString& ip, int rack, int slot);
    void disconnectRequested();
    void writeRequested(const QString& tagName, const QVariant& value);
    void batchWriteRequested(const QMap<QString, QVariant>& tagValues);
    void viewSelectionChanged(ViewType viewType, const QStringList& selectedTags);
    void systemResumed();

private:
    QColor getColorForGraph(int index);
};

#endif // MAINWINDOW_H