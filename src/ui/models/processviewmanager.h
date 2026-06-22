// src/ui/models/processviewmanager.h — SVG 工艺视图引擎 v2.0
#ifndef PROCESSVIEWMANAGER_H
#define PROCESSVIEWMANAGER_H

#include <QObject>
#include <QStringList>
#include <QMap>
#include <QVariant>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QSvgRenderer>
//#include <QtWidgets/qgraphicsview.h>
#include <QtSvgWidgets/qgraphicssvgitem.h>
#include "ProcessViewTypes.h"

class PLCData;
class UserManager;

class ProcessViewManager : public QObject
{
    Q_OBJECT

public:
    explicit ProcessViewManager(PLCData *plcData,
                                UserManager *userManager,
                                QObject *parent = nullptr);
    ~ProcessViewManager() override;

    // ── 初始化 ──
    void setGraphicsView(QGraphicsView *view);
    bool loadProject(const QString &svgPath,
                     const QString &mappingPath,
                     const QString &interlocksPath = QString());

    // ── 兼容旧接口 ──
    void setProcessTags(const QStringList &tags);
    QStringList getProcessTags() const;
    QMap<QString, QVariant> getProcessStatus() const;

    // ── 状态更新 ──
    void updateProcessData(const QMap<QString, QVariant> &data);
    void onTagValueChanged(const QString &deviceId,
                           const QString &tagName,
                           const QVariant &value);

    // ── 视图控制 ──
    void refreshViewNow();
    void fitToView();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    // 向外传递写操作请求
    void writeTagRequested(const QString &tagKey, const QVariant &value);
    // 操作日志
    void operationLogged(const QString &svgId, const QString &action,
                         bool success, const QString &message);
    // 显示详情
    void showDetailRequested(const QString &svgId, const QString &tagKey);

    // 兼容旧信号
    void processDataUpdated(const QMap<QString, QVariant> &data);
    void processStateChanged(const QString &deviceId, int state);
    void processTagsChanged(const QStringList &tags);

private slots:
    void onCustomContextMenu(const QPoint &pos);
    void onSceneDoubleClick(const QPointF &pos);

private:
    // ── 加载与解析 ──
    bool parseMappingConfig(const QString &path);
    bool parseInterlockConfig(const QString &path);
    void buildInteractiveItems();
    void buildReverseIndex();

    // ── 状态渲染 ──
    void applyStatusBinding(const QString &svgId,
                            const PVStatusBinding &binding,
                            const QVariant &value);

    // ── 交互处理 ──
    void executeContextAction(const QString &svgId,
                              const PVContextMenuItem &item);
    void showControlPanel(const QString &svgId);
    bool checkInterlocks(const QString &svgId, const QString &action,
                         QString &failMsg);
    bool checkWritePermission(const QString &action);
    bool confirmOperation(const QString &svgId,
                          const PVContextMenuItem &item);

    // ── 辅助 ──
    QString svgIdAtPos(const QPointF &scenePos) const;
    QColor valveColorByPosition(const QVariant &value);

    PLCData *m_plcData = nullptr;
    UserManager *m_userManager = nullptr;
    QGraphicsView *m_view = nullptr;
    QGraphicsScene *m_scene = nullptr;
    QSvgRenderer *m_renderer = nullptr;

    // 映射数据
    QMap<QString, PVMappingEntry> m_mappings;        // svgId → 映射条目
    QMap<QString, QGraphicsSvgItem *> m_items;       // svgId → 图形项
    QMap<QString, QList<PVInterlockRule>> m_interlocks; // svgId → 互锁规则

    // 反向索引
    QMap<QString, QStringList> m_tagKeyToSvgIds;     // tagKey → svgId 列表

    // 数据缓存
    QMap<QString, QVariant> m_currentData;           // tagKey → 最新值
    QStringList m_projectTags;                       // 所有已绑定的 tagKey

    QString m_svgPath;
    QString m_mappingPath;
};

#endif // PROCESSVIEWMANAGER_H
