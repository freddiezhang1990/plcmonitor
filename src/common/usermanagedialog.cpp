#include "usermanagedialog.h"
#include "ui_usermanagedialog.h"
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>

UserManageDialog::UserManageDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::UserManageDialog)
{
    ui->setupUi(this);
    setWindowTitle("用户管理");

    m_usernameEdit = ui->usernameEdit;
    m_roleCombo = ui->roleCombo;
    m_enabledCheck = ui->enabledCheck;
    m_departmentEdit = ui->departmentEdit;
    m_phoneEdit = ui->phoneEdit;
    m_emailEdit = ui->emailEdit;

    refreshUserList();

    connect(ui->btnAdd, &QPushButton::clicked, this, &UserManageDialog::onAddUser);
    connect(ui->btnEdit, &QPushButton::clicked, this, &UserManageDialog::onEditUser);
    connect(ui->btnDelete, &QPushButton::clicked, this, &UserManageDialog::onDeleteUser);
    connect(ui->tableUsers, &QTableWidget::itemSelectionChanged, this, &UserManageDialog::onUserSelected);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &UserManageDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &UserManageDialog::reject);

    clearForm();
    // 连接用户列表变化信号，自动刷新
    connect(&UserManager::instance(), &UserManager::userListChanged, this, &UserManageDialog::refreshUserList);
}

UserManageDialog::~UserManageDialog()
{
    delete ui;
}

void UserManageDialog::clearForm()
{
    m_usernameEdit->clear();
    m_roleCombo->setCurrentIndex(0);
    m_enabledCheck->setChecked(true);
    m_departmentEdit->clear();
    m_phoneEdit->clear();
    m_emailEdit->clear();
    ui->btnEdit->setEnabled(false);
    ui->btnDelete->setEnabled(false);
}

void UserManageDialog::refreshUserList()
{
    QTableWidget *table = ui->tableUsers;
    table->clear();
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({"ID", "用户名", "角色", "启用"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);

    QVector<UserInfo> users = UserManager::instance().getAllUsers();
    table->setRowCount(users.size());
    for (int i = 0; i < users.size(); ++i) {
        const UserInfo& u = users[i];
        table->setItem(i, 0, new QTableWidgetItem(QString::number(u.id)));
        table->setItem(i, 1, new QTableWidgetItem(u.username));
        table->setItem(i, 2, new QTableWidgetItem(userRoleToString(u.role)));
        table->setItem(i, 3, new QTableWidgetItem(u.enabled ? "是" : "否"));
    }
    table->resizeColumnsToContents();
}

void UserManageDialog::onUserSelected()
{
    int currentRow = ui->tableUsers->currentRow();
    if (currentRow < 0) {
        clearForm();
        return;
    }

    ui->btnEdit->setEnabled(true);
    ui->btnDelete->setEnabled(true);

    int userId = ui->tableUsers->item(currentRow, 0)->text().toInt();
    QString username = ui->tableUsers->item(currentRow, 1)->text();
    QString roleStr = ui->tableUsers->item(currentRow, 2)->text();
    bool enabled = (ui->tableUsers->item(currentRow, 3)->text() == "是");

    m_usernameEdit->setText(username);
    int index = m_roleCombo->findText(roleStr);
    if (index >= 0) m_roleCombo->setCurrentIndex(index);
    m_enabledCheck->setChecked(enabled);
}

void UserManageDialog::onAddUser()
{
    showUserDialog(-1);
}

void UserManageDialog::onEditUser()
{
    int row = ui->tableUsers->currentRow();
    if (row < 0) return;
    int userId = ui->tableUsers->item(row, 0)->text().toInt();
    showUserDialog(userId);
}

void UserManageDialog::onDeleteUser()
{
    int row = ui->tableUsers->currentRow();
    if (row < 0) return;
    int userId = ui->tableUsers->item(row, 0)->text().toInt();
    QString username = ui->tableUsers->item(row, 1)->text();
    if (username == UserManager::instance().currentUsername()) {
        QMessageBox::warning(this, "错误", "不能删除当前登录用户");
        return;
    }
    if (QMessageBox::question(this, "确认删除", QString("确定要删除用户 %1 吗？").arg(username))
        == QMessageBox::Yes) {
        if (UserManager::instance().deleteUser(userId)) {
            refreshUserList();
            clearForm();
        } else {
            QMessageBox::warning(this, "错误", "删除用户失败");
        }
    }
}

void UserManageDialog::showUserDialog(int userId)
{
    QDialog dialog(this);
    dialog.setWindowTitle(userId == -1 ? "添加用户" : "编辑用户");
    QFormLayout *layout = new QFormLayout(&dialog);

    QLineEdit *usernameEdit = new QLineEdit(&dialog);
    QLineEdit *passwordEdit = new QLineEdit(&dialog);
    passwordEdit->setEchoMode(QLineEdit::Password);
    QComboBox *roleCombo = new QComboBox(&dialog);
    roleCombo->addItem(userRoleToString(UserRole::ADMIN));
    roleCombo->addItem(userRoleToString(UserRole::ENGINEER));
    roleCombo->addItem(userRoleToString(UserRole::OPERATOR));
    roleCombo->addItem(userRoleToString(UserRole::VIEWER));
    QCheckBox *enabledCheck = new QCheckBox("启用", &dialog);
    enabledCheck->setChecked(true);

    layout->addRow("用户名:", usernameEdit);
    layout->addRow("密码:", passwordEdit);
    layout->addRow("角色:", roleCombo);
    layout->addRow("状态:", enabledCheck);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addRow(buttonBox);

    if (userId != -1) {
        // 编辑模式：预填信息
        usernameEdit->setText(m_usernameEdit->text());
        usernameEdit->setEnabled(false);
        passwordEdit->setPlaceholderText("留空则不修改密码");
        int idx = roleCombo->findText(m_roleCombo->currentText());
        if (idx >= 0) roleCombo->setCurrentIndex(idx);
        enabledCheck->setChecked(m_enabledCheck->isChecked());
    }

    connect(buttonBox, &QDialogButtonBox::accepted, [&]() {
        QString username = usernameEdit->text().trimmed();
        QString password = passwordEdit->text();
        UserRole role = stringToUserRole(roleCombo->currentText());
        bool enabled = enabledCheck->isChecked();

        if (username.isEmpty()) {
            QMessageBox::warning(&dialog, "错误", "用户名不能为空");
            return;
        }
        if (userId == -1 && password.isEmpty()) {
            QMessageBox::warning(&dialog, "错误", "密码不能为空");
            return;
        }

        bool ok = false;
        if (userId == -1) {
            ok = UserManager::instance().addUser(username, password, role);
            if (!ok) {
                QMessageBox::warning(&dialog, "错误", "添加用户失败，用户名可能已存在");
                return;
            }
        } else {
            ok = UserManager::instance().updateUser(userId, username, password, role, enabled);
            if (!ok) {
                QMessageBox::warning(&dialog, "错误", "更新用户失败");
                return;
            }
        }
        dialog.accept();
    });

    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        refreshUserList();
        // 刷新后选中刚刚操作的用户
        if (userId == -1) {
            ui->tableUsers->setCurrentCell(ui->tableUsers->rowCount() - 1, 0);
        } else {
            for (int i = 0; i < ui->tableUsers->rowCount(); ++i) {
                if (ui->tableUsers->item(i, 0)->text().toInt() == userId) {
                    ui->tableUsers->setCurrentCell(i, 0);
                    break;
                }
            }
        }
    }
}