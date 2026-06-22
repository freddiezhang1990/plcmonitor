#include "usermanager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>

UserManager& UserManager::instance()
{
    static UserManager instance;
    return instance;
}

UserManager::UserManager(QObject* parent) : QObject(parent)
{
}

UserManager::~UserManager()
{
    if (m_db.isOpen())
        m_db.close();
}

bool UserManager::openDatabase()
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dataDir);
    if (!dir.exists())
        dir.mkpath(".");
    QString dbPath = dataDir + "/users.db";
    m_db = QSqlDatabase::addDatabase("QSQLITE", "user_conn");
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        qWarning() << "Failed to open user database:" << m_db.lastError().text();
        return false;
    }
    return true;
}

bool UserManager::createTable()
{
    QSqlQuery query(m_db);
    QString sql = R"(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            password_hash TEXT NOT NULL,
            role TEXT NOT NULL,
            enabled INTEGER NOT NULL DEFAULT 1
        )
    )";
    if (!query.exec(sql)) {
        qWarning() << "Failed to create users table:" << query.lastError().text();
        return false;
    }
    return true;
}

QString UserManager::hashPassword(const QString& password) const
{
    QByteArray salted = password.toUtf8();
    QByteArray hash = QCryptographicHash::hash(salted, QCryptographicHash::Sha256);
    return hash.toHex();
}

bool UserManager::initialize()
{
    if (!openDatabase())
        return false;
    if (!createTable())
        return false;

    // 检查默认管理员
    QSqlQuery query(m_db);
    query.exec("SELECT COUNT(*) FROM users");
    if (query.next() && query.value(0).toInt() == 0) {
        QString adminHash = hashPassword("admin");
        QSqlQuery insert(m_db);
        insert.prepare("INSERT INTO users (username, password_hash, role, enabled) VALUES (?, ?, ?, ?)");
        insert.addBindValue("admin");
        insert.addBindValue(adminHash);
        insert.addBindValue(userRoleToString(UserRole::ADMIN));
        insert.addBindValue(1);
        if (!insert.exec()) {
            qWarning() << "Failed to create default admin user:" << insert.lastError().text();
            return false;
        }
        qDebug() << "Default admin user created (username: admin, password: admin)";
    }
    return true;
}

bool UserManager::login(const QString& username, const QString& password)
{
    qDebug() << "UserManager::login called from" << Q_FUNC_INFO;
    QSqlQuery query(m_db);
    query.prepare("SELECT id, username, password_hash, role, enabled FROM users WHERE username = ?");
    query.addBindValue(username);
    if (!query.exec() || !query.next()) {
        qWarning() << "Login failed: user not found" << username;
        return false;
    }

    int id = query.value(0).toInt();
    QString storedHash = query.value(2).toString();
    bool enabled = query.value(4).toInt();
    if (!enabled) {
        qWarning() << "Login failed: user disabled" << username;
        return false;
    }

    if (hashPassword(password) != storedHash) {
        qWarning() << "Login failed: wrong password" << username;
        return false;
    }

    m_currentUser.id = id;
    m_currentUser.username = query.value(1).toString();
    m_currentUser.role = stringToUserRole(query.value(3).toString());
    m_currentUser.enabled = enabled;

    emit userLoggedIn(m_currentUser.username);
    return true;
}

void UserManager::logout()
{
    qDebug() << "UserManager::logout() 被调用，当前用户:" << m_currentUser.username;
    m_currentUser = UserInfo();
    emit userLoggedOut();
    qDebug() << "logout 后 isLoggedIn()=" << isLoggedIn();
}

bool UserManager::hasPermission(UserRole requiredRole) const
{
    if (!isLoggedIn()) return false;
    if (m_currentUser.role == UserRole::ADMIN) return true;
    return roleLevel(m_currentUser.role) >= roleLevel(requiredRole);
}

QVector<UserInfo> UserManager::getAllUsers() const
{
    QVector<UserInfo> users;
    QSqlQuery query(m_db);
    query.exec("SELECT id, username, password_hash, role, enabled FROM users ORDER BY id");
    while (query.next()) {
        UserInfo info;
        info.id = query.value(0).toInt();
        info.username = query.value(1).toString();
        info.passwordHash = query.value(2).toString();
        info.role = stringToUserRole(query.value(3).toString());
        info.enabled = query.value(4).toInt();
        users.append(info);
    }
    return users;
}

bool UserManager::addUser(const QString& username, const QString& password, UserRole role)
{
    if (username.isEmpty() || password.isEmpty())
        return false;
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO users (username, password_hash, role, enabled) VALUES (?, ?, ?, ?)");
    query.addBindValue(username);
    query.addBindValue(hashPassword(password));
    query.addBindValue(userRoleToString(role));
    query.addBindValue(1);
    if (!query.exec()) {
        qWarning() << "Add user failed:" << query.lastError().text();
        return false;
    }
    emit userListChanged();
    return true;
}

bool UserManager::updateUser(int userId, const QString& username, const QString& password, UserRole role, bool enabled)
{
    if (username.isEmpty())
        return false;
    QSqlQuery query(m_db);
    if (password.isEmpty()) {
        query.prepare("UPDATE users SET username = ?, role = ?, enabled = ? WHERE id = ?");
        query.addBindValue(username);
        query.addBindValue(userRoleToString(role));
        query.addBindValue(enabled ? 1 : 0);
        query.addBindValue(userId);
    } else {
        query.prepare("UPDATE users SET username = ?, password_hash = ?, role = ?, enabled = ? WHERE id = ?");
        query.addBindValue(username);
        query.addBindValue(hashPassword(password));
        query.addBindValue(userRoleToString(role));
        query.addBindValue(enabled ? 1 : 0);
        query.addBindValue(userId);
    }
    if (!query.exec()) {
        qWarning() << "Update user failed:" << query.lastError().text();
        return false;
    }
    emit userListChanged();
    return true;
}

bool UserManager::deleteUser(int userId)
{
    if (userId == m_currentUser.id)
        return false;
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM users WHERE id = ?");
    query.addBindValue(userId);
    if (!query.exec()) {
        qWarning() << "Delete user failed:" << query.lastError().text();
        return false;
    }
    emit userListChanged();
    return true;
}

bool UserManager::canManageUsers() const
{
    return isLoggedIn() && m_currentUser.role == UserRole::ADMIN;
}

bool UserManager::canManageTags() const
{
    return isLoggedIn() && (m_currentUser.role == UserRole::ADMIN || m_currentUser.role == UserRole::ENGINEER);
}

bool UserManager::canWriteData() const
{
    return isLoggedIn() && roleLevel(m_currentUser.role) >= roleLevel(UserRole::OPERATOR);
}

bool UserManager::canConfigureSystem() const
{
    return isLoggedIn() && (m_currentUser.role == UserRole::ADMIN || m_currentUser.role == UserRole::ENGINEER);
}
