// src/view/ProcessViewTypes.h — 工艺视图专用数据结构
#pragma once

#include <QString>
#include <QList>
#include <QVariantMap>

// 状态绑定 — 一个 tagKey 驱动图元的一种视觉状态
struct PVStatusBinding {
    QString tagKey;          // "deviceId/tagName"
    QString type;            // "color" | "dynamicColor" | "opacity"
    QString trueColor;       // true 时颜色，如 "#00cc00"
    QString falseColor;      // false 时颜色，如 "#cc0000"，空=不变
    QString calc;            // dynamicColor 时的计算函数名
};

// 控制面板中的监视标签
struct PVMonitorTag {
    QString tagKey;          // "deviceId/tagName"
    QString label;
    QString unit;
};

// 控制面板配置
struct PVControlPanel {
    QString type;            // "switch" | "analog" | "enum"
    QString writeTagKey;     // 写入目标 tagKey
    QString feedbackTagKey;  // 反馈确认 tagKey
    QString onLabel;
    QString offLabel;
    QString unit;
    double min = 0.0;
    double max = 100.0;
    double step = 1.0;
    int timeout = 5000;
    QList<PVMonitorTag> monitorTags;
};

// 右键菜单项
struct PVContextMenuItem {
    QString text;
    QString action;          // "writeTag" | "showDetail" | "showControlPanel"
    QVariantMap params;      // 含 "tagKey" 和 "value"
    bool confirm = false;
    QString confirmMsg;
};

// 映射条目 — 一个 SVG 元素的完整交互配置
struct PVMappingEntry {
    QString svgId;
    QString tagKey;          // 主绑定标签
    QString labelName;
    QString deviceType;      // "pump" | "valve_regulating" | "valve_onoff" | "tank" | "pipe"
    QString clickAction;
    QString dblClickAction;
    QString tooltip;
    QList<PVContextMenuItem> contextMenu;
    PVControlPanel controlPanel;
    QList<PVStatusBinding> statusBindings; // 可多个 tagKey 驱动同一图元
};

// 互锁条件
struct PVInterlockCondition {
    QString tagKey;
    QString op;
    double value = 0.0;
    QString message;
};

// 互锁规则
struct PVInterlockRule {
    QString svgId;
    QString action;          // "start" | "stop" | "write"
    QList<PVInterlockCondition> conditions;
};
