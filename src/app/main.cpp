// main.cpp
#include "application.h"
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QDebug>
#include <qtimer.h>

int main(int argc, char *argv[])
{
    Application app(argc, argv);
    // 设置工作目录
    QString appDir = QCoreApplication::applicationDirPath();
    QString projectRoot = QDir(appDir).absoluteFilePath("../../../");
    if (QDir::setCurrent(projectRoot)) {
        qDebug() << "Current working directory set to:" << QDir::currentPath();
    } else {
        qDebug() << "Failed to set working directory to:" << projectRoot;
        return -1;
    }
    // 检查配置文件是否存在
    QString configPath = "configs/plc_config.json";
    qDebug() << "Attempting to load config from:" << configPath;
    if (!QFile::exists(configPath)) {
        qCritical() << "ERROR: Config file not found at:" << QDir::currentPath() + "/" + configPath;
        return -1;
    } else {
        qDebug() << "Config file found. Proceeding to initialize application.";
    }

    // MYSQL禁用SSL
    qputenv("MYSQL_SSL", "0");
    qputenv("MYSQL_SSL_CA", "");
    qputenv("MYSQL_SSL_CAPATH", "");

    // 初始化应用程序
    if (!app.initialize()) {
        return -1;
    }
    // 进入事件循环
    return app.exec();
}