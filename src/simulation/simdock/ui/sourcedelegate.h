#pragma once
#include "uistyle.h"
#include <ElaCheckBox.h>
#include <ElaTheme.h>
#include <QStandardItemModel>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QStyleOptionButton>
#include <functional>

namespace simdock {
class SourceMetricsStyle final : public QProxyStyle {
public:
    SourceMetricsStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}
    int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr, const QWidget* widget = nullptr) const override {
        if (metric == PM_IndicatorWidth || metric == PM_IndicatorHeight) return 21;
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
};
class SourceModel final : public QStandardItemModel {
public:
    using CheckRequest = std::function<bool(const QModelIndex&, Qt::CheckState)>;
    SourceModel(QObject* parent, CheckRequest request) : QStandardItemModel(parent), m_request(std::move(request)) {}
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override {
        if (role == Qt::CheckStateRole && index.isValid()) {
            if (!(flags(index) & Qt::ItemIsUserCheckable)) return false;
            if (index.data(role) == value) return true;
            return m_request(index, Qt::CheckState(value.toInt()));
        }
        return QStandardItemModel::setData(index, value, role);
    }
private:
    CheckRequest m_request;
};

class SourceDelegate final : public QStyledItemDelegate {
public:
    explicit SourceDelegate(QWidget* view)
        : QStyledItemDelegate(view), m_indicator(new ElaCheckBox(view))
    {
        m_indicator->hide();
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem row(option);
        initStyleOption(&row, index);
        painter->save();
        option.widget->style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &row, painter, option.widget);
        QStyleOptionButton box;
        box.rect = option.widget->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &row, option.widget);
        box.state = row.state & (QStyle::State_Enabled | QStyle::State_MouseOver);
        if (!(index.flags() & Qt::ItemIsUserCheckable)) box.state &= ~QStyle::State_Enabled;
        box.state |= row.checkState == Qt::Checked ? QStyle::State_On : QStyle::State_Off;
        m_indicator->style()->drawControl(QStyle::CE_CheckBox, &box, painter, m_indicator);
        const QRect textRect = row.rect.adjusted(36, 0, -8, 0);
        painter->setFont(row.font);
        painter->setPen(index.data(Qt::ForegroundRole).value<QBrush>().style() != Qt::NoBrush
            ? index.data(Qt::ForegroundRole).value<QBrush>().color()
            : eTheme->getThemeColor(eTheme->getThemeMode(), row.state.testFlag(QStyle::State_Enabled)
                ? ElaThemeType::BasicText : ElaThemeType::BasicTextDisable));
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
            row.fontMetrics.elidedText(row.text, Qt::ElideMiddle, textRect.width()));
        if (row.state.testFlag(QStyle::State_HasFocus)) {
            painter->setPen(eTheme->getThemeColor(eTheme->getThemeMode(), ElaThemeType::PrimaryNormal));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(row.rect.adjusted(1, 2, -2, -3), 4, 4);
        }
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return QSize(180, 32); }
private:
    ElaCheckBox* m_indicator;
};
}
