// src/app/mainwindow.cpp
#include "mainwindow.h"
#include "plcconnector.h"
#include "ui_mainwindow.h"

#include <QStyleFactory>
#include <QDebug>
#include <QFileDialog>
#include <QDir>
#include <QMenu>
#include <QMenuBar>
#include <QAction>
#include <QElapsedTimer>
#include <QSettings>
#include <QCloseEvent>
#include <QMetaType>
#include <QInputDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QHostAddress>
#include <QToolButton>

typedef QMap<QString, QVariant> DataMap;

MainWindow::MainWindow(PLCData *dataModel,
                       PLCConnector *plcConnector,
                       QThread *plcThread,
                       QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_currentState(ControllerState::DISCONNECTED)
    , m_isDarkTheme(false)
    , m_tagModel(dataModel)
    , m_plcConnector(plcConnector)
    , m_plcThread(plcThread)
    , m_controller(nullptr)
   // , m_tagModel(dataModel)
    , m_chart(nullptr)
    , m_chartView(nullptr)
    , m_realTimeDataModel(nullptr)
    , m_writeProgressDialog(nullptr)
    , m_statusLabel(nullptr)
    , m_ipLabel(nullptr)
    , m_pollingLabel(nullptr)
    , m_tagCountLabel(nullptr)
    , m_memoryLabel(nullptr)
    , m_timeLabel(nullptr)
    , m_connectTimer(nullptr)

{
    ui->setupUi(this);

    // 设置窗口标题
    setWindowTitle("PLC监控系统 v2.0");
    loadSettings();

    // 初始化视图
    setupViews();

    // 加载视图选择
    loadViewSelections();

    setupStatusBar();
    setupTableView();

    applyDarkTheme();
    setupThemeMenu();

    // 3. 设置信号连接
    setupConnections();

    // 4. 加载配置
    // 1. 正确获取 Application 实例
    Application* app = qobject_cast<Application*>(qApp);
    if (!app) {
        qCritical() << "无法获取 Application 实例！";
        return;
    }

    m_app = app;  // 将 m_app 改为正确的类型
    m_plcConfig = m_app->getPLCConfig();  // 现在应该可以正常调用了

    // 修改初始化代码
    bool configLoaded = m_app->loadPLCConfig("configs/plc_config.json");
    if (!configLoaded) {
        QMessageBox::warning(this, "配置加载失败",
                             "无法加载PLC配置文件，将使用默认配置。");
    }


    // 初始化完成后自动连接
    QTimer::singleShot(1000, this, [this]() {
        if (m_plcConfig.autoConnect && m_currentState != ControllerState::CONNECTED) {
            qDebug() << "自动连接PLC...";
            autoConnectPLC();
        }
    });


    setupTableView();
    setupChartView();

    // 5. 恢复窗口状态
    restoreWindowState();

    // 6. 初始化状态
    updateUIState(m_currentState);
}

MainWindow::~MainWindow()
{
    // 1. 清理 Controller
    cleanupResources();

    // 2. 清理 UI
    if (ui) {
        delete ui;
        ui = nullptr;
    }

    // 3. 清理模型
    if (m_tagModel) {
        delete m_tagModel;
        m_tagModel = nullptr;
    }

    if (m_realTimeDataModel) {
        delete m_realTimeDataModel;
        m_realTimeDataModel = nullptr;
    }
}

// mainwindow.cpp
/*/ mainwindow.cpp
void MainWindow::setupController()
{
    qDebug() << "=== 设置 Controller =======================";

    // 确保 m_tagModel 存在
    if (!m_tagModel) {
        qCritical() << "错误: PLCData 未初始化";
        m_tagModel = new PLCData(this);
    }

    // 确保 m_plcConnector 存在
    if (!m_plcConnector) {
        qCritical() << "错误: PLCConnector 未初始化";
        // 修复1: 传递正确的构造函数参数
        m_plcConnector = new PLCConnector(m_tagModel, this);
    }

    // 使用正确的构造函数创建 Controller
    m_controller = new Controller(m_tagModel, m_plcConnector, this);

    if (!m_controller) {
        qCritical() << "Controller 创建失败";
        return;
    }

    // 加载标签配置
    if (m_tagModel) {
        bool loaded = m_tagModel->loadFromConfig("tags.json");

        if (loaded) {
            int tagCount = m_tagModel->rowCount();
            qDebug() << "成功从 tags.json 加载了" << tagCount << "个标签";

            // 获取标签名称列表
            QStringList tagNames = m_tagModel->getAllTagNames();
            qDebug() << "标签列表:" << tagNames;

            // 设置标签到 PLCConnector
            if (m_plcConnector) {
                // 修复2: 直接调用 setTags 方法
                // 查看【文档内容】，PLCConnector 确实有 setTags 方法
                QVector<TagInfo> tags;
                tags.reserve(tagCount);
                for (int i = 0; i < tagCount; ++i) {
                    tags.append(m_tagModel->getTag(i));
                }
                 // 直接调用 setTags 方法
                m_plcConnector->setTags(tags);
                qDebug() << "已设置" << tags.size() << "个标签到 PLCConnector";
            }
        } else {
            qWarning() << "未能从 tags.json 加载标签，将使用默认标签或等待手动添加";
        }
    }

    // 初始化 Controller
    if (m_controller && m_controller->initialize()) {
        qDebug() << "Controller 初始化成功";
    } else {
        qWarning() << "Controller 初始化失败";
    }

    qDebug() << "==========================================";
}

void MainWindow::setDataModel(PLCData* dataModel)
{
    m_dataModel = dataModel;

    if (m_dataModel) {
        // 设置数据表模型
        ui->dataTableView->setModel(m_dataModel);

        // 连接数据变化信号
        connect(m_dataModel, &PLCData::tagValueChanged,
                this, &MainWindow::onTagValueChanged);

        // 更新标签计数
        m_tagCountLabel->setText(QString("标签: %1").arg(m_dataModel->rowCount()));
    }
}
*/

// 样式表加载函数
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

void MainWindow::applyDarkTheme()
{
    qDebug() << "应用深色主题";

    // 设置应用程序样式
    qApp->setStyle(QStyleFactory::create("Fusion"));

    // 设置暗色调色板
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(30, 30, 30));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
    darkPalette.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(45, 45, 48));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);

    qApp->setPalette(darkPalette);

    // 加载深色主题样式表
    bool loaded = loadStyleSheet(":/styles/dark.css");
    if (!loaded) {
        qWarning() << "无法加载深色主题样式表，使用默认样式";
    }

    m_isDarkTheme = true;

    // 保存主题设置
    QSettings settings("PLCMonitor", "PLCMonitor");
    settings.setValue("darkTheme", true);
}

void MainWindow::applyLightTheme()
{
    qDebug() << "应用淡色主题";

    // 设置应用程序样式
    qApp->setStyle(QStyleFactory::create("Fusion"));

    // 设置亮色调色板
    QPalette lightPalette;
    lightPalette.setColor(QPalette::Window, QColor(245, 245, 245));
    lightPalette.setColor(QPalette::WindowText, Qt::black);
    lightPalette.setColor(QPalette::Base, QColor(255, 255, 255));
    lightPalette.setColor(QPalette::AlternateBase, QColor(248, 248, 248));
    lightPalette.setColor(QPalette::ToolTipBase, Qt::white);
    lightPalette.setColor(QPalette::ToolTipText, Qt::black);
    lightPalette.setColor(QPalette::Text, Qt::black);
    lightPalette.setColor(QPalette::Button, QColor(240, 240, 240));
    lightPalette.setColor(QPalette::ButtonText, Qt::black);
    lightPalette.setColor(QPalette::BrightText, Qt::red);
    lightPalette.setColor(QPalette::Link, QColor(33, 150, 243));
    lightPalette.setColor(QPalette::Highlight, QColor(33, 150, 243));
    lightPalette.setColor(QPalette::HighlightedText, Qt::white);

    qApp->setPalette(lightPalette);

    // 加载淡色主题样式表
    bool loaded = loadStyleSheet(":/styles/light.css");
    if (!loaded) {
        qWarning() << "无法加载淡色主题样式表，使用默认样式";
    }

    m_isDarkTheme = false;

    // 保存主题设置
    QSettings settings("PLCMonitor", "PLCMonitor");
    settings.setValue("darkTheme", false);
}

void MainWindow::onThemeToggled(bool dark)
{
    if (dark) {
        applyDarkTheme();
    } else {
        applyLightTheme();
    }

    // 更新菜单状态
    if (m_darkThemeAction) {
        m_darkThemeAction->setChecked(dark);
    }
    if (m_lightThemeAction) {
        m_lightThemeAction->setChecked(!dark);
    }

    // 保存主题设置
    QSettings settings("PLCMonitor", "PLCMonitor");
    settings.setValue("darkTheme", dark);

    // 显示状态消息
    QString themeName = dark ? "深色主题" : "淡色主题";
    this->statusBar()->showMessage(tr("已切换到%1").arg(themeName), 3000);
}

void MainWindow::setupThemeMenu()
{
    // 创建主题菜单
    QMenu* themeMenu = new QMenu(tr("主题"), this);
    menuBar()->addMenu(themeMenu);

    // 深色主题动作
    m_darkThemeAction = new QAction(tr("深色主题"), this);
    m_darkThemeAction->setCheckable(true);
    m_darkThemeAction->setChecked(m_isDarkTheme);
    connect(m_darkThemeAction, &QAction::triggered, this, [this]() {
        applyDarkTheme();
        if (m_lightThemeAction) {
            m_lightThemeAction->setChecked(false);
        }
    });
    themeMenu->addAction(m_darkThemeAction);

    // 淡色主题动作
    m_lightThemeAction = new QAction(tr("淡色主题"), this);
    m_lightThemeAction->setCheckable(true);
    m_lightThemeAction->setChecked(!m_isDarkTheme);
    connect(m_lightThemeAction, &QAction::triggered, this, [this]() {
        applyLightTheme();
        if (m_darkThemeAction) {
            m_darkThemeAction->setChecked(false);
        }
    });
    themeMenu->addAction(m_lightThemeAction);

    // 添加分隔线
    themeMenu->addSeparator();

    // 主题切换动作（快捷键）
    QAction* toggleThemeAction = new QAction(tr("切换主题 (Ctrl+T)"), this);
    toggleThemeAction->setShortcut(QKeySequence("Ctrl+T"));
    connect(toggleThemeAction, &QAction::triggered, this, [this]() {
        if (m_isDarkTheme) {
            applyLightTheme();
        } else {
            applyDarkTheme();
        }
    });
    themeMenu->addAction(toggleThemeAction);

    // 添加图标
 //   m_darkThemeAction->setIcon(QIcon(":/icons/night_mode.svg"));
 //   m_lightThemeAction->setIcon(QIcon(":/icons/day_mode.svg"));
 //   toggleThemeAction->setIcon(QIcon(":/icons/theme.svg"));
}

void MainWindow::setupTableView()
{
    if (m_tagModel) {
        ui->realTimeTableView->setModel(m_tagModel);

        // 设置列宽
    //    ui->dataTableView->setColumnWidth(PLCData::NameColumn, 200);
    //    ui->dataTableView->setColumnWidth(PLCData::AddressColumn, 150);
    //    ui->dataTableView->setColumnWidth(PLCData::ValueColumn, 150);
    //    ui->dataTableView->setColumnWidth(PLCData::DataTypeColumn, 100);
    //   ui->dataTableView->setColumnWidth(PLCData::TimestampColumn, 200);

        // 启用排序
        ui->realTimeTableView->setSortingEnabled(true);

        // 启用交替行颜色
        ui->realTimeTableView->setAlternatingRowColors(true);

        // 设置选择行为
        ui->realTimeTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->realTimeTableView->setSelectionMode(QAbstractItemView::SingleSelection);
    }
}

void MainWindow::setupChartView()
{
    // 创建图表
    m_chart = new QChart();
    m_chart->setTitle("PLC 数据趋势图");
    m_chart->setTheme(QChart::ChartThemeDark);
    m_chart->setAnimationOptions(QChart::NoAnimation);

    // 创建坐标轴
    QDateTimeAxis *axisX = new QDateTimeAxis;
    axisX->setFormat("HH:mm:ss");
    axisX->setTitleText("时间");
    m_chart->addAxis(axisX, Qt::AlignBottom);

    QValueAxis *axisY = new QValueAxis;
    axisY->setTitleText("值");
    m_chart->addAxis(axisY, Qt::AlignLeft);

    // 创建图表视图
    m_chartView = new QChartView(m_chart);
    m_chartView->setRenderHint(QPainter::Antialiasing);

    // 添加到UI
    QVBoxLayout *layout = new QVBoxLayout(ui->chartView);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_chartView);
    ui->chartView->setLayout(layout);
}

void MainWindow::setupStatusBar()
{
    // 状态栏组件
    ui->statusbar->setStyleSheet("QStatusBar { background-color: #252526; color: #cccccc; }");

    // PLC连接状态和IP标签
    m_statusLabel = new QLabel("未连接", this);
    m_statusLabel->setStyleSheet("QLabel { padding: 2px 8px; }");
    ui->statusbar->addPermanentWidget(m_statusLabel);

    // IP地址标签
    m_ipLabel = new QLabel(QString("IP: %1").arg(m_plcConfig.ip), this);
    m_ipLabel->setStyleSheet("QLabel { padding: 2px 8px; color: #888; }");
    ui->statusbar->addPermanentWidget(m_ipLabel);

    // 轮询状态标签
    m_pollingLabel = new QLabel("轮询停止", this);
    m_pollingLabel->setStyleSheet("QLabel { padding: 2px 8px; }");
    ui->statusbar->addPermanentWidget(m_pollingLabel);

    // 标签数量标签
    m_tagCountLabel = new QLabel("标签: 0", this);
    m_tagCountLabel->setStyleSheet("QLabel { padding: 2px 8px; }");
    ui->statusbar->addPermanentWidget(m_tagCountLabel);

    // 内存使用标签
    m_memoryLabel = new QLabel("内存: 0 MB", this);
    m_memoryLabel->setStyleSheet("QLabel { padding: 2px 8px; }");
    ui->statusbar->addPermanentWidget(m_memoryLabel);
}


void MainWindow::setupConnections()
{
    qDebug() << "=== 设置信号连接 =========================";

    // 检查必要的对象是否已初始化
    if (!m_tagModel) {
        qCritical() << "错误：m_dataModel 为 nullptr";
        return;
    }

    if (!m_plcConnector) {
        qCritical() << "错误：m_plcConnector 为 nullptr";
        return;
    }

    if (!m_plcThread) {
        qCritical() << "错误：m_plcThread 为 nullptr";
        return;
    }
    // ===================== UI 信号连接 =====================

    // 8. 导航列表
    connect(ui->navigationList, &QListWidget::itemClicked,
            this, &MainWindow::onNavigationItemClicked);

    // 9. 连接按钮
    connect(ui->btnConnect, &QPushButton::clicked,
            this, [this](bool checked) {
                qDebug() << "连接按钮点击:" << (checked ? "连接" : "断开");
                ControllerState currentState = m_plcConnector->getControllerState();

                if (checked) {
                    // 发射连接请求信号
                    emit connectRequested(m_plcConfig.ip, m_plcConfig.rack, m_plcConfig.slot);
                 } else {
                    // 发射断开连接信号
                    emit disconnectRequested();
                }
            });

    // 10. 轮询按钮
    connect(ui->btnStartPoll, &QPushButton::clicked,
            this, [this](bool checked) {
                qDebug() << "轮询按钮点击:" << (checked ? "开始轮询" : "停止轮询");
                ControllerState currentState = m_plcConnector->getControllerState();

                if (checked) {
                    // 发射开始轮询信号
                    emit startPollingRequested();
                } else {
                    // 发射停止轮询信号
                    emit stopPollingRequested();
                }
            });

    // 11. 写入按钮
    connect(ui->btnWriteValue, &QPushButton::clicked,
            this, &MainWindow::onWriteButtonClicked);

    // 12. 导出按钮
    connect(ui->btnExportRealTime, &QPushButton::clicked,
            this, &MainWindow::onExportButtonClicked);

    // 13. 添加标签按钮
    connect(ui->btnAddTag, &QPushButton::clicked,
            this, &MainWindow::onAddTagButtonClicked);

    // 14. 设置按钮
    connect(ui->btnSettings, &QPushButton::clicked,
            this, &MainWindow::onSettingsButtonClicked);

    // 15. 添加图表按钮
    connect(ui->btnAddChart, &QPushButton::clicked,
            this, &MainWindow::onAddChartButtonClicked);

    // 16. 搜索框
 //   connect(ui->searchLineEdit, &QLineEdit::textChanged,
 //           this, &MainWindow::onSearchTextChanged);

    // 17. 时间范围选择框
 //   connect(ui->timeRangeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
 //           this, &MainWindow::onTimeRangeChanged);

    // 18. 表格选择变化
    if (ui->dataTableView->selectionModel()) {
        connect(ui->dataTableView->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, [this](const QItemSelection &selected, const QItemSelection &deselected) {
                    Q_UNUSED(deselected)
                    if (!selected.isEmpty()) {
                        QModelIndex index = selected.indexes().first();
                        if (index.isValid()) {
                            QString tagName = m_tagModel->data(index.siblingAtColumn(PLCData::NameColumn)).toString();
                            bool isSelected = ui->dataTableView->selectionModel()->isSelected(index);
                            emit m_tagModel->tagSelectionChanged(tagName, isSelected);
                        }
                    }
                });
    }

    qDebug() << "UI 信号连接完成";
    qDebug() << "所有信号连接设置完成";
    qDebug() << "==========================================";
}

// 更新状态同步方法
void MainWindow::updateUIState(ControllerState state)
{
    m_currentState = state;
    QString stateText = controllerStateToString(state);
    qDebug() << "更新UI状态为:" << stateText;

    // 根据状态统一更新所有UI元素
    switch (state) {
    case ControllerState::DISCONNECTED:
        ui->btnConnect->setEnabled(true);
        ui->btnConnect->setText("连接PLC");
        ui->btnConnect->setChecked(false);

        ui->btnStartPoll->setEnabled(false);
        ui->btnStartPoll->setText("开始轮询");
        ui->btnStartPoll->setChecked(false);

        ui->btnWriteValue->setEnabled(false);

        m_statusLabel->setText("未连接");
        m_statusLabel->setStyleSheet("QLabel { background-color: #f44336; color: white; }");
        break;

    case ControllerState::CONNECTING:
        ui->btnConnect->setEnabled(false);
        ui->btnConnect->setText("连接中...");
        ui->btnStartPoll->setEnabled(false);
        ui->btnWriteValue->setEnabled(false);

        m_statusLabel->setText("连接中...");
        m_statusLabel->setStyleSheet("QLabel { background-color: #ff9800; color: white; }");
        break;

    case ControllerState::CONNECTED:
        ui->btnConnect->setEnabled(true);
        ui->btnConnect->setText("断开连接");
        ui->btnConnect->setChecked(true);

        ui->btnStartPoll->setEnabled(true);
        ui->btnStartPoll->setText("开始轮询");
        ui->btnStartPoll->setChecked(false);

        ui->btnWriteValue->setEnabled(true);

        m_statusLabel->setText("已连接");
        m_statusLabel->setStyleSheet("QLabel { background-color: #4caf50; color: white; }");

        // 连接成功后自动启动轮询
        if (m_plcConfig.autoStartPolling) {
            QTimer::singleShot(1000, this, [this]() {
                if (m_plcConnector && m_plcConnector->getControllerState() == ControllerState::CONNECTED) {
                    m_plcConnector->startPolling();
                }
            });
        }
        break;

    case ControllerState::POLLING:
        ui->btnStartPoll->setEnabled(true);
        ui->btnStartPoll->setText("停止轮询");
        ui->btnStartPoll->setChecked(true);

        m_statusLabel->setText("轮询中");
        m_statusLabel->setStyleSheet("QLabel { background-color: #2196f3; color: white; }");
        break;

    case ControllerState::PAUSED:
        ui->btnStartPoll->setEnabled(true);
        ui->btnStartPoll->setText("开始轮询");
        ui->btnStartPoll->setChecked(false);

        m_statusLabel->setText("已连接(暂停)");
        m_statusLabel->setStyleSheet("QLabel { background-color: #4caf50; color: white; }");
        break;

    case ControllerState::ERROR:
        ui->btnConnect->setEnabled(true);
        ui->btnConnect->setText("重新连接");
        ui->btnConnect->setChecked(false);

        ui->btnStartPoll->setEnabled(false);
        ui->btnWriteValue->setEnabled(false);

        m_statusLabel->setText("错误");
        m_statusLabel->setStyleSheet("QLabel { background-color: #ff9800; color: white; }");
        break;

    case ControllerState::RECONNECTING:
        ui->btnConnect->setEnabled(false);
        ui->btnConnect->setText("重连中...");
        ui->btnStartPoll->setEnabled(false);
        ui->btnWriteValue->setEnabled(false);

        m_statusLabel->setText("重连中...");
        m_statusLabel->setStyleSheet("QLabel { background-color: #ff9800; color: white; }");
        break;

    case ControllerState::SHUTTING_DOWN:
        ui->btnConnect->setEnabled(false);
        ui->btnConnect->setText("关闭中...");
        ui->btnStartPoll->setEnabled(false);
        ui->btnWriteValue->setEnabled(false);

        m_statusLabel->setText("关闭中...");
        m_statusLabel->setStyleSheet("QLabel { background-color: #757575; color: white; }");
        break;
    }

    updatePollingStatus();
    updateStatusBarInfo();

}



void MainWindow::autoConnectPLC()
{
    qDebug() << "=== 自动连接PLC =========================";
    qDebug() << "IP:" << m_plcConfig.ip;
    qDebug() << "Rack:" << m_plcConfig.rack;
    qDebug() << "Slot:" << m_plcConfig.slot;

    ControllerState currentState = m_plcConnector->getControllerState();
    qDebug() << "当前状态:" << controllerStateToString(currentState);

    if (m_plcConfig.autoConnect &&
        (currentState == ControllerState::DISCONNECTED ||
         currentState == ControllerState::ERROR)) {

        qDebug() << "满足自动连接条件";
        ui->btnConnect->setChecked(true);

        emit connectRequested(m_plcConfig.ip, m_plcConfig.rack, m_plcConfig.slot);
    } else {
        qDebug() << "不满足自动连接条件:";
        qDebug() << "  自动连接配置:" << (m_plcConfig.autoConnect ? "启用" : "禁用");
        qDebug() << "  当前状态:" << controllerStateToString(currentState);
    }

    qDebug() << "==========================================";
}

void MainWindow::autoStartPolling()
{
    qDebug() << "=== 自动轮询启动检查 =======================";

    if (!m_plcConnector) {
        qWarning() << "PLC连接器未初始化";
        return;
    }

    ControllerState currentState = m_plcConnector->getControllerState();
    qDebug() << "当前Controller状态:" << controllerStateToString(currentState);
    qDebug() << "自动轮询配置:" << (m_plcConfig.autoStartPolling ? "启用" : "禁用");

    // 检查是否已经在轮询
    if (currentState == ControllerState::POLLING) {
        qDebug() << "已经在轮询状态，跳过自动启动";
        return;
    }

    if (m_plcConfig.autoStartPolling &&
        currentState == ControllerState::CONNECTED) {

        qDebug() << "满足自动轮询条件，开始启动轮询...";
        qDebug() << "轮询间隔:" << m_plcConfig.pollingInterval << "ms";

        // 延迟启动轮询，确保连接完全建立
        QTimer::singleShot(1000, this, [this]() {
            ControllerState newState = m_plcConnector->getControllerState();
            if (newState == ControllerState::CONNECTED) {
                emit startPollingRequested();
            } else {
                qWarning() << "状态已改变，无法启动自动轮询，当前状态:"
                           << controllerStateToString(newState);
            }
        });
    } else {
        qDebug() << "不满足自动轮询条件";
    }

    qDebug() << "==========================================";
}

// 槽函数实现
void MainWindow::onNavigationItemClicked(QListWidgetItem *item)
{
    int index = ui->navigationList->row(item);
    ui->mainTabWidget->setCurrentIndex(index);

    // 更新按钮状态
    switch (index) {
    case 0: // 实时监控
        ui->btnWriteValue->setEnabled(true);
        ui->btnExport->setEnabled(true);
        break;
    case 1: // 趋势图表
        ui->btnWriteValue->setEnabled(false);
        ui->btnExport->setEnabled(true);
        break;
    default:
        ui->btnWriteValue->setEnabled(false);
        ui->btnExport->setEnabled(false);
    }
}

void MainWindow::onConnectButtonClicked(bool checked)
{
    ControllerState currentState = m_plcConnector->getControllerState();

    if (checked) {
        // 连接请求
        if (currentState == ControllerState::DISCONNECTED ||
            currentState == ControllerState::ERROR) {
            m_plcConnector->connectToPLC(m_plcConfig.ip, m_plcConfig.rack, m_plcConfig.slot);
        } else {
            qWarning() << "无效的连接请求，当前状态为:" << controllerStateToString(currentState);
            // UI状态会通过onControllerStateChanged自动更新
        }
    } else {
        // 断开连接请求
        if (currentState == ControllerState::CONNECTED ||
            currentState == ControllerState::POLLING ||
            currentState == ControllerState::PAUSED) {
            m_plcConnector->disconnectFromPLC();
        } else {
            qWarning() << "无效的断开请求，当前状态为:" << controllerStateToString(currentState);
        }
    }
}

void MainWindow::onStartPollButtonClicked(bool checked)
{
    qDebug() << "=== 轮询按钮点击 =========================";
    qDebug() << "按钮状态:" << (checked ? "按下(启动轮询)" : "弹起(停止轮询)");

    ControllerState currentState = m_plcConnector->getControllerState();
    qDebug() << "当前Controller状态:" << controllerStateToString(currentState);

    if (!m_plcConnector) {
        qWarning() << "PLC连接器为空";
        return;
    }

    if (checked) {
        // 启动轮询
        if (currentState == ControllerState::CONNECTED) {
            qDebug() << "当前状态为CONNECTED，可以启动轮询";
            m_plcConnector->startPolling();
        } else {
            qWarning() << "无法启动轮询，当前状态为:" << controllerStateToString(currentState);
            ui->btnStartPoll->setChecked(false);
        }
    } else {
        // 停止轮询
        if (currentState == ControllerState::POLLING) {
            qDebug() << "当前状态为POLLING，可以停止轮询";
            m_plcConnector->stopPolling();
        } else {
            qWarning() << "无法停止轮询，当前状态为:" << controllerStateToString(currentState);
        }
    }

    qDebug() << "==========================================";
}


void MainWindow::onWriteButtonClicked()
{
    ControllerState currentState = m_currentState;  // 使用成员变量，避免重复获取

    if (currentState != ControllerState::CONNECTED &&
        currentState != ControllerState::POLLING) {
        QMessageBox::warning(this, "错误",
                             "PLC未连接或未就绪\n当前状态: " + controllerStateToString(currentState));
        return;
    }

    // 获取选中的行
    QModelIndexList selected = ui->dataTableView->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        QMessageBox::information(this, "提示", "请选择一个标签");
        return;
    }

    int row = selected.first().row();
    if (row < 0 || row >= m_tagModel->rowCount()) {
        QMessageBox::warning(this, "错误", "无效的行选择");
        return;
    }

    QString tagName = m_tagModel->data(m_tagModel->index(row, PLCData::NameColumn)).toString();
    QVariant currentValue = m_tagModel->data(m_tagModel->index(row, PLCData::ValueColumn));

    bool ok = false;
    QVariant newValue;

    // 根据数据类型获取输入
    switch (currentValue.userType()) {
    case QMetaType::Bool: {
        bool boolValue = QInputDialog::getInt(this, "写入BOOL值",
                                              QString("为标签 %1 选择新值 (0=false, 1=true):").arg(tagName),
                                              currentValue.toBool() ? 1 : 0, 0, 1, 1, &ok);
        if (ok) newValue = (boolValue == 1);
        break;
    }
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong: {
        int intValue = QInputDialog::getInt(this, "写入INT值",
                                            QString("为标签 %1 输入新INT值:").arg(tagName),
                                            currentValue.toInt(), -32768, 32767, 1, &ok);
        if (ok) newValue = intValue;
        break;
    }
    case QMetaType::Float:
    case QMetaType::Double: {
        double realValue = QInputDialog::getDouble(this, "写入REAL值",
                                                   QString("为标签 %1 输入新REAL值:").arg(tagName),
                                                   currentValue.toDouble(), -1e9, 1e9, 2, &ok);
        if (ok) newValue = realValue;
        break;
    }
    default: {
        QString strValue = QInputDialog::getText(this, "写入值",
                                                 QString("为标签 %1 输入新值:").arg(tagName),
                                                 QLineEdit::Normal, currentValue.toString(), &ok);
        if (ok) newValue = strValue;
        break;
    }
    }

    if (ok) {
        // 通过发射信号写入（而不是直接调用controller）
        ui->statusbar->showMessage(tr("正在写入到PLC..."));
        ui->btnWriteValue->setEnabled(false);
        ui->btnWriteValue->setText(tr("写入中..."));

        emit writeRequested(tagName, newValue);

        // 5秒后重置按钮状态（防止卡死）
        QTimer::singleShot(5000, this, [this]() {
            ui->btnWriteValue->setEnabled(true);
            ui->btnWriteValue->setText(tr("Write to PLC"));
        });
    }
}

void MainWindow::onSearchTextChanged(const QString &text)
{
    // 搜索过滤逻辑
    if (m_tagModel) {
        // 这里可以添加代理模型进行过滤
    }
}

void MainWindow::showWriteValueDialog(const TagInfo &tag, const QVariant &currentValue)
{
    bool ok = false;
    QVariant newValue;

    if (tag.dataType == TagDataType::BOOL) {
        bool boolValue = currentValue.toBool();
        QStringList boolOptions = {"false", "true"};
        QString selected = QInputDialog::getItem(this,
                                                 tr("写入BOOL值"),
                                                 tr("为标签 '%1' 选择新值:").arg(tag.name),
                                                 boolOptions,
                                                 boolValue ? 1 : 0,
                                                 false, &ok);
        if (ok) {
            newValue = (selected == "true");
        }
    }
    else if (tag.dataType == TagDataType::INT || tag.dataType == TagDataType::WORD) {
        int intValue = QInputDialog::getInt(this,
                                            tr("写入INT值"),
                                            tr("为标签 '%1' 输入新INT值:").arg(tag.name),
                                            currentValue.toInt(),
                                            -32768, 32767, 1, &ok);
        if (ok) {
            newValue = intValue;
        }
    }
    else if (tag.dataType == TagDataType::REAL) {
        double realValue = QInputDialog::getDouble(this,
                                                   tr("写入REAL值"),
                                                   tr("为标签 '%1' 输入新REAL值:").arg(tag.name),
                                                   currentValue.toDouble(),
                                                   -1e9, 1e9, 2, &ok);
        if (ok) {
            newValue = static_cast<float>(realValue);
        }
    }
    else if (tag.dataType == TagDataType::DWORD || tag.dataType == TagDataType::DINT) {
        unsigned int dwordValue = static_cast<unsigned int>(
            QInputDialog::getDouble(this,
                                    tr("写入DWORD值"),
                                    tr("为标签 '%1' 输入新DWORD值:").arg(tag.name),
                                    currentValue.toUInt(),
                                    0, 4294967295, 0, &ok));
        if (ok) {
            newValue = dwordValue;
        }
    }
    else {
        QMessageBox::warning(this, tr("不支持的数据类型"),
                             tr("标签 '%1' 的数据类型不支持写入").arg(tag.name));
        return;
    }

    if (ok) {
        writeValueToPLC(tag, newValue);
    }
}

void MainWindow::writeValueToPLC(const TagInfo &tag, const QVariant &value)
{
    if (m_currentState != ControllerState::CONNECTED &&
        m_currentState != ControllerState::POLLING) {
        QMessageBox::warning(this, tr("错误"),
                             QString("未连接到PLC或控制器未就绪\n当前状态: %1")
                                 .arg(controllerStateToString(m_currentState)));
        return;
    }

    // 在UI中显示写入状态
    ui->statusbar->showMessage(tr("正在写入到PLC..."));
    ui->btnWriteValue->setEnabled(false);
    ui->btnWriteValue->setText(tr("写入中..."));

    // 发射写入请求信号
    emit writeRequested(tag.name, value);

    // 重置按钮状态（写入完成后会通过信号恢复）
    QTimer::singleShot(5000, this, [this]() {
        ui->btnWriteValue->setEnabled(true);
        ui->btnWriteValue->setText(tr("Write to PLC"));
    });
}

void MainWindow::writeMultipleValues(const QMap<QString, QVariant> &tagValues)
{
    if (!m_controller || m_currentState != ControllerState::CONNECTED) {
        QMessageBox::warning(this, tr("错误"), tr("未连接到PLC或控制器未初始化"));
        return;
    }

    ui->statusbar->showMessage(tr("批量写入到PLC..."));

    ui->btnWriteValue->setEnabled(false);
    ui->btnWriteValue->setText(tr("批量写入中..."));

    // 通过 Controller 批量写入
    m_controller->writeBatchTags(tagValues);
}

void MainWindow::onWriteCompleted(const QString &tagName, bool success, const QString &error)
{
    ui->btnWriteValue->setEnabled(true);
    ui->btnWriteValue->setText(tr("Write to PLC"));
    if (success) {
        this->statusBar()->showMessage(tr("Write completed successfully"), 3000);
    } else {
        QMessageBox::warning(this, tr("Write Failed"),
                             tr("Failed to write value: %1").arg(error));
    }
}

void MainWindow::onBatchWriteCompleted(const QMap<QString, bool>& results, const QString& error)
{
    if (m_writeProgressDialog) {
        m_writeProgressDialog->close();
    }

    if (error.isEmpty()) {
        int successCount = 0;
        int totalCount = results.size();

        for (auto it = results.constBegin(); it != results.constEnd(); ++it) {
            if (it.value()) {
                successCount++;
            }
        }

        if (successCount == totalCount) {
            showNotification("批量写入", "所有标签写入成功");
        } else {
            QString errorMsg = QString("写入完成: %1/%2 个标签成功")
                                   .arg(successCount)
                                   .arg(totalCount);

            // 显示失败的标签
            QString failedTags;
            for (auto it = results.constBegin(); it != results.constEnd(); ++it) {
                if (!it.value()) {
                    failedTags += it.key() + ", ";
                }
            }
            failedTags.chop(2); // 移除最后的逗号和空格

            if (!failedTags.isEmpty()) {
                errorMsg += QString("\n失败的标签: %1").arg(failedTags);
            }

            QMessageBox::warning(this, "批量写入", errorMsg);
        }
    } else {
        QMessageBox::critical(this, "批量写入错误", error);
    }
}

// 导出按钮点击
void MainWindow::onExportButtonClicked()
{
    QString fileName = QFileDialog::getSaveFileName(this,
                                                    tr("导出数据"),
                                                    QDir::homePath(),
                                                    tr("CSV文件 (*.csv);;Excel文件 (*.xlsx);;所有文件 (*)"));

    if (fileName.isEmpty()) {
        return;
    }

    if (m_tagModel) {
        bool success = m_tagModel->exportToCsv(fileName);
        if (success) {
            this->statusBar()->showMessage(tr("数据导出成功"), 3000);
        } else {
            QMessageBox::warning(this, tr("导出失败"), tr("无法导出数据到文件"));
        }
    }
}

// 设置按钮点击
void MainWindow::onSettingsButtonClicked()
{
    QMessageBox::information(this, tr("设置"), tr("系统设置功能将在后续版本中实现"));
}

// 添加标签按钮点击
void MainWindow::onAddTagButtonClicked()
{
    bool ok = false;
    QString tagName = QInputDialog::getText(this,
                                            tr("添加标签"),
                                            tr("请输入标签名称:"),
                                            QLineEdit::Normal,
                                            "", &ok);

    if (ok && !tagName.isEmpty()) {
        QString address = QInputDialog::getText(this,
                                                tr("标签地址"),
                                                tr("请输入标签地址 (如: DB1.DBW0):"),
                                                QLineEdit::Normal,
                                                "", &ok);

        if (ok && !address.isEmpty()) {
            // 这里调用PLCData添加标签的方法
            if (m_tagModel) {
                // 假设PLCData有addTag方法
                // m_tagModel->addTag(tagName, address, "INT", true);
                this->statusBar()->showMessage(tr("已添加标签: %1").arg(tagName), 3000);
            }
        }
    }
}

// 时间范围改变
void MainWindow::onTimeRangeChanged(int index)
{
    // 时间范围：0=1小时, 1=6小时, 2=24小时
    int hours[] = {1, 6, 24};
    if (index >= 0 && index < 3) {
        qDebug() << "图表时间范围改为:" << hours[index] << "小时";

        if (m_chart) {
            QDateTimeAxis *axisX = qobject_cast<QDateTimeAxis*>(m_chart->axes(Qt::Horizontal).first());
            if (axisX) {
                QDateTime min = QDateTime::currentDateTime().addSecs(-hours[index] * 3600);
                QDateTime max = QDateTime::currentDateTime();
                axisX->setRange(min, max);
            }
        }
    }
}

// 添加图表按钮点击
void MainWindow::onAddChartButtonClicked()
{
    // 获取当前选中的标签
    QModelIndexList selected = ui->dataTableView->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        QMessageBox::information(this, tr("添加图表"), tr("请先在表格中选择一个标签"));
        return;
    }

    int row = selected.first().row();
    QString tagName = m_tagModel->data(m_tagModel->index(row, PLCData::NameColumn)).toString();

    // 添加标签到图表
    addTagToChart(tagName);
    this->statusBar()->showMessage(tr("已添加 %1 到图表").arg(tagName), 3000);
}

// 添加标签到图表
void MainWindow::addTagToChart(const QString &tagName, const QString &displayName)
{
    if (m_chartSeries.contains(tagName)) {
        qDebug() << "标签" << tagName << "已在图表中";
        return;
    }

    QString display = displayName.isEmpty() ? tagName : displayName;

    QLineSeries *series = new QLineSeries();
    series->setName(display);

    // 设置颜色
    static int colorIndex = 0;
    QColor colors[] = {
        QColor("#007acc"),  // 蓝色
        QColor("#d83b01"),  // 橙色
        QColor("#107c10"),  // 绿色
        QColor("#e3008c"),  // 粉色
        QColor("#ffb900"),  // 黄色
        QColor("#00b294"),  // 青色
    };

    series->setColor(colors[colorIndex % 6]);
    colorIndex++;

    m_chartSeries[tagName] = series;
    m_chart->addSeries(series);

    // 绑定坐标轴
    series->attachAxis(m_chart->axes(Qt::Horizontal).first());
    series->attachAxis(m_chart->axes(Qt::Vertical).first());

    qDebug() << "已添加标签" << tagName << "到图表";
}

// 系统功能
void MainWindow::onTakeScreenshot()
{
    QString fileName = QFileDialog::getSaveFileName(this,
                                                    tr("保存截图"),
                                                    QDir::homePath() + "/screenshot.png",
                                                    tr("PNG图片 (*.png);;JPEG图片 (*.jpg);;BMP图片 (*.bmp)"));

    if (fileName.isEmpty()) {
        return;
    }

    QPixmap pixmap = this->grab();
    if (pixmap.save(fileName)) {
        this->statusBar()->showMessage(tr("截图已保存"), 3000);
    } else {
        QMessageBox::warning(this, tr("保存失败"), tr("无法保存截图"));
    }
}

void MainWindow::onExportData()
{
    onExportButtonClicked();  // 重用导出按钮的逻辑
}

void MainWindow::onImportConfig()
{
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    tr("导入配置"),
                                                    QDir::homePath(),
                                                    tr("JSON文件 (*.json);;所有文件 (*)"));

    if (fileName.isEmpty()) {
        return;
    }

    QMessageBox::information(this, tr("导入配置"),
                             tr("配置文件 %1 导入功能将在后续版本中实现").arg(fileName));
    this->statusBar()->showMessage(tr("配置文件导入功能待实现"), 3000);
}

void MainWindow::onAbout()
{
    QMessageBox aboutBox(this);
    aboutBox.setWindowTitle(tr("关于 PLC 监控系统"));
    aboutBox.setTextFormat(Qt::RichText);
    aboutBox.setText(tr(
        "<h3>PLC 监控系统 v1.0</h3>"
        "<p>一个基于 Qt 和 Snap7 的 PLC 监控应用程序</p>"
        "<p>功能：</p>"
        "<ul>"
        "<li>PLC 连接与数据读取</li>"
        "<li>实时数据显示与图表</li>"
        "<li>数据导出功能</li>"
        "<li>标签管理</li>"
        "</ul>"
        "<p>© 2024 PLC Monitor Team</p>"
        ));
    aboutBox.setIconPixmap(QPixmap(":/icons/app_icon.png").scaled(64, 64));
    aboutBox.exec();
}

void MainWindow::onQuit()
{
    if (m_currentState == ControllerState::CONNECTED) {
        QMessageBox::StandardButton reply = QMessageBox::question(this,
                                                                  tr("退出"),
                                                                  tr("PLC 已连接，确定要退出吗？"),
                                                                  QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::No) {
            return;
        }
    }

    qApp->quit();
}


void MainWindow::closeEvent(QCloseEvent *event)
{
    qDebug() << "MainWindow 关闭事件";

    if (m_controller) {
        // 停止 Controller
        m_controller->stopController();

        // 等待 Controller 清理
        QThread::msleep(100);
    }

    // 保存状态
    saveWindowState();
    saveSettings();

    // 清理资源
    cleanupResources();

    event->accept();
}

void MainWindow::saveWindowState()
{
    QSettings settings("PLCMonitor", "PLCMonitor");

    // 保存窗口几何信息
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());

    // 保存分割器状态
    settings.setValue("splitterSizes", ui->mainSplitter->saveState());

    // 保存标签页状态
    settings.setValue("currentTab", ui->mainTabWidget->currentIndex());

    // 保存图表视图的交互状态
    if (m_chartView) {
        // 保存是否启用了交互功能
        settings.setValue("chartInteractive", m_chartView->isInteractive());

        // 保存图表范围
        if (m_chart && m_chart->axes().size() >= 2) {
            QRectF plotArea = m_chart->plotArea();
            settings.setValue("chartPlotArea",
                              QString("%1,%2,%3,%4")
                                  .arg(plotArea.x())
                                  .arg(plotArea.y())
                                  .arg(plotArea.width())
                                  .arg(plotArea.height()));

            // 保存坐标轴范围
            if (auto axisX = qobject_cast<QValueAxis*>(m_chart->axes(Qt::Horizontal).first())) {
                settings.setValue("chartAxisXMin", axisX->min());
                settings.setValue("chartAxisXMax", axisX->max());
            }
            if (auto axisY = qobject_cast<QValueAxis*>(m_chart->axes(Qt::Vertical).first())) {
                settings.setValue("chartAxisYMin", axisY->min());
                settings.setValue("chartAxisYMax", axisY->max());
            }
        }
    }

    qDebug() << "Window state saved";
}

void MainWindow::restoreWindowState()
{
    QSettings settings("PLCMonitor", "PLCMonitor");

    // 注意：主题设置已在构造函数中应用，这里不再重复应用

    // 恢复窗口几何信息
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());

    // 恢复分割器状态
    ui->mainSplitter->restoreState(settings.value("splitterSizes").toByteArray());

    // 恢复标签页状态
    int tabIndex = settings.value("currentTab", 0).toInt();
    ui->mainTabWidget->setCurrentIndex(tabIndex);

    qDebug() << "Window state restored";
}

void MainWindow::saveSettings()
{
    QSettings settings("PLCMonitor", "PLCMonitor");

    // 保存主题设置
    settings.setValue("darkTheme", m_isDarkTheme);

    // 保存连接参数
    settings.setValue("plc/ip", m_plcConfig.ip);
    settings.setValue("plc/rack", m_plcConfig.rack);
    settings.setValue("plc/slot", m_plcConfig.slot);
    settings.setValue("plc/port", m_plcConfig.port);
    settings.setValue("plc/autoConnect", m_plcConfig.autoConnect);

    // 保存轮询设置
    settings.setValue("polling/autoStart", m_plcConfig.autoStartPolling);
    settings.setValue("polling/interval", m_plcConfig.pollingInterval);

    // 保存选择的标签
    settings.setValue("selectedTags", QStringList(m_selectedTags.values()));

    qDebug() << "Settings saved";
}

void MainWindow::loadSettings()
{
    QSettings settings("PLCMonitor", "PLCMonitor");

    // 加载选择的标签
    QStringList selectedTags = settings.value("selectedTags").toStringList();
    for (const QString &tag : selectedTags) {
        m_selectedTags.insert(tag);
    }

    // 通知 Controller
    if (m_controller && !selectedTags.isEmpty()) {
        m_controller->setTableSelection(selectedTags);
    }
}

void MainWindow::cleanupResources()
{
    qDebug() << "开始清理资源...";

    // 1. 停止所有活动
    if (m_plcConnector) {
        m_plcConnector->disconnectFromPLC();
    }

    // 2. 清理图表
    if (m_chart) {
        m_chart->removeAllSeries();
    }

    // 3. 清理系列
    qDeleteAll(m_chartSeries);
    m_chartSeries.clear();

    // 4. 清理模型
    if (m_realTimeDataModel) {
        m_realTimeDataModel->clearData();
    }

    if (m_tagModel) {
        m_tagModel->clear();
    }

    // 5. 清理线程
    if (m_plcThread) {
        m_plcThread->quit();
        if (!m_plcThread->wait(2000)) {
            qWarning() << "PLC线程未正常结束，强制终止";
            m_plcThread->terminate();
            m_plcThread->wait();
        }
        delete m_plcThread;
        m_plcThread = nullptr;
    }

    // 6. 清理连接器
    if (m_plcConnector) {
        m_plcConnector->deleteLater();
        m_plcConnector = nullptr;
    }

    // 7. 清理控制器
    if (m_controller) {
        m_controller->stopController();
        m_controller->deleteLater();
        m_controller = nullptr;
    }

    qDebug() << "资源清理完成";
}

void MainWindow::updatePollingStatus()
{
    ControllerState currentState = m_plcConnector->getControllerState();

    switch (currentState) {
    case ControllerState::POLLING:
        m_pollingLabel->setText(QString("轮询中 (%1ms)").arg(m_plcConfig.pollingInterval));
        m_pollingLabel->setStyleSheet("QLabel { color: #4caf50; padding: 2px 8px; }");
        break;

    case ControllerState::PAUSED:
        m_pollingLabel->setText("轮询暂停");
        m_pollingLabel->setStyleSheet("QLabel { color: #ff9800; padding: 2px 8px; }");
        break;

    default:
        m_pollingLabel->setText("轮询停止");
        m_pollingLabel->setStyleSheet("QLabel { color: #888; padding: 2px 8px; }");
        break;
    }
}

// 实时数据表格相关方法
void MainWindow::setupRealTimeDataView()
{
    // 创建实时数据模型
    m_realTimeDataModel = new RealTimeDataModel(this);
    m_realTimeDataModel->setMaxRecords(1000);
    m_realTimeDataModel->setShowNewestFirst(true);

    // 设置表格视图
    ui->realTimeTableView->setModel(m_realTimeDataModel);
    ui->realTimeTableView->setSortingEnabled(true);
    ui->realTimeTableView->sortByColumn(0, Qt::DescendingOrder);

    // 设置列宽
    ui->realTimeTableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    ui->realTimeTableView->verticalHeader()->setDefaultSectionSize(25);

    // 添加工具栏控制
    setupRealTimeDataToolbar();
}

void MainWindow::setupRealTimeDataToolbar()
{
    // 刷新按钮
    QPushButton* btnRefresh = new QPushButton("刷新选中", this);
    btnRefresh->setToolTip("仅刷新选中的标签数据");
    connect(btnRefresh, &QPushButton::clicked, this, &MainWindow::refreshSelectedTags);
    ui->toolBar->addWidget(btnRefresh);

    // 全选/取消全选
    QPushButton* btnSelectAll = new QPushButton("全选", this);
    QPushButton* btnClearAll = new QPushButton("清空", this);

    connect(btnSelectAll, &QPushButton::clicked, this, &MainWindow::selectAllTags);
    connect(btnClearAll, &QPushButton::clicked, this, &MainWindow::clearAllTags);

    ui->toolBar->addWidget(btnSelectAll);
    ui->toolBar->addWidget(btnClearAll);

    // 排序控制
    QToolButton* sortButton = new QToolButton(this);
    sortButton->setText("排序");
    sortButton->setPopupMode(QToolButton::InstantPopup);

    QMenu* sortMenu = new QMenu(sortButton);
    QAction* sortNewestFirst = new QAction("最新数据在上方", sortMenu);
    QAction* sortOldestFirst = new QAction("最新数据在下方", sortMenu);

    sortNewestFirst->setCheckable(true);
    sortOldestFirst->setCheckable(true);
    sortNewestFirst->setChecked(true);

    QActionGroup* sortGroup = new QActionGroup(this);
    sortGroup->addAction(sortNewestFirst);
    sortGroup->addAction(sortOldestFirst);

    sortMenu->addAction(sortNewestFirst);
    sortMenu->addAction(sortOldestFirst);
    sortButton->setMenu(sortMenu);

    ui->toolBar->addWidget(sortButton);

    connect(sortNewestFirst, &QAction::triggered, this, [this]() {
        m_realTimeDataModel->setShowNewestFirst(true);
        ui->realTimeTableView->sortByColumn(0, Qt::DescendingOrder);
    });

    connect(sortOldestFirst, &QAction::triggered, this, [this]() {
        m_realTimeDataModel->setShowNewestFirst(false);
        ui->realTimeTableView->sortByColumn(0, Qt::AscendingOrder);
    });

    // 导出按钮
    QPushButton* btnExport = new QPushButton("导出CSV", this);
    connect(btnExport, &QPushButton::clicked, this, &MainWindow::exportRealTimeData);
    ui->toolBar->addWidget(btnExport);
}

void MainWindow::refreshSelectedTags()
{
    if (m_plcConnector && m_currentState == ControllerState::CONNECTED) {
        QMetaObject::invokeMethod(m_plcConnector, "pollData", Qt::QueuedConnection);
        ui->statusbar->showMessage("正在刷新选中的标签数据...", 2000);
    }
}

void MainWindow::selectAllTags()
{
    if (m_tagModel) {
        QStringList allTags = m_tagModel->getAllTagNames();
        for (const QString &tagName : allTags) {
            m_tagModel->setTagSelected(tagName, true);
        }
    }
}

void MainWindow::clearAllTags()
{
    if (m_tagModel) {
        m_tagModel->clearAllSelections();
    }
}

void MainWindow::exportRealTimeData()
{
    if (!m_realTimeDataModel) {
        QMessageBox::warning(this, "导出失败", "实时数据模型未初始化");
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(this,
                                                    "导出实时数据",
                                                    QString("%1/实时数据_%2.csv")
                                                        .arg(QDir::homePath())
                                                        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")),
                                                    "CSV文件 (*.csv)");

    if (!fileName.isEmpty()) {
        if (m_realTimeDataModel->exportToCsv(fileName)) {
            ui->statusbar->showMessage(QString("数据已导出到: %1").arg(fileName), 3000);
        } else {
            QMessageBox::warning(this, "导出失败", "无法导出数据到文件");
        }
    }
}



void MainWindow::updateStatusBarInfo()
{
    if (!m_tagModel || !m_plcConnector) return;

    // 更新连接状态
    ControllerState state = m_plcConnector->getControllerState();

    // 更新标签数量
    int totalTags = m_tagModel->rowCount();
    int selectedCount = m_selectedTags.size();

    // 更新实时数据记录数
    int recordCount = m_realTimeDataModel ? m_realTimeDataModel->rowCount() : 0;

    // 更新内存使用
    static QElapsedTimer memoryTimer;
    static qint64 lastMemory = 0;

    if (memoryTimer.elapsed() > 5000) { // 每5秒更新一次内存
#ifdef Q_OS_WIN
    //    PROCESS_MEMORY_COUNTERS pmc;
    //    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    //        lastMemory = pmc.WorkingSetSize;
    //    }
#endif
        memoryTimer.restart();
    }

    // 更新状态栏
    QString statusText = QString("状态: %1 | 标签: %2/%3 | 记录: %4 | 内存: %5 MB")
                             .arg(controllerStateToString(state))
                             .arg(selectedCount)
                             .arg(totalTags)
                             .arg(recordCount)
                             .arg(lastMemory / 1024 / 1024);

    m_statusLabel->setText(statusText);
}

void MainWindow::onPLCDataUpdated(const QMap<QString, QVariant> &data)
{
    if (m_selectedTags.isEmpty() || data.isEmpty())
        return;

    // 只收集选中的标签数据
    QMap<QString, QVariant> filteredData;
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        if (m_selectedTags.contains(it.key())) {
            filteredData[it.key()] = it.value();
        }
    }

    if (!filteredData.isEmpty()) {
        m_realTimeDataModel->addDataRecord(filteredData);
    }
}

void MainWindow::processDataForTable(const QMap<QString, QVariant> &data) {
    if (!m_tagModel || data.isEmpty()) {
        qDebug() << "processDataForTable: 数据为空或模型为空";
        return;
    }

    QElapsedTimer timer;
    timer.start();
    qint64 startTime = QDateTime::currentMSecsSinceEpoch();

    int updateCount = 0;
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        m_tagModel->updateTagValue(it.key(), it.value());
        updateCount++;
    }

    qint64 processTime = timer.elapsed();
    qDebug() << "表格数据处理: 更新" << updateCount << "个标签，耗时" << processTime << "ms";

    if (processTime > 100) {
        qWarning() << "表格数据处理耗时过长:" << processTime << "ms";
    }

    ui->dataTableView->viewport()->update();
}

void MainWindow::processDataForChart(const QMap<QString, QVariant> &data) {
    if (!m_chart || data.isEmpty()) return;

    qreal xValue = QDateTime::currentDateTime().toMSecsSinceEpoch();

    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        if (m_chartSeries.contains(it.key())) {
            QLineSeries* series = m_chartSeries.value(it.key());
            qreal yValue = it.value().toReal();
            series->append(xValue, yValue);

            // 限制数据点数量
            if (series->count() > 1000) {
                series->removePoints(0, series->count() - 1000);
            }
        }
    }

    // 自动调整Y轴范围
    updateChartAxis();
}

void MainWindow::processDataForRealTime(const QMap<QString, QVariant> &data) {
    if (!m_realTimeDataModel || data.isEmpty()) return;

    // 只处理选中的标签
    QMap<QString, QVariant> filteredData;
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        if (m_selectedTags.contains(it.key())) {
            filteredData[it.key()] = it.value();
        }
    }

    if (!filteredData.isEmpty()) {
        m_realTimeDataModel->addDataRecord(filteredData);
    }
}

// 单标签更新函数
void MainWindow::updateDataInTable(const QString &tagName, const QVariant &value) {
    if (m_tagModel) {
        m_tagModel->updateTagValue(tagName, value);
    }
}

void MainWindow::updateDataInChart(const QString &tagName, const QVariant &value) {
    if (m_chart && m_chartSeries.contains(tagName)) {
        QLineSeries* series = m_chartSeries.value(tagName);
        qreal xValue = QDateTime::currentDateTime().toMSecsSinceEpoch();
        qreal yValue = value.toReal();
        series->append(xValue, yValue);

        if (series->count() > 1000) {
            series->removePoints(0, series->count() - 1000);
        }

        updateChartAxis();
    }
}

void MainWindow::updateChartAxis()
{
    if (!m_chart || m_chartSeries.isEmpty()) {
        return;
    }

    static qint64 lastUpdate = 0;
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 限制图表更新频率
    if (now - lastUpdate < 200) { // 每200ms更新一次
        return;
    }

    // 获取所有序列
    QList<QLineSeries*> seriesList = m_chartSeries.values();
    if (seriesList.isEmpty() || seriesList[0]->count() == 0) {
        return;
    }

    // 计算Y轴范围
    qreal minY = qInf();
    qreal maxY = -qInf();
    bool hasValidData = false;

    for (QLineSeries* series : seriesList) {
        if (series->count() > 0) {
            hasValidData = true;

            // 只检查最近的点以提高性能
            int pointsToCheck = qMin(100, series->count());
            for (int i = series->count() - pointsToCheck; i < series->count(); ++i) {
                qreal y = series->at(i).y();
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }

    if (!hasValidData || minY == qInf() || maxY == -qInf()) {
        return;
    }

    // 添加边距
    qreal range = maxY - minY;
    qreal margin = (range > 0) ? range * 0.1 : 1.0;

    minY -= margin;
    maxY += margin;

    // 更新Y轴
    if (auto axisY = qobject_cast<QValueAxis*>(m_chart->axes(Qt::Vertical).first())) {
        qreal currentMin = axisY->min();
        qreal currentMax = axisY->max();

        // 只有当范围变化较大时才更新
        if (qAbs(currentMin - minY) > (margin * 0.1) ||
            qAbs(currentMax - maxY) > (margin * 0.1)) {
            axisY->setRange(minY, maxY);
        }
    }

    // 更新X轴（时间轴）
    if (auto axisX = qobject_cast<QDateTimeAxis*>(m_chart->axes(Qt::Horizontal).first())) {
        QVector<QPointF> points = seriesList[0]->points();
        if (!points.isEmpty()) {
            QDateTime minTime = QDateTime::fromMSecsSinceEpoch(points.last().x() - 60000); // 显示最近1分钟
            QDateTime maxTime = QDateTime::fromMSecsSinceEpoch(points.last().x());

            QDateTime currentMin = axisX->min();
            QDateTime currentMax = axisX->max();

            // 只有当时间范围变化较大时才更新
            if (currentMin.secsTo(minTime) > 10 ||
                currentMax.secsTo(maxTime) > 10) {
                axisX->setRange(minTime, maxTime);
            }
        }
    }

    lastUpdate = now;
}

void MainWindow::updateDataInRealTimeTable(const QString &tagName, const QVariant &value) {
    if (!m_realTimeDataModel || !m_selectedTags.contains(tagName)) {
        return;
    }

    QMap<QString, QVariant> singleData;
    singleData[tagName] = value;
    m_realTimeDataModel->addDataRecord(singleData);
}

// 在mainwindow.cpp文件的适当位置添加这些槽函数的实现

// 控制器数据更新槽
void MainWindow::onControllerDataUpdated(const QMap<QString, QVariant> &data)
{
    static int callCount = 0;
    callCount++;

    qDebug() << "\n=== Controller 数据更新 [" << callCount << "] ===";
    qDebug() << "时间:" << QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    qDebug() << "数据大小:" << data.size();

    if (data.isEmpty()) {
        qDebug() << "收到空数据";
        return;
    }

    QElapsedTimer timer;
    timer.start();

    // 1. 更新表格数据
    if (m_tagModel) {
        for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
            m_tagModel->updateTagValue(it.key(), it.value());
        }
    }

    // 2. 更新实时数据
    if (m_realTimeDataModel && !m_selectedTags.isEmpty()) {
        QMap<QString, QVariant> filteredData;
        for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
            if (m_selectedTags.contains(it.key())) {
                filteredData[it.key()] = it.value();
            }
        }
        if (!filteredData.isEmpty()) {
            m_realTimeDataModel->addDataRecord(filteredData);
        }
    }

    // 3. 更新图表
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        if (m_chartSeries.contains(it.key())) {
            QLineSeries* series = m_chartSeries.value(it.key());
            qreal xValue = QDateTime::currentDateTime().toMSecsSinceEpoch();
            qreal yValue = it.value().toReal();
            series->append(xValue, yValue);

            if (series->count() > 1000) {
                series->removePoints(0, series->count() - 1000);
            }
        }
    }

    // 更新图表轴
    if (!m_chartSeries.isEmpty()) {
        updateChartAxis();
    }

    qint64 processTime = timer.elapsed();
    qDebug() << "数据处理耗时:" << processTime << "ms";

    // 更新最后更新时间显示
    m_timeLabel->setText(QDateTime::currentDateTime().toString("hh:mm:ss"));
}

void MainWindow::onTableDataUpdated(const QMap<QString, QVariant>& data)
{
    if (m_tagModel) {
        m_tagModel->updateMultipleValues(data);

        // 更新表格视图
        ui->dataTableView->viewport()->update();

        // 更新状态栏
        m_statusLabel->setText(QString("数据更新: %1 个标签").arg(data.size()));
    }
}

void MainWindow::onChartDataUpdated(const QMap<QString, QVariant>& data)
{
    static qint64 lastUpdate = 0;
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 限制图表更新频率（每秒最多10次）
    if (now - lastUpdate < 100) {
        return;
    }
    lastUpdate = now;

    qreal xValue = now;

    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        QString tagName = it.key();
        QVariant value = it.value();

        if (m_chartSeries.contains(tagName)) {
            QLineSeries* series = m_chartSeries.value(tagName);
            series->append(xValue, value.toReal());

            // 限制数据点数量
            if (series->count() > 1000) {
                series->removePoints(0, series->count() - 1000);
            }
        }
    }

    if (!m_chartSeries.isEmpty()) {
        updateChartAxis();
    }
}

// 连接状态改变的处理
void MainWindow::onConnectionStateChanged(bool connected)
{
    qDebug() << "MainWindow::onConnectionStateChanged:" << (connected ? "已连接" : "已断开");

    // 通过getControllerState获取准确状态，而不是用bool参数
    ControllerState currentState = m_plcConnector->getControllerState();
    updateUIState(currentState);

    // 只在需要时显示状态消息
    if (connected) {
        if (currentState == ControllerState::CONNECTED) {
            statusBar()->showMessage("PLC连接成功", 3000);
            autoStartPolling();
        }
    } else {
        if (currentState == ControllerState::DISCONNECTED) {
            statusBar()->showMessage("PLC连接已断开", 3000);
        }
    }
}

// 报警触发槽
void MainWindow::onAlertTriggered(const AlertInfo &alert)
{
    // 显示报警消息
    QMessageBox::warning(this,
                         tr("PLC报警"),
                         tr("报警: %1\n标签: %2\n值: %3\n时间: %4")
                             .arg(alert.message)
                             .arg(alert.tagName)
                             .arg(alert.value.toString())
                             .arg(alert.timestamp.toString("yyyy-MM-dd HH:mm:ss")));

    // 在状态栏显示报警
    ui->statusbar->showMessage(tr("报警: %1").arg(alert.message), 5000);

    // 可以添加声音报警或其他通知方式
    QApplication::beep();
}

// 写入进度改变槽
void MainWindow::onWriteProgressChanged(int current, int total)
{
    if (total > 0) {
        ui->statusbar->showMessage(tr("写入进度: %1/%2").arg(current).arg(total), 1000);

        // 更新进度条（如果有的话）
        if (m_writeProgressDialog) {
            m_writeProgressDialog->setMaximum(total);
            m_writeProgressDialog->setValue(current);
        }
    } else {
        // 写入完成
        if (m_writeProgressDialog) {
            m_writeProgressDialog->close();
            delete m_writeProgressDialog;
            m_writeProgressDialog = nullptr;
        }
    }
}

// 标签选择槽
void MainWindow::onTagSelected(const QString &tagName, bool selected)
{
    if (selected) {
        m_selectedTags.insert(tagName);

        // 添加到图表
        addTagToChart(tagName);

        // 通知Controller
        if (m_controller) {
            m_controller->addChartTag(tagName);
        }
    } else {
        m_selectedTags.remove(tagName);

        // 从图表中移除
        if (m_chartSeries.contains(tagName)) {
            QLineSeries* series = m_chartSeries.take(tagName);
            m_chart->removeSeries(series);
            delete series;
        }

        // 通知Controller
        if (m_controller) {
            m_controller->removeChartTag(tagName);
        }
    }

    // 更新状态栏
    updateStatusBarInfo();

    // 保存选择的标签
    saveSettings();
}

// Controller状态改变的处理
void MainWindow::onControllerStateChanged(ControllerState newState)
{
    QString stateText = controllerStateToString(newState);
    qDebug() << "Controller 状态改变 ->" << stateText;

    // 统一通过 updateUIState 更新所有UI
    updateUIState(newState);

    // 根据状态执行特定操作
    switch (newState) {
    case ControllerState::CONNECTED:
        qDebug() << "PLC连接成功";
        statusBar()->showMessage("PLC连接成功", 3000);

        // 连接成功后的初始化
        if (m_tagModel) {
            m_tagModel->updateAllData(QVariantMap());
        }
        if (m_realTimeDataModel) {
            m_realTimeDataModel->clearData();
        }
        break;

    case ControllerState::POLLING:
        qDebug() << "轮询已启动，间隔:" << m_plcConfig.pollingInterval << "ms";
        statusBar()->showMessage("轮询已启动", 3000);
        break;

    case ControllerState::ERROR:
        qWarning() << "Controller 进入错误状态";
        statusBar()->showMessage("PLC通信错误", 3000);
        break;

    default:
        break;
    }
}

void MainWindow::onProcessDataUpdated(const QMap<QString, QVariant>& data)
{
    static int updateCount = 0;
    static QElapsedTimer updateTimer;

    if (updateCount == 0) {
        updateTimer.start();
    }

    updateCount++;

    // 1. 更新主数据表格
    if (m_tagModel && !data.isEmpty()) {
        for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
            QString tagName = it.key();
            QVariant value = it.value();

            // 更新数据模型
            m_tagModel->updateTagValue(tagName, value);

            // 如果标签被选中用于实时数据显示
            if (m_selectedTags.contains(tagName)) {
                updateRealTimeData(tagName, value);
            }

            // 如果标签在图表中显示
            if (m_chartSeries.contains(tagName)) {
                updateChartData(tagName, value);
            }
        }

        // 触发视图更新
        ui->dataTableView->viewport()->update();
    }

    // 定期输出处理统计（每秒一次）
    if (updateTimer.elapsed() >= 1000) {
        qDebug() << "[数据更新] 每秒处理次数:" << updateCount
                 << " 耗时:" << updateTimer.elapsed() << "ms";
        updateCount = 0;
    }
}

void MainWindow::onTagValueChanged(const QString& tagName, const QVariant& value)
{
    // 1. 检查报警条件
    checkAlarmCondition(tagName, value);

    // 2. 更新UI显示
    if (m_tagModel) {
        m_tagModel->updateTagValue(tagName, value);

        // 查找对应的行
        int row = m_tagModel->findTagRow(tagName);
        if (row >= 0) {
            // 高亮显示更新的单元格
            QModelIndex index = m_tagModel->index(row, PLCData::ValueColumn);

            // 使用代理设置动画效果
            QTimer::singleShot(500, this, [this, index]() {
                // 清除高亮效果
                ui->dataTableView->viewport()->update();
            });
        }
    }

    // 3. 如果被选中，更新实时数据
    if (m_selectedTags.contains(tagName) && m_realTimeDataModel) {
        QMap<QString, QVariant> singleData;
        singleData[tagName] = value;
        m_realTimeDataModel->addDataRecord(singleData);
    }

    // 4. 如果图表中显示，更新图表
    if (m_chartSeries.contains(tagName)) {
        qreal xValue = QDateTime::currentDateTime().toMSecsSinceEpoch();
        qreal yValue = value.toReal();

        QLineSeries* series = m_chartSeries.value(tagName);
        series->append(xValue, yValue);

        // 限制数据点数量
        if (series->count() > 1000) {
            series->removePoints(0, series->count() - 1000);
        }

        // 更新Y轴范围
        updateChartAxis();
    }
}

void MainWindow::showNotification(const QString &title, const QString &message, QSystemTrayIcon::MessageIcon icon)
{
 //   if (!m_trayIcon) {
 //       setupTrayIcon();
 //   }

    if (m_trayIcon && m_trayIcon->isVisible()) {
        m_trayIcon->showMessage(title, message, icon, 3000);
    } else {
        QMessageBox::information(this, title, message);
    }
}


void MainWindow::updateRealTimeData(const QString& tagName, const QVariant& value)
{
    if (!m_realTimeDataModel) return;

    QMap<QString, QVariant> data;
    data[tagName] = value;
    m_realTimeDataModel->addDataRecord(data);
}

void MainWindow::updateChartData(const QString& tagName, const QVariant& value)
{
    if (!m_chartSeries.contains(tagName)) return;

    QLineSeries* series = m_chartSeries.value(tagName);
    qreal xValue = QDateTime::currentDateTime().toMSecsSinceEpoch();
    qreal yValue = value.toReal();

    series->append(xValue, yValue);

    // 保持数据点数量合理
    if (series->count() > 2000) {
        series->removePoints(0, 1000);  // 移除前1000个点
    }
}

void MainWindow::checkAlarmCondition(const QString& tagName, const QVariant& value)
{
    // 这里可以实现报警条件检查
    // 例如：检查值是否超过阈值

    // 示例：简单的阈值检查
    if (tagName.contains("温度", Qt::CaseInsensitive)) {
        double temp = value.toDouble();
        if (temp > 100.0) {
            // 触发高温报警
            AlertInfo alert;
            alert.tagName = tagName;
            alert.value = value;
            alert.message = QString("温度过高: %1°C").arg(temp);
            alert.timestamp = QDateTime::currentDateTime();
         //   alert.level = AlertLevel::High;

         //   emit onAlertTriggered(alert);
        }
    }
}

// 设置视图
void MainWindow::setupViews()
{
    // 实时数据视图 - 默认选择所有标签
    connect(ui->btnSelectAllTags, &QPushButton::clicked, this, [this]() {
        selectAllTagsForView(REAL_TIME_VIEW);
    });

    connect(ui->btnClearRealTime, &QPushButton::clicked, this, [this]() {
        clearAllTagsForView(REAL_TIME_VIEW);
    });

    // 图表视图 - 初始为空
    ui->chartTagList->setSelectionMode(QAbstractItemView::MultiSelection);
    connect(ui->btnAddToChart, &QPushButton::clicked, this, [this]() {
        addSelectedTagsToView(CHART_VIEW);
    });

    connect(ui->btnClearChart, &QPushButton::clicked, this, [this]() {
        clearAllTagsForView(CHART_VIEW);
    });

    // 历史趋势视图 - 初始为空
    ui->trendTagList->setSelectionMode(QAbstractItemView::MultiSelection);
    connect(ui->btnAddToTrend, &QPushButton::clicked, this, [this]() {
        addSelectedTagsToView(TREND_VIEW);
    });

    connect(ui->btnClearTrend, &QPushButton::clicked, this, [this]() {
        clearAllTagsForView(TREND_VIEW);
    });

    // 工艺流程图视图 - 特殊处理
    setupProcessView();

    // 视图切换
    connect(ui->mainTabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        onViewChanged(static_cast<ViewType>(index));
    });

    // 标签选择变化
    if (ui->dataTableView->selectionModel()) {
        connect(ui->dataTableView->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, [this](const QItemSelection &selected, const QItemSelection &deselected) {
                    onTableSelectionChanged(selected, deselected);
                });
    }
}

// 工艺流程图视图设置
void MainWindow::setupProcessView()
{
    // 流程图视图会自动加载该页面所有标签
    // 这里可以添加特定的流程图标签管理逻辑
    m_processViewTags = loadProcessViewTags();

    // 为流程图添加刷新按钮
    QPushButton* btnRefreshProcess = new QPushButton("刷新工艺图", this);
    btnRefreshProcess->setToolTip("刷新工艺流程图中的所有标签");
    connect(btnRefreshProcess, &QPushButton::clicked, this, [this]() {
        refreshProcessViewTags();
    });

    // 添加到流程图工具栏
    if (ui->processToolBar) {
        ui->processToolBar->addWidget(btnRefreshProcess);
    }
}

// 加载流程图标签
QSet<QString> MainWindow::loadProcessViewTags()
{
    QSet<QString> tags;

    // 从配置文件加载流程图标签
    QSettings settings("PLCMonitor", "ProcessView");
    QStringList tagList = settings.value("processTags").toStringList();

    foreach (const QString &tag, tagList) {
        tags.insert(tag);
    }

    // 如果没有配置，则使用所有可用标签
    if (tags.isEmpty() && m_tagModel) {
        tags = m_tagModel->getAllTagNames().toSet();
    }

    return tags;
}

// 视图切换处理
void MainWindow::onViewChanged(ViewType viewType)
{
    qDebug() << "切换到视图:" << getViewName(viewType);

    // 更新当前视图的选择状态
    updateCurrentViewSelection(viewType);

    // 根据视图类型更新控制器
    if (m_controller) {
        switch (viewType) {
        case REAL_TIME_VIEW:
            // 实时数据视图 - 加载所有标签
            m_controller->setSelectedTags(m_selectedTags[viewType].toList());
            break;
        case CHART_VIEW:
        case TREND_VIEW:
            // 图表和历史视图 - 加载选择的标签
            m_controller->setSelectedTags(m_selectedTags[viewType].toList());
            break;
        case PROCESS_VIEW:
            // 工艺流程图 - 加载该页面所有标签
            m_controller->setSelectedTags(m_processViewTags.toList());
            break;
        default:
            break;
        }
    }

    // 更新UI显示
    updateViewDisplay(viewType);
}

// 表格选择变化处理
void MainWindow::onTableSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    Q_UNUSED(deselected)

    int currentView = ui->mainTabWidget->currentIndex();
    if (currentView < 0 || currentView >= VIEW_COUNT) {
        return;
    }

    ViewType viewType = static_cast<ViewType>(currentView);

    // 获取选中的标签
    QStringList selectedTags;
    QModelIndexList indexes = ui->dataTableView->selectionModel()->selectedRows();

    for (const QModelIndex &index : indexes) {
        if (index.isValid()) {
            QString tagName = m_tagModel->data(index.siblingAtColumn(PLCData::NameColumn)).toString();
            selectedTags.append(tagName);
        }
    }

    // 更新当前视图的选择
    updateViewSelectionFromTable(viewType, selectedTags);
}

// 更新视图选择
void MainWindow::updateViewSelection(ViewType viewType, const QString& tagName, bool selected)
{
    if (selected) {
        m_selectedTags[viewType].insert(tagName);
    } else {
        m_selectedTags[viewType].remove(tagName);
    }

    // 保存选择
    saveViewSelections();

    // 更新对应视图的显示
    switch (viewType) {
    case REAL_TIME_VIEW:
        updateRealTimeView();
        break;
    case CHART_VIEW:
        updateChartView();
        break;
    case TREND_VIEW:
        updateTrendView();
        break;
    case PROCESS_VIEW:
        // 流程图视图不在此处理
        break;
    default:
        break;
    }
}

// 为指定视图选择所有标签
void MainWindow::selectAllTagsForView(ViewType viewType)
{
    if (!m_tagModel) return;

    QStringList allTags = m_tagModel->getAllTagNames();
    m_selectedTags[viewType] = allTags.toSet();

    // 通知Controller
    if (m_controller) {
        m_controller->setSelectedTags(allTags);
    }

    // 更新显示
    updateViewDisplay(viewType);

    // 保存选择
    saveViewSelections();

    ui->statusbar->showMessage(
        QString("已为%1选择所有标签").arg(getViewName(viewType)),
        3000
        );
}

// 为指定视图清空选择
void MainWindow::clearAllTagsForView(ViewType viewType)
{
    m_selectedTags[viewType].clear();

    // 通知Controller
    if (m_controller) {
        m_controller->setSelectedTags(QStringList());
    }

    // 更新显示
    updateViewDisplay(viewType);

    // 保存选择
    saveViewSelections();

    ui->statusbar->showMessage(
        QString("已清空%1的选择").arg(getViewName(viewType)),
        3000
        );
}

// 添加选中标签到指定视图
void MainWindow::addSelectedTagsToView(ViewType viewType)
{
    QStringList selectedTags = getTableSelectedTags();

    if (selectedTags.isEmpty()) {
        QMessageBox::information(this, "提示", "请先在表格中选择标签");
        return;
    }

    // 添加到视图
    foreach (const QString &tag, selectedTags) {
        m_selectedTags[viewType].insert(tag);
    }

    // 通知Controller
    if (m_controller) {
        QStringList tags = m_selectedTags[viewType].toList();
        m_controller->setSelectedTags(tags);
    }

    // 更新显示
    updateViewDisplay(viewType);

    // 保存选择
    saveViewSelections();

    ui->statusbar->showMessage(
        QString("已添加%1个标签到%2").arg(selectedTags.size()).arg(getViewName(viewType)),
        3000
        );
}

// 从表格选择更新视图
void MainWindow::updateViewSelectionFromTable(ViewType viewType, const QStringList& selectedTags)
{
    m_selectedTags[viewType] = selectedTags.toSet();

    // 通知Controller
    if (m_controller) {
        m_controller->setSelectedTags(selectedTags);
    }

    // 更新显示
    updateViewDisplay(viewType);

    // 保存选择
    saveViewSelections();
}

// 获取表格中选中的标签
QStringList MainWindow::getTableSelectedTags()
{
    QStringList selectedTags;

    if (ui->dataTableView && ui->dataTableView->selectionModel()) {
        QModelIndexList indexes = ui->dataTableView->selectionModel()->selectedRows();

        for (const QModelIndex &index : indexes) {
            if (index.isValid()) {
                QString tagName = m_tagModel->data(index.siblingAtColumn(PLCData::NameColumn)).toString();
                selectedTags.append(tagName);
            }
        }
    }

    return selectedTags;
}

// 更新视图显示
void MainWindow::updateViewDisplay(ViewType viewType)
{
    switch (viewType) {
    case REAL_TIME_VIEW:
        // 更新实时数据视图
        updateRealTimeView();
        ui->lblRealTimeCount->setText(
            QString("已选择: %1 个标签").arg(m_selectedTags[viewType].size())
            );
        break;

    case CHART_VIEW:
        // 更新图表视图
        updateChartView();
        ui->lblChartCount->setText(
            QString("图表中: %1 个标签").arg(m_selectedTags[viewType].size())
            );
        break;

    case TREND_VIEW:
        // 更新历史趋势视图
        updateTrendView();
        ui->lblTrendCount->setText(
            QString("趋势中: %1 个标签").arg(m_selectedTags[viewType].size())
            );
        break;

    case PROCESS_VIEW:
        // 更新工艺流程图视图
        updateProcessView();
        ui->lblProcessCount->setText(
            QString("工艺点: %1 个").arg(m_processViewTags.size())
            );
        break;

    default:
        break;
    }
}

// 更新实时数据视图
void MainWindow::updateRealTimeView()
{
    if (!m_realTimeDataModel) return;

    // 清空现有数据
    m_realTimeDataModel->clearData();

    // 设置新的标签选择
    m_realTimeDataModel->setSelectedTags(m_selectedTags[REAL_TIME_VIEW].toList());

    // 更新列表显示
    ui->realTimeList->clear();
    foreach (const QString &tag, m_selectedTags[REAL_TIME_VIEW]) {
        ui->realTimeList->addItem(tag);
    }
}

// 更新图表视图
void MainWindow::updateChartView()
{
    // 清空现有图表
    m_chart->removeAllSeries();
    qDeleteAll(m_chartSeries);
    m_chartSeries.clear();

    // 为每个选中的标签添加序列
    int colorIndex = 0;
    QColor colors[] = {
        QColor("#007acc"),  // 蓝色
        QColor("#d83b01"),  // 橙色
        QColor("#107c10"),  // 绿色
        QColor("#e3008c"),  // 粉色
        QColor("#ffb900"),  // 黄色
        QColor("#00b294"),  // 青色
    };

    foreach (const QString &tag, m_selectedTags[CHART_VIEW]) {
        QLineSeries *series = new QLineSeries();
        series->setName(tag);
        series->setColor(colors[colorIndex % 6]);
        colorIndex++;

        m_chartSeries[tag] = series;
        m_chart->addSeries(series);

        // 绑定坐标轴
        if (!m_chart->axes().isEmpty()) {
            series->attachAxis(m_chart->axes(Qt::Horizontal).first());
            series->attachAxis(m_chart->axes(Qt::Vertical).first());
        }
    }

    // 更新列表显示
    ui->chartTagList->clear();
    foreach (const QString &tag, m_selectedTags[CHART_VIEW]) {
        ui->chartTagList->addItem(tag);
    }
}

// 更新历史趋势视图
void MainWindow::updateTrendView()
{
    // 更新趋势视图的标签列表
    ui->trendTagList->clear();
    foreach (const QString &tag, m_selectedTags[TREND_VIEW]) {
        ui->trendTagList->addItem(tag);
    }

    // 这里可以添加历史数据加载逻辑
    // loadHistoryDataForTrend();
}

// 更新工艺流程图视图
void MainWindow::updateProcessView()
{
    // 更新工艺流程图
    // 这里可以实现工艺图的更新逻辑

    ui->processTagList->clear();
    foreach (const QString &tag, m_processViewTags) {
        ui->processTagList->addItem(tag);
    }
}

// 刷新工艺视图标签
void MainWindow::refreshProcessViewTags()
{
    if (!m_tagModel) return;

    // 获取所有可用标签
    QStringList allTags = m_tagModel->getAllTagNames();
    m_processViewTags = allTags.toSet();

    // 保存到配置
    QSettings settings("PLCMonitor", "ProcessView");
    settings.setValue("processTags", allTags);

    // 更新显示
    updateProcessView();

    // 通知Controller
    if (m_controller) {
        m_controller->setSelectedTags(allTags);
    }

    ui->statusbar->showMessage(
        QString("工艺视图已刷新，共加载 %1 个标签").arg(allTags.size()),
        3000
        );
}

// 保存视图选择
void MainWindow::saveViewSelections()
{
    QSettings settings("PLCMonitor", "ViewSelections");

    // 保存每个视图的选择
    for (int i = 0; i < VIEW_COUNT; i++) {
        ViewType viewType = static_cast<ViewType>(i);
        QString key = QString("view_%1/tags").arg(i);
        QStringList tags = m_selectedTags[viewType].toList();
        settings.setValue(key, tags);
    }

    // 保存工艺视图标签
    settings.setValue("processView/tags", m_processViewTags.toList());

    qDebug() << "视图选择已保存";
}

// 加载视图选择
void MainWindow::loadViewSelections()
{
    QSettings settings("PLCMonitor", "ViewSelections");

    // 加载每个视图的选择
    for (int i = 0; i < VIEW_COUNT; i++) {
        QString key = QString("view_%1/tags").arg(i);
        QStringList tags = settings.value(key).toStringList();
        m_selectedTags[i] = tags.toSet();
    }

    // 加载工艺视图标签
    QStringList processTags = settings.value("processView/tags").toStringList();
    m_processViewTags = processTags.toSet();

    // 如果工艺视图标签为空，加载所有标签
    if (m_processViewTags.isEmpty() && m_tagModel) {
        m_processViewTags = m_tagModel->getAllTagNames().toSet();
    }

    qDebug() << "视图选择已加载";
}

// 获取视图名称
QString MainWindow::getViewName(ViewType viewType) const
{
    static const QString viewNames[] = {
        "实时数据",
        "趋势图表",
        "历史数据",
        "工艺流程图"
    };

    if (viewType >= 0 && viewType < VIEW_COUNT) {
        return viewNames[viewType];
    }
    return "未知视图";
}

// 保存选择到文件
void MainWindow::saveSelectionsToFile(const QString& fileName)
{
    QJsonObject root;
    QJsonArray viewsArray;

    for (int i = 0; i < VIEW_COUNT; i++) {
        QJsonObject viewObj;
        viewObj["viewName"] = getViewName(static_cast<ViewType>(i));

        QJsonArray tagsArray;
        foreach (const QString &tag, m_selectedTags[i]) {
            tagsArray.append(tag);
        }
        viewObj["selectedTags"] = tagsArray;

        viewsArray.append(viewObj);
    }

    // 工艺视图
    QJsonObject processViewObj;
    processViewObj["viewName"] = "工艺流程图";
    QJsonArray processTagsArray;
    foreach (const QString &tag, m_processViewTags) {
        processTagsArray.append(tag);
    }
    processViewObj["processTags"] = processTagsArray;
    viewsArray.append(processViewObj);

    root["views"] = viewsArray;
    root["saveTime"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

    QJsonDocument doc(root);
    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
        ui->statusbar->showMessage(QString("标签选择已保存到: %1").arg(fileName), 3000);
    } else {
        QMessageBox::warning(this, "保存失败", "无法保存标签选择到文件");
    }
}

// 从文件加载选择
void MainWindow::loadSelectionsFromFile(const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "加载失败", "无法打开标签选择文件");
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (doc.isNull() || !doc.isObject()) {
        QMessageBox::warning(this, "加载失败", "无效的标签选择文件格式");
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray viewsArray = root["views"].toArray();

    for (int i = 0; i < qMin(viewsArray.size(), VIEW_COUNT); i++) {
        QJsonObject viewObj = viewsArray[i].toObject();
        QJsonArray tagsArray = viewObj["selectedTags"].toArray();

        m_selectedTags[i].clear();
        for (int j = 0; j < tagsArray.size(); j++) {
            m_selectedTags[i].insert(tagsArray[j].toString());
        }
    }

    // 加载工艺视图
    if (viewsArray.size() > VIEW_COUNT) {
        QJsonObject processViewObj = viewsArray[VIEW_COUNT].toObject();
        QJsonArray processTagsArray = processViewObj["processTags"].toArray();

        m_processViewTags.clear();
        for (int i = 0; i < processTagsArray.size(); i++) {
            m_processViewTags.insert(processTagsArray[i].toString());
        }
    }

    // 更新当前视图
    int currentView = ui->mainTabWidget->currentIndex();
    if (currentView >= 0 && currentView < VIEW_COUNT) {
        updateViewDisplay(static_cast<ViewType>(currentView));
    }

    ui->statusbar->showMessage(QString("标签选择已从文件加载"), 3000);
}
