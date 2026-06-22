// src/ui/models/processviewmanager.cpp — SVG 工艺视图引擎 v2.0
#include "processviewmanager.h"
#include "plcdata.h"
#include "usermanager.h"
#include "common_types.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>
#include <QGraphicsColorizeEffect>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QDebug>
#include <QDialog>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QMouseEvent>

ProcessViewManager::ProcessViewManager(PLCData *plcData,
                                       UserManager *userManager,
                                       QObject *parent)
    : QObject(parent)
    , m_plcData(plcData)
    , m_userManager(userManager)
{
    qDebug() << "[ProcessView] v2.0 initialized";
}

ProcessViewManager::~ProcessViewManager()
{
    qDeleteAll(m_items);
    delete m_renderer;
    delete m_scene;
    qDebug() << "[ProcessView] destroyed";
}

// ================================================================
// 初始化
// ================================================================
void ProcessViewManager::setGraphicsView(QGraphicsView *view)
{
    m_view = view;
    if (!m_view) return;
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    // 如果有场景则复用，否则新建
    if (!m_scene) {
        m_scene = new QGraphicsScene(this);
    }
    m_view->setScene(m_scene);
    qDebug() << "[ProcessView] GraphicsView set";
}

bool ProcessViewManager::loadProject(const QString &svgPath,
                                     const QString &mappingPath,
                                     const QString &interlocksPath)
{
    qDebug() << "[ProcessView] Loading project:" << svgPath;

    m_svgPath = svgPath;
    m_mappingPath = mappingPath;

    // 清理旧状态
    m_mappings.clear();
    qDeleteAll(m_items);
    m_items.clear();
    m_interlocks.clear();
    m_tagKeyToSvgIds.clear();
    m_currentData.clear();
    m_projectTags.clear();

    // 加载 SVG 渲染器
    delete m_renderer;
    m_renderer = new QSvgRenderer(svgPath, this);
    if (!m_renderer->isValid()) {
        qWarning() << "[ProcessView] Failed to load SVG:" << svgPath;
        return false;
    }

    // 解析映射配置
    if (!parseMappingConfig(mappingPath)) {
        qWarning() << "[ProcessView] Failed to parse mapping:" << mappingPath;
        // 不退出，至少显示静态 SVG
    }

    // 解析互锁配置
    if (!interlocksPath.isEmpty()) {
        parseInterlockConfig(interlocksPath);
    }

    // 构建交互项
    if (m_scene) {
        m_scene->clear();
        buildInteractiveItems();
        m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
    }

    qDebug() << "[ProcessView] Project loaded. Mappings:" << m_mappings.size()
             << "Tags:" << m_projectTags.size();
    return true;
}

// ================================================================
// 配置解析
// ================================================================
bool ProcessViewManager::parseMappingConfig(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[ProcessView] Cannot open mapping:" << path;
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return false;
    QJsonObject root = doc.object();

    QJsonArray mappings = root["mappings"].toArray();
    for (const QJsonValue &v : mappings) {
        QJsonObject obj = v.toObject();
        PVMappingEntry entry;
        entry.svgId       = obj["svgId"].toString();
        entry.tagKey      = obj["tagKey"].toString();
        entry.labelName   = obj["labelName"].toString();
        entry.deviceType  = obj["deviceType"].toString();
        entry.clickAction = obj["clickAction"].toString();
        entry.dblClickAction = obj["dblClickAction"].toString();
        entry.tooltip     = obj["tooltip"].toString();

        // contextMenu
        QJsonArray ctxMenu = obj["contextMenu"].toArray();
        for (const QJsonValue &cmv : ctxMenu) {
            QJsonObject cm = cmv.toObject();
            PVContextMenuItem item;
            item.text     = cm["text"].toString();
            item.action   = cm["action"].toString();
            item.confirm  = cm["confirm"].toBool(false);
            item.confirmMsg = cm["confirmMsg"].toString();

            QJsonObject params = cm["params"].toObject();
            for (auto it = params.begin(); it != params.end(); ++it) {
                item.params[it.key()] = it.value().toVariant();
            }
            entry.contextMenu.append(item);
        }

        // controlPanel
        if (obj.contains("controlPanel") && !obj["controlPanel"].isNull()) {
            QJsonObject cp = obj["controlPanel"].toObject();
            entry.controlPanel.type        = cp["type"].toString();
            entry.controlPanel.writeTagKey  = cp["writeTagKey"].toString();
            entry.controlPanel.feedbackTagKey = cp["feedbackTagKey"].toString();
            entry.controlPanel.onLabel     = cp["onLabel"].toString();
            entry.controlPanel.offLabel    = cp["offLabel"].toString();
            entry.controlPanel.unit        = cp["unit"].toString();
            entry.controlPanel.min         = cp["min"].toDouble(0.0);
            entry.controlPanel.max         = cp["max"].toDouble(100.0);
            entry.controlPanel.step        = cp["step"].toDouble(1.0);
            entry.controlPanel.timeout     = cp["timeout"].toInt(5000);

            QJsonArray monTags = cp["monitorTags"].toArray();
            for (const QJsonValue &mtv : monTags) {
                QJsonObject mt = mtv.toObject();
                PVMonitorTag t;
                t.tagKey = mt["tagKey"].toString();
                t.label  = mt["label"].toString();
                t.unit   = mt["unit"].toString();
                entry.controlPanel.monitorTags.append(t);
            }
        }

        // statusBindings
        QJsonArray bindings = obj["statusBinding"].toArray();
        for (const QJsonValue &bv : bindings) {
            QJsonObject bo = bv.toObject();
            PVStatusBinding sb;
            sb.tagKey     = bo["tagKey"].toString();
            sb.type       = bo["type"].toString();
            sb.trueColor  = bo["trueColor"].toString();
            sb.falseColor = bo["falseColor"].toString();
            sb.calc       = bo["calc"].toString();
            entry.statusBindings.append(sb);
        }

        // 如果 statusBindings 为空且 tagKey 非空，自动添加默认颜色绑定
        if (entry.statusBindings.isEmpty() && !entry.tagKey.isEmpty()) {
            PVStatusBinding defaultBinding;
            defaultBinding.tagKey = entry.tagKey;
            defaultBinding.type = "color";
            defaultBinding.trueColor = "#00cc00";
            defaultBinding.falseColor = "#cc0000";
            entry.statusBindings.append(defaultBinding);
        }

        if (entry.svgId.isEmpty()) continue;
        m_mappings[entry.svgId] = entry;
        qDebug() << "[ProcessView] Mapped:" << entry.svgId << "→" << entry.tagKey;
    }

    return !m_mappings.isEmpty();
}

bool ProcessViewManager::parseInterlockConfig(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "[ProcessView] No interlocks config:" << path;
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return false;
    QJsonArray rules = doc.object()["interlocks"].toArray();

    for (const QJsonValue &v : rules) {
        QJsonObject r = v.toObject();
        PVInterlockRule rule;
        rule.svgId = r["svgId"].toString();
        rule.action = r["action"].toString();

        QJsonArray conds = r["conditions"].toArray();
        for (const QJsonValue &cv : conds) {
            QJsonObject c = cv.toObject();
            PVInterlockCondition cond;
            cond.tagKey  = c["tagKey"].toString();
            cond.op      = c["operator"].toString();
            cond.value   = c["value"].toDouble();
            cond.message = c["message"].toString();
            rule.conditions.append(cond);
        }

        m_interlocks[rule.svgId].append(rule);
    }
    qDebug() << "[ProcessView] Loaded interlocks for" << m_interlocks.size() << "elements";
    return true;
}

// ================================================================
// 构建交互项 + 反向索引
// ================================================================
void ProcessViewManager::buildInteractiveItems()
{
    if (!m_renderer || !m_scene) return;

    // 先渲染整个 SVG 作为背景
    auto *bgItem = new QGraphicsSvgItem();
    bgItem->setSharedRenderer(m_renderer);
    bgItem->setZValue(-1);
    m_scene->addItem(bgItem);

    QRectF bounds = bgItem->boundingRect();
    if (bounds.isValid()) {
        m_scene->setSceneRect(bounds);
    }

    // 为每个映射条目创建交互图元
    for (auto it = m_mappings.begin(); it != m_mappings.end(); ++it) {
        const QString &svgId = it.key();
        const PVMappingEntry &entry = it.value();

        if (!m_renderer->elementExists(svgId)) {
            qWarning() << "[ProcessView] SVG element not found:" << svgId;
            continue;
        }

        auto *item = new QGraphicsSvgItem();
        item->setSharedRenderer(m_renderer);
        item->setElementId(svgId);
        item->setFlag(QGraphicsItem::ItemIsSelectable, false);
        item->setAcceptHoverEvents(true);
        item->setCursor(Qt::PointingHandCursor);
        if (!entry.tooltip.isEmpty())
            item->setToolTip(entry.tooltip);
        else
            item->setToolTip(entry.labelName);
        item->setData(0, svgId); // 存储 svgId
        item->setZValue(1);
        m_scene->addItem(item);
        m_items[svgId] = item;
    }

    // 构建反向索引
    buildReverseIndex();

    // 如果场景有 contextMenuEvent，connect 的信号在构造函数中
    if (m_view) {
        m_view->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_view, &QWidget::customContextMenuRequested,
                this, &ProcessViewManager::onCustomContextMenu);
        // 双击事件通过 viewport 的 mouseDoubleClickEvent 处理
        m_view->viewport()->installEventFilter(this);
    }

    qDebug() << "[ProcessView] Built" << m_items.size() << "interactive items";
}

void ProcessViewManager::buildReverseIndex()
{
    m_tagKeyToSvgIds.clear();
    m_projectTags.clear();

    for (auto it = m_mappings.begin(); it != m_mappings.end(); ++it) {
        const QString &svgId = it.key();
        const PVMappingEntry &entry = it.value();

        // 主 tagKey
        if (!entry.tagKey.isEmpty() && !m_projectTags.contains(entry.tagKey)) {
            m_projectTags.append(entry.tagKey);
        }

        // 状态绑定中的 tagKey
        for (const PVStatusBinding &binding : entry.statusBindings) {
            if (!binding.tagKey.isEmpty()) {
                m_tagKeyToSvgIds[binding.tagKey].append(svgId);
                if (!m_projectTags.contains(binding.tagKey))
                    m_projectTags.append(binding.tagKey);
            }
        }
    }
    emit processTagsChanged(m_projectTags);
}

// ================================================================
// 兼容旧接口
// ================================================================
void ProcessViewManager::setProcessTags(const QStringList &tags)
{
    // v2.0: 旧接口保留兼容，但不做额外处理（由 loadProject 的 mapping 配置驱动）
    Q_UNUSED(tags);
}

QStringList ProcessViewManager::getProcessTags() const
{
    return m_projectTags;
}

QMap<QString, QVariant> ProcessViewManager::getProcessStatus() const
{
    return m_currentData;
}

void ProcessViewManager::updateProcessData(const QMap<QString, QVariant> &data)
{
    if (data.isEmpty()) return;
    for (auto it = data.begin(); it != data.end(); ++it) {
        m_currentData[it.key()] = it.value();
    }
    emit processDataUpdated(data);
}

// ================================================================
// 标签值变化处理（核心）
// ================================================================
void ProcessViewManager::onTagValueChanged(const QString &deviceId,
                                            const QString &tagName,
                                            const QVariant &value)
{
    QString tagKey = deviceId + "/" + tagName;
    m_currentData[tagKey] = value;

    // 查找受此 tagKey 影响的所有图元
    const QStringList &svgIds = m_tagKeyToSvgIds.value(tagKey);
    for (const QString &svgId : svgIds) {
        if (!m_mappings.contains(svgId)) continue;
        const PVMappingEntry &entry = m_mappings[svgId];
        for (const PVStatusBinding &binding : entry.statusBindings) {
            if (binding.tagKey == tagKey) {
                applyStatusBinding(svgId, binding, value);
            }
        }
    }
    emit processDataUpdated({{tagKey, value}});
}

// ================================================================
// 状态渲染
// ================================================================
void ProcessViewManager::applyStatusBinding(const QString &svgId,
                                             const PVStatusBinding &binding,
                                             const QVariant &value)
{
    if (!m_items.contains(svgId)) return;
    QGraphicsSvgItem *item = m_items[svgId];

    if (binding.type == "color") {
        // 布尔值颜色映射
        bool val = value.toBool();
        QString colorStr = val ? binding.trueColor : binding.falseColor;
        if (colorStr.isEmpty()) return; // falseColor 为空则不变

        // 移除旧效果，应用新颜色
        auto *oldEffect = dynamic_cast<QGraphicsColorizeEffect*>(
            item->graphicsEffect());
        if (oldEffect) delete oldEffect;

        auto *effect = new QGraphicsColorizeEffect();
        effect->setColor(QColor(colorStr));
        effect->setStrength(0.7);
        item->setGraphicsEffect(effect);

    } else if (binding.type == "dynamicColor") {
        QColor color;
        if (binding.calc == "valveColorByPosition") {
            color = valveColorByPosition(value);
        } else {
            color = QColor("#666666"); // 默认灰色
        }
        auto *effect = new QGraphicsColorizeEffect();
        effect->setColor(color);
        effect->setStrength(0.7);
        QGraphicsEffect *old = item->graphicsEffect();
        if (old) delete old;
        item->setGraphicsEffect(effect);

    } else if (binding.type == "opacity") {
        double val = value.toDouble();
        item->setOpacity(qBound(0.0, val, 1.0));
    }
}

QColor ProcessViewManager::valveColorByPosition(const QVariant &value)
{
    // 位置 0-100%，0=红色, 50=黄色, 100=绿色
    double pos = value.toDouble();
    pos = qBound(0.0, pos, 100.0);
    if (pos < 50)
        return QColor::fromRgbF(pos / 50.0, 0.0, 1.0 - pos / 50.0);
    else
        return QColor::fromRgbF(1.0 - (pos - 50.0) / 50.0, 1.0, 0.0);
}

// ================================================================
// 右键菜单
// ================================================================
void ProcessViewManager::onCustomContextMenu(const QPoint &pos)
{
    if (!m_view || !m_scene) return;
    QPointF scenePos = m_view->mapToScene(pos);
    QString svgId = svgIdAtPos(scenePos);

    if (svgId.isEmpty() || !m_mappings.contains(svgId)) return;
    const PVMappingEntry &entry = m_mappings[svgId];

    QMenu menu;
    for (const PVContextMenuItem &item : entry.contextMenu) {
        QAction *action = menu.addAction(item.text);
        connect(action, &QAction::triggered, this, [this, svgId, item]() {
            executeContextAction(svgId, item);
        });
    }

    if (entry.contextMenu.isEmpty()) {
        menu.addAction("无可用操作")->setEnabled(false);
    }

    menu.exec(m_view->viewport()->mapToGlobal(pos));
}

void ProcessViewManager::onSceneDoubleClick(const QPointF &pos)
{
    QString svgId = svgIdAtPos(pos);
    if (svgId.isEmpty() || !m_mappings.contains(svgId)) return;

    const PVMappingEntry &entry = m_mappings[svgId];
    if (entry.dblClickAction == "showControlPanel") {
        showControlPanel(svgId);
    } else {
        emit showDetailRequested(svgId, entry.tagKey);
    }
}

// ================================================================
// 交互操作执行
// ================================================================
void ProcessViewManager::executeContextAction(const QString &svgId,
                                               const PVContextMenuItem &item)
{
    if (item.action == "writeTag") {
        QString tagKey = item.params["tagKey"].toString();
        QVariant value = item.params["value"];

        if (tagKey.isEmpty()) {
            qWarning() << "[ProcessView] writeTag with empty tagKey";
            return;
        }

        // 权限检查
        if (!checkWritePermission("writeTag")) {
            QMessageBox::warning(nullptr, "权限不足", "您没有写入操作的权限");
            return;
        }

        // 互锁检查（非确认消息中的检查，确认后再检查一次）
        QString failMsg;
        // 提取 action 名（从 svgId 和 value 推断，或从 item.text）
        QString actionName = item.text.contains("启动") ? "start"
                           : item.text.contains("停止") ? "stop"
                           : item.text.contains("打开") ? "open"
                           : item.text.contains("关闭") ? "close"
                           : "write";
        if (!checkInterlocks(svgId, actionName, failMsg)) {
            QMessageBox::warning(nullptr, "互锁阻止", failMsg);
            emit operationLogged(svgId, item.text, false, failMsg);
            return;
        }

        // 确认操作
        if (item.confirm && !confirmOperation(svgId, item)) {
            return;
        }

        // 再次检查互锁（确认后）
        if (!checkInterlocks(svgId, actionName, failMsg)) {
            QMessageBox::warning(nullptr, "互锁阻止", failMsg);
            emit operationLogged(svgId, item.text, false, failMsg);
            return;
        }

        emit writeTagRequested(tagKey, value);
        emit operationLogged(svgId, item.text, true, "OK");
    }
    else if (item.action == "showDetail") {
        const PVMappingEntry &entry = m_mappings[svgId];
        emit showDetailRequested(svgId, entry.tagKey);
    }
    else if (item.action == "showControlPanel") {
        showControlPanel(svgId);
    }
}

void ProcessViewManager::showControlPanel(const QString &svgId)
{
    if (!m_mappings.contains(svgId)) return;
    const PVMappingEntry &entry = m_mappings[svgId];
    const PVControlPanel &cp = entry.controlPanel;

    if (cp.type.isEmpty()) {
        QMessageBox::information(nullptr, entry.labelName, "无控制面板配置");
        return;
    }

    if (cp.type == "switch") {
        // 开关控制面板
        QDialog dlg;
        dlg.setWindowTitle(entry.labelName + " — 控制面板");
        QVBoxLayout *layout = new QVBoxLayout(&dlg);

        layout->addWidget(new QLabel("<b>操作：</b>"));

        QPushButton *btnOn = new QPushButton(cp.onLabel);
        QPushButton *btnOff = new QPushButton(cp.offLabel);

        connect(btnOn, &QPushButton::clicked, [&]() {
            if (!checkWritePermission("control")) return;
            QString failMsg;
            if (!checkInterlocks(svgId, "start", failMsg)) {
                QMessageBox::warning(&dlg, "互锁阻止", failMsg);
                return;
            }
            emit writeTagRequested(cp.writeTagKey, 1);
            emit operationLogged(svgId, cp.onLabel, true, "OK");
            dlg.accept();
        });
        connect(btnOff, &QPushButton::clicked, [&]() {
            if (!checkWritePermission("control")) return;
            QString failMsg;
            if (!checkInterlocks(svgId, "stop", failMsg)) {
                QMessageBox::warning(&dlg, "互锁阻止", failMsg);
                return;
            }
            emit writeTagRequested(cp.writeTagKey, 0);
            emit operationLogged(svgId, cp.offLabel, true, "OK");
            dlg.accept();
        });

        layout->addWidget(btnOn);
        layout->addWidget(btnOff);

        // 监视标签
        if (!cp.monitorTags.isEmpty()) {
            layout->addWidget(new QLabel("<b>当前状态：</b>"));
            for (const PVMonitorTag &mt : cp.monitorTags) {
                QVariant val = m_plcData->getTagValue(
                    mt.tagKey.section('/', 0, 0),
                    mt.tagKey.section('/', 1));
                QString text = QString("%1: %2 %3")
                    .arg(mt.label)
                    .arg(val.isValid() ? val.toString() : "N/A")
                    .arg(mt.unit);
                layout->addWidget(new QLabel(text));
            }
        }

        dlg.exec();
    }
    else if (cp.type == "analog") {
        // 模拟量控制面板
        QDialog dlg;
        dlg.setWindowTitle(entry.labelName + " — 控制面板");
        QFormLayout *form = new QFormLayout(&dlg);

        QDoubleSpinBox *spinBox = new QDoubleSpinBox();
        spinBox->setRange(cp.min, cp.max);
        spinBox->setSingleStep(cp.step);
        spinBox->setSuffix(" " + cp.unit);
        form->addRow("设定值：", spinBox);

        QPushButton *btnSet = new QPushButton("设置");
        connect(btnSet, &QPushButton::clicked, [&]() {
            if (!checkWritePermission("control")) return;
            emit writeTagRequested(cp.writeTagKey, spinBox->value());
            emit operationLogged(svgId, "设定", true, QString::number(spinBox->value()));
            dlg.accept();
        });
        form->addRow(btnSet);

        // 监视标签
        if (!cp.monitorTags.isEmpty()) {
            for (const PVMonitorTag &mt : cp.monitorTags) {
                QVariant val = m_plcData->getTagValue(
                    mt.tagKey.section('/', 0, 0),
                    mt.tagKey.section('/', 1));
                QLabel *lbl = new QLabel(val.isValid() ? val.toString() + " " + mt.unit : "N/A");
                form->addRow(mt.label + "：", lbl);
            }
        }

        dlg.exec();
    }
}

// ================================================================
// 互锁检查
// ================================================================
bool ProcessViewManager::checkInterlocks(const QString &svgId,
                                          const QString &action,
                                          QString &failMsg)
{
    if (!m_interlocks.contains(svgId)) return true;

    for (const PVInterlockRule &rule : m_interlocks[svgId]) {
        if (rule.action != action) continue;

        for (const PVInterlockCondition &cond : rule.conditions) {
            int sep = cond.tagKey.indexOf('/');
            if (sep < 0) continue;
            QString devId  = cond.tagKey.left(sep);
            QString tName  = cond.tagKey.mid(sep + 1);

            QVariant tagValue = m_plcData->getTagValue(devId, tName);
            double v = tagValue.toDouble();
            bool satisfied = false;

            if      (cond.op == ">")  satisfied = v > cond.value;
            else if (cond.op == ">=") satisfied = v >= cond.value;
            else if (cond.op == "<")  satisfied = v < cond.value;
            else if (cond.op == "<=") satisfied = v <= cond.value;
            else if (cond.op == "==") satisfied = qFuzzyCompare(v + 1.0, cond.value + 1.0);
            else if (cond.op == "!=") satisfied = !qFuzzyCompare(v + 1.0, cond.value + 1.0);

            if (!satisfied) {
                failMsg = cond.message.isEmpty()
                    ? QString("互锁条件不满足: %1 %2 %3").arg(cond.tagKey, cond.op).arg(cond.value)
                    : cond.message;
                return false;
            }
        }
    }
    return true;
}

bool ProcessViewManager::checkWritePermission(const QString &action)
{
    if (!m_userManager) return true;
    if (!m_userManager->isLoggedIn()) return false;
    UserRole role = m_userManager->currentUserRole();
    Q_UNUSED(action);
    return role == UserRole::ADMIN || role == UserRole::ENGINEER || role == UserRole::OPERATOR;
}

bool ProcessViewManager::confirmOperation(const QString &svgId,
                                           const PVContextMenuItem &item)
{
    const PVMappingEntry &entry = m_mappings[svgId];
    QString msg = item.confirmMsg.isEmpty()
        ? QString("确认执行 %1 操作？").arg(item.text)
        : item.confirmMsg;
    return QMessageBox::question(nullptr, entry.labelName + " — " + item.text,
                                 msg) == QMessageBox::Yes;
}

// ================================================================
// 辅助函数
// ================================================================
QString ProcessViewManager::svgIdAtPos(const QPointF &scenePos) const
{
    if (!m_scene) return QString();
    QList<QGraphicsItem *> items = m_scene->items(scenePos);
    for (QGraphicsItem *item : items) {
        QVariant data = item->data(0);
        if (data.isValid() && !data.toString().isEmpty()) {
            return data.toString();
        }
    }
    return QString();
}

// ================================================================
// 视图控制
// ================================================================
void ProcessViewManager::refreshViewNow()
{
    if (!m_renderer || m_svgPath.isEmpty()) return;
    // 重新加载 SVG
    loadProject(m_svgPath, m_mappingPath);
}

void ProcessViewManager::fitToView()
{
    if (m_view && m_scene) {
        m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
    }
}

bool ProcessViewManager::eventFilter(QObject *watched, QEvent *event)
{
    if (m_view && watched == m_view->viewport()) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto *me = static_cast<QMouseEvent*>(event);
            QPointF scenePos = m_view->mapToScene(me->pos());
            onSceneDoubleClick(scenePos);
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}
