#ifndef USERMANAGER_H
#define USERMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include "common_types.h"

class UserManager : public QObject
{
    Q_OBJECT
public:
    static UserManager& instance();

    // 同步初始化（打开数据库、创建表、创建默认管理员）
    bool initialize();

    // 同步登录
    bool login(const QString& username, const QString& password);

    // 同步获取所有用户
    QVector<UserInfo> getAllUsers() const;

    // 同步添加用户
    bool addUser(const QString& username, const QString& password, UserRole role);

    // 同步更新用户
    bool updateUser(int userId, const QString& username, const QString& password, UserRole role, bool enabled);

    // 同步删除用户
    bool deleteUser(int userId);

    // 同步修改密码
    // 当前登录状态（内存中）
    bool isLoggedIn() const { return m_currentUser.id != -1; }
    UserRole currentUserRole() const { return m_currentUser.role; }
    QString currentUsername() const { return m_currentUser.username; }
    void logout();

    // 权限检查
    bool hasPermission(UserRole requiredRole) const;
    bool canManageUsers() const;
    bool canManageTags() const;
    bool canWriteData() const;
    bool canConfigureSystem() const;

signals:
    void userLoggedIn(const QString& username);
    void userLoggedOut();
    void userListChanged();   // 当用户列表变更时发出

private:
    explicit UserManager(QObject* parent = nullptr);
    ~UserManager();

    bool openDatabase();
    bool createTable();
    QString hashPassword(const QString& password) const;

    QSqlDatabase m_db;
    UserInfo m_currentUser;
};

#endif // USERMANAGER_H