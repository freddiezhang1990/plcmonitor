#include "logindialog.h"
#include "ui_logindialog.h"
#include "usermanager.h"
#include <QMessageBox>

LoginDialog::LoginDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::LoginDialog)
{
    ui->setupUi(this);
    setWindowTitle("登录");

    connect(ui->pushButtonLogin, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
    connect(ui->pushButtonCancel, &QPushButton::clicked, this, &LoginDialog::reject);
}

LoginDialog::~LoginDialog()
{
    delete ui;
}

void LoginDialog::onLoginClicked()
{
    QString username = ui->lineEditUsername->text().trimmed();
    QString password = ui->lineEditPassword->text();

    if (UserManager::instance().login(username, password)) {
        accept();  // 登录成功，关闭对话框
    } else {
        QMessageBox::warning(this, "登录失败",
                             "用户名或密码错误，或用户已被禁用");
        ui->lineEditPassword->clear();
        ui->lineEditPassword->setFocus();
    }
}