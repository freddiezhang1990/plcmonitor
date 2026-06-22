#include "ConnectionConfigDialog.h"
#include "ui_connectionconfigdialog.h"
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QMessageBox>
#include <QThread>
#include <snap7.h>   // 确保包含 Snap7 头文件

ConnectionConfigDialog::ConnectionConfigDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ConnectionConfigDialog),
    m_testSucceeded(false)
{
    ui->setupUi(this);

    setWindowTitle("PLC连接参数设置");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    // 设置IP输入验证
    QRegularExpression ipRegex("^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}"
                               "(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$");
    QRegularExpressionValidator *ipValidator = new QRegularExpressionValidator(ipRegex, this);
    ui->lineEditIP->setValidator(ipValidator);

    // 设置范围
    ui->spinBoxRack->setRange(0, 31);
    ui->spinBoxSlot->setRange(0, 31);
    ui->spinBoxPort->setRange(1, 65535);
    ui->spinBoxTimeout->setRange(1000, 30000);  // 1-30秒
    ui->spinBoxTimeout->setSingleStep(1000);
    ui->spinBoxInterval->setRange(100, 10000);  // 100ms-10秒
    ui->spinBoxInterval->setSingleStep(100);

    // 连接信号
    connect(ui->btnTest, &QPushButton::clicked, this, &ConnectionConfigDialog::onTestConnection);
    connect(ui->lineEditIP, &QLineEdit::textChanged, this, &ConnectionConfigDialog::onParamsChanged);
    connect(ui->spinBoxRack, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConnectionConfigDialog::onParamsChanged);
    connect(ui->spinBoxSlot, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConnectionConfigDialog::onParamsChanged);
    connect(ui->spinBoxPort, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConnectionConfigDialog::onParamsChanged);
    connect(ui->spinBoxTimeout, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConnectionConfigDialog::onParamsChanged);
    connect(ui->spinBoxInterval, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConnectionConfigDialog::onParamsChanged);
}

ConnectionConfigDialog::~ConnectionConfigDialog()
{
    delete ui;
}

void ConnectionConfigDialog::setPLCParams(const QString& ip, int rack, int slot, int port, int timeout, int interval)
{
    ui->lineEditIP->setText(ip);
    ui->spinBoxRack->setValue(rack);
    ui->spinBoxSlot->setValue(slot);
    ui->spinBoxPort->setValue(port);
    ui->spinBoxTimeout->setValue(timeout);
    ui->spinBoxInterval->setValue(interval);
    m_testSucceeded = false;   // 参数改变，重置测试状态
}

QString ConnectionConfigDialog::getPLCIP() const
{
    return ui->lineEditIP->text().trimmed();
}

int ConnectionConfigDialog::getPLCRack() const
{
    return ui->spinBoxRack->value();
}

int ConnectionConfigDialog::getPLCSlot() const
{
    return ui->spinBoxSlot->value();
}

int ConnectionConfigDialog::getPLCPort() const
{
    return ui->spinBoxPort->value();
}

int ConnectionConfigDialog::getPLCTimeout() const
{
    return ui->spinBoxTimeout->value();
}

int ConnectionConfigDialog::getPLCInterval() const
{
    return ui->spinBoxInterval->value();
}

void ConnectionConfigDialog::onParamsChanged()
{
    // 任何参数修改后，测试成功标志重置
    m_testSucceeded = false;
}

bool ConnectionConfigDialog::testPLCConnection(const QString& ip, int rack, int slot, int port, int timeoutMs)
{
    Q_UNUSED(port);      // Snap7 默认端口 102
    Q_UNUSED(timeoutMs); // 可后续通过 Cli_SetTimeout 设置

    // 创建客户端，返回 S7Object 类型
    S7Object client = Cli_Create();
    if (client == 0) {
        qWarning() << "Failed to create Snap7 client";
        return false;
    }

    // 可选：设置连接超时（单位毫秒）
    // Cli_SetTimeout(client, timeoutMs, timeoutMs, timeoutMs);

    // 尝试连接
    int result = Cli_ConnectTo(client, ip.toStdString().c_str(), rack, slot);
    bool success = (result == 0);

    if (success) {
        // 连接成功，断开连接
        Cli_Disconnect(client);
    } else {
        char errorText[256];
        Cli_ErrorText(result, errorText, sizeof(errorText));
        qWarning() << "Snap7 connection error:" << errorText;
    }

    // 销毁客户端
    Cli_Destroy(&client);
    return success;
}

void ConnectionConfigDialog::onTestConnection()
{
    QString ip = getPLCIP();
    if (ip.isEmpty()) {
        QMessageBox::warning(this, "错误", "请输入PLC IP地址");
        ui->lineEditIP->setFocus();
        return;
    }

    // 格式验证
    if (!ui->lineEditIP->hasAcceptableInput()) {
        QMessageBox::warning(this, "错误", "IP地址格式不正确");
        return;
    }

    int rack = getPLCRack();
    int slot = getPLCSlot();
    int port = getPLCPort();
    int timeout = getPLCTimeout();

    // 显示测试中提示
    ui->btnTest->setEnabled(false);
    ui->btnTest->setText("测试中...");
    QApplication::processEvents();

    // 进行实际连接测试（注意：会短暂阻塞UI，但通常在几秒内）
    bool connected = testPLCConnection(ip, rack, slot, port, timeout);

    ui->btnTest->setEnabled(true);
    ui->btnTest->setText("测试连接");

    if (connected) {
        m_testSucceeded = true;
        QMessageBox::information(this, "测试成功",
                                 QString("成功连接到PLC！\n"
                                         "IP: %1\nRack: %2 Slot: %3\n"
                                         "可以保存参数并连接。")
                                     .arg(ip).arg(rack).arg(slot));
    } else {
        m_testSucceeded = false;
        QMessageBox::warning(this, "测试失败",
                             QString("无法连接到PLC！\n"
                                     "请检查：\n"
                                     "- PLC IP地址是否正确\n"
                                     "- 网络连接是否正常\n"
                                     "- Rack/Slot参数是否正确\n"
                                     "- 防火墙是否允许通讯"));
    }
}

void ConnectionConfigDialog::accept()
{
    if (!m_testSucceeded) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "未测试连接",
            "尚未进行连接测试或测试未通过。\n"
            "确定要保存参数并尝试连接吗？\n"
            "如果参数有误，将导致连接失败。",
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return; // 用户取消
        }
        // 用户强制保存，仍然允许
    }
    QDialog::accept();
}