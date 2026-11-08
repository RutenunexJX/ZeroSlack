#pragma once
#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include <QStyle>
class CompactCandidateDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override {
        return {420, qMax(30, QFontMetrics(option.font).height() + 12)};
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem row(option); initStyleOption(&row, index); row.text.clear();
        const auto* style = row.widget ? row.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &row, painter, row.widget);
        painter->save(); painter->setFont(option.font);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const auto foreground = option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
        auto rect = option.rect.adjusted(10, 4, -10, -4);
        const auto badge = index.data(Qt::UserRole + 1).toString();
        const QFontMetrics metrics(option.font);
        if (!badge.isEmpty()) {
            QColor color = badge.contains(QStringLiteral("enum")) ? QColor("#c76812")
                : badge == QStringLiteral("module") || badge == QStringLiteral("instance") ? QColor("#366dcc")
                : badge == QStringLiteral("function") || badge == QStringLiteral("task") ? QColor("#8058b4")
                : badge == QStringLiteral("parameter") || badge == QStringLiteral("localparam") ? QColor("#93721a") : QColor("#258274");
            const int width = metrics.horizontalAdvance(badge) + 12;
            QRect box(rect.right() - width + 1, rect.top(), width, rect.height());
            painter->fillRect(box, color); painter->setPen(Qt::white);
            painter->drawText(box, Qt::AlignCenter, badge); rect.setRight(box.left() - 12);
        }
        painter->setPen(option.state.testFlag(QStyle::State_Enabled) ? foreground : option.palette.color(QPalette::Disabled, QPalette::Text));
        painter->drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, metrics.elidedText(index.data().toString(), Qt::ElideRight, rect.width()));
        painter->restore();
    }
};
