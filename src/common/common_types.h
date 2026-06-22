// common_types.h
#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <QString>
#include <QDateTime>
#include <QVariant>
#include <QSet>
#include <QMap>

enum class UserRole {
    ADMIN = 0,      // 管理员：所有权限
    ENGINEER = 1,   // 工程师：可查看、写入标签、管理标签、配置系统（不能管理用户）
    OPERATOR = 2,   // 操作员：可查看、写入标签（不能管理标签和系统配置）
    VIEWER = 3     // 观察者：仅查看，不能写入
};
inline QString userRoleToString(UserRole role)
{
    switch (role) {
    case UserRole::ADMIN:    return "管理员";
    case UserRole::ENGINEER: return "工程师";
    case UserRole::OPERATOR: return "操作员";
    case UserRole::VIEWER:   return "观察者";
    }
    return "未知";
}
inline UserRole stringToUserRole(const QString& str)
{
    if (str == "管理员")   return UserRole::ADMIN;
    if (str == "工程师")   return UserRole::ENGINEER;
    if (str == "操作员")   return UserRole::OPERATOR;
    if (str == "观察者")   return UserRole::VIEWER;
    return UserRole::VIEWER;
}
// 获取角色等级（数值越大权限越高）
inline int roleLevel(UserRole role)
{
    switch (role) {
    case UserRole::ADMIN:    return 3;
    case UserRole::ENGINEER: return 2;
    case UserRole::OPERATOR: return 1;
    case UserRole::VIEWER:   return 0;
    }
    return 0;
}

struct UserInfo {
    int id = -1;
    QString username;
    QString passwordHash;
    UserRole role = UserRole::VIEWER;
    bool enabled = false;
};

// 控制器状态枚举
enum class ControllerState {
    DISCONNECTED,   // 未连接
    CONNECTING,     // 连接中
    CONNECTED,      // 已连接但未轮询
    POLLING,        // 正在轮询
    PAUSED,         // 轮询暂停
    STATE_ERROR,          // 错误状态
    RECONNECTING,   // 重连中
    SHUTTING_DOWN   // 正在关闭
};

// PLC连接状态枚举
enum class PLCConnectionState {
    NOT_CONNECTED,   // 未连接
    CONNECTING,      // 连接中
    CONNECTED,       // 已连接
    DISCONNECTING,   // 正在断开
    DISCONNECTED,    // 已断开
    CONNECTION_FAILED, // 连接失败
    CONNECTION_LOST    // 连接丢失
};

// 报警级别枚举
enum class AlertLevel {
    INFO,      // 信息
    WARNING,   // 警告
    ERRORAlert,     // 错误
    CRITICAL   // 严重
};

// 计时器类型
enum class TimerType {
    NONE,
    HEARTBEAT,      // 心跳检测
    AUTO_CONNECT,   // 自动连接
    RECONNECT,      // 自动重连
    POLLING         // 轮询
};

// 视图类型枚举
enum class ViewType {
    TABLE_VIEW = 0,     // 表格视图
    CHART_VIEW = 1,     // 图表视图
    PROCESS_VIEW = 2,   // 工艺视图
    TREND_VIEW = 3,     // 趋势视图/历史视图
    VIEW_COUNT
};

// 流程状态枚举
enum class ProcessState {
    RUNNING,    // 运行中
    STOPPED,    // 已停止
    ALARM,      // 报警
    MAINTENANCE,// 维护
    OFFLINE     // 离线
};

// 数据质量枚举
enum class DataQuality {
    GOOD,       // 良好
    BAD,        // 不良
    UNCERTAIN,  // 不确定
    UNKNOWN     // 未知
};

// 标签数据类型枚举
enum class TagDataType {
    UNKNOWSN = 0,
    BOOL = 1,       // 布尔
    BYTE = 2,       // 字节
    WORD = 3 ,       // 字
    DWORD = 4,      // 双字
    INT = 5,        // 整数
    DINT = 6 ,       // 双整数
    REAL = 7 ,       // 实数
    STRING = 8,     // 字符串
    TIMER = 9 ,      // 定时器
    COUNTER = 10     // 计数器
};

// 报警配置结构体
struct AlertConfig {
    QString tagName;       // 标签名称
    bool minEnabled;       // 是否启用下限报警
    double minValue;       // 下限值
    bool maxEnabled;       // 是否启用上限报警
    double maxValue;       // 上限值
    AlertLevel level;      // 报警级别
    QString description;   // 报警描述

    AlertConfig()
        : minEnabled(false)
        , minValue(0.0)
        , maxEnabled(false)
        , maxValue(0.0)
        , level(AlertLevel::INFO)
    {
    }
};

// ==================================================
// 原有的 TagInfo 结构体保持不变
// ==================================================
struct TagInfo {
    QString name;
    QString address;
    int dbNumber = 0;
    int byteOffset = 0;
    int bitOffset = -1;
    TagDataType dataType = TagDataType::BOOL;
    bool writable = false;
    bool dbEnabled = false;
    QString description;
    double scalingFactor = 1.0;
    double offset = 0.0;
    QString unit;
    QVariant value;
    QVariant defaultValue;
    QDateTime lastUpdate;
    DataQuality quality = DataQuality::GOOD;
    AlertConfig alertConfig;

    bool operator==(const TagInfo& other) const { return name == other.name; }
    bool operator!=(const TagInfo& other) const { return !(*this == other); }
};
// ==================================================
// 新增：设备配置结构体
// ==================================================
// 标签组，支持树形结构
struct TagGroup {
    QString groupName;
    QVector<TagInfo> tags;
    QVector<TagGroup*> subGroups;

    TagGroup() = default;
    explicit TagGroup(const QString& name) : groupName(name) {}
    ~TagGroup() { qDeleteAll(subGroups); }
    // 禁用拷贝或实现深拷贝，视需要
    TagGroup(const TagGroup&) = delete;
    TagGroup& operator=(const TagGroup&) = delete;
    TagGroup(TagGroup&&) = default;
    TagGroup& operator=(TagGroup&&) = default;
};

struct DeviceConfig {
    QString deviceId;           // 设备唯一ID (内部使用)
    QString deviceName;         // 设备显示名称
    QString ip;           // IP地址
    int rack = 0;         // 机架号
    int slot = 2;         // 插槽号
    int port = 102;       // 端口号
    int pollingInterval = 1000; // 轮询间隔
    QVector<TagGroup*> rootGroups; // 根组列表

    // ---- 新增字段：Modbus 多协议支持 ----
    QString protocol = "S7";       // 协议类型: "S7" / "ModbusTCP" / "ModbusRTU"
    int     modbusSlaveId = 1;     // Modbus 从站地址（TCP Unit ID / RTU Slave ID）

    // Modbus RTU 专用串口参数
    QString serialPort;            // 串口名: "COM3" / "/dev/ttyUSB0"
    int     baudRate   = 9600;     // 波特率
    int     dataBits   = 8;        // 数据位
    int     stopBits   = 1;        // 停止位
    QString parity     = "N";      // 校验位: "N" / "E" / "O"

    // 构造函数
    DeviceConfig() = default;
    explicit DeviceConfig(const QString& deviceId) : deviceId(deviceId) {}
    ~DeviceConfig() { qDeleteAll(rootGroups); }
    // 禁用拷贝或实现深拷贝，视需要
    DeviceConfig(const DeviceConfig&) = delete;
    DeviceConfig& operator=(const DeviceConfig&) = delete;
    DeviceConfig(DeviceConfig&&) = default;
    DeviceConfig& operator=(DeviceConfig&&) = default;
};

// 报警信息结构体
struct AlertInfo {
    int id;                 // 报警ID
    QDateTime timestamp;    // 时间戳
    QString tagName;        // 标签名称
    QString message;        // 报警信息
    QVariant value;         // 触发值
    AlertLevel level;       // 报警级别
    bool acknowledged;      // 是否已确认
    QString operatorName;   // 操作员名称
    QDateTime acknowledgeTime; // 确认时间

    AlertInfo()
        : id(0)
        , level(AlertLevel::INFO)
        , acknowledged(false)
    {
    }
};

// PLC连接配置
struct PLCConfig {
    QString ip;             // IP地址
    int rack;               // 机架号
    int slot;               // 插槽号
    int port;               // 端口号
    int pollingInterval;    // 轮询间隔（毫秒）
    int timeout;            // 超时时间（毫秒）
    bool autoConnect;       // 自动连接
    bool autoStartPolling;  // 自动开始轮询
    QString stationName;    // 站点名称
    QString description;    // 描述

    // 新增 MySQL 配置字段
    QString mysqlHost;
    int mysqlPort;
    QString mysqlDatabase;
    QString mysqlUser;
    QString mysqlPassword;

    PLCConfig()
        : ip("192.168.0.1")
        , rack(0)
        , slot(2)
        , port(102)
        , pollingInterval(1000)
        , timeout(5000)
        , autoConnect(true)
        , autoStartPolling(true)
        , mysqlHost("")
        , mysqlPort(3306)
        , mysqlDatabase("")
        , mysqlUser("")
        , mysqlPassword("")
    {
    }
};

// 实时数据点
struct DataPoint {
    QDateTime timestamp;    // 时间戳
    QVariant value;         // 数值
    DataQuality quality;    // 数据质量
    QString tagName;        // 标签名称
    QString description;    // 描述

    DataPoint()
        : quality(DataQuality::GOOD)
    {
    }
};

// 用于数据库持久化的数据点结构体（不同于 DataPoint）
struct DbDataPoint {
    qint64 dataId;
    QString tagName;
    double value;
    int quality;
    QDateTime timestamp;
};

// 转换辅助函数声明
inline QString controllerStateToString(ControllerState state) {
    switch (state) {
    case ControllerState::DISCONNECTED:   return "未连接";
    case ControllerState::CONNECTING:     return "连接中";
    case ControllerState::CONNECTED:      return "已连接";
    case ControllerState::POLLING:        return "轮询中";
    case ControllerState::PAUSED:         return "已暂停";
    case ControllerState::STATE_ERROR:          return "错误";
    case ControllerState::RECONNECTING:   return "重连中";
    case ControllerState::SHUTTING_DOWN:  return "正在关闭";
    default:                              return "未知状态";
    }
}

inline QString plcConnectionStateToString(PLCConnectionState state) {
    switch (state) {
    case PLCConnectionState::NOT_CONNECTED:   return "未连接";
    case PLCConnectionState::CONNECTING:      return "连接中";
    case PLCConnectionState::CONNECTED:       return "已连接";
    case PLCConnectionState::DISCONNECTING:   return "正在断开";
    case PLCConnectionState::DISCONNECTED:    return "已断开";
    case PLCConnectionState::CONNECTION_FAILED: return "连接失败";
    case PLCConnectionState::CONNECTION_LOST:   return "连接丢失";
    default:                                  return "未知连接状态";
    }
}

inline QString alertLevelToString(AlertLevel level) {
    switch (level) {
    case AlertLevel::INFO:      return "信息";
    case AlertLevel::WARNING:   return "警告";
    case AlertLevel::ERRORAlert:     return "错误";
    case AlertLevel::CRITICAL:  return "严重";
    default:                    return "未知";
    }
}

inline QString tagDataTypeToString(TagDataType type) {
    switch (type) {
    case TagDataType::BOOL:     return "BOOL";
    case TagDataType::BYTE:     return "BYTE";
    case TagDataType::WORD:     return "WORD";
    case TagDataType::DWORD:    return "DWORD";
    case TagDataType::INT:      return "INT";
    case TagDataType::DINT:     return "DINT";
    case TagDataType::REAL:     return "REAL";
    case TagDataType::STRING:   return "STRING";
    case TagDataType::TIMER:    return "TIMER";
    case TagDataType::COUNTER:  return "COUNTER";
    default:                    return "UNKNOWN";
    }
}

inline QString processStateToString(ProcessState state) {
    switch (state) {
    case ProcessState::RUNNING:     return "运行中";
    case ProcessState::STOPPED:     return "已停止";
    case ProcessState::ALARM:       return "报警";
    case ProcessState::MAINTENANCE: return "维护";
    case ProcessState::OFFLINE:     return "离线";
    default:                        return "未知";
    }
}

inline QString dataQualityToString(DataQuality quality) {
    switch (quality) {
    case DataQuality::GOOD:      return "良好";
    case DataQuality::BAD:       return "不良";
    case DataQuality::UNCERTAIN: return "不确定";
    case DataQuality::UNKNOWN:   return "未知";
    default:                     return "无效";
    }
}

inline TagDataType stringToTagDataType(const QString& str) {
    QString upperStr = str.toUpper();
    if (upperStr == "BOOL" || upperStr == "BOOLEAN") return TagDataType::BOOL;
    if (upperStr == "BYTE") return TagDataType::BYTE;
    if (upperStr == "WORD") return TagDataType::WORD;
    if (upperStr == "DWORD") return TagDataType::DWORD;
    if (upperStr == "INT") return TagDataType::INT;
    if (upperStr == "DINT") return TagDataType::DINT;
    if (upperStr == "REAL" || upperStr == "FLOAT") return TagDataType::REAL;
    if (upperStr == "STRING") return TagDataType::STRING;
    if (upperStr == "TIMER") return TagDataType::TIMER;
    if (upperStr == "COUNTER") return TagDataType::COUNTER;
    return TagDataType::BOOL;  // 默认值
}

inline AlertLevel stringToAlertLevel(const QString& str) {
    QString upperStr = str.toUpper();
    if (upperStr == "INFO") return AlertLevel::INFO;
    if (upperStr == "WARNING") return AlertLevel::WARNING;
    if (upperStr == "ERROR") return AlertLevel::ERRORAlert;
    if (upperStr == "CRITICAL") return AlertLevel::CRITICAL;
    return AlertLevel::INFO;  // 默认值
}

// 状态转换验证函数声明
inline bool isValidStateTransition(ControllerState from, ControllerState to) {
    // 定义静态转换规则映射
    static const QMap<ControllerState, QSet<ControllerState>> validTransitions = {
        {ControllerState::DISCONNECTED,   {ControllerState::CONNECTING, ControllerState::STATE_ERROR, ControllerState::SHUTTING_DOWN}},
        {ControllerState::CONNECTING,     {ControllerState::CONNECTED, ControllerState::DISCONNECTED, ControllerState::STATE_ERROR}},
        {ControllerState::CONNECTED,      {ControllerState::POLLING, ControllerState::DISCONNECTED, ControllerState::STATE_ERROR, ControllerState::SHUTTING_DOWN}},
        {ControllerState::POLLING,        {ControllerState::PAUSED, ControllerState::CONNECTED, ControllerState::DISCONNECTED, ControllerState::STATE_ERROR}},
        {ControllerState::PAUSED,         {ControllerState::POLLING, ControllerState::CONNECTED, ControllerState::DISCONNECTED, ControllerState::STATE_ERROR}},
        {ControllerState::STATE_ERROR,          {ControllerState::DISCONNECTED, ControllerState::RECONNECTING, ControllerState::SHUTTING_DOWN}},
        {ControllerState::RECONNECTING,   {ControllerState::CONNECTING, ControllerState::DISCONNECTED, ControllerState::STATE_ERROR}},
        {ControllerState::SHUTTING_DOWN,  {ControllerState::DISCONNECTED}}
    };

    return validTransitions.value(from).contains(to);
}

#endif // COMMON_TYPES_H