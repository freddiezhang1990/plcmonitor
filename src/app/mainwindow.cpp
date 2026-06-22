// mainwindow.cpp
#include "mainwindow.h"
#include "ChartManager.h"
#include "application.h"
#include "databaseconfigdialog.h"
#include "historyquerydialog.h"
#include "processviewmanager.h"
#include "trendmanager.h"
#include "databasemanager.h"
#include "ui_mainwindow.h"
#include "plcdata.h"
#include "commmanager.h"
#include "controller.h"
#include "common_types.h"
#include "deviceconfigdialog.h"   // 新增设备配置对话框
#include <QFileDialog>
#include <QMessageBox>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QStandardPaths>
#include <QDateTime>
#include <QTextStream>
#include <QHeaderView>
#include <QTreeWidgetItem>
#include <QGraphicsItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsTextItem>
#include <QGraphicsLineItem>
#ifdef Q_OS_WIN
#include <windows.h>
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#endif

MainWindow::MainWindow(PLCData* dataModel, CommManager* commManager, QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_plcData(dataModel)
    , m_commManager(commManager)
    , m_controller(nullptr)
    , m_connectionState(ControllerState::DISCONNECTED)
    , m_chartPlaying(false)
    , m_chartTimeRange(5)
    , m_currentTheme("apple")
    , m_themeMenu(nullptr)
    , m_actionDarkTheme(nullptr)
    , m_actionLightTheme(nullptr)
    , m_loginDialog(nullptr)
{
    ui->setupUi(this);

    m_realTimePlot = nullptr;
    m_historyPlot = nullptr;
    m_updateTimer = new QTimer(this);
    m_systemTimer = new QTimer(this);

    setupThemes();
    setWindowTitle("PLC数据监控系统");
    resize(1600, 900);
    setupUI();

    // 初始化设备下拉框
    refreshDeviceComboBox();

    // 连接设备切换信号
  //  connect(ui->comboBoxDevices, QOverload<int>::of(&QComboBox::currentIndexChanged),
  //          this, &MainWindow::onDeviceComboChanged);

    // 连接配置加载信号
    Application* app = qobject_cast<Application*>(qApp);
    if (app) {
        connect(app, &Application::plcConfigLoaded,
                this, &MainWindow::onPLCConfigLoaded, Qt::QueuedConnection);
        connect(app, &Application::configLoaded,
                this, [this](const PLCConfig& config) {
                    onPLCConfigLoaded(config.ip, config.rack, config.slot, config.port);
                }, Qt::QueuedConnection);
    }

    updateSystemTime();

    // 数据信号连接（部分依赖 m_realTimeModel 等，先连接，后面会在 performOneTimeInitialization 中绑定）
    connectDataSignals();

    setupConnections();

    setupAlarmCenter();

    connect(&DatabaseManager::instance(), &DatabaseManager::historyQueryResultReady,
            this, &MainWindow::onHistoryQueryResultReady);

    m_systemTimer->start(1000);
    m_updateTimer->start(500);

    // 用户管理
    setupUserMenu();
    QTimer::singleShot(100, this, [this]() {
        UserManager::instance().logout();
        if (!UserManager::instance().isLoggedIn()) {
            if (!m_loginDialog) {
                m_loginDialog = new LoginDialog(this);
                connect(m_loginDialog, &QDialog::finished, this, [this](int result) {
                    if (result == QDialog::Accepted)
                        onUserLoggedIn(UserManager::instance().currentUsername());
                    m_loginDialog = nullptr;
                });
            }
            m_loginDialog->show();
            m_loginDialog->raise();
            m_loginDialog->activateWindow();
        }
    });
    updateUIPermissions();
    updateLoginStatusButton();

    qDebug() << "MainWindow 初始化完成";
}

MainWindow::~MainWindow()
{
    if (m_plcData)
 //       disconnect(m_plcData, &PLCData::allDataUpdated, this, nullptr);
    m_chartManager = nullptr;
    m_trendManager = nullptr;
    m_processViewManager = nullptr;
    m_realTimeModel = nullptr;

    if (m_updateTimer && m_updateTimer->isActive())
        m_updateTimer->stop();
    if (m_systemTimer && m_systemTimer->isActive())
        m_systemTimer->stop();

    delete ui;
    delete m_realTimePlot;
    delete m_historyPlot;
}

void MainWindow::setupUI()
{
    applyAppleTheme();
    setStyleSheet("QMainWindow { background-color: #F5F5F5; }");

    // 分割器初始比例
    ui->mainSplitter->setSizes(QList<int>() << 200 << 1200);
    ui->navigationList->setMaximumWidth(190);
    ui->mainSplitter->setMinimumSize(200, 400);

    // 状态栏
    ui->statusbar->show();
    ui->btnLoginStatus->setText("登录");
    ui->btnLoginStatus->setEnabled(true);
    ui->btnLoginStatus->setStyleSheet("QPushButton { border: none; background: transparent; color: #0078D7; }");
    updateDatabaseStatus(false);

    ui->statusbar->clearMessage();
    ui->statusbar->addWidget(ui->statusLabelPLC);
    ui->statusbar->addWidget(ui->statusLabelComm);
    ui->statusbar->addWidget(ui->statusLabelSystem, 1);
    ui->statusbar->addWidget(ui->statusLabelAlarm, 1);
    ui->statusbar->addWidget(ui->statusLabelTime);
    ui->statusbar->addWidget(ui->statusLabelUpdate);

    ui->statusLabelPLC->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    ui->statusLabelComm->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    ui->statusLabelSystem->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    ui->statusLabelAlarm->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    ui->statusLabelTime->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    ui->statusLabelUpdate->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    ui->statusLabelPLC->setMinimumWidth(120);
    ui->statusLabelComm->setMinimumWidth(100);

    ui->statusLabelPLC->setText("PLC: 未连接");
    ui->statusLabelComm->setText("通讯: 未初始化");
    ui->statusLabelAlarm->setText("无报警");
    ui->statusLabelUpdate->setText("最近更新: 无");
    ui->statusLabelTime->setText(QDateTime::currentDateTime().toString("HH:mm:ss"));

    // 实时表格属性
    ui->realTimeTableView->setAlternatingRowColors(true);
    ui->realTimeTableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->realTimeTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->realTimeTableView->setSortingEnabled(true);
    ui->realTimeTableView->horizontalHeader()->setStretchLastSection(true);
    ui->realTimeTableView->verticalHeader()->setVisible(false);

    // 工艺视图
    ui->graphicsViewProcessFlow->setRenderHint(QPainter::Antialiasing);
    ui->graphicsViewProcessFlow->setDragMode(QGraphicsView::ScrollHandDrag);

    updateStatusBar();
    qDebug() << "UI 设置完成";
}

void MainWindow::setupConnections()
{
    qDebug() << "开始设置 MainWindow 信号连接...";
    connect(ui->btnConnectPLC, &QPushButton::clicked, this, &MainWindow::onConnectButtonClicked);
    connect(ui->btnTagManagerHeader, &QPushButton::clicked, this, &MainWindow::onDeviceConfigClicked);
    connect(ui->btnLoginStatus, &QPushButton::clicked, this, &MainWindow::onLoginStatusClicked);
    connect(ui->navigationList, &QListWidget::itemClicked, this, &MainWindow::onNavigationItemClicked);
    connect(ui->mainTabWidget, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(ui->btnWriteValue, &QPushButton::clicked, this, &MainWindow::onWriteValueClicked);
    connect(ui->btnExportRealTime, &QPushButton::clicked, this, [this]() { exportCurrentViewData(ViewType::TABLE_VIEW); });
    connect(ui->btnRefreshProcess, &QPushButton::clicked, this, &MainWindow::onRefreshProcessClicked);
    connect(ui->btnPlayChart, &QPushButton::clicked, this, &MainWindow::onChartPlayPauseClicked);
    connect(ui->comboTimeRange, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onChartTimeRangeChanged);
    connect(ui->btnSaveChart, &QPushButton::clicked, this, &MainWindow::onSaveChartClicked);
    connect(ui->btnToggleGrid, &QPushButton::toggled, this, [this](bool checked) {
        if (m_chartManager && m_chartManager->getCustomPlot()) {
            QCustomPlot* plot = m_chartManager->getCustomPlot();
            plot->xAxis->grid()->setVisible(checked);
            plot->yAxis->grid()->setVisible(checked);
            plot->replot();
        }
    });
    connect(ui->btnToggleLegend, &QPushButton::toggled, this, [this](bool checked) {
        if (m_chartManager && m_chartManager->getCustomPlot()) {
            QCustomPlot* plot = m_chartManager->getCustomPlot();
            plot->legend->setVisible(checked);
            plot->replot();
        }
    });
    connect(ui->btnClearChart, &QPushButton::clicked, this, &MainWindow::clearChart);
    connect(ui->btnSelectTrendTags, &QPushButton::clicked, this, &MainWindow::onSelectTrendTags);
    connect(ui->btnQueryHistory, &QPushButton::clicked, this, &MainWindow::onQueryHistoryClicked);
    connect(ui->btnExportHistory, &QPushButton::clicked, this, &MainWindow::onExportHistoryClicked);
    connect(&DatabaseManager::instance(), &DatabaseManager::mysqlConnectionChanged, this, &MainWindow::onDatabaseStatusChanged);
    connect(ui->actionDisconnectPLC, &QAction::triggered, this, [this]() {
        if (m_connectionState == ControllerState::CONNECTED || m_connectionState == ControllerState::POLLING) {
            if (UserManager::instance().hasPermission(UserRole::ENGINEER))
                emit disconnectRequested();
            else
                QMessageBox::warning(this, "权限不足", "当前用户权限不足，无法断开PLC连接\n需要工程师或管理员权限");
        }
    });
    connect(ui->actionExport, &QAction::triggered, this, &MainWindow::onExportActionTriggered);
    connect(ui->actionExit, &QAction::triggered, this, &MainWindow::onExitActionTriggered);
    connect(ui->actionTagManager, &QAction::triggered, this, &MainWindow::onDeviceConfigClicked);
    connect(ui->actionShowStatusBar, &QAction::toggled, this, &MainWindow::onShowStatusBarToggled);
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::onAboutActionTriggered);
    connect(ui->actionUserManual, &QAction::triggered, this, &MainWindow::onUserManualActionTriggered);
    connect(ui->actionConnectPLC, &QAction::triggered, this, &MainWindow::onConnectButtonClicked);
    connect(ui->actionDbConfig, &QAction::triggered, this, [this]() {
        if (UserManager::instance().hasPermission(UserRole::ENGINEER)) {
            DatabaseConfigDialog dlg(this);
            dlg.exec();
        } else {
            QMessageBox::warning(this, "权限不足", "只有工程师及以上用户才能修改数据库配置");
        }
    });

    // 主题菜单连接
    if (m_actionDarkTheme) {
        connect(m_actionDarkTheme, &QAction::triggered, this, [this]() {
            if (!m_actionDarkTheme->isChecked()) {
                m_actionDarkTheme->setChecked(true);
                return;
            }
            applyDarkTheme();
            m_actionLightTheme->setChecked(false);
        });
    }
    if (m_actionLightTheme) {
        connect(m_actionLightTheme, &QAction::triggered, this, [this]() {
            if (!m_actionLightTheme->isChecked()) {
                m_actionLightTheme->setChecked(true);
                return;
            }
            applyLightTheme();
            m_actionDarkTheme->setChecked(false);
        });
    }
    if (m_actionmacTheme) {
        connect(m_actionmacTheme, &QAction::triggered, this, [this]() {
            if (!m_actionmacTheme->isChecked()) {
                m_actionmacTheme->setChecked(true);
                return;
            }
            applyAppleTheme();
            m_actionmacTheme->setChecked(false);
        });
    }

    QList<QAction*> viewActions = {ui->actionViewProcess, ui->actionViewRealTime, ui->actionViewChart,
                                    ui->actionViewHistory, ui->actionViewControl, ui->actionViewAlarm};
    for (QAction* action : viewActions)
        connect(action, &QAction::triggered, this, [this, action]() { onViewActionTriggered(action); });

    connect(m_updateTimer, &QTimer::timeout, this, &MainWindow::onUpdateTimerTimeout);
    connect(m_systemTimer, &QTimer::timeout, this, &MainWindow::onSystemTimerTimeout);

    qDebug() << "MainWindow 信号连接设置完成";
}

void MainWindow::connectDataSignals()
{
    qDebug() << "=== 连接数据信号 ===";
    if (!m_plcData) {
        qWarning() << "PLCData 为空，无法连接信号";
        return;
    }

    // 在 MainWindow 构造函数中，连接数据信号之后添加
    connect(m_plcData, &PLCData::viewSelectionChanged, this, [this](ViewType type, const QStringList&){
        if (type == ViewType::CHART_VIEW || type == ViewType::TABLE_VIEW || type == ViewType::PROCESS_VIEW)
            updateTagCounts();
    });
/*
    connect(m_plcData, &PLCData::allDataUpdated, this,
            [this](const QMap<QString, QVariant>& data) {
                qDebug() << "收到PLC数据更新，标签数量:" << data.size();
                if (m_chartManager)
                    m_chartManager->updateChartData(data);
                if (m_processViewManager)
                    m_processViewManager->updateProcessData(data);
                if (m_trendManager)
                    m_trendManager->updateTrendData(data);
                if (!data.isEmpty()) {
                    QString firstTag = data.keys().first();
                    ui->statusLabelUpdate->setText(QString("最近更新: %1").arg(firstTag));
                }
            });    */
    qDebug() << "数据信号连接完成";
}

void MainWindow::setupCharts()
{
    if (!ui->chartWidget) return;
    m_chartManager = new ChartManager(this);
    m_chartManager->setCustomPlot(ui->chartWidget);
    m_chartManager->setChartTimeRange(300);
    connect(m_chartManager, &ChartManager::chartTagsChanged, this, &MainWindow::onChartTagsChanged);
    connect(m_chartManager, &ChartManager::chartCleared, this, &MainWindow::onChartCleared);
    connect(m_chartManager, &ChartManager::chartTimeRangeChanged, this, &MainWindow::onChartTimeRangeChanged);
    connect(m_chartManager, &ChartManager::chartPlayStateChanged, this, &MainWindow::onChartPlayPauseClicked);
}

void MainWindow::setuptrend()
{
    if (!ui->historyChartWidget) return;
    m_trendManager = new TrendManager(this);
    m_trendManager->setCustomPlot(ui->historyChartWidget);
    if (m_plcData) {
        QStringList trendTags = m_plcData->getSelectedTagKeysForView(ViewType::TREND_VIEW);
        m_trendManager->setTrendTags(trendTags);
    }
    ui->dateTimeFrom->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    ui->dateTimeTo->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    QDateTime now = QDateTime::currentDateTime();
    ui->dateTimeTo->setDateTime(now);
    ui->dateTimeFrom->setDateTime(now.addSecs(-3600));
    connect(m_trendManager, &TrendManager::trendTagsChanged, this, [this](const QStringList& tags){
        ui->lblTrendCount->setText(QString("趋势中: %1 个标签").arg(tags.size()));
    });
}

void MainWindow::setDataManagers(RealTimeDataModel* realTimeModel,
                                 ChartManager* chartManager,
                                 TrendManager* trendManager,
                                 ProcessViewManager* processViewManager)
{
    qDebug() << "=== MainWindow 设置数据管理器 ===";
    m_realTimeModel = realTimeModel;
    m_chartManager = chartManager;
    m_trendManager = trendManager;
    m_processViewManager = processViewManager;
    bindModelsToViews();
}

void MainWindow::bindModelsToViews()
{
    qDebug() << "=== MainWindow 绑定模型到视图 ===";
    if (!m_plcData) {
        qWarning() << "PLCData 未初始化，跳过模型绑定";
        return;
    }

    // 实时数据表格
    if (m_realTimeModel && ui->realTimeTableView) {
        ui->realTimeTableView->setModel(m_realTimeModel);
        QStringList initialTags = m_plcData->getSelectedTagKeysForView(ViewType::TABLE_VIEW);
        m_realTimeModel->setVisibleTags(initialTags);
        ui->realTimeTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->realTimeTableView->setAlternatingRowColors(true);
        ui->realTimeTableView->setSortingEnabled(true);
        ui->realTimeTableView->horizontalHeader()->setStretchLastSection(true);
        ui->realTimeTableView->setColumnWidth(0, 180);
        ui->realTimeTableView->setColumnWidth(1, 100);
        ui->realTimeTableView->setColumnWidth(2, 80);
        ui->realTimeTableView->setColumnWidth(3, 100);
        ui->realTimeTableView->setColumnWidth(4, 120);
        connect(ui->realTimeTableView->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, &MainWindow::onRealtimeTableSelectionChanged);
        qDebug() << "  实时数据表格绑定完成";
    }

    // 图表管理器
    if (m_chartManager) {
        m_chartManager->setCustomPlot(ui->chartWidget);
        if (m_plcData) {
            QStringList chartTags = m_plcData->getSelectedTagKeysForView(ViewType::CHART_VIEW);
            m_chartManager->setChartTags(chartTags);
        }
        if (ui->btnPlayChart) {
            connect(ui->btnPlayChart, &QPushButton::clicked, this, [this]() {
                if (!m_chartManager) return;
                if (m_chartManager->isPlaying()) {
                    m_chartManager->playChart(false);
                    ui->btnPlayChart->setText("启动");
                } else {
                    m_chartManager->playChart(true);
                    ui->btnPlayChart->setText("暂停");
                }
            });
        }
        if (ui->btnClearChart) {
            connect(ui->btnClearChart, &QPushButton::clicked, m_chartManager, &ChartManager::clearChartData);
        }
        if (ui->comboTimeRange) {
            connect(ui->comboTimeRange, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this](int index) {
                        if (m_chartManager) {
                            static const int timeRanges[] = {60, 300, 900, 3600, 21600, 86400};  // 30秒到1小时
                            if (index >= 0 && index < 6) {
                                m_chartManager->setChartTimeRange(timeRanges[index]);
                            }
                        }
                    });
        }
        qDebug() << "  实时图表管理器绑定完成";
    }

    // 历史趋势管理器
    if (m_trendManager) {
        m_trendManager->setCustomPlot(ui->historyChartWidget);
        if (m_plcData) {
            QStringList trendTags = m_plcData->getSelectedTagKeysForView(ViewType::TREND_VIEW);
            m_trendManager->setTrendTags(trendTags);
        }

        // ========== 新增：重新连接趋势标签变化信号 ==========
        connect(m_trendManager, &TrendManager::trendTagsChanged, this,
                [this](const QStringList& tags) {
                    ui->lblTrendCount->setText(QString("趋势中: %1 个标签").arg(tags.size()));
                });

        // ========== 新增：确保历史查询时间范围是当前时间 ==========
        QDateTime now = QDateTime::currentDateTime();
        ui->dateTimeTo->setDateTime(now);
        ui->dateTimeFrom->setDateTime(now.addSecs(-3600)); // 1小时前

        connect(ui->btnExportHistory, &QPushButton::clicked, this, [this]() {
            if (!m_trendManager) return;
            QString filePath = QFileDialog::getSaveFileName(this, "导出历史数据",
                                                            QString("history_data_%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")),
                                                            "CSV文件 (*.csv);;Excel文件 (*.xlsx);;所有文件 (*.*)");
            if (!filePath.isEmpty())
                m_trendManager->exportHistoryData(filePath);
        });
        qDebug() << "  历史趋势管理器绑定完成";
    }
    // 工艺流程管理器
    if (m_processViewManager) {
        m_processViewManager->setGraphicsView(ui->graphicsViewProcessFlow);
        if (m_plcData) {
            QStringList processTags = m_plcData->getSelectedTagKeysForView(ViewType::PROCESS_VIEW);
            m_processViewManager->setProcessTags(processTags);
            // 核心连接：PLCData 标签值变化 → 驱动工艺视图状态变色
            connect(m_plcData, &PLCData::tagValueChanged,
                    m_processViewManager, &ProcessViewManager::onTagValueChanged);
        }
        connect(ui->btnRefreshProcess, &QPushButton::clicked, m_processViewManager, &ProcessViewManager::refreshViewNow);
        qDebug() << "  工艺流程管理器绑定完成";
    }

    connectDataSignals();
    qDebug() << "=== 模型绑定完成 ===";
}

// ---------- 设备配置与切换 ----------
void MainWindow::onDeviceConfigClicked()
{
    DeviceConfigDialog dlg(m_plcData, this);
    connect(&dlg, &DeviceConfigDialog::tagDatabaseEnabledChanged,
            this, &MainWindow::syncDatabaseEnabledTags);
    if (dlg.exec() == QDialog::Accepted) {
        // 用户点了“确定”并已保存，只需刷新界面
        refreshDeviceComboBox();           // 刷新设备下拉框
        applyCurrentViewSelection();       // 重新应用视图选择（标签可能变化）
        // 注意：不再调用 saveToJson
        // 如果当前选中的设备被删除，则切换到第一个
        if (ui->comboBoxDevices->count() == 0) {
            m_currentDeviceId.clear();
        } else if (!m_currentDeviceId.isEmpty()) {
            int idx = ui->comboBoxDevices->findData(m_currentDeviceId);
            if (idx < 0)
                onDeviceComboChanged(0);
            else
                onDeviceComboChanged(idx);
        }
    }
    // 如果用户取消，什么都不做
}

void MainWindow::syncDatabaseEnabledTags()
{
    if (!m_plcData) return;
    QStringList enabledTagKeys = m_plcData->getEnabledTagKeys(); // 需要实现此方法
    DatabaseManager::instance().setEnabledTags(enabledTagKeys);
    qDebug() << "同步数据库启用标签，数量:" << enabledTagKeys.size();
}

void MainWindow::refreshDeviceComboBox()
{
    ui->comboBoxDevices->blockSignals(true);
    ui->comboBoxDevices->clear();
    auto devices = m_plcData->getAllDevices();
    for (DeviceConfig* dev : devices) {
        ui->comboBoxDevices->addItem(dev->deviceName.isEmpty() ? dev->deviceId : dev->deviceName, dev->deviceId);
    }
    ui->comboBoxDevices->blockSignals(false);
    if (devices.size() > 0 && m_currentDeviceId.isEmpty())
        onDeviceComboChanged(0);  // 选择第一个设备作为默认
}

void MainWindow::onDeviceComboChanged(int index)
{
    if (index < 0) return;
    QString deviceId = ui->comboBoxDevices->itemData(index).toString();
    if (m_currentDeviceId == deviceId) return;
    m_currentDeviceId = deviceId;
    qDebug() << "切换到设备用于连接:" << deviceId;

    // 注意：此处不再调用 applyCurrentViewSelection()，视图不受影响
    // 仅更新 PLC 连接参数（如果已连接，可能需要重新连接？由用户手动触发）
    // 从设备配置中获取连接参数并保存到 m_currentPLCIP 等变量
    DeviceConfig* dev = m_plcData->getDevice(deviceId);
    if (dev) {
        m_currentPLCIP = dev->ip;
        m_currentPLCRack = dev->rack;
        m_currentPLCSlot = dev->slot;
        m_currentPLCPort = dev->port;
        // 可更新状态栏提示
        showStatusMessage(QString("当前连接目标: %1 (%2)").arg(dev->deviceName, dev->ip), 3000);
    }
}

void MainWindow::applyCurrentViewSelection()
{
    ViewType currentView = mapTabIndexToViewType(ui->mainTabWidget->currentIndex());
    QStringList selectedTagKeys = m_plcData->getSelectedTagKeysForView(currentView);

    qDebug() << "Apply view selection for" << static_cast<int>(currentView) << selectedTagKeys;

    // 直接使用所有选中的 tagKey，不过滤设备
    // 实时数据模型
    if (m_realTimeModel) {
        m_realTimeModel->setVisibleTags(selectedTagKeys);
    }
    // 图表管理器（如果当前视图是图表视图）
    if (currentView == ViewType::CHART_VIEW && m_chartManager) {
        m_chartManager->setChartTags(selectedTagKeys);
    }
    // 趋势管理器（如果当前视图是历史趋势视图）
    if (currentView == ViewType::TREND_VIEW && m_trendManager) {
        m_trendManager->setTrendTags(selectedTagKeys);
    }
    // 工艺视图等其他视图类似...

    updateTagCounts();
}

// ---------- 历史数据查询 ----------
void MainWindow::onSelectTrendTags()
{
    if (!m_plcData || !m_trendManager) {
        QMessageBox::warning(this, "错误", "数据未初始化");
        return;
    }
    QStringList currentTags = m_trendManager->getTrendTags();
    HistoryQueryDialog dlg(m_plcData, currentTags, this);
    if (dlg.exec() == QDialog::Accepted) {
        QStringList selected = dlg.getSelectedTags();
        m_trendManager->setTrendTags(selected);
        m_plcData->setViewSelection(ViewType::TREND_VIEW, selected);
    }
}

void MainWindow::onQueryHistoryClicked()
{
    if (!m_trendManager) {
        QMessageBox::warning(this, "错误", "趋势管理器未初始化");
        return;
    }
    QStringList tags = m_trendManager->getTrendTags();
    if (tags.isEmpty()) {
        QMessageBox::warning(this, "警告", "请先选择趋势标签");
        return;
    }
    QDateTime from = ui->dateTimeFrom->dateTime();
    QDateTime to = ui->dateTimeTo->dateTime();
    if (from >= to) {
        QMessageBox::warning(this, "警告", "起始时间必须早于结束时间");
        return;
    }
    static int requestId = 0;
    m_currentHistoryRequestId = ++requestId;
    ui->btnQueryHistory->setEnabled(false);
    ui->btnExportHistory->setEnabled(false);
    DatabaseManager::instance().queryHistoryAsync(tags, from.toUTC(), to.toUTC(), true, m_currentHistoryRequestId);
}

void MainWindow::onHistoryQueryResultReady(int requestId, const QList<DatabaseManager::HistoryResult>& results)
{
    if (requestId != m_currentHistoryRequestId) return;
    ui->btnQueryHistory->setEnabled(true);
    if (results.isEmpty()) {
        QMessageBox::information(this, "提示", "未查询到数据");
        return;
    }
    m_lastHistoryResults = results;
    ui->btnExportHistory->setEnabled(true);
    if (m_trendManager)
        m_trendManager->plotHistoryQueryResult(results);
}

void MainWindow::exportHistoryResultsToCsv(const QList<DatabaseManager::HistoryResult>& results, const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法创建文件");
        return;
    }
    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
    out.setCodec("UTF-8");
#else
    out.setEncoding(QStringConverter::Utf8);
#endif
    QSet<QDateTime> allTimes;
    for (const auto& res : results)
        for (const auto& ts : res.timestamps)
            allTimes.insert(ts);
    QList<QDateTime> sortedTimes = allTimes.values();
    std::sort(sortedTimes.begin(), sortedTimes.end());
    out << "时间戳";
    for (const auto& res : results)
        out << "," << res.tagName;
    out << "\n";
    for (const QDateTime& ts : sortedTimes) {
        out << ts.toString("yyyy-MM-dd HH:mm:ss.zzz");
        for (const auto& res : results) {
            int idx = res.timestamps.indexOf(ts);
            out << "," << (idx != -1 ? QString::number(res.values[idx]) : "");
        }
        out << "\n";
    }
    file.close();
    QMessageBox::information(this, "成功", "历史数据已导出");
}

void MainWindow::onConnectButtonClicked()
{
    UserManager& userManager = UserManager::instance();

    // 如果未登录，先提示登录
    if (!userManager.isLoggedIn()) {
        QMessageBox::information(this, "提示", "请先登录后再操作PLC连接");
        return;
    }

    // 获取当前用户权限
    UserRole currentRole = userManager.currentUserRole();

    // 获取当前连接状态
    ControllerState currentState = m_connectionState;

    // 情况1: 未连接或连接错误状态
    if (currentState == ControllerState::DISCONNECTED ||
        currentState == ControllerState::STATE_ERROR) {

        // 获取当前选中的设备
        int idx = ui->comboBoxDevices->currentIndex();
        if (idx < 0) {
            QMessageBox::warning(this, "错误", "未选择任何设备，请先在设备配置中添加设备");
            return;
        }
        QString deviceId = ui->comboBoxDevices->itemData(idx).toString();
        DeviceConfig* dev = m_plcData->getDevice(deviceId);
        if (!dev) {
            QMessageBox::warning(this, "错误", "未找到有效的设备配置");
            return;
        }

        // 权限检查
        switch (currentRole) {
        case UserRole::VIEWER: {
            // 观察者：无权操作
            QMessageBox::warning(this, "权限不足",
                                 "观察者权限无法连接PLC\n"
                                 "请使用操作员或以上权限账号登录");
            return;
        }

        case UserRole::OPERATOR: {
            // 操作员：只能使用默认参数连接
            if (currentState == ControllerState::STATE_ERROR) {
                // 如果之前连接出错，操作员无法重试，需要工程师处理
                QMessageBox::critical(this, "连接错误",
                                      "PLC连接失败！\n"
                                      "可能原因：\n"
                                      "1. PLC IP地址错误\n"
                                      "2. 网络连接问题\n"
                                      "3. PLC参数配置错误\n\n"
                                      "请联系工程师检查连接参数！");
                return;
            }

            // 操作员正常连接，使用当前设备的参数
            emit connectRequested(dev->ip, dev->rack, dev->slot);
            showStatusMessage(QString("正在连接到 %1...").arg(dev->ip), 2000);
            break;
        }

        case UserRole::ENGINEER:
        case UserRole::ADMIN: {
            // 工程师和管理员：可以配置参数
            handleEngineerConnectAction(currentState);
            break;
        }
        }
    }
    // 情况2: 已连接状态
    else if (currentState == ControllerState::CONNECTED ||
             currentState == ControllerState::POLLING) {

        // 断开连接权限检查
        switch (currentRole) {
        case UserRole::VIEWER:
        case UserRole::OPERATOR: {
            // 观察者和操作员无权断开连接
            QMessageBox::warning(this, "权限不足",
                                 QString("当前用户权限不足，无法断开PLC连接\n"
                                         "需要工程师或管理员权限\n"
                                         "当前权限：%1").arg(userRoleToString(currentRole)));
            break;
        }

        case UserRole::ENGINEER:
        case UserRole::ADMIN: {
            // 工程师和管理员可以断开连接
            int result = QMessageBox::question(this, "确认",
                                               "确定要断开PLC连接吗？",
                                               QMessageBox::Yes | QMessageBox::No);

            if (result == QMessageBox::Yes) {
                emit disconnectRequested();
            }
            break;
        }
        }
    }
    // 情况3: 连接中状态
    else if (currentState == ControllerState::CONNECTING) {
        QMessageBox::information(this, "提示", "PLC正在连接中，请稍候...");
    }
}

void MainWindow::onRefreshProcessClicked() { refreshProcessView(); showStatusMessage("工艺流程已刷新", 2000); }
void MainWindow::onWriteValueClicked()
{
    QModelIndexList selected = ui->realTimeTableView->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        QMessageBox::warning(this, "警告", "请先选择一个标签");
        return;
    }

    for (const QModelIndex& index : selected) {
        int col = index.column();
        if (col == 0) continue; // 时间戳列不可写

        // 从实时数据模型的表头获取 tagKey（格式 "deviceId/tagName"）
        QString tagKey = m_realTimeModel->headerData(col, Qt::Horizontal).toString();
        if (tagKey.isEmpty()) continue;

        // 解析 tagKey 获取设备 ID 和标签名
        int slash = tagKey.indexOf('/');
        if (slash == -1) {
            qWarning() << "Invalid tagKey format:" << tagKey;
            continue;
        }
        QString deviceId = tagKey.left(slash);
        QString tagName = tagKey.mid(slash + 1);

        // 获取标签信息以判断数据类型和可写性
        TagInfo* tag = m_plcData->findTag(deviceId, tagName);
        if (!tag || !tag->writable) {
            QMessageBox::warning(this, "警告", QString("标签 %1 不可写").arg(tagKey));
            continue;
        }

        // 根据数据类型设置默认写入值（简化示例）
        QVariant newValue;
        switch (tag->dataType) {
        case TagDataType::BOOL:   newValue = true; break;
        case TagDataType::INT:
        case TagDataType::WORD:   newValue = 100;  break;
        case TagDataType::REAL:   newValue = 50.5; break;
        default:                  newValue = 0;    break;
        }

        // 发射写入请求，传递 tagKey 和值
        emit writeRequested(tagKey, newValue);
    }
}
void MainWindow::onExportDataClicked() { exportCurrentViewData(ViewType::TABLE_VIEW); }
void MainWindow::onSearchTextChanged(const QString& text) { Q_UNUSED(text); }
void MainWindow::onNavigationItemClicked(QListWidgetItem* item)
{
    QString text = item->text();

    if (text.contains("工艺总貌")) {
        ui->mainTabWidget->setCurrentIndex(0);
    } else if (text.contains("实时数据")) {
        ui->mainTabWidget->setCurrentIndex(1);
    } else if (text.contains("趋势图表")) {
        ui->mainTabWidget->setCurrentIndex(2);
    } else if (text.contains("历史数据")) {
        ui->mainTabWidget->setCurrentIndex(3);
    } else if (text.contains("控制面板")) {
        ui->mainTabWidget->setCurrentIndex(4);
    } else if (text.contains("报警中心")) {
        ui->mainTabWidget->setCurrentIndex(5);
    }
}
void MainWindow::onTabChanged(int index) { applyCurrentViewSelection(); }
void MainWindow::onChartPlayPauseClicked()
{
    m_chartPlaying = !m_chartPlaying;

    if (m_chartPlaying) {
        ui->btnPlayChart->setText("暂停");
        ui->btnPlayChart->setIcon(QIcon(":/icons/pause.png"));
        showStatusMessage("图表开始实时更新", 2000);
    } else {
        ui->btnPlayChart->setText("播放");
        ui->btnPlayChart->setIcon(QIcon(":/icons/play.png"));
        showStatusMessage("图表暂停更新", 2000);
    }
}
void MainWindow::onChartTimeRangeChanged(int seconds)
{
    qDebug() << "onChartTimeRangeChanged called with seconds:" << seconds;
    // 当 ChartManager 的时间范围变化时更新 UI
    QString timeText = QString("%1 秒").arg(seconds);
    showStatusMessage(QString("图表时间范围已更改为: %1").arg(timeText), 2000);

    // 如果需要更新组合框的显示
    int index = -1;
    static const int timeRanges[] = {60, 300, 900, 3600, 21600, 86400};
    for (int i = 0; i < 6; ++i) {
        if (timeRanges[i] == seconds) {
            index = i;
            break;
        }
    }

    if (index >= 0 && ui->comboTimeRange) {
        ui->comboTimeRange->blockSignals(true);
        ui->comboTimeRange->setCurrentIndex(index);
        ui->comboTimeRange->blockSignals(false);
    }
}
void MainWindow::onSaveChartClicked() {
    if (!m_chartManager) return;
    QCustomPlot* plot = m_chartManager->getCustomPlot();  // 需在 ChartManager 中添加 getter
    if (!plot) return;
    QString filename = QFileDialog::getSaveFileName(this, "保存图表", "", "PNG (*.png);;PDF (*.pdf)");
    if (filename.isEmpty()) return;
    if (filename.endsWith(".png")) {
        plot->savePng(filename);
    } else if (filename.endsWith(".pdf")) {
        plot->savePdf(filename);
    }
    showStatusMessage("图表已保存", 2000);
}
void MainWindow::onRealtimeTableSelectionChanged(const QItemSelection &selected,
                                                 const QItemSelection &deselected)
{
    Q_UNUSED(deselected);

    if (selected.isEmpty()) return;

    QModelIndex index = selected.indexes().first();
    if (index.isValid() && m_realTimeModel) {
        int row = index.row();
        int col = index.column();

        // 获取数据
        QVariant timeData = m_realTimeModel->data(m_realTimeModel->index(row, 0));
        QVariant tagName = m_realTimeModel->headerData(col, Qt::Horizontal);
        QVariant tagValue = m_realTimeModel->data(index);

        qDebug() << "选中表格项: 行" << row << ", 列" << col;
        qDebug() << "时间:" << timeData.toString();
        qDebug() << "标签:" << tagName.toString();
        qDebug() << "值:" << tagValue.toString();

        // 添加到图表
        if (m_chartManager && col > 0) {  // 第0列是时间
            m_chartManager->addChartTag(tagName.toString());
        }
    }
}
// 导出历史数据
void MainWindow::onExportHistoryClicked()
{
    if (m_lastHistoryResults.isEmpty()) {
        QMessageBox::warning(this, "警告", "没有可导出的历史数据，请先执行查询");
        return;
    }
    QString fileName = QFileDialog::getSaveFileName(this, "导出历史数据", "history_data.csv", "CSV文件 (*.csv)");
    if (fileName.isEmpty()) return;
    exportHistoryResultsToCsv(m_lastHistoryResults, fileName);
}
void MainWindow::onExportActionTriggered()
{
    int currentTab = ui->mainTabWidget->currentIndex();

    switch (currentTab) {
    case 0: exportCurrentViewData(ViewType::PROCESS_VIEW); break;
    case 1: exportCurrentViewData(ViewType::TABLE_VIEW); break;
    case 2: exportCurrentViewData(ViewType::CHART_VIEW); break;
    case 3: exportCurrentViewData(ViewType::TREND_VIEW); break;
    default:
        QMessageBox::information(this, "提示", "当前视图不支持导出");
    }
}
void MainWindow::onExitActionTriggered()
{
    if (QMessageBox::question(this, "确认", "确定要退出程序吗？") == QMessageBox::Yes) {
        QApplication::quit();
    }
}
void MainWindow::onViewActionTriggered(QAction* action)
{
    if (action == ui->actionViewProcess) {
        ui->mainTabWidget->setCurrentIndex(0);
    } else if (action == ui->actionViewRealTime) {
        ui->mainTabWidget->setCurrentIndex(1);
    } else if (action == ui->actionViewChart) {
        ui->mainTabWidget->setCurrentIndex(2);
    } else if (action == ui->actionViewHistory) {
        ui->mainTabWidget->setCurrentIndex(3);
    } else if (action == ui->actionViewControl) {
        ui->mainTabWidget->setCurrentIndex(4);
    } else if (action == ui->actionViewAlarm) {
        ui->mainTabWidget->setCurrentIndex(5);
    }
}
void MainWindow::onShowStatusBarToggled(bool checked) { ui->statusbar->setVisible(checked); }
void MainWindow::onAboutActionTriggered()
{
    QMessageBox::about(this, "关于 PLC数据监控系统",
                       "<h3>PLC数据监控系统 v1.1.0</h3>"
                       "<p>基于 Qt 开发的工业自动化监控软件</p>"
                       "<p>功能特点：</p>"
                       "<ul>"
                       "<li>实时数据监控</li>"
                       "<li>工艺流程展示</li>"
                       "<li>趋势图表分析</li>"
                       "<li>历史数据查询</li>"
                       "<li>标签管理功能</li>"
                       "</ul>"
                       "<p>© 2024 您的公司 版权所有</p>");
}
void MainWindow::onUserManualActionTriggered()
{
    QDialog *dialog = new QDialog(this);
    dialog->setWindowTitle("用户手册 - PLC数据监控系统 v1.1");
    dialog->resize(900, 650);
    QVBoxLayout *layout = new QVBoxLayout(dialog);
    QTextBrowser *browser = new QTextBrowser(dialog);
    browser->setOpenExternalLinks(false);
    browser->setSource(QUrl("qrc:/help/user_manual.html"));
    layout->addWidget(browser);
    dialog->setLayout(layout);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}
void MainWindow::onUpdateTimerTimeout() { updateStatusBar(); }
void MainWindow::onSystemTimerTimeout() { updateSystemTime(); }
void MainWindow::updateConnectionStatus(ControllerState state, const QString& message)
{
    m_connectionState = state;
    if (!message.isEmpty()) ui->statusLabelSystem->setText(message);
}
void MainWindow::updateSystemTime()
{
    ui->statusLabelTime->setText(QString("系统时间: %1").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")));
}
void MainWindow::updateStatusBar()
{
    // 通讯状态检测
    QString commStatus = "通讯: ";
    if (m_commManager) {
        if (m_connectionState == ControllerState::CONNECTED ||
            m_connectionState == ControllerState::POLLING) {
            commStatus += "正常";
        } else if (m_connectionState == ControllerState::CONNECTING) {
            commStatus += "连接中...";
        } else if (m_connectionState == ControllerState::DISCONNECTED) {
            commStatus += "已断开";
        } else if (m_connectionState == ControllerState::STATE_ERROR) {
            commStatus += "错误";
        } else {
            commStatus += "未知";
        }
    } else {
        commStatus += "未初始化";
    }

    ui->statusLabelComm->setText(commStatus);

    // 更新时间标签
    //  updateSystemTime();
}
void MainWindow::updateTagCounts()
{
    if (!m_plcData) return;
    int chartViewVal = static_cast<int>(ViewType::CHART_VIEW);
    qDebug() << "updateTagCounts: chartViewVal =" << chartViewVal;

    ui->lblProcessCount->setText(QString("工艺点: %1 个").
            arg(m_plcData->getSelectedTagKeysForView(ViewType::PROCESS_VIEW).size()));
    qDebug() << "PROCESS_VIEW view selected tags:" << m_plcData->getSelectedTagKeysForView(ViewType::PROCESS_VIEW);
    ui->lblRealTimeCount->setText(QString("已选择: %1 个标签").
            arg(m_plcData->getSelectedTagKeysForView(ViewType::TABLE_VIEW).size()));
    qDebug() << "TABLE_VIEW view selected tags:" << m_plcData->getSelectedTagKeysForView(ViewType::TABLE_VIEW);
    ui->lblChartCount->setText(QString("图表中: %1 个标签").
            arg(m_plcData->getSelectedTagKeysForView(ViewType::CHART_VIEW).size()));
    qDebug() << "Chart view selected tags:" << m_plcData->getSelectedTagKeysForView(ViewType::CHART_VIEW);

    //以下是调试代码，稍后删除
    auto chartTags = m_plcData->getSelectedTagKeysForView(ViewType::CHART_VIEW);
    qDebug() << "updateTagCounts: CHART_VIEW tags =" << chartTags;

}
void MainWindow::showStatusMessage(const QString& message, int timeout)
{
    ui->statusbar->showMessage(message, timeout);
}
void MainWindow::clearChart()
{
    if (m_chartManager) m_chartManager->clearChartData();
    if (m_trendManager) m_trendManager->clearTrendData();
    showStatusMessage("图表已清空", 2000);
}
void MainWindow::refreshProcessView()
{
    // 重新加载工艺流程
    showStatusMessage("工艺流程已刷新", 2000);
}
void MainWindow::exportCurrentViewData(ViewType viewType)
{
    QString defaultName;
    QMap<QString, QVariant> data;

    switch (viewType) {
    case ViewType::PROCESS_VIEW:
        defaultName = "工艺数据";
        data = m_lastProcessData;
        break;
    case ViewType::TABLE_VIEW:
        defaultName = "实时数据";
        data = m_lastTableData;
        break;
    case ViewType::CHART_VIEW:
        defaultName = "图表数据";
        data = m_lastChartData;
        break;
    case ViewType::TREND_VIEW:
        defaultName = "历史数据";
        // 这里应该获取历史数据
        break;
    case ViewType::VIEW_COUNT:
        // 或者使用 Q_ASSERT
        Q_ASSERT_X(false, "exportCurrentViewData", "VIEW_COUNT 不应该被处理");
        return;
    }

    if (data.isEmpty()) {
        QMessageBox::information(this, "提示", "当前没有数据可导出");
        return;
    }

    QString filename = QFileDialog::getSaveFileName(
        this,
        "导出数据",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/" + defaultName + ".csv",
        "CSV 文件 (*.csv);;所有文件 (*.*)"
        );

    if (!filename.isEmpty()) {
        exportToCsv(data, filename);
    }
}
void MainWindow::exportToCsv(const QMap<QString, QVariant>& data, const QString& filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", QString("无法创建文件: %1").arg(file.errorString()));
        return;
    }

    QTextStream out(&file);

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");  // Qt5
#else
    out.setEncoding(QStringConverter::Utf8);  // Qt6
#endif

    // 写入表头
    out << "标签名,值,数据类型,时间\n";

    // 写入数据
    QDateTime timestamp = QDateTime::currentDateTime();
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        out << "\"" << it.key() << "\","
            << "\"" << it.value().toString() << "\","
            << "\"未知\","
            << "\"" << timestamp.toString("yyyy-MM-dd HH:mm:ss") << "\"\n";
    }

    file.close();
    showStatusMessage(QString("数据已导出到: %1").arg(filename), 3000);
}
void MainWindow::setupThemes()
{
    // 创建主题菜单
    m_themeMenu = new QMenu(tr("主题"), this);

    // 创建深色主题菜单项
    m_actionDarkTheme = new QAction(tr("深色主题"), this);
    m_actionDarkTheme->setCheckable(true);
    m_actionDarkTheme->setChecked(false);

    // 创建浅色主题菜单项
    m_actionLightTheme = new QAction(tr("浅色主题"), this);
    m_actionLightTheme->setCheckable(true);
    m_actionLightTheme->setChecked(true);  // 默认浅色主题

    // 创建mac主题菜单项
    m_actionmacTheme = new QAction(tr("现代风"), this);
    m_actionmacTheme->setCheckable(true);
    m_actionmacTheme->setChecked(false);  // 默认浅色主题

    // 添加到菜单
    m_themeMenu->addAction(m_actionDarkTheme);
    m_themeMenu->addAction(m_actionLightTheme);
    m_themeMenu->addAction(m_actionmacTheme);

    // 添加到菜单栏
    ui->menubar->insertMenu(ui->menuHelp->menuAction(), m_themeMenu);

    // 连接信号
    connect(m_actionDarkTheme, &QAction::triggered, this, &MainWindow::onThemeMenuTriggered);
    connect(m_actionLightTheme, &QAction::triggered, this, &MainWindow::onThemeMenuTriggered);
    connect(m_actionmacTheme, &QAction::triggered, this, &MainWindow::onThemeMenuTriggered);

    // 从设置加载主题偏好
    loadThemeSettings();

    qDebug() << "主题菜单初始化完成";
}
void MainWindow::applyDarkTheme()
{
    // 直接调用加载函数
    if (loadStyleSheet(":/styles/dark.css")) {
        m_currentTheme = "dark";
        m_actionDarkTheme->setChecked(true);
        m_actionLightTheme->setChecked(false);
        m_actionmacTheme->setChecked(false);
        saveThemeSettings();
        showStatusMessage("已切换到深色主题", 2000);
    } else {
        // 如果加载失败，使用默认深色样式
        qWarning() << "深色主题样式表加载失败，使用内置样式";
        QString darkStyle = R"(
            QMainWindow { background-color: #2b2b2b; color: #ffffff; }
            QStatusBar { background-color: #333333; color: #ffffff; }
        )";
        qApp->setStyleSheet(darkStyle);
        m_currentTheme = "dark";
    }
}

void MainWindow::applyLightTheme()
{
    // 直接调用加载函数
    if (loadStyleSheet(":/styles/light.css")) {
        m_currentTheme = "light";
        m_actionDarkTheme->setChecked(false);
        m_actionLightTheme->setChecked(true);
        m_actionmacTheme->setChecked(false);
        saveThemeSettings();
        showStatusMessage("已切换到浅色主题", 2000);
    } else {
        // 如果加载失败，使用默认样式
        qWarning() << "浅色主题样式表加载失败，使用默认样式";
        qApp->setStyleSheet("");
        m_currentTheme = "light";
    }
}
void MainWindow::applyAppleTheme()
{
    if (loadStyleSheet(":/styles/mac.css")) {
        m_currentTheme = "apple";

        // 更新主题菜单选中状态
        m_actionDarkTheme->setChecked(false);
        m_actionLightTheme->setChecked(false);
        m_actionmacTheme->setChecked(true);

        saveThemeSettings();
        showStatusMessage("已切换到苹果风格主题", 2000);
    } else {
        // 如果样式表加载失败，使用内置苹果风格
        QString appleStyle = R"(
            /* ... 内联样式表 ... */
        )";

        qApp->setStyleSheet(appleStyle);
        m_currentTheme = "apple";

        // 更新主题菜单选中状态
        m_actionDarkTheme->setChecked(false);
        m_actionLightTheme->setChecked(false);
        m_actionmacTheme->setChecked(true);
    }
}
void MainWindow::onThemeMenuTriggered(bool checked)
{
    Q_UNUSED(checked);

    QAction* action = qobject_cast<QAction*>(sender());
    if (!action) return;

    if (action == m_actionDarkTheme) {
        applyDarkTheme();
    } else if (action == m_actionLightTheme) {
        applyLightTheme();
    } else if (action == m_actionmacTheme) {
        applyAppleTheme();
    }
}
void MainWindow::loadThemeSettings()
{
    QSettings settings("PLCMonitor", "PLCMonitor");
    QString theme = settings.value("theme", "mac").toString();  // 默认苹果风格

    if (theme == "dark") {
        m_actionDarkTheme->setChecked(true);
        m_actionLightTheme->setChecked(false);
        m_actionmacTheme->setChecked(false);
        applyDarkTheme();
    } else if (theme == "mac") {
        m_actionDarkTheme->setChecked(false);
        m_actionLightTheme->setChecked(false);
        m_actionmacTheme->setChecked(true);
        applyAppleTheme();
    } else {  // light
        m_actionLightTheme->setChecked(true);
        m_actionDarkTheme->setChecked(false);
        m_actionmacTheme->setChecked(false);
        applyLightTheme();
    }
}

void MainWindow::saveThemeSettings()
{
    QSettings settings("PLCMonitor", "PLCMonitor");
    settings.setValue("theme", m_currentTheme);
    //  settings.setValue("darkTheme", m_isDarkTheme);
    settings.sync();
    qDebug() << "主题设置已保存: " << m_currentTheme;
}
bool MainWindow::loadStyleSheet(const QString &fileName)
{
    // 1. 首先尝试从 Qt 资源系统加载 (qrc)
    // 检查传入的 fileName 是否已经是资源路径格式 (以 ":/" 开头)
    if (fileName.startsWith(":/")) {
        QFile file(fileName);
        if (file.open(QFile::ReadOnly)) {
            QString styleSheet = QLatin1String(file.readAll());
            file.close();
            qApp->setStyleSheet(styleSheet);
            qDebug() << "成功从资源系统加载样式表:" << fileName;
            return true;
        } else {
            qWarning() << "无法打开资源文件:" << fileName << "错误:" << file.errorString();
        }
    }

    // 2. 如果资源加载失败，尝试从 "可执行文件所在目录/styles/" 下加载
    // 获取可执行文件所在的目录路径
    QString exeDir = QCoreApplication::applicationDirPath();
    // 构建预期的文件路径：例如 D:/PLCMonitor/build/bin/styles/dark.css
    QString localFilePath = exeDir + "/styles/" + QFileInfo(fileName).fileName();

    qDebug() << "尝试从文件系统加载:" << localFilePath;

    QFile file2(localFilePath);
    if (file2.open(QFile::ReadOnly)) {
        QString styleSheet = QLatin1String(file2.readAll());
        file2.close();
        qApp->setStyleSheet(styleSheet);
        qDebug() << "成功从文件系统加载样式表:" << localFilePath;
        return true;
    } else {
        qWarning() << "无法打开文件系统样式表:" << localFilePath << "错误:" << file2.errorString();
    }

    // 3. 最后尝试从当前工作目录 (通常是构建目录) 加载
    // 这是为了兼容某些调试环境
    QString currentDirPath = QDir::currentPath() + "/styles/" + QFileInfo(fileName).fileName();

    if (currentDirPath != localFilePath) { // 避免重复尝试
        QFile file3(currentDirPath);
        if (file3.open(QFile::ReadOnly)) {
            QString styleSheet = QLatin1String(file3.readAll());
            file3.close();
            qApp->setStyleSheet(styleSheet);
            qDebug() << "成功从当前工作目录加载样式表:" << currentDirPath;
            return true;
        } else {
            qWarning() << "无法打开当前工作目录样式表:" << currentDirPath << "错误:" << file3.errorString();
        }
    }

    // 所有方法都失败了
    qWarning() << "致命错误：无法加载任何样式表文件。请检查资源文件或" << exeDir << "/styles/ 目录下是否存在" << QFileInfo(fileName).fileName();
    return false;
}
void MainWindow::setupThemeMenu() { /* 原有实现 */ }
void MainWindow::setupUserMenu()
{

    QMenu *userMenu = menuBar()->addMenu(tr("用户(&U)"));
    m_actionLogin = new QAction(tr("登录"), this);
    m_actionLogout = new QAction(tr("注销"), this);
    m_actionUserManage = new QAction(tr("用户管理"), this);

    connect(m_actionLogin, &QAction::triggered, this, [this](){
        LoginDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            updateUIPermissions();
            updateLoginStatusButton();
        }
    });

    connect(m_actionLogout, &QAction::triggered, this, [this](){
        UserManager::instance().logout();
        updateUIPermissions();
        updateLoginStatusButton();
        // 不再自动弹出登录框，由用户手动点击“登录”菜单
        statusBar()->showMessage(tr("已注销，请重新登录"), 3000);
    });

    connect(m_actionUserManage, &QAction::triggered, this, [this](){
        if (UserManager::instance().hasPermission(UserRole::ADMIN)) {
            UserManageDialog dlg(this);
            dlg.exec();
            updateUIPermissions(); // 可能当前用户被禁用或角色变更，刷新权限
        } else {
            QMessageBox::warning(this, "权限不足", "只有管理员可以管理用户");
        }
    });

    userMenu->addAction(m_actionLogin);
    userMenu->addAction(m_actionLogout);
    userMenu->addSeparator();
    userMenu->addAction(m_actionUserManage);
}

void MainWindow::updateUIPermissions()
{
    bool loggedIn = UserManager::instance().isLoggedIn();
    bool canManageUsers = UserManager::instance().canManageUsers();
    bool canManageTags = UserManager::instance().canManageTags();
    bool canWriteData = UserManager::instance().canWriteData();
    bool canConfigureSystem = UserManager::instance().canConfigureSystem();

    // 菜单项
    m_actionLogin->setEnabled(!loggedIn);
    m_actionLogout->setEnabled(loggedIn);
    m_actionUserManage->setEnabled(canManageUsers);

    // 设备配置按钮（工程师及以上可配置设备）
    ui->btnTagManagerHeader->setEnabled(canManageTags);

    // 写入值按钮（操作员及以上可写）
    ui->btnWriteValue->setEnabled(canWriteData);

    // 系统配置相关（管理员和工程师）
    ui->actionConnectPLC->setEnabled(canConfigureSystem);
    ui->actionDisconnectPLC->setEnabled(canConfigureSystem);
    ui->actionDbConfig->setEnabled(canConfigureSystem);
}

void MainWindow::onUserLoggedIn(const QString& username)
{
    statusBar()->showMessage(tr("欢迎 %1 登录").arg(username), 3000);
    updateUIPermissions();
    updateLoginStatusButton();
}
void MainWindow::onUserLoggedOut()
{
    statusBar()->showMessage(tr("已注销"), 3000);
    updateUIPermissions();
    updateLoginStatusButton();
}

void MainWindow::onLoginStatusClicked()
{
    // 仅当未登录时才弹出登录对话框
    if (!UserManager::instance().isLoggedIn()) {
        LoginDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            updateUIPermissions();          // 刷新界面权限
            updateLoginStatusButton();      // 刷新按钮显示
        }
    }
    // 已登录时按钮可禁用或什么都不做，我们通过 updateLoginStatusButton 设置 enabled=false
}

void MainWindow::updateLoginStatusButton()
{
    if (UserManager::instance().isLoggedIn()) {
        QString username = UserManager::instance().currentUsername();
        ui->btnLoginStatus->setText(QString("当前用户: %1").arg(username));
        ui->btnLoginStatus->setEnabled(false);   // 已登录不可点击
        ui->btnLoginStatus->setStyleSheet("QPushButton { border: none; background: transparent; color: #888; }");
    } else {
        ui->btnLoginStatus->setText("登录");
        ui->btnLoginStatus->setEnabled(true);
        ui->btnLoginStatus->setStyleSheet("QPushButton { border: none; background: transparent; color: #0078D7; }");
    }
}
void MainWindow::onDatabaseStatusChanged(bool mysqlConnected)
{
    updateDatabaseStatus(mysqlConnected);
}
void MainWindow::updateDatabaseStatus(bool mysqlConnected)
{
    if (mysqlConnected) {
        ui->statusLabelSystem->setText("数据库: MySQL 已连接");
        ui->statusLabelSystem->setStyleSheet("color: green; font-weight: bold;");
    } else {
        ui->statusLabelSystem->setText("数据库: 本地 SQLite");
        ui->statusLabelSystem->setStyleSheet("color: orange; font-weight: bold;");
    }
}
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr  *result)
{
#ifdef Q_OS_WIN
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_POWERBROADCAST) {
        if (msg->wParam == PBT_APMRESUMEAUTOMATIC || msg->wParam == PBT_APMRESUMESUSPEND) {
            qDebug() << "System resumed from sleep, requesting PLC reconnect";
            emit systemResumed();   // 发出信号，由外部连接处理
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}
bool MainWindow::savePLCConfig()
{
    // 权限验证
    if (!UserManager::instance().hasPermission(UserRole::ENGINEER)) {
        return false;
    }

    // 调用 Application 的保存功能
    return Application::instance()->savePLCConfig("configs/plc_config.json");
}
ViewType MainWindow::mapTabIndexToViewType(int index) const
{
    switch (index) {
    case 0: return ViewType::PROCESS_VIEW;
    case 1: return ViewType::TABLE_VIEW;
    case 2: return ViewType::CHART_VIEW;
    case 3: return ViewType::TREND_VIEW;
    default: return ViewType::TABLE_VIEW;
    }
}
void MainWindow::performOneTimeInitialization()
{ /* 原逻辑中创建数据管理器，但已在 setDataManagers 中创建，故清空 */ }
void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);

    // 使用一个静态变量确保只初始化一次
    static bool hasInitialized = false;
    if (!hasInitialized) {
        hasInitialized = true;
        qDebug() << "=== 开始执行一次性 UI 初始化 ===";
        performOneTimeInitialization();
        qDebug() << "=== 一次性 UI 初始化完成 ===";
    }
}

void MainWindow::handleEngineerConnectAction(ControllerState currentState)
{
    if (currentState == ControllerState::STATE_ERROR) {
        // 连接失败，显示参数配置对话框
        int result = QMessageBox::question(this, "连接失败",
                                           "PLC连接失败，是否要修改连接参数？",
                                           QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

        if (result == QMessageBox::Yes) {
            showConnectionConfigDialog();
        } else if (result == QMessageBox::No) {
            // 使用当前参数重试
            emit connectRequested(m_currentPLCIP, m_currentPLCRack, m_currentPLCSlot);
            showStatusMessage(QString("重试连接到 %1...").arg(m_currentPLCIP), 2000);
        }
    } else {
        // 正常连接，可以配置参数
        int result = QMessageBox::question(this, "连接PLC",
                                           "请选择连接方式：\n"
                                           "是(Y)：使用当前参数直接连接\n"
                                           "否(N)：设置连接参数后连接",
                                           QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

        if (result == QMessageBox::Yes) {
            // 使用当前参数连接
            emit connectRequested(m_currentPLCIP, m_currentPLCRack, m_currentPLCSlot);
            showStatusMessage(QString("正在连接到 %1...").arg(m_currentPLCIP), 2000);
        } else if (result == QMessageBox::No) {
            // 显示参数配置对话框
            showConnectionConfigDialog();
        }
    }
}
void MainWindow::showConnectionConfigDialog()
{
    ConnectionConfigDialog dlg(this);

    // 设置当前参数
    dlg.setPLCParams(m_currentPLCIP, m_currentPLCRack, m_currentPLCSlot,
                     m_currentPLCPort, m_PLCTimeout, m_pollingInterval);

    if (dlg.exec() == QDialog::Accepted) {
        // 获取新参数
        QString ip = dlg.getPLCIP();
        int rack = dlg.getPLCRack();
        int slot = dlg.getPLCSlot();
        int port = dlg.getPLCPort();
        int timeout = dlg.getPLCTimeout();
        int interval = dlg.getPLCInterval();

        // 验证参数
        if (ip.isEmpty()) {
            QMessageBox::warning(this, "错误", "IP地址不能为空");
            return;
        }

        if (rack < 0 || rack > 31) {
            QMessageBox::warning(this, "错误", "Rack号必须在0-31之间");
            return;
        }

        if (slot < 0 || slot > 31) {
            QMessageBox::warning(this, "错误", "Slot号必须在0-31之间");
            return;
        }

        if (port < 1 || port > 65535) {
            QMessageBox::warning(this, "错误", "端口号必须在1-65535之间");
            return;
        }

        if (timeout < 1000 || timeout > 30000) {
            QMessageBox::warning(this, "错误", "连接超时必须设置在1-30秒之间");
            return;
        }

        if (interval < 100 || interval > 10000) {
            QMessageBox::warning(this, "错误", "轮询间隔必须设置在100-10000毫秒之间");
            return;
        }

        // 更新当前参数
        m_currentPLCIP = ip;
        m_currentPLCRack = rack;
        m_currentPLCSlot = slot;
        m_currentPLCPort = port;
        m_PLCTimeout = timeout;
        m_pollingInterval = interval;

        // 保存配置
        if (savePLCConfig()) {
            QMessageBox::information(this, "成功", "PLC连接参数已保存");
        } else {
            QMessageBox::warning(this, "警告", "PLC连接参数保存失败");
        }

        // 使用新参数连接
        emit connectRequested(ip, rack, slot);
        showStatusMessage(QString("正在连接到 %1:%2...").arg(ip).arg(port), 2000);
    }
}
void MainWindow::onTableDataUpdated(const QMap<QString, QVariant>& data)
{
    m_lastTableData = data;

    // 关键：将数据添加到实时数据模型
    if (m_realTimeModel) {
        m_realTimeModel->addDataRecord(data);
    }

    if (ui->mainTabWidget->currentIndex() == 1) {
        if (m_plcData) {
            QStringList currentSelectedTags = m_plcData->getSelectedTagKeysForView(ViewType::TABLE_VIEW);
            // 注意：setVisibleTags 已经在 Controller::onTagSelectionChanged 中调用过，
            // 这里不需要重复调用，否则会频繁重置模型列。
            // 如果仍需确保列同步，可保留但建议只调用一次。
            // m_realTimeModel->setVisibleTags(currentSelectedTags);
            ui->lblRealTimeCount->setText(QString("已选择: %1 个标签").arg(currentSelectedTags.size()));
        }
    }
}
void MainWindow::onChartDataUpdated(const QMap<QString, QVariant>& data)
{
    // 将数据传递给图表管理器
    if (m_chartManager) {
        m_chartManager->updateChartData(data);
    }
}
void MainWindow::onProcessDataUpdated(const QMap<QString, QVariant>& data)
{
    m_lastProcessData = data;
    if (ui->mainTabWidget->currentIndex() == 0 && m_processViewManager) m_processViewManager->updateProcessData(data);
}
void MainWindow::onChartTagsChanged(const QStringList& tags)
{
    qDebug() << "Chart tags changed:" << tags;
}
void MainWindow::onChartCleared()
{
    // 处理图表清空事件
    qDebug() << "Chart cleared";

    // 更新UI状态
    ui->btnClearChart->setEnabled(false);
    ui->btnSaveChart->setEnabled(false);

    // 显示状态信息
    statusBar()->showMessage("图表数据已清空", 2000);
}

void MainWindow::onPLCConfigLoaded(const QString& ip, int rack, int slot, int port)
{
    qDebug() << "MainWindow: 收到PLC配置信号";
    qDebug() << "  IP:" << ip;
    qDebug() << "  Rack:" << rack;
    qDebug() << "  Slot:" << slot;
    qDebug() << "  Port:" << port;

    // 保存配置
    m_currentPLCIP = ip;
    m_currentPLCRack = rack;
    m_currentPLCSlot = slot;
    m_currentPLCPort = port;

    // 更新UI显示（如果有相关的UI控件）

    // 显示状态消息
    QString message = QString("已加载PLC配置: IP=%1, Rack=%2, Slot=%3")
                          .arg(ip).arg(rack).arg(slot);
    showStatusMessage(message, 3000);
}
void MainWindow::onConnectionStateChanged(ControllerState state)
{
    qDebug() << "=== onConnectionStateChanged 被调用 ===";
    qDebug() << "状态: " << static_cast<int>(state);
    qDebug() << "状态字符串: " << controllerStateToString(state);

    m_connectionState = state;

    // 更新状态栏
    QString stateText;
    QString color;
    QString buttonText = "连接PLC";
    bool buttonEnabled = true;

    switch (state) {
    case ControllerState::DISCONNECTED:
        stateText = "未连接";
        color = "red";
        buttonText = "连接PLC";
        buttonEnabled = true;
        break;
    case ControllerState::CONNECTING:
        stateText = "连接中...";
        color = "orange";
        buttonText = "连接中";
        buttonEnabled = false;
        break;
    case ControllerState::CONNECTED:
        stateText = "已连接";
        color = "green";
        buttonText = "断开连接";

        // 只有工程师以上权限才能断开连接
        if (UserManager::instance().hasPermission(UserRole::ENGINEER)) {
            buttonEnabled = true;
        } else {
            buttonEnabled = false;
        }
        break;
    case ControllerState::STATE_ERROR:
        stateText = "连接错误";
        color = "red";
        buttonText = "连接PLC";
        buttonEnabled = true;
        break;
    case ControllerState::POLLING:
        stateText = "已连接,轮询中";
        color = "blue";
        buttonText = "断开连接";

        // 只有工程师以上权限才能断开连接
        if (UserManager::instance().hasPermission(UserRole::ENGINEER)) {
            buttonEnabled = true;
        } else {
            buttonEnabled = false;
        }
        break;
    case ControllerState::PAUSED:
    case ControllerState::RECONNECTING:
    case ControllerState::SHUTTING_DOWN:
        break;
    }

    ui->statusLabelPLC->setText(QString("PLC: %1").arg(stateText));
    ui->statusLabelPLC->setStyleSheet(QString("color: %1;").arg(color));
    ui->statusLabelPLC->setMinimumWidth(120);

    ui->btnConnectPLC->setText(buttonText);
    ui->btnConnectPLC->setEnabled(buttonEnabled);

    // 更新菜单项
    ui->actionConnectPLC->setEnabled(state == ControllerState::DISCONNECTED ||
                                     state == ControllerState::STATE_ERROR);
    ui->actionDisconnectPLC->setEnabled((state == ControllerState::CONNECTED ||
                                         state == ControllerState::POLLING) &&
                                        UserManager::instance().hasPermission(UserRole::ENGINEER));

    updateStatusBar();
}
void MainWindow::onTagValueChanged(const QString& tagName, const QVariant& value)
{
    ui->statusLabelUpdate->setText(QString("最近更新: %1").arg(tagName));
}
void MainWindow::onAlertTriggered(const AlertInfo& alert)
{
    qDebug() << "MainWindow::onAlertTriggered received, alarm id:" << alert.id;
    QString fullMessage = QString("[%1] %2").arg(alertLevelToString(alert.level), alert.message);
   // showStatusMessage(fullMessage, 5000);
    qDebug() << "告警触发:" << fullMessage;

    // 更新状态栏报警信息
    if (ui->statusLabelAlarm) {
        QString displayText = QString("报警: %1").arg(fullMessage);
        ui->statusLabelAlarm->setText(displayText);
        // 根据报警级别设置不同颜色
        switch (alert.level) {
        case AlertLevel::INFO:
            ui->statusLabelAlarm->setStyleSheet("color: green;");
            break;
        case AlertLevel::WARNING:
            ui->statusLabelAlarm->setStyleSheet("color: orange;");
            break;
        case AlertLevel::ERRORAlert:
            ui->statusLabelAlarm->setStyleSheet("color: red;");
            break;
        case AlertLevel::CRITICAL:
            ui->statusLabelAlarm->setStyleSheet("color: magenta;");
            break;
        default:
            ui->statusLabelAlarm->setStyleSheet("");
            break;
        }
    }

    // 添加到报警中心模型
    if (m_alarmModel) {
        m_alarmModel->addAlarm(alert);
    }
}

void MainWindow::onWriteResult(const QString& tagName, bool success, const QString& error)
{
    if (success) {
        showStatusMessage(QString("标签 %1 写入成功").arg(tagName), 2000);
    } else {
        QString errorMsg = QString("标签 %1 写入失败: %2").arg(tagName, error);
        showStatusMessage(errorMsg, 5000);
        QMessageBox::warning(this, "写入失败", errorMsg);
    }
}QColor MainWindow::getColorForGraph(int index)
{
    static QVector<QColor> colors = {
        Qt::red,
        Qt::blue,
        Qt::green,
        Qt::magenta,
        Qt::cyan,
        Qt::darkYellow,
        Qt::darkCyan,
        Qt::darkMagenta};
    return colors.at(index % colors.size());
}

void MainWindow::setupAlarmCenter()
{
    // 通过 objectName 查找报警中心标签页
    QWidget *alarmTab = ui->mainTabWidget->findChild<QWidget*>("tabAlarm");
    if (!alarmTab) {
        qWarning() << "setupAlarmCenter: 未找到 tabAlarm";
        return;
    }

    // 清除原有布局及子控件（例如开发中的标签）
    QLayout *oldLayout = alarmTab->layout();
    if (oldLayout) {
        QLayoutItem *item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            if (item->widget()) delete item->widget();
            delete item;
        }
        delete oldLayout;
    } else {
        // 如果没有布局，直接删除所有子控件
        QList<QWidget*> children = alarmTab->findChildren<QWidget*>();
        for (QWidget *w : children) {
            delete w;
        }
    }

    // 创建新布局
    QVBoxLayout *layout = new QVBoxLayout(alarmTab);

    // 表格视图
    m_alarmView = new QTableView(alarmTab);
    m_alarmModel = new AlarmListModel(this);
    m_alarmView->setModel(m_alarmModel);
    m_alarmView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_alarmView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_alarmView->horizontalHeader()->setStretchLastSection(true);

    // 设置列宽
    m_alarmView->setColumnWidth(AlarmListModel::ColId, 60);      // ID
    m_alarmView->setColumnWidth(AlarmListModel::ColTime, 160);    // 时间
    m_alarmView->setColumnWidth(AlarmListModel::ColTag, 200);      // 标签
    m_alarmView->setColumnWidth(AlarmListModel::ColMessage, 300);  // 消息（可拉伸）
    m_alarmView->setColumnWidth(AlarmListModel::ColValue, 80);     // 值
    m_alarmView->setColumnWidth(AlarmListModel::ColLevel, 80);     // 级别
    m_alarmView->setColumnWidth(AlarmListModel::ColStatus, 80);    // 状态

    // 让消息列随窗口拉伸
    m_alarmView->horizontalHeader()->setSectionResizeMode(AlarmListModel::ColMessage, QHeaderView::Stretch);
    // 其他列固定宽度
    for (int i = 0; i < AlarmListModel::ColCount; ++i) {
        if (i != AlarmListModel::ColMessage)
            m_alarmView->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Fixed);
    }

    layout->addWidget(m_alarmView);

    // 确认按钮
    QPushButton *ackBtn = new QPushButton("确认选中报警", alarmTab);
    layout->addWidget(ackBtn);
    connect(ackBtn, &QPushButton::clicked, this, &MainWindow::onAcknowledgeAlarm);

    // 加载现有活动报警
    QTimer::singleShot(500, this, [this]() {
        QList<AlertInfo> alarms = DatabaseManager::instance().getActiveAlarms();
        qDebug() << "加载活动报警数量:" << alarms.size();
        if (m_alarmModel) {
            m_alarmModel->setAlarms(alarms);
        }
    });
}

void MainWindow::onAcknowledgeAlarm()
{
    QItemSelectionModel *selectionModel = m_alarmView->selectionModel();
    if (!selectionModel || selectionModel->selectedRows().isEmpty()) {
        QMessageBox::warning(this, "提示", "请先选择要确认的报警");
        return;
    }

    if (!UserManager::instance().hasPermission(UserRole::OPERATOR)) {
        QMessageBox::warning(this, "权限不足", "您没有确认报警的权限");
        return;
    }

    QList<int> alarmIds;
    for (const QModelIndex &idx : selectionModel->selectedRows()) {
        alarmIds.append(m_alarmModel->data(idx.sibling(idx.row(), AlarmListModel::ColId)).toInt());
    }

    if (alarmIds.isEmpty())
        return;

    if (QMessageBox::question(this, "批量确认",
                              QString("确认要确认 %1 个报警吗？").arg(alarmIds.size()),
                              QMessageBox::Yes, QMessageBox::No) != QMessageBox::Yes)
        return;

    QString currentUser = UserManager::instance().currentUsername();
    // 批量确认数据库
    DatabaseManager::instance().acknowledgeAlarms(alarmIds, currentUser);
    // 更新模型显示
    m_alarmModel->updateAcknowledgedBatch(alarmIds);

    showStatusMessage(QString("已确认 %1 个报警").arg(alarmIds.size()), 3000);
}