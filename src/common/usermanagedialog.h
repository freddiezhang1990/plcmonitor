#ifndef USERMANAGEDIALOG_H
#define USERMANAGEDIALOG_H

#include <QDialog>
#include <qcheckbox.h>
#include <qcombobox.h>
#include <qlineedit.h>
#include "usermanager.h"

namespace Ui {
class UserManageDialog;
}

class UserManageDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UserManageDialog(QWidget *parent = nullptr);
    ~UserManageDialog();

private slots:
    void refreshUserList();
    void onUserSelected();
    void onAddUser();
    void onEditUser();
    void onDeleteUser();
    void clearForm();

private:
    void showUserDialog(int userId = -1);

    Ui::UserManageDialog *ui;
    QLineEdit *m_usernameEdit;
    QComboBox *m_roleCombo;
    QCheckBox *m_enabledCheck;
    QLineEdit *m_departmentEdit;
    QLineEdit *m_phoneEdit;
    QLineEdit *m_emailEdit;
};

#endif // USERMANAGEDIALOG_H