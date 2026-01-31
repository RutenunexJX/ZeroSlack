#ifndef SCOPEITEM_H
#define SCOPEITEM_H

#include <QGraphicsItem>
#include <QRectF>
#include <QColor>
#include <QPen>
#include <QBrush>

/**
 * 作用域层级可视化（Alpha Blending）
 * 使用 QGraphicsItem 表示 module / logic 范围，半透明叠加以体现嵌套深度。
 */

/** 模块作用域项：表示 module ... endmodule 范围，作为容器置于底层 */
class ModuleScopeItem : public QGraphicsItem
{
public:
    static constexpr int kZValue = -10;
    static constexpr int kPadding = 2;

    explicit ModuleScopeItem(QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    /** 根据子项（LogicScopeItem 等）重新计算并设置自身几何，含 padding */
    void updateLayout();

    /** 设置显示区域（由外部按行高换算的矩形），不依赖子项时可直接用 */
    void setRect(const QRectF& rect);
    QRectF rect() const { return m_rect; }

protected:
    QRectF m_rect;
};

/** 逻辑/信号块作用域项：表示 logic 等块，作为 ModuleScopeItem 的子项 */
class LogicScopeItem : public QGraphicsItem
{
public:
    static constexpr int kZValue = -9;

    explicit LogicScopeItem(QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    void setRect(const QRectF& rect);
    QRectF rect() const { return m_rect; }

    /** 是否绘制细虚线边框（标识 driver 来源） */
    void setDrawDashedBorder(bool draw) { m_drawDashedBorder = draw; }
    bool drawDashedBorder() const { return m_drawDashedBorder; }

protected:
    QRectF m_rect;
    bool m_drawDashedBorder = true;
};

#endif // SCOPEITEM_H
