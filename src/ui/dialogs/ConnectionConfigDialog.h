#ifndef CONNECTIONCONFIGDIALOG_H
#define CONNECTIONCONFIGDIALOG_H

#include <QDialog>
#include <QTimer>

namespace Ui {
class ConnectionConfigDialog;
}

class ConnectionConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectionConfigDialog(QWidget *parent = nullptr);
    ~ConnectionConfigDialog();

    void setPLCParams(const QString& ip, int rack, int slot, int port, int timeout, int interval);
    QString getPLCIP() const;
    int getPLCRack() const;
    int getPLCSlot() const;
    int getPLCPort() const;
    int getPLCTimeout() const;
    int getPLCInterval() const;

protected:
    void accept() override;   // 重写确定按钮行为

private slots:
    void onTestConnection();
    void onParamsChanged();   // 参数修改时重置测试成功标志

private:
    bool testPLCConnection(const QString& ip, int rack, int slot, int port, int timeoutMs);
    void showTestingMessage(bool testing);

    Ui::ConnectionConfigDialog *ui;
    bool m_testSucceeded = false;
    QTimer *m_testTimer;      // 用于模拟测试超时（可选）
};

#endif // CONNECTIONCONFIGDIALOG_H