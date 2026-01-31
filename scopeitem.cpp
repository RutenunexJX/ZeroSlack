#include "scopeitem.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>

// --- ModuleScopeItem ---

ModuleScopeItem::ModuleScopeItem(QGraphicsItem* parent)
    : QGraphicsItem(parent)
{
    setZValue(kZValue);
    setAcceptedMouseButtons(Qt::NoButton);
    setFlag(QGraphicsItem::ItemIsSelectable, false);
    setFlag(QGraphicsItem::ItemIsFocusable, false);
    setFlag(QGraphicsItem::ItemClipsChildrenToShape, false);
}

QRectF ModuleScopeItem::boundingRect() const
{
    return m_rect;
}

void ModuleScopeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);
    if (m_rect.isEmpty()) return;
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 40));
    painter->drawRect(m_rect);
}

void ModuleScopeItem::updateLayout()
{
    QRectF childRect;
    for (QGraphicsItem* child : childItems())
        childRect = childRect.united(child->boundingRect().translated(child->pos()));
    if (childRect.isValid())
        m_rect = childRect.adjusted(-kPadding, -kPadding, kPadding, kPadding);
    prepareGeometryChange();
}

void ModuleScopeItem::setRect(const QRectF& rect)
{
    if (m_rect == rect) return;
    prepareGeometryChange();
    m_rect = rect;
}

// --- LogicScopeItem ---

LogicScopeItem::LogicScopeItem(QGraphicsItem* parent)
    : QGraphicsItem(parent)
{
    setZValue(kZValue);
    setAcceptedMouseButtons(Qt::NoButton);
    setFlag(QGraphicsItem::ItemIsSelectable, false);
    setFlag(QGraphicsItem::ItemIsFocusable, false);
}

QRectF LogicScopeItem::boundingRect() const
{
    return m_rect;
}

void LogicScopeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);
    if (m_rect.isEmpty()) return;
    painter->setBrush(QColor(0, 80, 0, 45));
    if (m_drawDashedBorder) {
        QPen pen(QColor(0, 100, 0, 120));
        pen.setStyle(Qt::DashLine);
        pen.setWidth(1);
        painter->setPen(pen);
    } else {
        painter->setPen(Qt::NoPen);
    }
    painter->drawRect(m_rect);
}

void LogicScopeItem::setRect(const QRectF& rect)
{
    if (m_rect == rect) return;
    prepareGeometryChange();
    m_rect = rect;
}

/*
 * 使用示例：将 ModuleScopeItem / LogicScopeItem 加入 QGraphicsScene 并建立父子关系
 * ---
 * QGraphicsScene* scene = new QGraphicsScene(this);
 * QGraphicsView* view = new QGraphicsView(scene);
 *
 * ModuleScopeItem* moduleItem = new ModuleScopeItem();  // parent 可传 nullptr，稍后加入 scene
 * scene->addItem(moduleItem);
 * moduleItem->setRect(QRectF(0, 0, 200, 400));         // 按行高换算的矩形
 *
 * LogicScopeItem* logicItem = new LogicScopeItem(moduleItem);  // 父项为 moduleItem
 * logicItem->setPos(0, 0);
 * logicItem->setRect(QRectF(10, 50, 180, 80));         // 逻辑块在模块内的相对区域
 * moduleItem->updateLayout();                          // 根据子项更新 module 的 boundingRect
 *
 * view->setRenderHint(QPainter::Antialiasing);
 * view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // 与编辑器同步滚动时由外部控制
 */
