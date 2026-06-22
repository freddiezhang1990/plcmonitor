// ConnectionConfigDialog.h
#ifndef CONNECTIONCONFIGDIALOG_H
#define CONNECTIONCONFIGDIALOG_H

#include <QDialog>

namespace Ui {
class ConnectionConfigDialog;
}

class ConnectionConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectionConfigDialog(QWidget *parent = nullptr);
    ~ConnectionConfigDialog();

    // 设置当前参数
    void setPLCParams(const QString& ip, int rack, int slot, int port, int timeout, int interval);

    bool testPLCConnection(const QString& ip, int rack, int slot, int port, int timeoutMs);

    // 获取参数
    QString getPLCIP() const;
    int getPLCRack() const;
    int getPLCSlot() const;
    int getPLCPort() const;
    int getPLCTimeout() const;
    int getPLCInterval() const;

    void accept();

private slots:
    void onTestConnection();
    void onParamsChanged();

private:
    Ui::ConnectionConfigDialog *ui;

    bool m_testSucceeded;
};

#endif // CONNECTIONCONFIGDIALOG_H