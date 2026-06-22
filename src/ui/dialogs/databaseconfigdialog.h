#ifndef DATABASECONFIGDIALOG_H
#define DATABASECONFIGDIALOG_H

#include <QDialog>

class QLineEdit;
class QPushButton;
class QCheckBox;

class DatabaseConfigDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DatabaseConfigDialog(QWidget* parent = nullptr);

private slots:
    void onTestConnection();
    void onSave();

private:
    QLineEdit* m_hostEdit;
    QLineEdit* m_portEdit;
    QLineEdit* m_dbEdit;
    QLineEdit* m_userEdit;
    QLineEdit* m_pwdEdit;
    QPushButton* m_testBtn;
    QPushButton* m_saveBtn;
    QPushButton* m_cancelBtn;

    bool testConnection(const QString& host, int port, const QString& dbName,
                        const QString& user, const QString& pwd);
};

#endif // DATABASECONFIGDIALOG_H