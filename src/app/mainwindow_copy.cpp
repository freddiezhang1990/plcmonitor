// mainwindow.cpp
#include "mainwindow.h"
#include "ChartManager.h"
#include "application.h"
#include "databaseconfigdialog.h"
#include "historyquerydialog.h"
#include "processviewmanager.h"
#include "trendmanager.h"
#include "databasemanager.h"
#include "ui_mainwindow.h"  // 由Qt Designer生成的UI头文件
#include "plcdata.h"
#include "plcconnector.h"
#include "controller.h"
#include "common_types.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDateTime>
#include <QTextStream>
#include <QHeaderView>
#include <QTreeWidgetItem>
#include <QGraphicsItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsTextItem>
#include <QGraphicsLineItem>
#include <QDateTime>       // 时间处理
#ifdef Q_OS_WIN
  #include <windows.h>
  // 确保支持电源管理消息（需要 Windows Vista 或更高版本）
  #ifndef _WIN32_WINNT
   #define _WIN32_WINNT 0x0600
  #endif
#endif

MainWindow::MainWindow(PLCData* dataModel, PLCConnector* plcConnector, QThread* plcThread, QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_plcData(dataModel)
    , m_plcConnector(plcConnector)
    , m_controller(nullptr)
    , m_plcThread(plcThread)
    , m_connectionState(ControllerState::DISCONNECTED)
    , m_chartPlaying(false)
    , m_chartTimeRange(5)  // 默认5分钟
    , m_currentTheme("apple")  // 默认浅色主题
    , m_themeMenu(nullptr)
    , m_actionDarkTheme(nullptr)
    , m_actionLightTheme(nullptr)
{
    ui->setupUi(this);
    setWindowTitle("PLC数据监控系统");
    resize(1600, 900);

    // 仅设置基本状态栏（可选）
    ui->statusbar->show();
    ui->statusLabelPLC->setText("PLC: 未连接");
    ui->statusLabelComm->setText("通讯: 未初始化");

    // 所有其他初始化全部移除，延迟到 showEvent 中执行
    qDebug() << "MainWindow 构造完成（极简模式）";
}

/*
{
    ui->setupUi(this);
    // 初始化组件
    m_realTimePlot = nullptr;
    m_historyPlot = nullptr;

    m_updateTimer = new QTimer(this);
    m_systemTimer = new QTimer(this);

    // 初始化主题
    setupThemes();

    // 设置窗口属性
    setWindowTitle("PLC数据监控系统");
    resize(1600, 900);

    // 初始化UI
    setupUI();

    // ---------- 用户管理初始化延迟执行 ----------
    // 注意：原来的同步初始化代码已被移除，改用延迟调用
    // 这样可以确保主窗口完全构建后再弹出登录对话框，避免事件循环冲突

    // 其他初始化（不依赖用户管理的部分）正常执行
    connectDataSignals();
    setupCharts();
    setuptrend();
    setupTagManager();

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

    // 标签管理器
    m_tagManager = new TagManager(m_plcData, this);
    m_tagManager->setTrees(ui->treeWidgetProcessTags,
                           ui->treeWidgetRealtimeTags,
                           ui->treeWidgetChartTags,
                           ui->treeWidgetHistoryTags);
    m_tagManager->loadTags();
    m_tagManager->loadSelections();

    int currentTab = ui->mainTabWidget->currentIndex();
    ViewType initialView = mapTabIndexToViewType(currentTab);
    m_tagManager->setTabWidget(ui->tabWidgetTagViews);
    m_tagManager->setActiveView(initialView);

    setupConnections();

    connect(&DatabaseManager::instance(), &DatabaseManager::historyQueryResultReady,
            this, &MainWindow::onHistoryQueryResultReady);

    // 启动定时器
    m_systemTimer->start(1000);
    m_updateTimer->start(500);

    // ----- 延迟执行用户管理初始化（包括登录对话框）-----
    QTimer::singleShot(100, this, [this]() {
        if (!UserManager::instance().isLoggedIn()) {
            // 1. 将 loginDlg 定义为成员变量，或者使用 new 在堆上创建
            // 这里假设你将其改为 MainWindow 的成员变量 m_loginDialog
            if (!m_loginDialog) {
                m_loginDialog = new LoginDialog(this);
                // 连接登录成功和关闭的信号，以便清理
                connect(m_loginDialog, &QDialog::finished, this, [this](int result){
                    if (result == QDialog::Accepted) {
                        onUserLoggedIn(UserManager::instance().currentUsername());
                    }
                    m_loginDialog = nullptr; // 关闭后清空指针
                });
            }
            // 2. 使用 show() 而不是 exec()，使其成为非模态窗口
            m_loginDialog->show();
            m_loginDialog->raise(); // 确保窗口在最前面
            m_loginDialog->activateWindow();
        }
    });

    qDebug() << "MainWindow 初始化完成";
}   */

MainWindow::~MainWindow()
{
    if (m_updateTimer && m_updateTimer->isActive()) {
        m_updateTimer->stop();
    }
    if (m_systemTimer && m_systemTimer->isActive()) {
        m_systemTimer->stop();
    }

    delete ui;
    delete m_realTimePlot;
    delete m_historyPlot;

}

void MainWindow::setupUI()
{
    // 设置苹果主题窗口样式
  //   applyAppleTheme();

    // 设置窗口样式
    setStyleSheet("QMainWindow { background-color: #F5F5F5; }");

    // 初始化状态栏
    // 初始化状态栏
  //  ui->statusbar->setStyleSheet("background-color: #F0F0F0; color: black; border-top: 1px solid #C0C0C0;");
    ui->statusbar->show();
    // 数据库状态标签
   // ui->statusbar->addPermanentWidget(ui->statusLabelDB, 1);
    updateDatabaseStatus(false); // 初始状态

  //  QLabel* timeLabel = new QLabel("12:00:00");
  //  timeLabel->setStyleSheet("color: #555555; font-weight: bold;");

    ui->statusbar->clearMessage();

    ui->statusbar->addWidget(ui->statusLabelPLC);
    ui->statusbar->addWidget(ui->statusLabelComm);
    ui->statusbar->addWidget(ui->statusLabelSystem, 1);
    ui->statusbar->addWidget(ui->statusLabelTime);
    ui->statusbar->addWidget(ui->statusLabelUpdate);

    // 设置大小策略
    ui->statusLabelPLC->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    ui->statusLabelComm->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    ui->statusLabelSystem->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    ui->statusLabelTime->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    ui->statusLabelUpdate->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);

    ui->statusLabelPLC->setMinimumWidth(120);
    ui->statusLabelComm->setMinimumWidth(100);

    // 4. 初始化文本
    ui->statusLabelPLC->setText("PLC: 未连接");
    ui->statusLabelComm->setText("通讯: 未初始化");
    ui->statusLabelUpdate->setText("最近更新: 无");
    ui->statusLabelTime->setText(QDateTime::currentDateTime().toString("HH:mm:ss"));

    // 7. 启动定时器更新时间（如果您有这个功能的话）
  //  m_systemTimer->start(1000); // 确保您的定时器已启动,已经在构造函数启动

    // 设置表格属性
    ui->realTimeTableView->setAlternatingRowColors(true);
    ui->realTimeTableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->realTimeTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->realTimeTableView->setSortingEnabled(true);
    ui->realTimeTableView->horizontalHeader()->setStretchLastSection(true);
    ui->realTimeTableView->verticalHeader()->setVisible(false);

    // 设置标签树
    QStringList headers = {"选择","标签名","地址","数据类型","实时值","描述","记录DB"};
    ui->treeWidgetProcessTags->setHeaderLabels(headers);
    ui->treeWidgetRealtimeTags->setHeaderLabels(headers);
    ui->treeWidgetChartTags->setHeaderLabels(headers);
    ui->treeWidgetHistoryTags->setHeaderLabels(headers);

    // 设置图表视图
//    ui->chartWidget->setRenderHint(QPainter::Antialiasing);
//    ui->historyChartWidget->setRenderHint(QPainter::Antialiasing);

    // 设置工艺流程视图
    ui->graphicsViewProcessFlow->setRenderHint(QPainter::Antialiasing);
    ui->graphicsViewProcessFlow->setDragMode(QGraphicsView::ScrollHandDrag);

    // 更新状态栏
    updateStatusBar();

    qDebug() << "UI 设置完成";
}

void MainWindow::setupConnections()
{
    qDebug() << "开始设置 MainWindow 信号连接...";

    // 连接按钮
    connect(ui->btnConnectPLC, &QPushButton::clicked, this, &MainWindow::onConnectButtonClicked);
    //标签管理器按钮
  //  connect(ui->btnTagManagerHeader, &QPushButton::clicked, ui->dockTagManager, &QDockWidget::show);

    // 导航列表
    connect(ui->navigationList, &QListWidget::itemClicked, this, &MainWindow::onNavigationItemClicked);

    // 标签页切换
    connect(ui->mainTabWidget, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);

    // 实时数据页
    connect(ui->btnWriteValue, &QPushButton::clicked, this, &MainWindow::onWriteValueClicked);
    connect(ui->btnExportRealTime, &QPushButton::clicked, this, [this]() { exportCurrentViewData(ViewType::TABLE_VIEW); });

    // 工艺总貌页
    connect(ui->btnRefreshProcess, &QPushButton::clicked, this, &MainWindow::onRefreshProcessClicked);

    // 图表页
    connect(ui->btnPlayChart, &QPushButton::clicked, this, &MainWindow::onChartPlayPauseClicked);
    connect(ui->comboTimeRange, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onChartTimeRangeChanged);
    connect(ui->btnSaveChart, &QPushButton::clicked, this, &MainWindow::onSaveChartClicked);
    // 网格切换连接,图例显示切换
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

    // 历史数据页
    connect(ui->btnSelectTrendTags, &QPushButton::clicked, this, &MainWindow::onSelectTrendTags);
    connect(ui->btnQueryHistory, &QPushButton::clicked, this, &MainWindow::onQueryHistoryClicked);
    connect(ui->btnExportHistory, &QPushButton::clicked, this, &MainWindow::onExportHistoryClicked);

    // 标签管理器
    connect(ui->lineEditTagSearch, &QLineEdit::textChanged, m_tagManager, &TagManager::filterTags);
    connect(ui->btnClearSearch, &QPushButton::clicked, this, [this]() { ui->lineEditTagSearch->clear(); });

    connect(ui->btnSelectAll, &QPushButton::clicked, m_tagManager, &TagManager::selectAll);
    connect(ui->btnClear, &QPushButton::clicked, m_tagManager, &TagManager::clearSelection);
    connect(ui->btnApply, &QPushButton::clicked, m_tagManager, &TagManager::applySelection);

    connect(&DatabaseManager::instance(), &DatabaseManager::mysqlConnectionChanged,
            this, &MainWindow::onDatabaseStatusChanged);

    // 连接 PLCData 的实时值变化信号到 TagManager
    if (m_plcData && m_tagManager) {
        connect(m_plcData, &PLCData::tagValueChanged, m_tagManager, &TagManager::onTagValueChanged);
    }

    // 连接菜单栏的断开连接动作
    connect(ui->actionDisconnectPLC, &QAction::triggered, this, [this]() {
        if (m_connectionState == ControllerState::CONNECTED ||
            m_connectionState == ControllerState::POLLING) {
            if (UserManager::instance().hasPermission(UserRole::ENGINEER)) {
                emit disconnectRequested();
            } else {
                QMessageBox::warning(this, "权限不足",
                                     "当前用户权限不足，无法断开PLC连接\n需要工程师或管理员权限");
            }
        }
    });

    // 菜单栏
    connect(ui->actionExport, &QAction::triggered, this, &MainWindow::onExportActionTriggered);
    connect(ui->actionExit, &QAction::triggered, this, &MainWindow::onExitActionTriggered);

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
    for (QAction* action : viewActions) {
        connect(action, &QAction::triggered, this, [this, action]() { onViewActionTriggered(action); });
    }

    connect(ui->actionShowTagManager, &QAction::toggled, this, &MainWindow::onShowTagManagerToggled);
    connect(ui->actionShowStatusBar, &QAction::toggled, this, &MainWindow::onShowStatusBarToggled);
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::onAboutActionTriggered);
    connect(ui->actionConnectPLC, &QAction::triggered, this, &MainWindow::onConnectButtonClicked);
    connect(ui->actionDbConfig, &QAction::triggered, this, [this](){
        if (UserManager::instance().hasPermission(UserRole::ENGINEER)) {
            DatabaseConfigDialog dlg(this);
            dlg.exec();
        } else {
            QMessageBox::warning(this, "权限不足", "只有工程师及以上用户才能修改数据库配置");
        }
    });
  //  connect(ui->actionDisconnectPLC, &QAction::triggered, this, &MainWindow::onDisconnectButtonClicked);

    // 定时器
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

    // 1. 连接视图选择变化信号（只连接一次，用于更新实时数据模型列和标签计数）
    connect(m_plcData, &PLCData::viewSelectionChanged, this,
            [this](ViewType type, const QStringList& tags) {
                if (type == ViewType::TABLE_VIEW && m_realTimeModel) {
                    qDebug() << "视图选择变化，更新实时数据模型列:" << tags;
                    m_realTimeModel->setVisibleTags(tags);
                }
                // 更新状态栏标签计数
                updateTagCounts();
            }
            );

    // 2. 连接 PLC 数据更新信号
    connect(m_plcData, &PLCData::allDataUpdated, this,
            [this](const QMap<QString, QVariant>& data) {
                qDebug() << "收到PLC数据更新，标签数量:" << data.size();

                // 1. 实时数据模型相关（仅设置一次，不需要每次重复设置）
                if (m_realTimeModel) {
                    // 注意：addDataRecord 已在 Controller 的订阅路径或 onTableDataUpdated 中完成
                    // 这里不应重复添加，否则会导致数据重复。
                    // 如果您已经通过 Controller 的 tableDataUpdated 信号调用 addDataRecord，
                    // 则此处不应再调用。下面仅保留辅助设置。
                    // m_realTimeModel->setMaxRecords(200) 等最好放在初始化时，避免重复设置。
                    // 为了清晰，将它们移出 lambda（见下方说明）。
                }

                // 2. 更新图表
                if (m_chartManager) {
                    m_chartManager->updateChartData(data);
                }

                // 3. 更新工艺流程
                if (m_processViewManager) {
                    m_processViewManager->updateProcessData(data);
                }

                // 4. 更新历史趋势
                if (m_trendManager) {
                    m_trendManager->updateTrendData(data);
                }

                // 5. 更新状态栏（显示最新更新的标签名）
                if (!data.isEmpty()) {
                    QString firstTag = data.keys().first();
                    ui->statusLabelUpdate->setText(QString("最近更新: %1").arg(firstTag));
                }
            }
            );

    qDebug() << "数据信号连接完成";
}
void MainWindow::setupCharts()
{
    // 初始化图表管理器
    m_chartManager = new ChartManager(this);
    m_chartManager->setCustomPlot(ui->chartWidget);

    // 设置初始时间范围
    m_chartManager->setChartTimeRange(300);

    // 连接信号
    connect(m_chartManager, &ChartManager::chartDataUpdated,
            this, &MainWindow::onChartDataUpdated);
    connect(m_chartManager, &ChartManager::chartTagsChanged,
            this, &MainWindow::onChartTagsChanged);
    connect(m_chartManager, &ChartManager::chartCleared,
            this, &MainWindow::onChartCleared);
    connect(m_chartManager, &ChartManager::chartTimeRangeChanged,
            this, &MainWindow::onChartTimeRangeChanged);
    connect(m_chartManager, &ChartManager::chartPlayStateChanged,
            this, &MainWindow::onChartPlayPauseClicked);
}

// mainwindow.cpp 中实现
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

    qDebug() << "  RealTimeDataModel:" << (m_realTimeModel ? "有效" : "nullptr");
    qDebug() << "  ChartManager:" << (m_chartManager ? "有效" : "nullptr");
    qDebug() << "  TrendManager:" << (m_trendManager ? "有效" : "nullptr");
    qDebug() << "  ProcessViewManager:" << (m_processViewManager ? "有效" : "nullptr");

    // 🆕 绑定数据模型到UI视图
  //  bindModelsToViews();
}

// MainWindow.cpp
void MainWindow::bindModelsToViews()
{
    qDebug() << "=== MainWindow 绑定模型到视图 ===";

    // 检查数据模型是否已设置
    if (!m_plcData) {
        qWarning() << "  PLCData 未初始化，跳过模型绑定";
        return;
    }

    // 检查关键对象
    qDebug() << "m_plcData:" << m_plcData;
    qDebug() << "m_realTimeModel:" << m_realTimeModel;

    if (m_realTimeModel) {
        qDebug() << "实时模型行数:" << m_realTimeModel->rowCount();
        qDebug() << "实时模型列数:" << m_realTimeModel->columnCount();
    }

    // 1. 绑定实时数据表格
    if (m_realTimeModel && ui->realTimeTableView) {
        // 设置模型
        ui->realTimeTableView->setModel(m_realTimeModel);

        // 确保在绑定模型时，模型知道应该显示哪些列
        if (m_plcData) {
            QStringList initialTags = m_plcData->getSelectedTagsForView(ViewType::TABLE_VIEW);
            qDebug() << "初始化实时数据模型列，当前选中标签:" << initialTags;

            m_realTimeModel->setVisibleTags(initialTags); // 这一步会触发模型重置列
        }

        // 设置表格属性
        ui->realTimeTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->realTimeTableView->setAlternatingRowColors(true);
        ui->realTimeTableView->setSortingEnabled(true);
        ui->realTimeTableView->horizontalHeader()->setStretchLastSection(true);

        // 设置列宽
        ui->realTimeTableView->setColumnWidth(0, 150);  // 标签名
        ui->realTimeTableView->setColumnWidth(1, 100);  // 地址
        ui->realTimeTableView->setColumnWidth(2, 80);   // 数据类型
        ui->realTimeTableView->setColumnWidth(3, 100);  // 值
        ui->realTimeTableView->setColumnWidth(4, 120);  // 时间戳

        // 连接选择变化信号
        connect(ui->realTimeTableView->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, &MainWindow::onRealtimeTableSelectionChanged);

        qDebug() << "  实时数据表格绑定完成，模型行数:" << m_realTimeModel->rowCount();
        // 绑定后检查

        qDebug() << "表格视图模型:" << ui->realTimeTableView->model();
    } else {
        qWarning() << "  实时数据表格绑定失败:";
        qWarning() << "    m_realTimeModel:" << (m_realTimeModel ? "有效" : "nullptr");
        qWarning() << "    ui->realTimeTableView:" << (ui->realTimeTableView ? "有效" : "nullptr");
    }

    // 2. 绑定图表管理器
    if (m_chartManager) {
        // 设置 QCustomPlot
        m_chartManager->setCustomPlot(ui->chartWidget);

        // 获取图表标签
        if (m_plcData) {
            QStringList chartTags = m_plcData->getSelectedTagsForView(ViewType::CHART_VIEW);
            m_chartManager->setChartTags(chartTags);
        }

        // 连接控制信号
        if (ui->btnPlayChart) {
            // 修改为切换状态的控制
            connect(ui->btnPlayChart, &QPushButton::clicked, this, [this]() {
                if (!m_chartManager) return;

                if (m_chartManager->isPlaying()) {
                    m_chartManager->playChart(false);
                    ui->btnPlayChart->setText("启动");
                    ui->btnPlayChart->setIcon(QIcon(":/icons/play.png"));
                } else {
                    m_chartManager->playChart(true);
                    ui->btnPlayChart->setText("暂停");
                    ui->btnPlayChart->setIcon(QIcon(":/icons/pause.png"));
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
    } else {
        qWarning() << "  图表管理器未初始化";
    }

    // 3. 绑定历史趋势管理器
    if (m_trendManager) {
        // 设置历史趋势图表
        m_trendManager->setCustomPlot(ui->historyChartWidget);

        // 获取趋势标签
        if (m_plcData) {
            QStringList trendTags = m_plcData->getSelectedTagsForView(ViewType::TREND_VIEW);
            m_trendManager->setTrendTags(trendTags);
        }

        /*/ 连接查询按钮
        if (ui->btnQueryHistory) {
            connect(ui->btnQueryHistory, &QPushButton::clicked, this, [this]() {
                if (m_trendManager) {
                    QDateTime from = ui->dateTimeFrom->dateTime();
                    QDateTime to = ui->dateTimeTo->dateTime();
                    m_trendManager->queryHistoryData(from, to);
                }
            });
        }
*/
        // 连接导出按钮
        if (ui->btnExportHistory) {
            connect(ui->btnExportHistory, &QPushButton::clicked, this, [this]() {
                if (!m_trendManager) return;

                QString filePath = QFileDialog::getSaveFileName(
                    this, "导出历史数据",
                    QString("history_data_%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")),
                    "CSV文件 (*.csv);;Excel文件 (*.xlsx);;所有文件 (*.*)"
                    );

                if (!filePath.isEmpty()) {
                    m_trendManager->exportHistoryData(filePath);
                }
            });
        }

        /*/ 连接缩放按钮
        if (ui->btnZoomIn) {
            connect(ui->btnZoomIn, &QPushButton::clicked, m_trendManager, &TrendManager::zoomIn);
        }

        if (ui->btnZoomOut) {
            connect(ui->btnZoomOut, &QPushButton::clicked, m_trendManager, &TrendManager::zoomOut);
        }
*/
        qDebug() << "  历史趋势管理器绑定完成";
    } else {
        qWarning() << "  趋势管理器未初始化";
    }

    // 4. 绑定工艺流程管理器
    if (m_processViewManager) {
        // 设置工艺流程视图
        m_processViewManager->setGraphicsView(ui->graphicsViewProcessFlow);

        // 获取工艺标签
        if (m_plcData) {
            QStringList processTags = m_plcData->getSelectedTagsForView(ViewType::PROCESS_VIEW);
            m_processViewManager->setProcessTags(processTags);
        }

        // 连接刷新按钮
        if (ui->btnRefreshProcess) {
            connect(ui->btnRefreshProcess, &QPushButton::clicked,
                    m_processViewManager, &ProcessViewManager::refreshViewNow);
        }

        qDebug() << "  工艺流程管理器绑定完成";
    } else {
        qWarning() << "  工艺流程管理器未初始化";
    }

    // 5. 绑定数据更新信号
    connectDataSignals();

    // 6. 初始化标签树

    qDebug() << "=== 模型绑定完成 ===";
}

void MainWindow::setuptrend()
{
    m_trendManager = new TrendManager(this);
    // 注意：不再调用 setCustomPlot，因为绘图直接使用 ui->historyChartWidget

    // 从 PLCData 获取初始趋势标签（从 TREND_VIEW 选择中加载）
    if (m_plcData) {
        QStringList trendTags = m_plcData->getSelectedTagsForView(ViewType::TREND_VIEW);
        m_trendManager->setTrendTags(trendTags);
      //  ui->lblTrendCount->setText(QString("趋势中: %1 个标签").arg(trendTags.size()));
    } else {
        m_trendManager->setTrendTags(QStringList());
        qWarning() << "PLCData 未初始化，历史趋势标签列表为空";
    }

    // 设置历史数据查询的默认时间范围：最近1小时
    ui->dateTimeFrom->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    ui->dateTimeTo->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    QDateTime now = QDateTime::currentDateTime();
    ui->dateTimeTo->setDateTime(now);
    ui->dateTimeFrom->setDateTime(now.addSecs(-3600)); // 1小时前
    // 可选：连接标签变化信号，更新计数
    connect(m_trendManager, &TrendManager::trendTagsChanged, this, [this](const QStringList& tags){
        ui->lblTrendCount->setText(QString("趋势中: %1 个标签").arg(tags.size()));
    });
}

void MainWindow::setupTagManager()
{
    // 设置标签树的复选框
    ui->treeWidgetProcessTags->setColumnWidth(0, 50);
    ui->treeWidgetRealtimeTags->setColumnWidth(0, 50);
    ui->treeWidgetChartTags->setColumnWidth(0, 50);
    ui->treeWidgetHistoryTags->setColumnWidth(0, 50);

    // 新增：btnTagManagerHeader 按钮控制标签管理器显示/隐藏
    if (ui->btnTagManagerHeader) {
        // 设置初始文本
        if (ui->dockTagManager->isVisible()) {
            ui->btnTagManagerHeader->setText("隐藏标签管理器");
        } else {
            ui->btnTagManagerHeader->setText("显示标签管理器");
        }

        // 连接点击信号
        connect(ui->btnTagManagerHeader, &QPushButton::clicked, this, [this]() {
            bool willBeVisible = !ui->dockTagManager->isVisible();

            // 控制标签管理器显示/隐藏
            ui->dockTagManager->setVisible(willBeVisible);

            // 同步菜单项状态
            ui->actionShowTagManager->setChecked(willBeVisible);

            // 更新按钮文本
            if (willBeVisible) {
                ui->btnTagManagerHeader->setText("隐藏标签管理器");
                showStatusMessage("标签管理器已显示", 2000);
            } else {
                ui->btnTagManagerHeader->setText("显示标签管理器");
                showStatusMessage("标签管理器已隐藏", 2000);
            }
        });
    }

    // 展开所有标签
    ui->treeWidgetProcessTags->expandAll();
    ui->treeWidgetRealtimeTags->expandAll();

    // 连接折叠/展开按钮
    connect(ui->btnCollapseAll, &QPushButton::clicked, this, [this]() {
        ui->treeWidgetProcessTags->collapseAll();
        ui->treeWidgetRealtimeTags->collapseAll();
        ui->treeWidgetChartTags->collapseAll();
        ui->treeWidgetHistoryTags->collapseAll();
    });

    connect(ui->btnExpandAll, &QPushButton::clicked, this, [this]() {
        ui->treeWidgetProcessTags->expandAll();
        ui->treeWidgetRealtimeTags->expandAll();
        ui->treeWidgetChartTags->expandAll();
        ui->treeWidgetHistoryTags->expandAll();
    });

    // 刷新标签按钮
    connect(ui->btnRefreshTags, &QPushButton::clicked, this, [this]() {
        m_tagManager->loadTags();
        showStatusMessage("标签已刷新", 2000);
    });

    qDebug() << "标签管理器设置完成";
}


// 数据更新槽函数
void MainWindow::onTableDataUpdated(const QMap<QString, QVariant>& data)
{
    m_lastTableData = data;

    // 关键：将数据添加到实时数据模型
    if (m_realTimeModel) {
        m_realTimeModel->addDataRecord(data);
    }

    if (ui->mainTabWidget->currentIndex() == 1) {
        if (m_plcData) {
            QStringList currentSelectedTags = m_plcData->getSelectedTagsForView(ViewType::TABLE_VIEW);
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

    if (ui->mainTabWidget->currentIndex() == 0) {  // 工艺总貌标签页
        m_processViewManager->updateProcessData(data);
    }
}


void MainWindow::onChartTagsChanged(const QStringList& tags)
{
    // 处理图表标签变化
    qDebug() << "Chart tags changed:" << tags;

    // 更新UI显示，比如更新标签列表显示
//    updateChartTagsDisplay(tags);

    // 保存当前图表标签配置
//    saveChartConfiguration();
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
// PLC连接状态
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

// 标签值变化
void MainWindow::onTagValueChanged(const QString& tagName, const QVariant& value)
{
    // 更新状态栏显示最近更新的标签
    ui->statusLabelUpdate->setText(QString("最近更新: %1").arg(tagName));

    // 如果有告警检查，可以在这里添加
}

// 告警处理
void MainWindow::onAlertTriggered(const AlertInfo& alert)
{
    QString fullMessage = QString("[%1] %2").arg(alertLevelToString(alert.level), alert.message);

    // 在状态栏显示告警
    showStatusMessage(fullMessage, 5000);

    // 如果有告警列表，可以添加到告警中心
    qDebug() << "告警触发:" << fullMessage;

}

// 写入结果反馈
void MainWindow::onWriteResult(const QString& tagName, bool success, const QString& error)
{
    if (success) {
        showStatusMessage(QString("标签 %1 写入成功").arg(tagName), 2000);
    } else {
        QString errorMsg = QString("标签 %1 写入失败: %2").arg(tagName, error);
        showStatusMessage(errorMsg, 5000);
        QMessageBox::warning(this, "写入失败", errorMsg);
    }
}

// UI事件处理函数
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

        // 验证配置
        if (m_currentPLCIP.isEmpty()) {
            QMessageBox::warning(this, "配置错误", "PLC IP地址未配置，请检查配置文件");
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

            // 操作员正常连接
            emit connectRequested(m_currentPLCIP, m_currentPLCRack, m_currentPLCSlot);
            showStatusMessage(QString("正在连接到 %1...").arg(m_currentPLCIP), 2000);
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

// 工程师连接处理函数
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

// 添加连接参数配置对话框函数
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

bool MainWindow::savePLCConfig()
{
    // 权限验证
    if (!UserManager::instance().hasPermission(UserRole::ENGINEER)) {
        return false;
    }

    // 调用 Application 的保存功能
    return Application::instance()->savePLCConfig("configs/plc_config.json");
}

/*
void MainWindow::onDisconnectButtonClicked()
{
    if (m_connectionState == ControllerState::CONNECTED) {
        emit disconnectRequested();
    }
}
*/
void MainWindow::onRefreshProcessClicked()
{
    refreshProcessView();
    showStatusMessage("工艺流程已刷新", 2000);
}

void MainWindow::onWriteValueClicked()
{
    // 获取表格中选中的行
    QModelIndexList selected = ui->realTimeTableView->selectionModel()->selectedRows();

    if (selected.isEmpty()) {
        QMessageBox::warning(this, "警告", "请先选择一个标签");
        return;
    }

    // 这里应该弹出一个对话框让用户输入要写入的值
    // 简化处理：写入固定值
    for (const QModelIndex& index : selected) {
        QString tagName = m_plcData->data(index.siblingAtColumn(0)).toString();

        // 获取标签信息
        TagInfo tag = m_plcData->getTag(tagName);
        if (tag.writable) {
            QVariant newValue;

            // 根据数据类型设置默认值
            switch (tag.dataType) {
            case TagDataType::BOOL:
                newValue = true;
                break;
            case TagDataType::INT:
            case TagDataType::WORD:
                newValue = 100;
                break;
            case TagDataType::REAL:
                newValue = 50.5;
                break;
            default:
                newValue = 0;
            }

            emit writeRequested(tagName, newValue);
        } else {
            QMessageBox::warning(this, "警告", QString("标签 %1 不可写").arg(tagName));
        }
    }
}

void MainWindow::onExportDataClicked()
{
    exportCurrentViewData(ViewType::TABLE_VIEW);
}

void MainWindow::onSearchTextChanged(const QString& text)
{
    // 过滤表格数据
    // 这里需要实现表格的过滤功能
    // 由于表格是绑定到 PLCData 模型的，可能需要通过代理模型实现
    qDebug() << "搜索文本:" << text;
}

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

void MainWindow::onTabChanged(int index)
{
    ViewType view = mapTabIndexToViewType(index);
    if (m_tagManager) {
        m_tagManager->setActiveView(view);
    }
    // 原有的其他代码（如状态栏消息）可以保留
    QString tabName = ui->mainTabWidget->tabText(index);
    showStatusMessage(QString("切换到: %1").arg(tabName), 2000);
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

void MainWindow::onTagSearchTextChanged(const QString& text)
{
    if (m_tagManager) {
        m_tagManager->filterTags(text);
    }
}

// 图表控制
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

// mainwindow.cpp
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

// 历史数据查询
// 选择趋势标签
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
        // 保存到 PLCData 的 TREND_VIEW 中（持久化）
        m_plcData->setViewSelection(ViewType::TREND_VIEW, selected);
        // 计数标签已在趋势标签变化信号中更新，这里无需重复
    }
}

// 查询历史数据
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
    QDateTime utcFrom = from.toUTC();
    QDateTime utcTo = to.toUTC();
    DatabaseManager::instance().queryHistoryAsync(tags, utcFrom, utcTo, true, m_currentHistoryRequestId);
}

// 接收查询结果
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
    if (m_trendManager) {
        m_trendManager->plotHistoryQueryResult(results);
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
    // 收集所有时间戳
    QSet<QDateTime> allTimes;
    for (const auto& res : results) {
        for (const auto& ts : res.timestamps)
            allTimes.insert(ts);
    }
    QList<QDateTime> sortedTimes = allTimes.values();
    std::sort(sortedTimes.begin(), sortedTimes.end());
    // 表头
    out << "时间戳";
    for (const auto& res : results)
        out << "," << res.tagName;
    out << "\n";
    // 数据行
    for (const QDateTime& ts : sortedTimes) {
        out << ts.toString("yyyy-MM-dd HH:mm:ss.zzz");
        for (const auto& res : results) {
            int idx = res.timestamps.indexOf(ts);
            if (idx != -1)
                out << "," << res.values[idx];
            else
                out << ",";
        }
        out << "\n";
    }
    file.close();
    QMessageBox::information(this, "成功", "历史数据已导出");
}

// 菜单动作
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

void MainWindow::onShowTagManagerToggled(bool checked)
{
    // 控制标签管理器显示/隐藏
    ui->dockTagManager->setVisible(checked);

    // 同步 btnTagManagerHeader 按钮状态
    if (ui->btnTagManagerHeader) {
        if (checked) {
            ui->btnTagManagerHeader->setText("隐藏标签管理器");
        } else {
            ui->btnTagManagerHeader->setText("显示标签管理器");
        }
    }

    showStatusMessage(checked ? "标签管理器已显示" : "标签管理器已隐藏", 2000);
}

void MainWindow::onShowStatusBarToggled(bool checked)
{
    ui->statusbar->setVisible(checked);
}

void MainWindow::onAboutActionTriggered()
{
    QMessageBox::about(this, "关于 PLC数据监控系统",
                       "<h3>PLC数据监控系统 v1.0.0</h3>"
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

// 定时器槽
void MainWindow::onUpdateTimerTimeout()
{
    // 定期更新UI状态
    updateStatusBar();
}

void MainWindow::onSystemTimerTimeout()
{
    updateSystemTime();
}

// 辅助函数
void MainWindow::updateConnectionStatus(ControllerState state, const QString& message)
{
    m_connectionState = state;

    if (!message.isEmpty()) {
        ui->statusLabelSystem->setText(message);
    }
}

void MainWindow::updateSystemTime()
{
    QDateTime current = QDateTime::currentDateTime();
    ui->statusLabelTime->setText(QString("系统时间: %1").arg(current.toString("yyyy-MM-dd HH:mm:ss")));
}

void MainWindow::updateStatusBar()
{
    // 通讯状态检测
    QString commStatus = "通讯: ";
    if (m_plcConnector) {
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

    // 从 PLCData 获取各视图的选中标签数量
    int processCount = m_plcData->getSelectedTagsForView(ViewType::PROCESS_VIEW).size();
    int realtimeCount = m_plcData->getSelectedTagsForView(ViewType::TABLE_VIEW).size();
    int chartCount = m_plcData->getSelectedTagsForView(ViewType::CHART_VIEW).size();
    int trendCount = m_plcData->getSelectedTagsForView(ViewType::TREND_VIEW).size();

    // 更新各个视图的标签计数
    ui->lblProcessCount->setText(QString("工艺点: %1 个").arg(processCount));
    ui->lblRealTimeCount->setText(QString("已选择: %1 个标签").arg(realtimeCount));
    ui->lblChartCount->setText(QString("图表中: %1 个标签").arg(chartCount));
 //   ui->lblTrendCount->setText(QString("趋势中: %1 个标签").arg(trendCount));
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


// mainwindow.cpp
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
        }
    });

    connect(m_actionLogout, &QAction::triggered, this, [this](){
        UserManager::instance().logout();
        updateUIPermissions();
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

    // 标签管理器按钮（工程师及以上可管理标签）
    ui->btnTagManagerHeader->setEnabled(canManageTags);
    // 标签管理器中的全选/清空/应用按钮（管理员和工程师可用）
    ui->btnSelectAll->setEnabled(canManageTags);
    ui->btnClear->setEnabled(canManageTags);
    ui->btnApply->setEnabled(canManageTags);
    // 刷新标签按钮（所有登录用户？建议工程师及以上）
    ui->btnRefreshTags->setEnabled(canManageTags);

    // 写入值按钮（操作员及以上可写）
    ui->btnWriteValue->setEnabled(canWriteData);

    // 系统配置相关（管理员和工程师）
    // 例如：设置菜单、连接配置等
    ui->actionConnectPLC->setEnabled(canConfigureSystem);
    ui->actionDisconnectPLC->setEnabled(canConfigureSystem);
    ui->actionDbConfig->setEnabled(canConfigureSystem);

    // 观察者只能查看，不能做任何修改，但可以导出数据（可选）
    // 导出按钮通常允许所有人，但如果需要限制，可以：
    // ui->btnExportRealTime->setEnabled(loggedIn);
    // ui->btnExportHistory->setEnabled(loggedIn);
}

void MainWindow::onUserLoggedIn(const QString& username)
{
    statusBar()->showMessage(tr("欢迎 %1 登录").arg(username), 3000);
    updateUIPermissions();
}
void MainWindow::onUserLoggedOut()
{
    statusBar()->showMessage(tr("已注销"), 3000);
    updateUIPermissions();
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

// 添加数据库状态更新函数
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

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        // 使用单次定时器，让窗口先完成第一次绘制
        QTimer::singleShot(0, this, [this]() {
            qDebug() << "开始逐步初始化 UI...";

            // 1. 初始化主题（如果必要，可先跳过）
            // setupThemes();

            // 2. 初始化 UI 布局（不涉及图表等重量级控件）
            setupUI();  // 但 setupUI 中可能包含 applyAppleTheme 等，确保这些已注释

            // 3. 初始化标签管理器（仅加载标签配置，不进行视图绑定）
            m_tagManager = new TagManager(m_plcData, this);
            m_tagManager->setTrees(ui->treeWidgetProcessTags,
                                   ui->treeWidgetRealtimeTags,
                                   ui->treeWidgetChartTags,
                                   ui->treeWidgetHistoryTags);
            m_tagManager->loadTags();  // 仅读取 JSON，不阻塞

            // 4. 延迟加载标签选择和视图设置
            QTimer::singleShot(0, this, [this]() {
                m_tagManager->loadSelections();
                int currentTab = ui->mainTabWidget->currentIndex();
                ViewType initialView = mapTabIndexToViewType(currentTab);
                m_tagManager->setTabWidget(ui->tabWidgetTagViews);
                m_tagManager->setActiveView(initialView);
            });

            // 5. 初始化图表（最耗时，最后执行）
            QTimer::singleShot(10, this, [this]() {
                setupCharts();
                setuptrend();
                bindModelsToViews();  // 绑定表格、图表视图
                connectDataSignals();
                setupConnections();   // 信号连接
                qDebug() << "所有 UI 初始化完成";
            });
        });
    }
}