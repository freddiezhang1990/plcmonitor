#include "databaseconfigdialog.h"
#include "application.h"
#include "databasemanager.h"
#include "simplecrypt.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QSqlDatabase>
#include <QSqlError>

DatabaseConfigDialog::DatabaseConfigDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("MySQL 数据库配置");
    setModal(true);
    resize(400, 250);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QFormLayout* formLayout = new QFormLayout;

    m_hostEdit = new QLineEdit;
    m_portEdit = new QLineEdit;
    m_dbEdit = new QLineEdit;
    m_userEdit = new QLineEdit;
    m_pwdEdit = new QLineEdit;
    m_pwdEdit->setEchoMode(QLineEdit::Password);

    formLayout->addRow("主机地址:", m_hostEdit);
    formLayout->addRow("端口:", m_portEdit);
    formLayout->addRow("数据库名:", m_dbEdit);
    formLayout->addRow("用户名:", m_userEdit);
    formLayout->addRow("密码:", m_pwdEdit);

    mainLayout->addLayout(formLayout);

    QHBoxLayout* btnLayout = new QHBoxLayout;
    m_testBtn = new QPushButton("测试连接");
    m_saveBtn = new QPushButton("保存");
    m_cancelBtn = new QPushButton("取消");
    btnLayout->addWidget(m_testBtn);
    btnLayout->addWidget(m_saveBtn);
    btnLayout->addWidget(m_cancelBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_testBtn, &QPushButton::clicked, this, &DatabaseConfigDialog::onTestConnection);
    connect(m_saveBtn, &QPushButton::clicked, this, &DatabaseConfigDialog::onSave);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    // 加载当前配置
    Application* app = qobject_cast<Application*>(qApp);
    if (app) {
        PLCConfig cfg = app->getPLCConfig();
        m_hostEdit->setText(cfg.mysqlHost);
        m_portEdit->setText(QString::number(cfg.mysqlPort));
        m_dbEdit->setText(cfg.mysqlDatabase);
        m_userEdit->setText(cfg.mysqlUser);
        SimpleCrypt crypt;
        QString decrypted = crypt.decryptString(cfg.mysqlPassword);
        m_pwdEdit->setText(decrypted);
    }
}

bool DatabaseConfigDialog::testConnection(const QString& host, int port, const QString& dbName,
                                          const QString& user, const QString& pwd)
{
    {
        QSqlDatabase testDb = QSqlDatabase::addDatabase("QMYSQL", "test_conn");
        testDb.setHostName(host);
        testDb.setPort(port);
        testDb.setDatabaseName(dbName);
        testDb.setUserName(user);
        testDb.setPassword(pwd);

        // MariaDB 连接选项 - 禁用 SSL
        QStringList options;
        options << "MYSQL_OPT_SSL_MODE=DISABLED";  // 尝试这个
        options << "MARIADB_OPT_SSL_MODE=DISABLED"; // MariaDB 专用
        options << "CLIENT_SSL=0";                   // 禁用客户端 SSL
        testDb.setConnectOptions(options.join(";"));

        if (!testDb.open())
            return false;
        testDb.close();
    } // testDb 在此销毁
    QSqlDatabase::removeDatabase("test_conn");
    return true;
}

void DatabaseConfigDialog::onTestConnection()
{
    QString host = m_hostEdit->text().trimmed();
    int port = m_portEdit->text().toInt();
    QString dbName = m_dbEdit->text().trimmed();
    QString user = m_userEdit->text().trimmed();
    QString pwd = m_pwdEdit->text();

    if (host.isEmpty() || dbName.isEmpty()) {
        QMessageBox::warning(this, "错误", "主机地址和数据库名不能为空");
        return;
    }

    if (testConnection(host, port, dbName, user, pwd)) {
        QMessageBox::information(this, "测试成功", "数据库连接成功");
    } else {
        QMessageBox::warning(this, "测试失败", "无法连接到数据库，请检查参数");
    }
}

void DatabaseConfigDialog::onSave()
{
    QString host = m_hostEdit->text().trimmed();
    int port = m_portEdit->text().toInt();
    QString dbName = m_dbEdit->text().trimmed();
    QString user = m_userEdit->text().trimmed();
    QString pwd = m_pwdEdit->text();

    if (host.isEmpty() || dbName.isEmpty()) {
        QMessageBox::warning(this, "错误", "主机地址和数据库名不能为空");
        return;
    }

    // 加密密码
    SimpleCrypt crypt;
    QString encryptedPwd = crypt.encryptString(pwd);

    // 保存到 Application 配置
    Application* app = qobject_cast<Application*>(qApp);
    if (!app) {
        QMessageBox::warning(this, "错误", "无法获取应用实例");
        return;
    }

    PLCConfig cfg = app->getPLCConfig();
    cfg.mysqlHost = host;
    cfg.mysqlPort = port;
    cfg.mysqlDatabase = dbName;
    cfg.mysqlUser = user;
    cfg.mysqlPassword = encryptedPwd;
    app->setPLCConfig(cfg);

    if (!app->savePLCConfig()) {
        QMessageBox::warning(this, "错误", "保存配置文件失败");
        return;
    }

    // 重新初始化 DatabaseManager 的 MySQL 连接
    DatabaseManager::instance().reconfigure(host, port, dbName, user, pwd);

    QMessageBox::information(this, "成功", "数据库配置已保存并生效");
    accept();
}