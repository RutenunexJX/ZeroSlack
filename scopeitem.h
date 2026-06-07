#ifndef SCOPEITEM_H
#define SCOPEITEM_H

#include <QGraphicsItem>
#include <QRectF>
#include <QColor>
#include <QPen>
#include <QBrush>

/**
 */

class ModuleScopeItem : public QGraphicsItem
{
public:
    static constexpr int kZValue = -10;
    static constexpr int kPadding = 2;

    explicit ModuleScopeItem(QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    void updateLayout();

    void setRect(const QRectF& rect);
    QRectF rect() const { return m_rect; }

protected:
    QRectF m_rect;
};

class LogicScopeItem : public QGraphicsItem
{
public:
    static constexpr int kZValue = -9;

    explicit LogicScopeItem(QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    void setRect(const QRectF& rect);
    QRectF rect() const { return m_rect; }

    void setDrawDashedBorder(bool draw) { m_drawDashedBorder = draw; }
    bool drawDashedBorder() const { return m_drawDashedBorder; }

protected:
    QRectF m_rect;
    bool m_drawDashedBorder = true;
};

#endif // SCOPEITEM_H
